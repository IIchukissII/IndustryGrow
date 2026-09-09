<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# A01-CLIMATE — module specification

- **Status:** Working specification, pre-schematic capture. `E0011` not laid out, not fabricated
- **Date:** 2026-09-08
- **E-number:** `E0011` · module class ID `0x80`
- **Governing ADRs:** ADR-0031 (rev 1), ADR-0032, ADR-0014 (rev 4), ADR-0015, ADR-0016, ADR-0017 (rev 2), ADR-0018, ADR-0003
- **Companions:** `E0002-R-specification.md`, `E0006-R-specification.md`, `E0008-R-specification.md`, `E0009-R-specification.md`
- **Supersedes:** `spec/A01-THERMAL-specification.md` (2026-09-07)

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the A01-CLIMATE actuator module in the indoor deployment variant: commanded
elements, actuator complement, drive, interfaces, power, thermal, interlock, mechanical and
firmware requirements, and their verification. The module acts on the grow-volume air and
carries every regulated variable of that medium — air temperature, humidity, and CO₂ when
populated (ADR-0031 rev 1 d2).

Not specified here:

| Subject | Owner |
|---|---|
| Carrier design and header allocation | `store/E0001-VVVVVV-D-pinmap.md` |
| Cultivation setpoints | `profiles/strawberry-day-neutral-v1.json` |
| Control-loop structure and gains | ADR-0015 d8/d18, `gateway/control_model.py` |
| Outdoor deployment variant | Not specified — `O-106` |
| Luminaire, its window, and the enclosure | A02-LIGHT and the enclosure design — `O-114` |
| Rejection loop as a purchased assembly | ADR-0032, deferred |

## 2. Identification

| | Value |
|---|---|
| Module class | A01-CLIMATE |
| Module class ID | `0x80` — first actuator class (ADR-0031 d1, rev 1 d2; range per ADR-0014 rev 4 d6) |
| ID transport | Serial EEPROM, 24Cxx class, byte 0, I²C `0x50` (ADR-0014 d6). The 3-bit strap cannot express `0x80` |
| E-number | `E0011` — first assembly. A populate variant takes its own assembly E-number at design commit (ADR-0017 rev 2 d4) |
| Bare design | One layout, one class ID, one firmware image |
| Carrier | `E0001-000100` or later (ADR-0031 d10). **Not `E0001-000003`** — see `O-100` |

### 2.1 Deployment variant — indoor

| | Indoor |
|---|---|
| Deployment envelope | Canopy ≤ 1 m², enclosed volume ≤ 3 m³, day cooling load ≤ 150 W (ADR-0032 d1) |
| Thermal boundary | A cabinet standing inside a building |
| Ambient | The room enclosing the cabinet, one step out (ADR-0014 d4) |
| Ambient air temperature | 15…30 °C (`E0009-R-specification.md`, indoor variant); 22 °C is the design value |
| Ambient relative humidity | 25…65 %RH (same) |
| Rejection sink | The same room air, through the liquid loop |
| Rejection load into the room | Per `T6`, bounded by `T8` |
| Field bus leaves the enclosure | No |
| Environmental qualification | Indoor, as M01–M05 |

### 2.2 Design basis

The loads the complement of §4 is sized against. These are inputs to this specification, not
requirements on it.

| Quantity | Value | Source |
|---|---|---|
| Grow-volume air | 0.43 m³ | enclosure, 516 × 516 × 1616 mm internal |
| Envelope area | 4.80 m² | same |
| Envelope conductance `U·A` | 2.95 W/K (12 mm OSB + 30 mm PIR) | computed; `O-105` for the identified value |
| Room temperature | 22 °C | §2.1 |
| Air setpoints | 20 °C day, 14 °C night | `profiles/strawberry-day-neutral-v1.json` |
| VPD band | 0.8–1.2 kPa, both photoperiods | same |
| Photoperiod | 16 h on, 8 h off | same |
| Luminaire | Outside the grow volume, radiating through a window; 19.2 W enters the volume | `O-114` |
| Transpiration | 140 ml/day, 5.8 g/h during the photoperiod | assumed; `O-111` |
| Main-tract load, day | 25 W | `U·A`·ΔT 5.9 W + luminaire 19.2 W + fans 3.9 W − humidity tract 4.1 W |
| Main-tract load, night | 26 W | `U·A`·ΔT 23.6 W + fans 3.9 W − humidity tract 1.5 W |

With the luminaire inside the grow volume the day load is 38 W and `D10` is reached at the wet
edge of the VPD band; that case is outside this variant (`O-114`).

## 3. Function

