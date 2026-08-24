<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE — module specification

- **Date:** 2026-08-24
- **E-number:** `E0002` · module-ID strap `0b001`
- **Governing ADRs:** ADR-0014 (rev 3), ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 2), ADR-0018
- **Companions:** `M02-LIGHT-specification.md`, `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`
- **Supersedes:** the M01 sections of `M01-M06-air-nodes-specification.md` (2026-08-03), split into this document and `M06-VENTILATION-specification.md` on 2026-08-04

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M01-CLIMATE sensor module: sensor complement, measurement accuracy,
electrical, thermal, mechanical and firmware requirements, and their verification.

Not specified here: carrier design (`store/E0001-000003-D-pinmap.md`), cultivation setpoints
(ADR-0003), gateway-side handling of published subjects (ADR-0014 d7).

## 2. Identification

| | Value |
|---|---|
| Module class | M01-CLIMATE |
| Module-ID strap | `0b001` |
| E-number | `E0002` — fully-populated Phase-1 assembly |
| Design version | `E0002-000001` — schematic captured 2026-08-05 |
| Bare design | One layout; each standard populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) |
| Carrier | `E0001`, sensor-module header pair 2×12 + 2×8 (ADR-0014 d5) |

## 3. Function

M01 measures the state of the air at the canopy, inside the growing enclosure.

Expected values below are for the reference cultivation profile
`profiles/strawberry-day-neutral-v1.json`, which is the source of truth for setpoints
(ADR-0000 d2): air temperature 20 °C day / 14 °C night, VPD band 0.8–1.2 kPa in both
photoperiods, CO₂ ambient without enrichment, photoperiod 16 h on / 8 h off, DLI 17–20
mol·m⁻²·d⁻¹ at the fruiting canopy.

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| Air temperature | U1 | −40…+125 °C | 14 °C night, 20 °C day; 12…28 °C including excursions | ±0.1 °C typ |
| Relative humidity | U1 | 0…100 %RH | 48…66 %RH day, 25…50 %RH night (from the VPD band, §6.1) | ±1.0 %RH typ |
| Air VPD (derived, §6.1) | U1 | — | 0.8…1.2 kPa, both photoperiods | ±0.024 kPa at the day band edge |
| CO₂ | U3 | 0…40 000 ppm output; 400…5000 ppm specified | 400…450 ppm ambient baseline; below 400 ppm on depletion during photoperiod (§6.3); elevated at night | ±(50 ppm + 2.5 % of reading) at 400–1000 ppm |
| Barometric pressure | U2 | 300…1100 hPa | 950…1050 hPa | ±0.6 hPa (0–65 °C) |
| Gas resistance (VOC trend) | U2 | No absolute range is specified by the device datasheet | Trend only; no absolute expectation | Uncalibrated. Resolution 0.08 % typ (0.05 min, 0.11 max); noise 1.5 % RMS |
| Secondary T / RH | U2 | Per device | As above | ±0.5 °C, ±3 %RH — not used for VPD |
| Secondary T / RH | U3 | Per device | As above | ±0.8 °C, ±6 %RH at 15–35 °C — not used for VPD; valid only after §10 offset calibration |

Publication rate: seconds. CO₂ is not free-running — U3's periodic measurement interval is
fixed at 5 s, or ≈ 30 s in low-power mode; no other interval exists.

Location: canopy, inside the growing enclosure. Relocatable to any canopy point. All sensors
board-mounted; no leads, no cable-borne I²C.

### 3.1 Environmental envelope of the populated module

U3 is the binding part and narrows the module below the envelope of U1 and U2.

| | U1 SHT45 | U2 BME688 | U3 SCD41 | **Module** |
|---|---|---|---|---|
| Temperature | −40…+125 °C | −40…+85 °C | −10…+60 °C | **−10…+60 °C** |
| Humidity | 0…100 %RH, condensing tolerated | 0…100 %RH, non-condensing | 0…95 %RH, non-condensing | **0…95 %RH, non-condensing** |

§3 admits condensation on excursions. That places U2 and U3 outside their stated operating
conditions, not merely outside their accuracy. U1 is unaffected — it is specified as fully
functional in a condensing environment, and its heater exists for exactly this recovery. The
consequence for U2 and U3 is an unquantified post-excursion validity gap. O-47.

### 3.2 Exclusions

The following are outside M01's scope and are specified by the module class named.

| Quantity | Owning class |
|----------|--------------|
| Spectrum, PPFD, UV-A at the canopy | M02-LIGHT |
| Air velocity, filter Δp, envelope Δp | M06-VENTILATION |
| Canopy surface temperature, leaf VPD | M04-PLANT |
| Solution pH, EC, solution temperature | M03-ANALYTICS |
| Ambient T/RH, absolute pressure reference, ambient CO₂ reference, ambient irradiance | M07-AMBIENT |
| Heading, tilt, installation integrity | M07-AMBIENT |
| Bus voltage and current, door, leak | M05-SAFETY |

M01 carries no light sensor, no inertial or magnetic sensor, and no air-velocity sensor.

### 3.3 Partial populations

Optional parts shall be footprints that can be left unpopulated with no other change to the
board. U3 shall not sit in the I²C pull-up path or in the sense path of any other part.

A partially populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) and
requires no firmware change; the boot probe registers responders only (§10).

Two parts exist solely to serve U3 — U5 and FB1 (§7.4). A population without U3 leaves all
three unfitted. R4 and R5 (§5.1) are fitted in every population without exception.

U3 dominates the assembly cost of the fully-populated board, so the CO₂-less population is a
real procurement option, not a theoretical one.

## 4. Sensor complement

| # | Device | Ordering part | LCSC | Function | Rail | I²C address | Address type |
|---|--------|---------------|------|----------|------|-------------|--------------|
| U1 | Sensirion SHT45 | `SHT45-AD1F-R2` | C5360602 | Primary T / RH, VPD source | 3.3 V | `0x44` | Fixed |
| U2 | Bosch BME688 | `BME688` | C3664478 | Gas / VOC trend, barometric pressure, secondary T / RH | VDD 1.8 V, VDDIO 3.3 V | `0x76` | Strap, SDO low; `0x77` unused |
| U3 | Sensirion SCD41 | `SCD41-D-R2` | C3659294 | CO₂, photoacoustic NDIR | 2.8 V | `0x62` | Fixed |

