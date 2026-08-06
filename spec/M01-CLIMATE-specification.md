<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE — module specification

- **Status:** Schematic captured, not frozen. `E0002-000001` schematic exists; not laid out, not fabricated
- **Date:** 2026-08-05
- **E-number:** `E0002` · module-ID strap `0b001`
- **Governing ADRs:** ADR-0014 (rev 3), ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 2), ADR-0018
- **Companions:** `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`
- **Supersedes:** the M01 sections of `M01-M06-air-nodes-specification.md` (2026-08-03), split into this document and `M06-VENTILATION-specification.md` on 2026-08-04

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M01-CLIMATE sensor module: sensor complement, measurement accuracy,
electrical, thermal, mechanical and firmware requirements, and their verification.

Not specified here: carrier design (`store/E0001-000002-D-pinmap.md`), cultivation setpoints
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
| Gas resistance (VOC trend) | U2 | kΩ…MΩ (`verify`) | Trend only; no absolute expectation | Uncalibrated |
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
| U1 | DFN-4 | 1.5 × 1.5 mm; height per the membrane variant outline, `verify` | Centre die pad shall not be soldered and shall carry no net; no copper under the device except at the pin pads |
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
| U1 | `SHT45-AD1B-R2` (C5221601) | Loses the PTFE membrane, therefore loses the only physical protection of the sensor opening (M1). Accuracy, response time and address unchanged |
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

Module-ID bit 1 (`STRAP_1`, PA6) is unrouted on carrier revision `E0001-000001`
(`firmware/common/carrier/e0001.h`). M01's pattern has bit 1 = 0 and reads correctly on that
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
| Pull-up ownership | Not stated in the header contract (ADR-0014 d5). Every sensor module must carry its own; the rule exists in `E0002` and `E0006` and in no document. O-42 |
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
| Software | Sensor API only. BSEC is a closed-source binary under a separate licence agreement and is incompatible with the AGPL-3.0-or-later reference firmware (ADR-0002 d5) |
| Consequence | Humidity compensation, baseline tracking and long-term drift correction of the gas signal are BSEC functions and are not available. They are the node's problem or nobody's |
| Excluded outputs | IAQ, CO₂-equivalent, bVOC-equivalent and gas-scan classification are BSEC outputs and are not published. CO₂ is measured by U3, not estimated from VOC |
| Thresholds | Relative to a tracked per-device baseline. Absolute thresholds shall not be used |
| Confounding | Responds to temperature and humidity; a VOC excursion concurrent with a T or RH excursion is not independent evidence |
| Drift | Hotplate ageing; baseline is per-device and moves |
| Heater profile | U2's parallel mode sequences up to 10 hotplate temperature steps in hardware, yielding a vector of resistances per cycle rather than a scalar. Whether the node publishes the vector or a reduction of it is undefined. O-49 |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured. O-35 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02). M01 exceeds it |
| Burst reflected to `+12 V` | ≈ 60–70 mA (`verify`, 205 mA × 3.3 V / 12 V ÷ η, η = 0.85), plus the losses of §7.4 |

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
| Value | **4.7 µF effective**, the upper characterized point of U5's stability, PSRR and load-transient data. Closes O-3 |
| Nameplate | **4.7 µF.** Not to be raised: 4.7 µF is also the upper characterized point, so a larger nameplate part leaves U5's characterized range |
| Specification basis | Effective capacitance at 2.8 V DC bias, not nameplate. Held near 4.7 µF by dielectric, voltage rating and case size rather than by nameplate value; the part is selected against its DC-bias curve at BOM release. O-50 |
| Local decoupling | 100 nF at U3's supply pins, in addition and not as a substitute |