A01 acts on the grow-volume air through two air tracts in parallel. Each element takes its own
demand and validity deadline (ADR-0031 d4); the node conditions and limits every demand on its
own state (`D9`, `D10`, `D11`) and executes it (ADR-0015 d18).

| Tract | Flow | Sets | Method |
|---|---|---|---|
| Main | 56 m³/h | Air temperature | `E1` on HX1, floored above the grow-volume dew point (`D10`) |
| Humidity | 1.7 m³/h | Humidity ratio | `E3` subcools to a commanded coil temperature (`D11`), `E4` pumps reheat from WB2 at constant humidity ratio (`D12`, ADR-0032 d7) |

![Gateway demands into a conditioned node; a main tract with a two-module thermoelectric stack on a finned exchanger rejecting into a water block, and a humidity tract with a subcooler, a thermal break with a condensate siphon, and a reheater that pumps its heat back out of the subcooler's rejection plate; four hardware interlocks gate the driver enables](./figures/a01-climate-principle.svg)

### 3.1 Commanded elements

| ID | Element | Device | Command range | Expected operating range | Resolution |
|---|---|---|---|---|---|
| `E1` | Main thermoelectric stack | 2 × 40 × 40 mm module, series | −1.000 … +1.000, signed; positive heats the grow volume | −0.30 … 0 | 0.001 (`verify` against DSDL type, `O-102`) |
| `E2` | Main-tract fan | ebm-papst 612/614 NHH, integral to the exchanger | 0.000 … 1.000 | 0.4 … 1.0 continuous, 1.0 in the `D15` pulse | 0.001 |
| `E3` | Subcooler thermoelectric module | 1 × 40 × 40 mm module | 0.000 … 1.000, unsigned; cooling only | 0 … 0.30 | 0.001 |
| `E4` | Reheater | Thermoelectric module bridging WB2 and HX3, cold face on WB2 | 0.000 … 1.000, unsigned; heating only | 0 … 0.35 | 0.001 |
| `E5` | Humidity-tract fan | Sepa MFB 50 E 05 A | 0.000 … 1.000 | 0.17 … 0.40 | 0.001 |
| `E6` | CO₂ metering valve | Solenoid, pulse-dosed — **not populated in the baseline** | 0.000 … 1.000 | — | 0.001 |

Full scale of `E1`, `E3` and `E4` corresponds to the string current limit of `D2`, not to the module's
`Imax`. `E6` is unpopulated under ADR-0003 d8 and §3.4.

### 3.2 Published quantities

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|---|---|---|---|---|
| Rejection-side (water block) temperature | U1 DS18B20, 1-Wire | −55…+125 °C (`verify`) | 22…30 °C, ceiling 45 °C (`D9`) | ±0.5 K over −10…+85 °C (`verify`) |
| Main exchanger base temperature | U2 DS18B20, same bus | −55…+125 °C (`verify`) | 9…17 °C; 15.8 °C day, 9.6 °C night | ±0.5 K over −10…+85 °C (`verify`) |
| Subcooler coil base temperature | U3 DS18B20, same bus | −55…+125 °C (`verify`) | 5…12 °C; 7.6 °C day, 5.5 °C night, floor +1 °C (`D11`) | ±0.5 K over −10…+85 °C (`verify`) |
| Reheater base temperature | U4 DS18B20, same bus | −55…+125 °C (`verify`) | 8…30 °C; 25.4 °C at the `D12` design point | ±0.5 K over −10…+85 °C (`verify`) |
| Applied demand echo, per element | — (firmware state) | per §3.1 | per §3.1 | exact |
| Drive state, per element | — (firmware state) | `off` / `run` / `inhibited` / `derated` / `tripped` / `fault` | — | exact |
| Interlock state | `T2`–`T5` hardware lines | `armed` / `tripped` per line | `armed` | exact |

### 3.3 Exclusions

| Quantity | Owning module class |
|---|---|
| Grow-volume air temperature, humidity, VPD, CO₂ concentration | M01-CLIMATE |
| Air velocity at the canopy | M06-VENTILATION |
| Room temperature and humidity | M07-AMBIENT |
| Canopy PPFD and spectrum | M02-LIGHT |
| Cabinet `+12 V` bus current, door, leak | M05-SAFETY |
| Actuator energy consumption | DIN kWh meter over S0, read by M05 (ADR-0018 d5) |
| Condensate accumulation rate | M05-SAFETY, counted pulse input (ADR-0014 rev 7 d4) |
| Transpiration rate derived from condensate | Gateway soft sensor (ADR-0016 d20) |
| Luminaire drive | A02-LIGHT — not specified (ADR-0031 rev 1 d2) |

### 3.4 Partial populations

| Population | Fitted | Omitted |
|---|---|---|
| Baseline | `E1`–`E5` and their drivers, sensors and interlocks | `E6` and its valve, regulator and dosing branch |
| CO₂ variant | All of `E1`–`E6` | — |

Both populations carry class ID `0x80` and one firmware image; `F2` and `F12` govern what an
unpopulated branch publishes and commands (ADR-0014 d1, d2).

## 4. Element and device complement

| Ref | Device | Function | Supply | Rail |
|---|---|---|---|---|
| U1–U4 | DS18B20, 1-Wire, 12-bit, 750 ms conversion (`verify`) | Reported temperatures per §3.2 | 3.3 V | 3V3 |
| RT1 | NTC 10 kΩ B25/85 3435, at the water block | `T2` trip element | — | — |
| RT2 | NTC 10 kΩ, on a lead in the grow volume | `T3` trip element, both thresholds (ADR-0018 d10) | — | — |
| TEC1, TEC2 | 40 × 40 mm, 127 couple, `Imax` 6.4 A, `Qmax` 57 W, `ΔTmax` 66 K at `Th` 25 °C, α 0.0608 V/K, R 2.78 Ω, K 0.864 W/K (all `verify`) | `E1`, series pair | Series string | `+24 V` actuator |
| TEC3 | Same part | `E3`, single | — | `+24 V` actuator |
| TEC4 | Module for `E4`, not the TEC1–TEC3 part; sized by `D16` and `T11` (`O-103`) | `E4`, single | — | `+24 V` actuator |
| HX1 | Fischer LA 6 150 12, 62 × 74 mm, `Rth` 0.17 K/W (`verify`, `O-112`) | Grow-volume exchanger of the main tract, and the cabinet's circulation | — | — |
| HX2 | Fischer LAM 5, 100 mm, `h·A` 1.64 W/K (`verify`) | Cold section of the humidity tract | — | — |
| HX3 | Fischer LAM 5 50 5, `h·A` 0.82 W/K (`verify`) | Hot section of the humidity tract, carrying TEC4's hot face | — | — |
| WB1 | Water block, 40 × 120 mm | Rejection side of `E1`, and its clamping plate | — | — |
| WB2 | Water block or cold plate, two working faces | Rejection side of `E3` and cold-side source for `E4` | — | — |
| U5 | TI DRV8262, HTSSOP-44, **single-H-bridge mode**. 4.5…60 V; 50 mΩ HS+LS; integrated charge pump for 100 % duty; integrated high-side current sense, IPROPI ±4 %; UVLO, CPUV, OCP, OTSD, `nFAULT` | `E1` direction and current regulation (`D2`, `D3`) | 3.3 V logic, `+24 V` power | `+24 V` |
| U6 | TI DRV8262, **dual-half-bridge mode** | `E3` current regulation and `E4` duty, unidirectional (`D11`, `D12`) | 3.3 V logic, `+24 V` power | `+24 V` |
| L1, L2 | Series inductors, string side (`verify` — values from `D4`) | Ripple filters for the drivers' off-time chopping | — | — |
| U7 | Comparator, water-block trip on `RT1` (`T2`) | Hardware interlock | 3.3 V | 3V3 |
| U8 | Window comparator, grow-volume high and low trips on `RT2` (`T3`) | Hardware interlock (ADR-0018 d10) | 3.3 V | 3V3 |
| U9 | 16-channel PWM generator, I²C (`verify` — part from `O-113`) | Module-local drive expansion (ADR-0031 rev 1 d11) | 3.3 V | 3V3 |
| U10 | 24Cxx serial EEPROM, I²C `0x50` | Module class ID (ADR-0014 d6) | 3.3 V | 3V3 |
| SF1 | Coolant-flow switch, purchased (`T4`) | Rejection-loop interlock | — | — |
| FA1 | Alarm output of the `E5` fan (`T5`) | Humidity-tract airflow interlock | — | — |
| TB1 | Thermal-break insert between HX2 and HX3, with the low point and condensate outlet | `T9`, `M7` | — | — |

Deliberately absent:

| Not fitted | Reference |
|---|---|
| Digital trip element. `T2`–`T5` act without the MCU; U1–U4 report and derate, and trip nothing | ADR-0031 d7, `T10` |
| Current-sense shunt. U5 and U6 integrate high-side sense | `D2` |
| Interlock path through a driver input. `T2`–`T5` act on the driver enable | `T10` |
| Any device at `0x50`–`0x57` other than U10. The block is reserved project-wide for the ID EEPROM | ADR-0014 d6 |

## 5. Interfaces

Header signals claimed (ADR-0014 d5):

| Header signal | Carrier pin | Use |
|---|---|---|
| `PWM_1` | PC6 / TIM3_CH1 | U5 EN — `E1` magnitude (`D2`) |
| `PWM_2` | PB0 / TIM3_CH3 | U5 VREF — `E1` current limit, RC-filtered to analog (`D2`) |
| `PWM_3` | PC7 / TIM3_CH2 | U6 EN1 — `E3` magnitude (`D11`) |
| `PWM_4` | PB1 / TIM3_CH4 | U6 VREF1 — `E3` current limit, RC-filtered to analog (`D11`) |
| `ADC_1` | PC4 | U5 IPROPI — `E1` string current |
| `ADC_2` | PC5 | U6 IPROPI1 — `E3` string current |
| `OW_DATA` | PA0 | U1–U4 on one 1-Wire bus; external 4.7 kΩ pull-up |
| `I2C1_SCL` / `I2C1_SDA` | PB6 / PB7 | U10 ID EEPROM `0x50`; U9 drive expansion |
| `GPIO_1` | PA9 | `T2` interlock state (input) |
| `GPIO_2` | PA10 | `T3` interlock state (input) |
| `GPIO_3` | PA15 | `T4` interlock state (input) |
| `GPIO_4` | PB12 | `T5` interlock state (input) |
| `STRAP_0`, reallocated | — | U5 and U6 `nFAULT`, wired-OR, open-drain (input) |
| `STRAP_1`, `STRAP_2`, reallocated | — | Spare |

Module-local expansion on U9 (ADR-0031 rev 1 d11):

| U9 channel | Use |
|---|---|
| 0 | `E2` main-tract fan |
| 1 | `E4` reheater duty |
| 2 | `E5` humidity-tract fan, including the `D13` start pulse |
| 3 | `E6` CO₂ valve — unpopulated in the baseline |
| 4 | U5 PH — `E1` direction (`D3`) |
| 5, 6 | U5, U6 nSLEEP — driver enable from firmware |

Module-ID straps are not used for identification by this module (ADR-0031 d10); the table above
reallocates all three. The pin-map changes this requires are `O-100`.

## 6. Drive requirements

| ID | Requirement | Reference |
|---|---|---|
| `D1` | `E1` is two modules wired as one series string. At the design points the string carries 1.45 A at 9.5 V (day, ΔT 12 K, `Qc` 25 W) and 1.68 A at 11.2 V (night, ΔT 15 K, `Qc` 26 W), all `verify` | `O-103` |
| `D2` | `E1` drive is closed-loop **current**, regulated by U5 against its `VREF` setpoint. String current limit 2.0 A (`verify`). The integrated sense serves this loop only; no energy or consumption quantity is published from it | ADR-0018 d5 |
| `D3` | `E1` direction reversal is by the U5 PH input. Reversal is permitted only after the drive has been at zero for `D5` | ADR-0031 d5 |
| `D4` | Driver off-time chopping is set to one of 7 / 16 / 24 / 32 µs; `L1` and `L2` are sized so each string sees ripple ≤ 5 % of setpoint current (`verify`) | `O-103` |
| `D5` | Dead band \|demand\| < 0.05 commands zero drive; minimum dwell at zero before a sign change is 60 s (`verify`) | ADR-0031 d5 |
| `D6` | Every element of §3.1 is commanded independently; no element's demand forces another's | ADR-0031 d4 |
| `D7` | Demand slew is limited to 0.05 s⁻¹ on `E1` and `E3` (`verify`) | ADR-0015 d18 |
| `D8` | A demand whose validity deadline has expired drives its element to the safe output: `E1`, `E3`, `E4`, `E6` to zero; `E2` and `E5` to full | ADR-0031 rev 1 d6 |
| `D9` | Water-block temperature from U1 derates every thermoelectric demand: full scale below 35 °C, linear to zero at **45 °C**, re-enable below 32 °C (`verify`). Runs on the node and applies to any commanded value | ADR-0031 d4 |
| `D10` | Main exchanger base temperature from U2 is floored so the surface stays above the grow-volume dew point. The operative floor arrives with the demand — 13.4 °C day, 3.7 °C night at the wet edge of the band (`verify`). The node holds a commissioned absolute minimum of 3.0 °C (`verify`) that no command lowers. `E1` cooling demand is derated to hold whichever floor is higher | ADR-0032 d5, ADR-0015 d18, `O-102` |
| `D11` | `E3` is unidirectional. Its demand is conditioned against a coil-temperature setpoint measured at U3, with a hard floor of **+1 °C** (`verify`) below which cooling demand is driven to zero | ADR-0032 d4, ADR-0003 d7 |
| `D12` | `E4` is the last element of the humidity tract, downstream of HX2, and pumps from WB2 rather than dissipating. `E4` is inhibited whenever `T5` is tripped or `E5` demand is below `D13`'s minimum | `T5`, ADR-0032 d7 |
| `D13` | `E5` starts with a full-scale pulse of 250 ms (`verify`) before settling to its commanded duty; commanded duty below 0.15 is driven as zero | `E5` |
| `D14` | `E6` is commanded as a pulse train against a commissioned minimum open time; it is inhibited whenever `E5` demand is zero. Verified at the CO₂ population only | `O-110` |
| `D15` | `E2` also carries the pollination pulse: a full-scale excursion over the flowering zone at the profile's duration and interval, issued by the gateway as an ordinary demand. No pulse timing is node-local | ADR-0003 d13, ADR-0031 rev 1 d2 |
| `D16` | `E4` is unidirectional. At the design point it delivers 6.6 W to HX3 at 0.51 A and 1.61 V, drawing **0.82 W**; at the largest lift, WB2 at 22 °C against a 30 °C HX3, it draws **1.69 W** (both `verify`, computed on the TEC1–TEC3 parameters pending `O-103`). Demand is conditioned against U4 | ADR-0032 d7, `O-103` |

## 7. Power

| ID | Requirement | Reference |
|---|---|---|
| `P1` | Node logic is powered from the `+12 V` SELV sensor bus and derives 3.3 V on the carrier. No actuator element is on that rail | ADR-0018 d3 |
| `P2` | Elements are fed from the `+24 V` actuator section: TDK-Lambda Vega 650 (`K60050B`), C5 module, 24 V 10 A. Chassis total 650 W is shared across fitted modules (`verify`) | ADR-0018 d3, `O-104` |
| `P3` | Draw at the worst simultaneous point: `E1` 18.7 W, `E3` 9.5 W, `E4` 1.7 W, `E2` 2.9 W, `E5` 0.5 W — 33.3 W, 1.4 A from `+24 V` (`verify`). One C5 module carries the module with the balance available to other branches | `D1` |
| `P4` | Per-branch overcurrent protection is the supply module's own current limit plus the drivers' OCP; no central per-load fuse | ADR-0018 d8 |
| `P5` | The switched return is not shared with the logic or analog ground at the module. Isolation of the command path lives at this actuator | ADR-0018 d7, d8 |
| `P6` | U5 dissipates 0.20 W and U6 0.14 W at the `D2` and `D11` limits (50 mΩ HS+LS × I²). Each thermal pad is bonded to a copper pour sized for it (`verify`) | U5, U6 |
| `P7` | Module energy at the design profile is 0.73 kWh/day: `E1` 0.37, `E3` 0.15, `E4` 0.01, `E2` 0.07, `E5` 0.01, rejection-loop pump 0.12 (`verify`). Divergence from the M05 S0 meter beyond 15 % invalidates the §2.2 basis | ADR-0018 d5 |

## 8. Thermal requirements and interlocks

| ID | Requirement | Reference |
|---|---|---|
| `T1` | `E1` removes ≥ 26 W from the grow volume at a module ΔT of 15 K with the string at `i` ≤ 0.30 of `Imax` (`verify`) | ADR-0032 d6, §2.2, `O-105` |
| `T2` | **Rejection over-temperature trip** (self-protective), independent of the MCU, gateway and cloud: `RT1` → U7 → driver enable on U5 and U6. Trip at 60 °C (`verify`), 15 K above the `D9` ceiling | ADR-0031 d7 |
| `T3` | **Grow-volume temperature window trip** (process-protective), independent of the MCU, gateway and cloud: `RT2` → U8 → driver enable on U5 and U6. High trip 35 °C, low trip 5 °C (both `verify`) | ADR-0018 d10, ADR-0031 d7 |
| `T4` | **Coolant-flow interlock** (self-protective): loss of flow in the rejection loop removes driver enable on U5 and U6 in hardware | ADR-0031 d7 |
| `T5` | **Humidity-tract airflow interlock** (self-protective): loss of the `FA1` alarm output removes drive from **both** `E3` and `E4` in hardware | ADR-0031 d7 |
| `T6` | The rejection loop dissipates ≥ 65 W continuous with supply water at ≤ 25 °C in a 22 °C room (`verify`) | ADR-0032 d3, `P3`, `T1` |
| `T7` | Condensate forms only on HX2, is collected at the `TB1` low point through a liquid seal, and is drained clear of the grow volume. HX1 runs above the grow-volume dew point under `D10` and forms none | ADR-0032 d5, `O-107` |
| `T8` | The room absorbs the `T6` rejection load with an ambient rise ≤ 2 K (`verify`). Ambient outside 15…30 °C is outside this variant | §2.1 |
| `T9` | Conduction between HX2 and HX3 through `TB1` is ≤ 1 W at a section-to-section ΔT of 18 K (`verify`) | `M7` |
| `T10` | `T2`–`T5` act on the driver enables directly. Firmware reads their state but cannot override or re-arm any of them | ADR-0015 d11, ADR-0018 d10 |
| `T11` | **Zero drive on `E4` is not zero heat transfer.** TEC4 conducts WB2's heat into HX3 whenever WB2 is the warmer plate, including at the `D8` safe output and at every interlock trip. TEC4 is sized so that this uncommanded reheat stays ≤ **3 W** (`verify`) at the largest reverse lift, WB2 at the `D9` ceiling against a 25.4 °C HX3. The TEC1–TEC3 part fails that bound at 0.864 W/K and is therefore not TEC4 | ADR-0032 d7, ADR-0031 d6, `D8`, `O-103` |

## 9. Mechanical requirements

![Vertical section through the cabinet: both tracts sit outside the grow volume and reach it through four sealed, thermally broken penetrations; the main tract draws from a floor intake, passes HX1 with its fan E2 in an outer shaft and returns through a horizontal ceiling slot aimed at the far wall, so the air crosses the ceiling, falls down the far wall and returns along the floor; the humidity tract draws high on the opposite wall, passes E5, the condensing exchanger HX2 with E3, the thermal break TB1 carrying the low point and liquid seal, and the reheat exchanger HX3 with E4, returning low; condensate leaves TB1 by a drain clear of the volume, the luminaire radiates through a ceiling window, and RT2 reaches the volume on a lead through the floor](./figures/a01-climate-air-path.svg)

| ID | Requirement | Reference |
|---|---|---|
| `M1` | Each thermoelectric module is clamped between its exchanger and its water block at the datasheet clamping force, through a metal plate with disc springs. No printed part carries clamping load | `O-103` |
| `M2` | Both faces of every module carry a thermal interface material rated for continuous 100 °C (`verify`) | `O-103` |
| `M3` | Each tract penetrates the cabinet wall; every penetration is sealed and thermally broken, and no metal is carried through the insulation | `O-107` |
| `M4` | The main-tract discharge is a horizontal slot at the ceiling, gap ≤ 20 mm, directed at the far wall. Its intake is at the floor. The two tracts' intakes are separated in height | ADR-0003 air movement |
| `M5` | `RT2` reaches the grow volume on a lead; no I²C crosses that lead | ADR-0014 d3, ADR-0018 d10 |
| `M6` | Printed parts are PETG. No printed part is in direct thermal contact with a module hot face, a water block or HX3, and no printed surface exceeds **60 °C** continuous (`verify` — PETG HDT ≈ 70 °C at 0.45 MPa) | `O-107` |
| `M7` | `TB1` is a printed insert of 15–20 mm with thin walls and an internal air passage, carrying the tract's low point and its liquid seal | `T9`, `T7` |
| `M8` | HX1, HX2 and HX3 are degreased before assembly | `T7` |

## 10. Firmware requirements

| ID | Requirement | Reference |
|---|---|---|
| `F1` | Read the class ID from EEPROM `0x50` byte 0 at boot. `0x00` and `0xFF` are *unidentified*, never a class | ADR-0014 d6 |
| `F2` | Enumerate the 1-Wire bus at boot and publish only the quantities in §3.2 whose devices respond | ADR-0014 d2 |
| `F3` | The inner loop runs on the node at the element's rate; the gateway issues each demand and its validity deadline only | ADR-0015 d18 |
| `F4` | Enforce `D2`, `D5`, `D7`–`D14` in node firmware, independently of the commanded value | ADR-0015 d18 |
| `F5` | Publish the state of `T2`–`T5`. A trip is reported and latched in telemetry until the node is reset | `T10` |
| `F6` | Publish no energy or consumption quantity | ADR-0018 d5 |
| `F7` | Deployment constants — current limits, dead band, dwell, slew, the `D10` floor, the `D11` coil floor, fan mapping — are node-local and set at commissioning, not carried in the profile | ADR-0015 d18, ADR-0028 |
| `F8` | Loss of U1 for more than 5 s (`verify`) is treated as the `D9` ceiling reached; loss of U2 or U3 drives `E1` or `E3` respectively to zero | `D9`, `D10`, `D11` |
| `F9` | A `nFAULT` assertion drives both strings to zero, is published as drive state `fault`, and is latched until the node is reset | U5, U6 |
| `F10` | The humidity setpoint the node accepts is a **coil temperature** for U3, not a humidity or VPD value. No M01 quantity is an input to this node | ADR-0032 d4, ADR-0031 d8, `D11` |
| `F11` | Loss of the U9 I²C link drives every U9-borne element to its `D8` safe output. `E2` and `E5` are wired so that a de-asserted U9 output runs them at full without firmware acting | ADR-0031 rev 1 d6, d11, `O-113` |
| `F12` | An unpopulated branch publishes no demand echo and accepts no demand for its elements | §3.4, ADR-0014 d2 |

## 11. Verification

| ID | Verifies | Method |
|---|---|---|
| `V1` | `D1`, `P3` | Measure `E1` string current and voltage at 24 V against a water block held at 25 °C; record the ΔT–current curve to `i` = 0.30 |
| `V2` | `D2`, `D4` | Measure each string's current and ripple at 25 %, 50 % and 100 % of its limit with a current probe; cross-check against IPROPI |
| `V3` | `D3`, `D5` | Command an `E1` sign reversal; confirm zero drive is held for the dwell and that no reversal occurs inside it |
| `V4` | `D7`, `D8`, `F4` | Step each element to full scale and confirm slew. Stop publishing each demand; confirm `E1`, `E3`, `E4` reach zero and `E2`, `E5` reach full within the deadline |
| `V5` | `T1`, `T6` | Step `E1` to the limit into a cabinet at a known start temperature; record the pull-down curve, the water-block rise and the loop supply temperature |
| `V6` | `T2` | Heat `RT1` past the trip with the MCU held in reset; confirm both driver enables are removed |
| `V7` | `T3` | Drive `RT2` past each threshold in turn with the MCU held in reset; confirm both driver enables are removed at each |
| `V8` | `T4` | Stop coolant flow with `E1` at the limit; confirm both driver enables are removed |
| `V9` | `T5` | Stall the `E5` fan with `E3` and `E4` running; confirm both lose drive in hardware |
| `V10` | `T10` | Attempt to re-arm each trip from firmware; confirm it cannot |
| `V11` | `D9`, `F8` | Raise the water block past 35 °C with `E1` at full scale; record the derate curve and the 45 °C clamp. Disconnect U1; confirm the clamp |
| `V12` | `D10`, `T7` | Run `E1` at the night load with the grow volume at the wet edge of the band for 4 h; record U2 against the M01 dew point and confirm no free water on HX1. Command a floor below 3.0 °C; confirm the node holds 3.0 °C |
| `V13` | `D11` | Command a coil setpoint below +1 °C; confirm cooling demand is driven to zero at the floor and that HX2 does not frost over 8 h |
| `V14` | `D12`, `T9` | Run the humidity tract at its design point; measure the humidity ratio and temperature at tract inlet and outlet, and the HX2-to-HX3 conduction by substituting a known electrical load for `E3` |
| `V15` | `D13` | Command `E5` from zero to 0.17; confirm the start pulse and that the rotor starts on ten of ten attempts |
| `V16` | `D6`, `D15` | Command every element independently at three levels; confirm no element's demand moves another. Command the pollination pulse; confirm `E2` reaches full for the commanded duration and returns to its prior demand |
| `V17` | `F9` | Force a driver fault at reduced `VREF`; confirm zero drive on both strings, the published state, and the latch |
| `V18` | `F11` | Open the U9 I²C link with all elements running; confirm `E1`, `E3`, `E4` reach zero and `E2`, `E5` run full |
| `V19` | `F1`, `F6`, `F12` | Read the published class ID; confirm `0x80`. Enumerate every published subject; confirm no energy or consumption quantity is among them, and that the unpopulated CO₂ branch publishes and accepts nothing |
| `V20` | `P6` | Measure U5 and U6 case temperatures at their limits for 1 h |
| `V21` | `P7` | Log the M05 S0 meter over a full 24 h profile; compare against the `P7` figure |
| `V22` | `M6` | Thermograph the printed parts at the `T6` rejection load; confirm no surface exceeds 60 °C |
| `V23` | `M4` | Trace the main-tract jet with the cabinet loaded; confirm it reaches the far wall and that intake air is not drawn directly from the discharge |
| `V24` | `T8` | Run at the `T6` rejection load for 4 h in the installed room; record the ambient rise |
| `V25` | `P1`, `P5` | Measure isolation between the switched return and both logic and analog ground; confirm no element current returns on the `+12 V` bus |
| `V26` | `M1`, `M2`, `M8` | Record clamping force at assembly against the module datasheet, the TIM temperature rating, and that each exchanger was degreased |
| `V27` | `M3`, `M5`, `M7` | Inspect every penetration for seal and thermal break, confirm no metal crosses the insulation, confirm no I²C runs on the `RT2` lead, and confirm `TB1` drains at its low point |
| `V28` | `F2`, `F10`, `F12` | Remove one 1-Wire device; confirm its quantity is not published. Offer the node a humidity, VPD or CO₂ setpoint; confirm it is refused |
| `V29` | `F3`, `F5`, `F7` | Record the node loop rate; confirm a trip is published and latched until reset; confirm every `F7` constant is read from node-local storage and none from the profile |
| `V30` | `P2`, `P4` | Short one element at its connector with the string at its limit; confirm the supply module's current limit and the driver OCP act, that no other branch on the C5 module drops out, and that the supply recovers on removal |
| `V31` | `D16`, `T11`, ADR-0032 d7 | Reheat delivered to HX3 measured against `E4`'s electrical input at the design point and at the largest lift; the coefficient of performance shall exceed 1 at both. With `E4` at zero drive and WB2 held at the `D9` ceiling, the heat crossing into HX3 measured against `T11`'s 3 W bound |

## 12. Open items

- `O-100` — Actuator class IDs require the EEPROM transport, so A01 cannot run on carrier `E0001-000003` (ADR-0031 d10). The `E0001-000100` pin map must also carry the 1-Wire line and reallocate `STRAP_0` as a general input (§5). Blocks fabrication.
- ~~`O-101`~~ — ~~No actuator-taxonomy ADR (ADR-0014 d9).~~ — closed 2026-09-07 by ADR-0031.
- `O-102` — No DSDL type for a signed or unsigned actuator demand with a validity deadline. Blocks `F3`.
- `O-103` — Thermoelectric module not selected; α, R, K, clamping force and TIM are unconfirmed, and `L1`, `L2` follow from them. Driver continuous RMS current by package is unread. TEC4 is a separate selection under this item and is not the TEC1–TEC3 part: bounded below by `D16`'s largest lift, above by `T11`'s conductance ceiling. Blocks `D1`, `D4`, `D16`, `M1`, `M2`, `T11`.
- `O-104` — Vega 650 startup, inhibit and output-isolation behaviour unconfirmed against the manual. Blocks `P2`.
- `O-105` — Envelope conductance is computed, not identified (ADR-0016 survey). Blocks sufficiency of `T1` and the `D10` floor.
- `O-106` — Outdoor deployment variant not specified.
- `O-107` — Wall penetrations, thermal breaks and the condensate drain are mechanical design. Blocks `M3`, `M6`, `T7`.
- `O-108` — The dry edge of the night VPD band is not reachable: 1.2 kPa at 14 °C requires a coil at −6 °C, and `D11`'s floor yields 0.94 kPa. Resolved by the profile — narrow the night band, add a defrost cycle, or raise the night setpoint — not by this module.
- `O-109` — The condensate collector, its count transducer and its lead are mechanical design of this module's humidity tract; the counter itself is M05's (ADR-0014 rev 7 d4) and `E0006-000001` has no free pulse input (`O-116`). Volume per count is a commissioning constant of the collector and is unset. Blocks the `O-111` measurement.
- `O-110` — CO₂ branch unspecified: source, regulator, valve, minimum dose against the §2.2 grow volume, and CO₂ accumulation in an occupied room. Blocks `D14` and the §3.4 CO₂ population.
- `O-111` — Transpiration is assumed at 140 ml/day and is unmeasured; it sets the humidity-tract mass flow. Closed by the first `O-109` measurement.
- `O-112` — HX1's `Rth` is a catalogue figure that excludes spreading from two point-source modules on the base. It sets `D10` directly. Bench measurement required: a resistor of known power, thermocouples on the base and in the stream.
- `O-113` — U9 part not selected; `F11`'s fail-to-safe wiring follows from its output behaviour when its outputs are de-asserted. Blocks `F11`.
- `O-114` — The luminaire's position relative to the grow volume sets 19 W or 32 W of §2.2 day load and decides whether `D10` binds at the wet edge of the band. Owned by A02-LIGHT and the enclosure, consumed here.
- ~~`O-115`~~ — ~~No governing ADR for cabinet climate conditioning.~~ — closed 2026-09-08 by ADR-0032.

## 13. Maturity

| Rung | State |
|---|---|
| **Pre-schematic** | **Current.** Complement and requirements fixed; values estimated or `verify` |
| Schematic-frozen | Not reached |
| As-built | Not reached |

Next rung requires: `O-103`, `O-104`, `O-102` and `O-113` closed, and the component values that
follow — `L1`, `L2`, both `VREF` dividers, driver off-times, the `U7` and `U8` thresholds —
computed against the selected parts.