U1 is the primary T/RH source. U2 and U3 internal T/RH shall be published as their own subjects
and shall not enter the VPD computation.

### 4.1 Packages

| # | Package | Body size | Notes |
|---|---------|-----------|-------|
| U1 | DFN-4 | 1.5 ±0.1 × 1.5 ±0.1 × **0.69 ±0.1 mm**, the membrane variant. Sensor opening ⌀0.4 mm | Centre die pad shall not be soldered and shall carry no net; no copper under the device except at the pin pads |
| U2 | LGA-8 | 3.0 × 3.0 × 0.93 mm | Pin numbering runs clockwise in top view — an inversion-class hazard |
| U3 | LGA-20 | 10.1 × 10.1 × 6.5 mm | `DNC` pads shall be soldered to floating pads and connected to no net. Pin-1 key is the notched corner of the protection membrane |

Every footprint shall be checked against the physical part with a 1:1 paper printout before
ordering (V6). The check shall include the U1 die-pad exclusion and the U3 pad count — neither
is visible in the schematic.

### 4.2 Approved alternatives

Both alternatives share the layout, the footprint and the I²C address of the part they replace.
Substitution is a BOM line change and requires no board revision.

| # | Alternative | Consequence |
|---|-------------|-------------|
| U1 | `SHT45-AD1B-R2` (C5221601) | Loses the PTFE membrane, therefore loses the only physical protection of the sensor opening (M1). Accuracy, response time and address unchanged. Package is 0.54 ±0.05 mm tall against 0.69 ±0.1 mm, and the opening widens to ⌀0.6 mm; both are footprint-compatible |
| U2 | `BME680` | Temperature accuracy ±1.0 °C instead of ±0.5 °C; pressure accuracy degrades; no parallel mode, therefore no hardware-sequenced heater profile (§6.4). Requires the firmware discrimination of §10 |

### 4.3 Supporting active parts

| # | Part | LCSC | Function |
|---|------|------|----------|
| U4 | `TLV70018DCKR` | C133796 | 1.8 V rail for U2 VDD (§7.4) |
| U5 | `NCP114AMX280TCG` | C133054 | 2.8 V rail for U3 (§7.4) |
| FB1 | `BLM21AG121SN1D` | C88990 | Series element of the U3 supply filter (§7.4) |

## 5. Interfaces