The value is taken from the regulator's characterization rather than computed from a droop
budget, because the computed figure depends on a transient response interval that the datasheet
already publishes as a measured curve. Confirm against that curve that the excursion at a
190 mA step stays well inside 400 mV.

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | U1 shaded from direct line of sight to the luminaire, air exchange maintained (M1, M5). Canopy irradiance from the profile: DLI 17–20 mol·m⁻²·d⁻¹ over 16 h = 295–347 µmol·m⁻²·s⁻¹ ≈ 65–76 W/m² PAR, before non-PAR emission. Unshaded bias ΔT ≈ α·E/h, of order several kelvin at α ≈ 0.3, h ≈ 10 W·m⁻²·K⁻¹ (`verify`) | Profile, ADR-0003 d11/d12; O-38 |
| T2 | Bias at U1 from all board-internal sources ≤ **0.1 K**, giving ≤ 1.7 % induced VPD error per §6.2. Proposed value; confirm or replace before layout freeze | O-33 |
| T3 | Maximum lateral separation between U1 and every heat source — U2, U3, U4, U5; thermal relief slot between U1 and U2; no shared copper pour | — |
| T4 | Maximum lateral separation between U1 and U3; no shared copper pour | — |

On-board heat sources: U2's gas hotplate, cycled to several hundred °C; U3's self-heating
≈ 1 K typical at the module (`verify`); U4 and U5, each dissipating the rail difference times
the load current.