Schematic: `store/E0002-000001.kicad_sch`.

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1, U2, U3 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b001`: STRAP_0 high, STRAP_1 low, STRAP_2 low |
| Header B pin 1 / 2 — 3V3, GND | Strap reference |

No other header signal is claimed. `GPIO_1`/`GPIO_2` (PA9/PA10) are not used, so USART1 remains
available as the carrier debug console throughout boot and run (pin map note 6).

Module-ID bit 1 (`STRAP_1`, PA6) reaches the MCU from carrier revision `E0001-000003`
(`firmware/common/carrier/e0001.h`). M01's pattern has bit 1 = 0 and reads correctly on every
revision.

The strap bit index equals the strap signal index: `STRAP_0` is bit 0. `0b001` is therefore
STRAP_0 high with the other two low. A transposed pattern is electrically valid and produces a
node that identifies as another module class; neither ERC nor the 1:1 printout detects it.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, single local segment, all devices board-mounted |
| Speed | 100 kHz standard mode. Platform default set by the shared carrier driver, not by any device on this module: U1 supports fast mode plus, U2 up to 3.4 MHz, U3 up to 400 kHz |
| Pull-ups | R4, R5 = 4.7 kΩ to 3.3 V, on the module. The carrier carries none |
| Pull-up ownership | Not stated in the header contract (ADR-0014 d5). Every sensor module must carry its own; the rule exists in `E0002` and `E0006` and in no document. O-51 |
| Sizing check | 4.7 kΩ sinks 0.70 mA, against U1's 390 Ω floor and U3's 3 mA test condition. Rise time at 100 kHz admits ≈ 250 pF, against three devices on board traces |
| Population | R4 and R5 are fitted in every population (§3.3) |

## 6. Measurement requirements

### 6.1 VPD

Computed on-node from U1 only:

```
VPD = e_s(T) · (1 − RH/100)        e_s(T) = 0.61078 · exp(17.27·T / (T + 237.3))   [kPa]
```

The quantity published is **air VPD** (ADR-0003 d7). Leaf VPD is not published (§3.2).

Profile band edges, from `profiles/strawberry-day-neutral-v1.json`:

| Photoperiod | T | e_s | VPD 0.8 kPa → RH | VPD 1.2 kPa → RH |
|-------------|---|-----|------------------|------------------|
| Day | 20 °C | 2.338 kPa | 65.8 %RH | 48.7 %RH |
| Night | 14 °C | 1.599 kPa | 50.0 %RH | 24.9 %RH |

Uncertainty at the day band edge (20 °C, 65.8 %RH, VPD 0.800 kPa):

| Term | Sensitivity | U1 accuracy | Contribution |
|------|-------------|-------------|--------------|
| Temperature | ∂VPD/∂T = (1 − RH/100) · de_s/dT = 0.050 kPa/K | ±0.1 °C typ | ±0.005 kPa |
| Humidity | ∂VPD/∂RH = e_s/100 = 0.023 kPa per %RH | ±1.0 %RH typ | ±0.023 kPa |
| Combined (RSS) | — | — | ±0.024 kPa, ±3.0 % of reading |

Relative uncertainty from the humidity term, independent of temperature:

```
δVPD / VPD  =  δRH / (100 − RH)
```

| Operating point | RH | VPD | Relative uncertainty at ±1.0 %RH |
|-----------------|----|-----|----------------------------------|
| Day, band low | 65.8 % | 0.800 kPa | ±2.9 % |
| Day, band high | 48.7 % | 1.200 kPa | ±1.9 % |
| Night, band low | 50.0 % | 0.800 kPa | ±2.0 % |
| Night, band high | 24.9 % | 1.200 kPa | ±1.3 % |
| Excursion above band | 85 % | 0.351 kPa (20 °C) | ±6.7 % |

Within the profile band the uncertainty is ±1.3…±2.9 %. It degrades on humid excursions above
the band. These figures use the typical accuracy of U1; the maximum is defined by a datasheet
graph rather than a single figure and is larger. No VPD uncertainty limit is stated by ADR-0003
d7 or by the profile instance. O-39.

Prior revisions of this document carried ±1.5 %RH for U1, a figure matching no member of the
SHT4x family — SHT45 is ±1.0 %RH typ, SHT40 and SHT41 are ±1.8 %RH. The error propagated from
ADR-0014 d4 and inflated the VPD budget by half. ADR-0014 d4 no longer states an accuracy for
U1; this document is its only home.

### 6.2 Temperature bias at U1

A bias ΔT at U1 raises reported temperature and lowers reported RH; both raise computed VPD.
Evaluated at the day band edge (true 20 °C, 65.8 %RH, VPD 0.800 kPa):

| Bias ΔT | Reported RH | Computed VPD | Error |
|---------|-------------|--------------|-------|
| +0.1 K | 65.4 % | 0.814 kPa | +1.7 % |
| +0.5 K | 63.8 % | 0.873 kPa | +9.1 % |
| +1.0 K | 61.9 % | 0.948 kPa | +18.5 % |

This transfer function sets the thermal requirements of §8.

### 6.3 CO₂

| Requirement | Value | Reference |
|-------------|-------|-----------|
| Regulation | None. CO₂ runs at ambient, monitored not enriched | ADR-0003 d8 |
| Range of interest | Ambient baseline 400–450 ppm; depletion during photoperiod; accumulation from respiration at night | ADR-0014 d4 |
| Output range | 0–40 000 ppm. Specified accuracy applies over 400–5000 ppm only | — |
| Specified accuracy, by band | ±(50 ppm + 2.5 % of reading) 400–1000 ppm; ±(50 ppm + 3 % of reading) 1001–2000 ppm; ±(40 ppm + 5 % of reading) 2001–5000 ppm. Datasheet default conditions: 25 °C, 50 %RH, 1013 mbar, periodic measurement, 3.3 V. These are the SCD41 figures; the SCD40 carries ±(50 ppm + 5 % of reading) across 400–2000 ppm | — |
| Depletion below 400 ppm | Reported, outside the specified accuracy band. The device neither saturates nor faults. Closes O-41: no part change is required | §6.3.1 |
| Pressure compensation | Required. Absolute pressure written to the SCD41 compensation register; uncompensated pressure error propagates ≈ 1:1 into reported concentration | — |
| Compensation source | **U2, on board.** ±0.6 hPa is 0.06 % of 1013 hPa, ≈ 0.25 ppm at 400 ppm, against U3's ±50 ppm floor — a margin of two orders. Closes O-34 | §6.3.2 |
| Automatic self-calibration | **Disabled.** ASC requires exposure to ≈ 400 ppm at least weekly, which a closed cabinet need not reach | §6.3.1 |
| Forced recalibration | Required in place of ASC. Procedure §6.3.1; interval undefined. O-5 | — |
| Cross-calibration | Against M07's reference instance before any M01↔M07 concentration difference is published — undefined. O-7 | — |

#### 6.3.1 Field calibration

ASC and depletion below 400 ppm are one problem seen twice: exposure below 400 ppm degrades
accuracy **while ASC is enabled**, and ASC cannot function in a volume that never returns to
400 ppm. Disabling ASC resolves both and makes FRC mandatory.

FRC procedure, per the device datasheet:

1. Operate in the mode used in normal operation, at the supply voltage used in the application,
   for at least 3 minutes in air of homogeneous and constant CO₂ concentration.
2. Send `stop_periodic_measurement`; wait 500 ms.
3. Send `perform_forced_recalibration` with the reference concentration; after 400 ms read the
   correction magnitude. A return of `0xffff` means the FRC failed.

Two constraints on when it may be performed:

- **Not earlier than five days after the sensor is soldered.** Assembly temporarily displaces
  accuracy; it is restored by FRC or ASC only after that interval.
- **At 2.8 V**, the rail of §7.4, not at bench 3.3 V. The datasheet requires the application
  voltage. Calibrating at the wrong rail injects an error that is invisible afterwards.

#### 6.3.2 Pressure source when U2 is not fitted

§6.3 resolves the compensation source for the fully-populated board. A population without U2
has no on-board barometer, and the firmware shall fall back to a fixed value from the deployment
profile or to M07's instance. Rule undefined. O-34 is closed for the populated case; this
residue is O-48.

### 6.4 Gas / VOC

| Requirement | Value |
|-------------|-------|
| Output | Gas resistance, uncalibrated. Published as a trend, not a concentration |
| Sweep | **R(T) across four setpoints — 200, 250, 320, 400 °C**, 150 ms dwell each, taken as consecutive forced-mode shots within one scan (~760 ms). Parallel mode is not used, so the §4.2 alternative produces the same vector and `variant_id` stays off the critical path. Analytes separate by where in temperature they respond; a single point cannot express that at any baseline |
| Scan interval | **10 s**, not the 1 s publish tick. The hotplate holds 320 °C for 150 ms per scan; at 1 Hz that is a 15 % duty cycle beside U1, whose stability is T2. At 10 s it is 1.5 %. A VOC baseline moves over hours, and BSEC itself samples gas at 3 s (low power) and 300 s (ultra-low power), so the rate costs no trend information. `M01_GAS_PERIOD_S`, reported on the boot line |
| Parking | `M01_GAS_SCAN` = 0 stops the hotplate entirely and withholds 4117, leaving U2 as the barometer of §6.3 alone. Pressure and the secondary T/RH are unaffected either way — they come from a conversion on every publish tick with the gas step disabled |
| Software | Sensor API only. BSEC is a closed-source binary under a separate licence agreement and is incompatible with the AGPL-3.0-or-later reference firmware (ADR-0002 d5) |
| Consequence | Humidity compensation, baseline tracking and long-term drift correction of the gas signal are BSEC functions and are not available on the node. They are derived gateway-side as a soft sensor under ADR-0016 d5, as DLI is; the node publishes raw resistance and its setpoint, and holds neither a baseline across a reset nor a wall clock to age one against |
| Excluded outputs | IAQ, CO₂-equivalent, bVOC-equivalent and gas-scan classification are BSEC outputs and are not published. CO₂ is measured by U3, not estimated from VOC |
| Measured response | Exhaled breath at the sensor, 2026-08-24: as the stimulus cleared, resistance rose **+187 % at 200 °C, +128 % at 250 °C, +65 % at 320 °C, +38 % at 400 °C**, and R(200 °C)/R(400 °C) moved 2.20 → 4.56. Light reducing species respond most on a cool plate; a single 320 °C point sees roughly a third of what 200 °C sees, which is what the low setpoints are for |
| CO₂ estimation, measured | During that same stimulus CO₂ rose 52 % while the 200 °C resistance rose 58 % — the reducing-gas load fell while CO₂ climbed. Correlation over the whole transient is r = −0.79 but inverts on the leading edge, so a fit calibrated against U3 passes on the decay and fails on the rise. The exclusion above is measured on this board, not assumed |
| Thresholds | Relative to a tracked per-device baseline. Absolute thresholds shall not be used |
| Baseline validity | A baseline is tied to the scan interval as well as the setpoint: the hotplate cools fully between scans at 10 s and did not at 1 s, and the same air read 161 kΩ at the 1 s rate against 18 kΩ at 10 s (bench 2026-08-24). Changing `M01_GAS_PERIOD_S` invalidates every baseline collected at the previous rate |
| Confounding | Responds to temperature and humidity; a VOC excursion concurrent with a T or RH excursion is not independent evidence |
| Drift | Hotplate ageing; baseline is per-device and moves |
| Heater profile | The node publishes the vector, not a reduction of it — closes O-49. U2's parallel mode would sequence the steps in hardware but exists only on the BME688; consecutive forced-mode shots give the same R(T) on both variants and are what §6.4's sweep uses |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured at the 2026-08-24 bring-up (§11.2). O-35, V3 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02). M01 exceeds it |
| Burst reflected to `+12 V` | ≈ 60–70 mA, calculated as 205 mA × 3.3 V / 12 V ÷ η at η = 0.85, plus the losses of §7.4. Measured by V3. O-35 |

### 7.2 Device currents

| # | Device | Idle | Typical | Peak | Rail |
|---|--------|------|---------|------|------|
| U1 | SHT45 | 0.08 µA | 320 µA typ / 500 µA max during conversion | 60 mA typ / **100 mA max** with the 200 mW heater | 3.3 V |
| U2 | BME688 | 0.15 µA sleep | 12 mA at 320 °C hotplate; 3.96 mA average in standard gas-scan mode | 18 mA at hotplate switch-on | 1.8 V |
| U3 | SCD41 | 0.15 mA | 15 mA typ / 18 mA max, periodic mode at 5 s | 175 mA typ / **205 mA max**, sustained | 2.8 V |

U1's heater has three power levels (20 / 110 / 200 mW) and two durations (0.1 / 1 s), with a
maximum duty cycle of 10 %. Firmware selects the level; the rail shall tolerate the maximum.
The datasheet is internally inconsistent on this current — its text says ≈ 75 mA where its table
says 60 mA typ / 100 mA max. 100 mA is taken as the design figure.

U2's figures are stated by its datasheet at VDD ≤ 1.8 V. §7.4 supplies 1.8 V, so they apply as
written. This is the reason the rail exists.

U3's peak is a sustained current, not a microsecond transient: at 15 mA average and a 5 s
interval the device draws ≈ 205 mA for a substantial fraction of a second. No capacitor supplies
that; the regulator does. §7.5 covers only the regulator's transient response.

### 7.3 3.3 V rail

Supplied by the carrier's TPS54302 buck (400 kHz fixed, 5 ms internal soft start), shared with
the MCU and CAN transceiver. No rail redesign. On this module the 3.3 V rail feeds U1, U2's
VDDIO, the I²C pull-ups, the strap reference, and the inputs of U4 and U5.

The buck's 5 ms soft start gives 0.66 V/ms, against U1's 20 V/ms ceiling above which the device
may reset. Margin is thirtyfold; no further action.

### 7.4 Module-local rails

M01 is the first sensor module to generate rails of its own. ADR-0002 d3 names 3.3 V as the only
rail *on the carrier*; a rail generated on a module is a different object and is not covered by
the header contract (ADR-0014 d5). One module is not a pattern; recorded, not generalized. O-43.

| Rail | Source | Consumer | Reason |
|------|--------|----------|--------|
| 3.3 V | Header | U1, U2 VDDIO, R4/R5, straps | — |
| 1.8 V | U4, from 3.3 V | U2 VDD only | U2's currents, efficiency and heat dissipation are characterized at ≤ 1.8 V and unspecified at 3.3 V. The excess appears as on-die dissipation beside U1, against the 0.1 K budget of T2 |
| 2.8 V | U5, from 3.3 V via FB1 | U3 only | U3 requires ≤ 30 mV of unloaded supply ripple and recommends a dedicated LDO. §7.4.1 |

Neither rail is a headroom risk: U2's minimum VDD is 1.71 V against 1.8 V nominal; U3's minimum
is 2.4 V against 2.8 V nominal, and U5's dropout is 155 mV at 300 mA.

#### 7.4.1 U3 supply filter

The 30 mV limit is on the rail's *own* ripple, measured without the sensor's load. It is
therefore a statement about the carrier's buck, and no bulk capacitance addresses it: a
capacitor answers droop, not the converter's ripple.

The carrier runs in pulse-skipping at this load. Peak inductor current at the node's draw is
≈ 0.37 A against the device's ≈ 0.5 A skip threshold, so the converter is in pulse-skipping
continuously — and U3's own 205 mA burst lifts it above the threshold and back every 5 s, so
the mode transition is coincident with the measurement.

Pulse-skipping ripple has two components, and they are attenuated by different elements:

| Component | Frequency | Element | Attenuation |
|-----------|-----------|---------|-------------|
| Individual pulses | 400 kHz | FB1 with the 20 µF that follows it | ≈ 22 dB. U5's PSRR is at its minimum here (20–27 dB) and does not carry this band |
| Burst envelope | kHz range | U5 | ≈ 55 dB at 10 kHz, flat across load current |

The two are complementary, not redundant. Removing FB1 leaves the 400 kHz component with only
U5's PSRR trough; removing U5 leaves the envelope with a series element that is transparent
below 100 kHz. Neither shall be treated as optional.

Filter arrangement is series FB1 followed by 2 × 10 µF at U5's input. A capacitor ahead of FB1
is not fitted: the carrier's own output capacitance is the low-impedance source, and moving
capacitance to the source side would reduce the shunt and cost 6 dB. LC resonance is ≈ 82 kHz
at nominal capacitance with Q ≈ 1 from FB1's 100 mΩ DCR — damped, below the switching
frequency, no peaking of consequence.

**Nothing here is measured.** No oscilloscope is available to the project, so the ripple that
motivates U5 and FB1 has never been observed. The design is dimensioned against the datasheet
limit rather than against the rail. O-44.

### 7.5 Bulk capacitance at U3

| Parameter | Value |
|-----------|-------|
| Function | Covers U5's load-transient response interval, not the 205 mA burst itself |
| Step current | ΔI = 205 − 18 = 187 mA |
| Allowed droop | 400 mV, from the 2.8 V rail to U3's 2.4 V minimum |
| Stability range | **1.0…4.7 µF effective.** 1.0 µF is U5's minimum output capacitance; 4.7 µF its upper characterized point. Closes O-3 |
| Nameplate | **4.7 µF.** Not to be raised above U5's characterized range |
| Specification basis | Effective capacitance at 2.8 V DC bias, not nameplate. Recovered by dielectric, voltage rating and case size |
| Dielectric | **X7R minimum.** X5R not permitted |
| Voltage rating | **≥ 25 V** |
| Case | **0805 minimum** |
| Acceptance | **≥ 2.0 µF and ≤ 4.7 µF effective** at 2.8 V DC bias over −10…+60 °C, from the vendor DC-bias curve for the ordering part. V8 |
| Recorded in the design | Only `4.7uF`, `0805` and the distributor code `C1779`. Dielectric, voltage rating and manufacturer part are **not** captured, so the three requirements above cannot be checked against what was fitted. O-73 |
| Local decoupling | 100 nF at U3's supply pins, in addition and not as a substitute |

The value is taken from the regulator's characterization rather than computed from a droop
budget, because the computed figure depends on a transient response interval that the datasheet
already publishes as a measured curve. Confirm against that curve that the excursion at a
190 mA step stays well inside 400 mV.

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | U1 shaded from direct line of sight to the luminaire, air exchange maintained (M1, M5). Canopy irradiance from the profile: DLI 17–20 mol·m⁻²·d⁻¹ over 16 h = 295–347 µmol·m⁻²·s⁻¹ ≈ 65–76 W/m² PAR, before non-PAR emission | Profile, ADR-0003 d11/d12 |
| T2 | Bias at U1 from all board-internal sources ≤ **0.1 K**, giving ≤ 1.7 % induced VPD error per §6.2 | §6.2, §8.1 |
| T3 | Maximum lateral separation between U1 and every heat source — U2, U3, U4, U5; thermal relief slot between U1 and U2; no shared copper pour | — |
| T4 | Maximum lateral separation between U1 and U3; no shared copper pour | — |
| T5 | U1's centre die pad unsoldered, per §4.1 and §11.1 | §8.1 |

Unshaded bias follows ΔT ≈ α·E/h. α and h are fixed by the enclosure design, not by any part;
O-38, verified by V2.

On-board heat sources: U2's gas hotplate, cycled to several hundred °C; U4 and U5, each
dissipating the rail difference times the load current; U3, whose contribution is not separable
by specification — see §8.1.

Supplying U2 at 1.8 V does not remove heat from the board — it relocates it from U2's die to U4,
whose position is unconstrained by the diffusion access that M2 imposes on U2. That freedom is
the benefit; it is realized only if T3 is honoured for U4 and U5 as well.

### 8.1 U1 thermal metrics

Device datasheet values for the **die pad not soldered** configuration required by T5.
Simulation-based.

| Metric | Heater off | Heater on, 200 mW |
|--------|-----------|-------------------|
| R_θJA, junction-to-ambient | 297 K/W | 357 K/W |
| R_θJC, junction-to-case | 191 K/W | 257 K/W |
| R_θJB, junction-to-board | 193 K/W | 258 K/W |
| Ψ_JB, junction-to-board characterization | 191 K/W | 254 K/W |
| Ψ_JT, junction-to-top characterization | 44 K/W | 112 K/W |

Die pad soldered gives R_θJA 246 K/W. T5 requires it unsoldered.

| Derived | Value |
|---------|-------|
| U1 self-heating, measurement | 500 µA max at 3.3 V = 1.65 mW → 0.49 K at steady state. At 1 Hz with an 8.3 ms high-repeatability conversion the duty is 0.83 %, so the sustained contribution is ≈ 0.004 K |
| U1 self-heating, heater | 200 mW → 59 K at steady state. Bounded by the 10 % maximum duty cycle and the 0.1 / 1 s pulse lengths, not by the rail. Sets the §10 rule that a heater pulse shall not overlap a U3 measurement |

U3 self-heating: no datasheet figure. Its temperature offset compounds measurement mode,
self-heating of nearby components, ambient temperature and air flow; default 4 °C, recommended
range 0–20 °C, determined in the finished board at thermal equilibrium. Measured by V7. O-45.

## 9. Mechanical and enclosure

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | U1: diffusion access to canopy air; shaded per T1; no conformal coating over the sensor opening. The PTFE membrane of the fitted variant protects against particles; it is not a coating mask and does not remove this requirement | T1, §4.2 |
| M2 | U2: diffusion access to canopy air; no conformal coating over the gas port | — |
| M3 | U3: air exchange with the measured volume; no conformal coating over the optical cavity. The protection membrane and dust cover shall not be removed or wetted | — |
| M4 | Conformal coating on all other areas. Installed in the growing volume: expected 25–66 %RH, condensation possible on excursions | §3 |
| M5 | Shading per T1 shall not obstruct the air exchange required by M1 | T1 |
| M6 | Enclosure shall not seal U1 into a pocket without air exchange | §10, heater |
| M7 | Mounted at canopy height; height adjusted as the canopy grows, recorded as deployment metadata | ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8, supported at both ends; no standoffs required | Pin map, header section |

M4 and the assembly constraints of §11.1 conflict: U3 forbids any board-wash step after reflow,
and conformal coating conventionally follows cleaning. Either a no-clean process is qualified
end to end or M4 is narrowed. Unresolved. O-46.

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b001` — STRAP_0 high, STRAP_1 low, STRAP_2 low |
| Boot probe addresses | `0x44`, `0x76`, `0x62`. An ACK is not identification: M05's INA226 (`E0006`) answers `0x44` as well, so an M01 image on an E0006 finds U1 "present". The addresses are on different modules and never share a bus, so this is not a conflict — but the probe is backed by a device-specific read (U1's serial, U2's `chip_id`, U3's word CRC), and a foreign part fails it. Observed on the bench 2026-08-10 |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| U2 variant discrimination | `chip_id` is `0x61` on both BME688 and BME680 and does not distinguish them. `variant_id` at `0xF0` does: `0x01` for the fitted BME688, `0x00` for the BME680. Source: the vendor's BME68x Sensor API — `BME68X_REG_VARIANT_ID` `0xF0`, `BME68X_VARIANT_GAS_HIGH` `0x01`, `BME68X_VARIANT_GAS_LOW` `0x00`. Required before parallel mode is commanded — the alternative part of §4.2 does not implement it |
| Primary T/RH | U1 only |
| Derived | Air VPD per §6.1 |
| CO₂ compensation | Pressure from U2 written to U3's compensation register (§6.3), refreshed each cycle and only when the value changed. Fallback when U2 is absent: **1013 hPa**, compiled in — closes O-48 |
| CO₂ self-calibration | ASC disabled; FRC per §6.3.1. FRC is not implemented: §6.3.1 bars it until five days after assembly and requires a reference concentration, so it is a commanded bench operation and awaits the command surface |
| U3 temperature offset | Shall be determined for this board under its operating conditions in thermal equilibrium and written to the device. The default is 4 °C and is not this board's value. U3's published T and RH are invalid until it is set. O-45. Firmware reads the offset at boot and logs it; it does not write one, because writing the default back would be a value pretending to be a calibration |
| U3 settings persistence | `persist_settings` writes EEPROM rated for at least 2000 cycles. It shall be issued only when configuration actually changed, never unconditionally at boot |
| VOC | Trend against a tracked per-device baseline; raw gas resistance via the sensor API (§6.4). **Vector, forced mode** — the four setpoints of §6.4 run back to back within one scan, each 150 ms, and publish as one `GasResistance.2.0` with its setpoints; closes O-49. Parallel mode is not commanded and is not needed. Scanned every **10 s**, not every publish tick. On the other ticks the conversion runs with no heater profile and the gas step cleared in `ctrl_gas_1`, taking ~20 ms instead of ~190 ms and yielding pressure and the secondary T/RH only. Pressure and the secondary T/RH are published once per scan, from the step that opens it, so a sweep does not put the same air on the wire four times. `M01_GAS_SCAN` = 0 parks the hotplate and withholds 4117 |
| SHT45 heater | Disabled by default. When enabled for condensate recovery, its pulse shall not overlap a U3 measurement window. Lowest sufficient power level preferred; the rail tolerates the highest |
| Rail failure | Failure of U4 or U5 removes U2 or U3 from the bus. The boot probe handles this as absence; *not fitted*, *failed* and *unpowered* remain indistinguishable. O-37 |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m01_climate/` — `main.c`, `module_id.h`, `sensors.{h,c}`, `drivers/{sensirion,sht4x,bme68x,scd4x}` |
| Node-ID | 96 is M05's; M01 takes **97**, static for bring-up (ADR-0005 d6) |
| Publication rate | 1 s for U1 and the derived VPD; U2 lands ≈ 190 ms later in the same second (its conversion is triggered on the tick and collected afterwards); U3 publishes at its own fixed 5 s |

### 10.1 Published subjects

Baked defaults in the unregulated range. ADR-0005 d7 makes these
`uavcan.pub.<name>.id` register entries; the firmware does not yet carry them.
M05 holds 4096–4102.

| ID | Quantity | Source | Type |
|----|----------|--------|------|
| 4112 | Air temperature | U1 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4113 | Air relative humidity | U1 | `industryflow.greenhouse.climate.RelativeHumidity` (ratio) |
| 4114 | Air VPD | derived, §6.1 | `uavcan.si.sample.pressure.Scalar` (Pa) |
| 4115 | CO₂ | U3 | `industryflow.greenhouse.climate.Co2Concentration` (mole fraction) |
| 4116 | Barometric pressure | U2 | `uavcan.si.sample.pressure.Scalar` (Pa) |
| 4117 | Gas resistance sweep | U2 | `industryflow.greenhouse.climate.GasResistance.2.0` — setpoints (°C) and resistances (Ω) index-for-index, one `valid` for the sweep. Published at the scan interval of §6.4, not at the 1 s tick; absent entirely while parked |
| 4118 | Secondary temperature | U2 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4119 | Secondary humidity | U2 | `industryflow.greenhouse.climate.RelativeHumidity` (ratio) |
| 4120 | Secondary temperature | U3 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4121 | Secondary humidity | U3 | `industryflow.greenhouse.climate.RelativeHumidity` (ratio) |

Ten subjects, not one record: a partial population must be able to omit any one
of them (ADR-0005 d8). 4118–4121 are the secondary sources of §4 and are not
admissible for VPD; 4120 and 4121 additionally carry the uncalibrated offset of
O-45 until V7 runs.

The three climate types are minted because the standard set has no humidity,
concentration or electrical-resistance sample type (ADR-0005 d2). All three are
unscaled — ratio, mole fraction, ohm — for the reason ADR-0005 rev 1 gave for
joule over watt-hour: %RH, ppm and kΩ are display conventions, and display is
the gateway's concern.

## 11. Verification

| ID | Verifies | Method | Status |
|----|----------|--------|--------|
| V1 | T2 | Board at thermal equilibrium in still air, U1's heater disabled throughout. U1's reported temperature recorded in four states: (a) U2 gas scan idle and U3 idle; (b) U2 gas scan running, U3 idle; (c) U2 idle, U3 periodic at 5 s; (d) both running. T2 is met when the spread across the four states is ≤ 0.1 K. The measurement is differential and needs no absolute reference — an offset common to all four states is not T2's subject. A reference thermometer in the same air is used only to confirm the ambient held during the run | Not executed |
| V2 | T1 | U1 reading with and without the luminaire at the profile's operating PPFD, against a shaded reference thermometer in the same air | Not executed. No luminaire on the bench |
| V3 | §7.1, §7.4 | Node draw on `+12 V` measured at first bring-up, average and during a U3 burst. Rail voltages at U2 VDD and U3 VDD measured under load, to detect a regulator out of regulation. Rail ripple against the 30 mV limit of §7.4.1 requires instrumentation the project does not have — O-44 | Not executed at the 2026-08-24 bring-up. O-35 |
| V4 | §6.1 | U1 against a reference hygrometer at two points of the ADR-0003 d7 band | Not executed |
| V5 | §6.3 | CO₂ against M07's reference instance in the same air (O-7), no earlier than five days after assembly and at the 2.8 V rail | Blocked: no M07 instance exists. O-7 |
| V6 | §4.1 footprints | 1:1 paper printout against the physical part, all three devices, including U1's unsoldered die pad and U3's full pad count | Not executed. Parts now in hand |
| V7 | §10, §8.1 | U3 temperature offset determined and written; U3 T/RH against a reference in the same air before and after. Determined in the finished board at thermal equilibrium in the operating mode used in the application, per the device datasheet. This is also the only source of U3's self-heating contribution (§8.1) | Not executed. Device holds the 4.000 °C factory default (§11.2). O-45 |
| V8 | §7.5 | C8's effective capacitance at 2.8 V DC bias read from the vendor's DC-bias curve for the specific ordering part, against the 2.0–4.7 µF acceptance band. Executed at BOM release, before the `L` document is issued | **Not executable as written, 2026-08-24.** The design data identifies no ordering part: `E0002-000001-L.csv` carries `4.7uF / 0805 / C1779` and the schematic carries no manufacturer, dielectric or voltage rating for C8. There is no curve to read. O-73 |

### 11.1 Assembly constraints

The board is machine-assembled; U1 and U3 are not hand-solderable with the equipment available.

| Constraint | Source |
|------------|--------|
| Reflow peak ≤ 245 °C, above liquidus < 60 s | U3, the binding part. U1 and U2 admit 260 °C |
| No vapour-phase reflow; no second reflow pass; no added flux | U3 |
| No board-wash step after reflow | U3 — conflicts with M4, see §9 |
| Solder height ≥ 50 µm after reflow | U2, for mechanical decoupling; affects its pressure offset |
| U1 die pad unsoldered, no copper beneath except pin pads | U1 |
| U3 requires an assembly fixture and is Standard-PCBA only | Vendor constraint; sets the service class for the whole board |
| MSL | U1 and U2 are MSL 1; U3 is MSL 3 with 168 h floor time |

### 11.2 Bench bring-up 2026-08-24

`E0002-000001` on carrier `E0001-000002`, node-ID 97, firmware `store/E0001-000001-F.hex`. One
M05 node also on the bus.

| Item | Result |
|------|--------|
| U1 SHT45 at `0x44` | Present; serial read CRC-valid |
| U2 BME68x at `0x76` | Present; variant **BME688**, not the §4.2 alternative |
| U3 SCD41 at `0x62` | Present; serial read CRC-valid |
| U3 temperature offset | 4.000 °C, the device default. Not this board's value — O-45, V7 |
| U3 automatic self-calibration | Found already disabled; no EEPROM cycle spent this boot (§6.3) |
| Published subjects | All ten of §10.1. Rates confirmed by frame count: 1 Hz for U1, U2 and pressure; 0.2 Hz for U3; 0.1 Hz for the 4117 sweep |
| Gateway | Node identified as M01-CLIMATE; subjects 4112–4121 decoded |
| U1, bench air | 23.18 °C, 45.6 %RH, VPD 1.545 kPa |
| U2, bench air | 23.95 °C, 44.6 %RH, 1021.6 hPa, gas 161 kΩ at the 320 °C heater profile |
| U3, bench air | 528 ppm CO₂, 24.25 °C, 44.9 %RH |
| U1 against M05's U1 in the same air | 0.12 K apart |
| Node draw on `+12 V` | Not measured. O-35, V3 |

The figures above are the first run, on the as-received firmware. The protocol was re-executed
end to end after the corrections below, and §6.1's VPD transfer function was checked against the
published T and RH: recomputed 1637.3 Pa against 1637.8 Pa on the wire, 0.03 %. That is the one
requirement of §6 this bring-up verifies. U1 and U3 accuracy remain the subjects of V4 and V7,
and no V item of §11 is discharged.

Secondary-sensor offsets from U1 move with the gas configuration and are recorded against it:
+0.77 K (U2) and +1.07 K (U3) at the original 1 Hz single setpoint, +0.03 K and +0.29 K with the
hotplate parked, +1.42 K and −1.13 K with the shipped four-point sweep. Neither has an
acceptance limit — §8 budgets bias at U1, and §4 makes both secondary. Whether any of it
displaces U1 is V1, unexecuted.

Firmware defects found and corrected at this bring-up: a latched I²C `AF` made every later
address phase report a result it had not obtained, so the node published heartbeat only while
all three devices were reported present and health stayed NOMINAL (recorded against O-37); the
SCD41 identity read that triggered it ran while the device was measuring; and the gas channel
was sampled at 15 % hotplate duty for a signal no consumer could read. PRs #185, #189, #190,
#191.

## 12. Open items

Continues the `O-` namespace shared with the M02, M05, M06 and M07 specifications. O-2, O-4,
O-6, O-8, O-10, O-11 and **O-42** are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's;
O-52 to O-62 are M02's.

| ID | Item | Blocks |
|----|------|--------|
| ~~O-3~~ | ~~Bulk capacitor value and dielectric at U3~~ — closed 2026-08-05 by §7.5: 4.7 µF effective, from U5's characterization | — |
| O-5 | FRC interval, and the operational trigger for performing one. Procedure and constraints now fixed (§6.3.1) | CO₂ validity |
| O-7 | Cross-calibration procedure, M01 CO₂ against M07 reference | Survey method, ADR-0016 identification |
| O-9 | Fate of ADR-0014 d3, the ≤ 30 cm short-lead provision | ADR-0014 hygiene |
| ~~O-32~~ | ~~Remaining `verify` values~~ — closed 2026-08-06. Height and `variant_id` answered in §4.1 and §10. Gas-resistance range is not specified by the device; §3 carries resolution and noise instead. U3 self-heating has no datasheet figure — measured, V7 and O-45. T1's α and h are enclosure properties — O-38 | — |
| ~~O-33~~ | ~~T2 value and V1 method not confirmed~~ — closed 2026-08-06: T2 fixed at 0.1 K per §6.2; V1 fixed as a four-state differential measurement | — |
| ~~O-34~~ | ~~Pressure source for CO₂ compensation: U2 or M07~~ — closed 2026-08-05 by §6.3 in favour of U2. Unpopulated-U2 residue moved to O-48 | — |
| O-35 | Node power unmeasured; M05's O-31 node-count ceilings assume M05-class nodes | Distribution-board sizing, O-31 |
| ~~O-36~~ | ~~ADR-0014 d2's partial-BOM example cites M01's airflow sensor, moved to M06 in rev 2~~ — closed 2026-08-04 by ADR-0014 rev 3 | — |
| O-37 | Boot probe does not distinguish *not fitted*, *failed* and *unpowered*. A bus fault is a fourth case: at the 2026-08-24 bring-up (§11.2) a latched I²C flag made every device read as present while no transfer completed, and the node published nothing but heartbeat with health NOMINAL | Gateway fault handling, node health reporting |
| O-38 | T1 fixed as a requirement in §8. Residue: no enclosure design realizes the shading, α and h are unfixed (from O-32), and V2 is not executed | Enclosure design, V2 |
| O-39 | Neither ADR-0003 d7 nor the profile instance states a VPD uncertainty limit; §6.1 gives ±1.3…±2.9 % in band, ±6.7 % on humid excursion | Profile validity, U1 part grade |
| O-40 | ADR-0003 d7 does not state air VPD or leaf VPD | Profile interpretation |
| ~~O-41~~ | ~~SCD41's specified range starts at 400 ppm~~ — closed 2026-08-05 by §6.3: depletion is reported outside the specified accuracy band, no part change required | — |
| ~~O-42~~ | ~~Pull-up ownership~~ — **renumbered to O-51** on 2026-08-13. `O-42` was already M06's (module-ID bit 1 unrouted, `M06-VENTILATION-specification.md`, 2026-08-04) and this file duplicated it on 2026-08-06. The number stays M06's; the item moves | — |
| O-51 | Pull-up ownership and value are not in the header contract (ADR-0014 d5). The rule exists in three boards and no document | Next module designed against the contract |
| O-73 | Passive attributes that §7.5 makes acceptance criteria — dielectric, voltage rating, manufacturer part — are not recorded in the design data for C8; the BOM carries a distributor code alone. The same gap applies to every passive whose specification is more than a value and a case | V8, `L` release, and any BOM re-sourcing |
| O-43 | Module-local rails are outside ADR-0002 d3 and the header contract. One instance is not a pattern; revisit at the second | ADR-0014 / ADR-0002 revision |
| O-44 | Carrier runs in pulse-skipping at node load; rail ripple against U3's 30 mV limit has never been measured, and no instrument is available. Design is dimensioned against the datasheet, not the rail | Confidence in §7.4, not the design |
| O-45 | U3 temperature offset uncalibrated; U3's T and RH are invalid until V7 is executed | U3 T/RH validity |
| O-46 | U3 forbids board wash after reflow; M4 requires conformal coating, which conventionally follows cleaning | Coating process, M4 |
| O-47 | Condensation on excursions places U2 and U3 outside their stated operating conditions. Post-excursion validity and recovery undefined | Excursion handling, data validity |
| ~~O-48~~ | ~~Pressure source for CO₂ compensation when U2 is not populated~~ — closed 2026-08-10 by §10: 1013 hPa compiled in. Sea-level standard; the residual CO₂ error is proportional to the pressure deviation, ≈ 1.5 % at 950 hPa, inside U3's ±50 ppm floor at ambient concentration. A deployment at altitude populates U2 or provisions the value | — |
| ~~O-49~~ | ~~Whether the node publishes U2's resistance vector or a reduction of it~~ — **re-closed 2026-08-24 in favour of the vector**, superseding the 2026-08-10 closure to a scalar. That closure rested on parallel mode being unavailable on the §4.2 alternative; the sweep is taken as consecutive forced-mode shots instead, which both variants run, so `variant_id` never reaches the critical path. Carried by `GasResistance.2.0` — a major bump, not the minor one the 1.0 type anticipated, because an extent is shared across a major version and 32 bytes does not hold a sweep | — |
| ~~O-50~~ | ~~C8's dielectric, voltage rating and case are unfixed~~ — closed 2026-08-06 by §7.5: X7R minimum, ≥ 25 V, 0805 minimum, acceptance 2.0–4.7 µF effective at 2.8 V bias, confirmed by V8 | — |

## 13. Maturity

**Schematic captured, not frozen — and fabricated ahead of the rung.** No `verify` value
remains. `E0002` exists, runs and publishes (§11.2), but V6 and V8 were not executed before
fabrication and still hold the rung; O-35 and every V item of §11 remain open.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic captured** ← here | Parts fixed to ordering part numbers; schematic exists; component values determined | `store/E0002-000001.kicad_sch` exists; O-3, O-34, O-41 closed |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-32, O-50 closed ✔; O-33 closed ✔; O-38 fixed as a requirement ✔; **V6 not executed**; **V8 not executed** |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0002` fabricated ✔; bench bring-up executed ✔ (§11.2); **O-35 not measured**; **no V item of §11 executed** |

`M05-SAFETY-specification.md` is the as-built form of this document class. The transition is an
edit of this file; no second E-number is issued for the module having been built.