Supplying U2 at 1.8 V does not remove heat from the board — it relocates it from U2's die to U4,
whose position is unconstrained by the diffusion access that M2 imposes on U2. That freedom is
the benefit; it is realized only if T3 is honoured for U4 and U5 as well.

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
| Boot probe addresses | `0x44`, `0x76`, `0x62` |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| U2 variant discrimination | `chip_id` is `0x61` on both BME688 and BME680 and does not distinguish them. `variant_id` at `0xF0` does: `0x01` for BME688, `0x00` for BME680 (`verify`). Required before parallel mode is commanded — the alternative part of §4.2 does not implement it |
| Primary T/RH | U1 only |
| Derived | Air VPD per §6.1 |
| CO₂ compensation | Pressure from U2 written to U3's compensation register (§6.3). Fallback when U2 is absent undefined — O-48 |
| CO₂ self-calibration | ASC disabled; FRC per §6.3.1 |
| U3 temperature offset | Shall be determined for this board under its operating conditions in thermal equilibrium and written to the device. The default is 4 °C and is not this board's value. U3's published T and RH are invalid until it is set. O-45 |
| U3 settings persistence | `persist_settings` writes EEPROM rated for at least 2000 cycles. It shall be issued only when configuration actually changed, never unconditionally at boot |
| VOC | Trend against a tracked per-device baseline; raw gas resistance via the sensor API (§6.4) |
| SHT45 heater | Disabled by default. When enabled for condensate recovery, its pulse shall not overlap a U3 measurement window. Lowest sufficient power level preferred; the rail tolerates the highest |
| Rail failure | Failure of U4 or U5 removes U2 or U3 from the bus. The boot probe handles this as absence; *not fitted*, *failed* and *unpowered* remain indistinguishable. O-37 |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m01_climate/` — does not exist |

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | T2 | Board at steady state against a reference thermometer in the same air; U2 hotplate and U3 measurement cycle each running and idle. Method to be fixed with T2 (O-33) |
| V2 | T1 | U1 reading with and without the luminaire at the profile's operating PPFD, against a shaded reference thermometer in the same air |
| V3 | §7.1, §7.4 | Node draw on `+12 V` measured at first bring-up, average and during a U3 burst. Rail voltages at U2 VDD and U3 VDD measured under load, to detect a regulator out of regulation. Rail ripple against the 30 mV limit of §7.4.1 requires instrumentation the project does not have — O-44 |
| V4 | §6.1 | U1 against a reference hygrometer at two points of the ADR-0003 d7 band |
| V5 | §6.3 | CO₂ against M07's reference instance in the same air (O-7), no earlier than five days after assembly and at the 2.8 V rail |
| V6 | §4.1 footprints | 1:1 paper printout against the physical part, all three devices, including U1's unsoldered die pad and U3's full pad count |
| V7 | §10 | U3 temperature offset determined and written; U3 T/RH against a reference in the same air before and after |

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

## 12. Open items

Continues the `O-` namespace shared with the M05, M06 and M07 specifications. O-2, O-4, O-6,
O-8, O-10 and O-11 are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's.

| ID | Item | Blocks |
|----|------|--------|
| ~~O-3~~ | ~~Bulk capacitor value and dielectric at U3~~ — closed 2026-08-05 by §7.5: 4.7 µF effective, from U5's characterization | — |
| O-5 | FRC interval, and the operational trigger for performing one. Procedure and constraints now fixed (§6.3.1) | CO₂ validity |
| O-7 | Cross-calibration procedure, M01 CO₂ against M07 reference | Survey method, ADR-0016 identification |
| O-9 | Fate of ADR-0014 d3, the ≤ 30 cm short-lead provision | ADR-0014 hygiene |
| O-32 | Remaining `verify` values: U2 gas-resistance range, U1 membrane-variant height, U3 self-heating, BME680 `variant_id`, T1's radiative constants | `L` release |
| O-33 | T2 value and V1 method not confirmed | Layout freeze |
| ~~O-34~~ | ~~Pressure source for CO₂ compensation: U2 or M07~~ — closed 2026-08-05 by §6.3 in favour of U2. Unpopulated-U2 residue moved to O-48 | — |
| O-35 | Node power unmeasured; M05's O-31 node-count ceilings assume M05-class nodes | Distribution-board sizing, O-31 |
| ~~O-36~~ | ~~ADR-0014 d2's partial-BOM example cites M01's airflow sensor, moved to M06 in rev 2~~ — closed 2026-08-04 by ADR-0014 rev 3 | — |
| O-37 | Boot probe does not distinguish *not fitted*, *failed* and *unpowered* | Gateway fault handling |
| O-38 | T1 has no confirmed shading design or measured bias | Enclosure design |
| O-39 | Neither ADR-0003 d7 nor the profile instance states a VPD uncertainty limit; §6.1 gives ±1.3…±2.9 % in band, ±6.7 % on humid excursion | Profile validity, U1 part grade |
| O-40 | ADR-0003 d7 does not state air VPD or leaf VPD | Profile interpretation |
| ~~O-41~~ | ~~SCD41's specified range starts at 400 ppm~~ — closed 2026-08-05 by §6.3: depletion is reported outside the specified accuracy band, no part change required | — |
| O-42 | Pull-up ownership and value are not in the header contract (ADR-0014 d5). The rule exists in two boards and no document | Next module designed against the contract |
| O-43 | Module-local rails are outside ADR-0002 d3 and the header contract. One instance is not a pattern; revisit at the second | ADR-0014 / ADR-0002 revision |
| O-44 | Carrier runs in pulse-skipping at node load; rail ripple against U3's 30 mV limit has never been measured, and no instrument is available. Design is dimensioned against the datasheet, not the rail | Confidence in §7.4, not the design |
| O-45 | U3 temperature offset uncalibrated; U3's T and RH are invalid until V7 is executed | U3 T/RH validity |
| O-46 | U3 forbids board wash after reflow; M4 requires conformal coating, which conventionally follows cleaning | Coating process, M4 |
| O-47 | Condensation on excursions places U2 and U3 outside their stated operating conditions. Post-excursion validity and recovery undefined | Excursion handling, data validity |
| O-48 | Pressure source for CO₂ compensation when U2 is not populated | Firmware, partial populations |
| O-49 | Whether the node publishes U2's 10-step resistance vector or a reduction of it | DSDL, ADR-0005 |
| O-50 | C8's dielectric, voltage rating and case are unfixed; §7.5 requires 4.7 µF *effective* at 2.8 V bias from a 4.7 µF nameplate part, which the DC-bias curve must be checked to deliver | `L` release, §7.5 |

## 13. Maturity

**Schematic captured, not frozen.**

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic captured** ← here | Parts fixed to ordering part numbers; schematic exists; component values determined | `store/E0002-000001.kicad_sch` exists; O-3, O-34, O-41 closed |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-32, O-50 closed; O-33, O-38 fixed as requirements; V6 executed |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0002` fabricated and bench-verified; O-35 measured |

`M05-SAFETY-specification.md` is the as-built form of this document class. The transition is an
edit of this file; no second E-number is issued for the module having been built.
