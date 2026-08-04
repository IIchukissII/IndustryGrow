<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE — module specification

- **Status:** Working specification, pre-schematic capture. `E0002` not laid out, not fabricated
- **Date:** 2026-08-04
- **E-number:** `E0002` · module-ID strap `0b001`
- **Governing ADRs:** ADR-0014 (rev 2), ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 2), ADR-0018
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
| Air temperature | U1 | −40…+125 °C (`verify`) | 14 °C night, 20 °C day; 12…28 °C including excursions | ±0.1 °C typ (`verify`) |
| Relative humidity | U1 | 0…100 %RH | 49…66 %RH day, 25…50 %RH night (from the VPD band, §6.1) | ±1.5 %RH (`verify`) |
| Air VPD (derived, §6.1) | U1 | — | 0.8…1.2 kPa, both photoperiods | ±0.035 kPa at the day point |
| CO₂ | U3 | 400…5000 ppm specified (`verify`) | 400…450 ppm ambient baseline; below 400 ppm on depletion during photoperiod (O-41); elevated at night | ±(50 ppm + 5 % of reading) (`verify`) |
| Barometric pressure | U2 | 300…1100 hPa (`verify`) | 950…1050 hPa | ±0.6 hPa (`verify`) |
| Gas resistance (VOC trend) | U2 | kΩ…MΩ (`verify`) | Trend only; no absolute expectation | Uncalibrated |
| Secondary T / RH | U2, U3 | Per device | As above | Not used for VPD |

Publication rate: seconds, all subjects.

Location: canopy, inside the growing enclosure. Relocatable to any canopy point. All sensors
board-mounted; no leads, no cable-borne I²C.

### 3.1 Exclusions

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

### 3.2 Partial populations

Optional parts shall be footprints that can be left unpopulated with no other change to the
board. U3 shall not sit in the I²C pull-up path or in the sense path of any other part.

A partially populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) and
requires no firmware change; the boot probe registers responders only (§10).

## 4. Sensor complement

| # | Device | Function | Supply | Rail | I²C address | Address type |
|---|--------|----------|--------|------|-------------|--------------|
| U1 | Sensirion SHT45 | Primary T / RH, VPD source | 1.08–3.6 V | 3.3 V | `0x44` | Fixed (variant `-AD1B`) |
| U2 | Bosch BME688 | Gas / VOC trend, barometric pressure, secondary T / RH | 1.71–3.6 V | 3.3 V | `0x76` | Strap, SDO low; `0x77` unused |
| U3 | Sensirion SCD41 | CO₂, photoacoustic NDIR | 2.4–5.5 V | 3.3 V | `0x62` | Fixed |

U1 is the primary T/RH source. U2 and U3 internal T/RH shall be published as their own subjects
and shall not enter the VPD computation.

| # | Device | Package | Body size |
|---|--------|---------|-----------|
| U1 | SHT45 | DFN-4 | 1.5 × 1.5 × 0.5 mm |
| U2 | BME688 | LGA-8 | 3.0 × 3.0 × 0.93 mm |
| U3 | SCD41 | Custom SMD module | 10.1 × 10.1 × 6.5 mm |

Every footprint shall be checked against the physical part with a 1:1 paper printout before
ordering. U3's footprint is in no standard library.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1, U2, U3 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b001` |
| Header B pin 1 / 2 — 3V3, GND | Strap reference |

No other header signal is claimed. `GPIO_1`/`GPIO_2` (PA9/PA10) are not used, so USART1 remains
available as the carrier debug console throughout boot and run (pin map note 6).

Module-ID bit 1 (`STRAP_1`, PA6) is unrouted on carrier revision `E0001-000001`
(`firmware/common/carrier/e0001.h`). M01's pattern has bit 1 = 0 and reads correctly on that
revision.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, single local segment, all devices board-mounted |
| Speed | 100 kHz standard mode, set by U3 (`verify`) |
| Pull-ups | Sized for 100 kHz at the actual segment capacitance; shall not depend on U3 being populated |
| Segment capacitance | Three devices on board traces; below the 400 pF limit |

## 6. Measurement requirements

### 6.1 VPD

Computed on-node from U1 only:

```
VPD = e_s(T) · (1 − RH/100)        e_s(T) = 0.61078 · exp(17.27·T / (T + 237.3))   [kPa]
```

The quantity published is **air VPD** (ADR-0003 d7). Leaf VPD is not published (§3.1).

Profile band edges, from `profiles/strawberry-day-neutral-v1.json`:

| Photoperiod | T | e_s | VPD 0.8 kPa → RH | VPD 1.2 kPa → RH |
|-------------|---|-----|------------------|------------------|
| Day | 20 °C | 2.338 kPa | 65.8 %RH | 48.7 %RH |
| Night | 14 °C | 1.599 kPa | 50.0 %RH | 24.9 %RH |

Uncertainty at the day band edge (20 °C, 65.8 %RH, VPD 0.800 kPa):

| Term | Sensitivity | U1 accuracy | Contribution |
|------|-------------|-------------|--------------|
| Temperature | ∂VPD/∂T = (1 − RH/100) · de_s/dT = 0.050 kPa/K | ±0.1 °C (`verify`) | ±0.005 kPa |
| Humidity | ∂VPD/∂RH = e_s/100 = 0.023 kPa per %RH | ±1.5 %RH (`verify`) | ±0.035 kPa |
| Combined (RSS) | — | — | ±0.035 kPa, ±4.4 % of reading |

Relative uncertainty from the humidity term, independent of temperature:

```
δVPD / VPD  =  δRH / (100 − RH)
```

| Operating point | RH | VPD | Relative uncertainty at ±1.5 %RH |
|-----------------|----|-----|----------------------------------|
| Day, band low | 65.8 % | 0.800 kPa | ±4.4 % |
| Day, band high | 48.7 % | 1.200 kPa | ±2.9 % |
| Night, band low | 50.0 % | 0.800 kPa | ±3.0 % |
| Night, band high | 24.9 % | 1.200 kPa | ±2.0 % |
| Excursion above band | 85 % | 0.351 kPa (20 °C) | ±10 % |

Within the profile band the uncertainty is ±2.0…±4.4 %. It degrades on humid excursions above
the band. No VPD uncertainty limit is stated by ADR-0003 d7 or by the profile instance. O-39.

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
| Specified sensor range | 400–5000 ppm (`verify`). Depletion below 400 ppm falls outside it | O-41 |
| Pressure compensation | Required. Absolute pressure written to the SCD41 compensation register; uncompensated pressure error propagates ≈ 1:1 into reported concentration (`verify`) | — |
| Compensation source | U2 on-board, or the deployment's M07 barometric instance — unresolved | O-34, M07 O-23 |
| Automatic self-calibration | ASC assumes periodic exposure to ≈ 400 ppm, which a closed cabinet need not reach. If disabled, a manual calibration procedure and interval are required — undefined | O-5 |
| Cross-calibration | Against M07's reference instance before any M01↔M07 concentration difference is published — undefined | O-7 |

### 6.4 Gas / VOC

| Requirement | Value |
|-------------|-------|
| Output | Gas resistance, uncalibrated. Published as a trend, not a concentration |
| Thresholds | Relative to a tracked per-device baseline. Absolute thresholds shall not be used |
| Confounding | Responds to temperature and humidity; a VOC excursion concurrent with a T or RH excursion is not independent evidence |
| Drift | Hotplate ageing; baseline is per-device and moves |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured. O-35 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02). M01 exceeds it |
| Burst reflected to `+12 V` | ≈ 60–70 mA (`verify`, 205 mA × 3.3 V / 12 V ÷ η, η = 0.85) |

### 7.2 3.3 V rail

Supplied by the carrier's TPS54302 buck, shared with the MCU and CAN transceiver. No rail
redesign; the U3 burst is covered by a local bulk capacitor (§7.3).

| # | Device | Idle | Typical | Peak | Source |
|---|--------|------|---------|------|--------|
| U1 | SHT45 | ≈ 0.08 µA | ≈ 320 µA during conversion | ≈ 60 mA (heater, optional) | `verify` |
| U2 | BME688 | ≈ 0.15 µA | ≈ 12 mA during heater phase | ≈ 18 mA | `verify` |
| U3 | SCD41 | ≈ 0.15 mA | ≈ 15–18 mA, periodic mode | 205 mA | `verify` |

### 7.3 Bulk capacitor at U3

Sized to cover the regulator's control-loop response interval, not to supply the burst.

| Parameter | Value |
|-----------|-------|
| Step current ΔI = I_peak − I_avg | ≈ 190 mA |
| Loop response interval t_r | TPS54302 loop bandwidth — `verify` |
| Allowed droop ΔV | Referenced to U3's 2.4 V minimum |
| Capacitance | C ≥ ΔI · t_r / ΔV, computed at schematic capture. O-3 |
| Specification basis | Effective capacitance at 3.3 V DC bias, not nameplate |
| ESR | Low; ESR × ΔI adds directly to droop |
| Placement | At the U3 supply pins, minimum loop area |

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | U1 shaded from direct line of sight to the luminaire, air exchange maintained (M1, M5). Canopy irradiance from the profile: DLI 17–20 mol·m⁻²·d⁻¹ over 16 h = 295–347 µmol·m⁻²·s⁻¹ ≈ 65–76 W/m² PAR, before non-PAR emission. Unshaded bias ΔT ≈ α·E/h, of order several kelvin at α ≈ 0.3, h ≈ 10 W·m⁻²·K⁻¹ (`verify`) | Profile, ADR-0003 d11/d12; O-38 |
| T2 | Bias at U1 from all board-internal sources ≤ **0.1 K**, giving ≤ 1.7 % induced VPD error per §6.2. Proposed value; confirm or replace before layout freeze | O-33 |
| T3 | Maximum lateral separation between U1 and U2; thermal relief slot between them; no shared copper pour | — |
| T4 | Maximum lateral separation between U1 and U3; no shared copper pour | — |

On-board heat sources: U2 gas hotplate, cycled to several hundred °C; U3 self-heating ≈ 1 K
typical at the module (`verify`). Both are duty-cycled, so their contribution varies with the
measurement schedule.

## 9. Mechanical and enclosure

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | U1: diffusion access to canopy air; shaded per T1; no conformal coating over the sensor opening | T1 |
| M2 | U2: diffusion access to canopy air; no conformal coating over the gas port | — |
| M3 | U3: air exchange with the measured volume; no conformal coating over the optical cavity | — |
| M4 | Conformal coating on all other areas. Installed in the growing volume: expected 25–66 %RH, condensation possible on excursions | §3 |
| M5 | Shading per T1 shall not obstruct the air exchange required by M1 | T1 |
| M6 | Enclosure shall not seal U1 into a pocket without air exchange | §10, heater |
| M7 | Mounted at canopy height; height adjusted as the canopy grows, recorded as deployment metadata | ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8, supported at both ends; no standoffs required | Pin map, header section |

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b001` |
| Boot probe addresses | `0x44`, `0x76`, `0x62` |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| Primary T/RH | U1 only |
| Derived | Air VPD per §6.1 |
| CO₂ compensation | Pressure written to the SCD41 compensation register; source per O-34 |
| CO₂ self-calibration | Per §6.3 |
| VOC | Trend against a tracked per-device baseline |
| SHT45 heater | Disabled by default. When enabled for condensate recovery, its pulse shall not overlap a U3 measurement window |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m01_climate/` — does not exist |

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | T2 | Board at steady state against a reference thermometer in the same air; U2 hotplate and U3 measurement cycle each running and idle. Method to be fixed with T2 (O-33) |
| V2 | T1 | U1 reading with and without the luminaire at the profile's operating PPFD, against a shaded reference thermometer in the same air |
| V3 | §7.1 | Node draw on `+12 V` measured at first bring-up, average and during a U3 burst |
| V4 | §6.1 | U1 against a reference hygrometer at two points of the ADR-0003 d7 band |
| V5 | §6.3 | CO₂ against M07's reference instance in the same air (O-7) |
| V6 | §4 footprints | 1:1 paper printout against the physical part, all three devices |

## 12. Open items

Continues the `O-` namespace shared with the M05, M06 and M07 specifications. O-2, O-4, O-6,
O-8, O-10 and O-11 are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's.

| ID | Item | Blocks |
|----|------|--------|
| O-3 | Bulk capacitor value and dielectric at U3 | Schematic capture |
| O-5 | SCD41 self-calibration mode and, if disabled, the manual calibration procedure and interval | Firmware, CO₂ validity |
| O-7 | Cross-calibration procedure, M01 CO₂ against M07 reference | Survey method, ADR-0016 identification |
| O-9 | Fate of ADR-0014 d3, the ≤ 30 cm short-lead provision | ADR-0014 hygiene |
| O-32 | `verify` values unconfirmed against datasheets: §7.2 currents, U1 accuracy in §6.1, SCD41 bus ceiling, U3 self-heating, TPS54302 loop bandwidth | `L` release, O-3 |
| O-33 | T2 value and V1 method not confirmed | Layout freeze |
| O-34 | Pressure source for CO₂ compensation: U2 or M07 | Firmware, M07's O-23 |
| O-35 | Node power unmeasured; M05's O-31 node-count ceilings assume M05-class nodes | Distribution-board sizing, O-31 |
| O-36 | ADR-0014 d2's partial-BOM example cites M01's airflow sensor, moved to M06 in rev 2 | ADR-0014 hygiene |
| O-37 | Boot probe does not distinguish *not fitted* from *failed* | Gateway fault handling |
| O-38 | T1 has no confirmed shading design or measured bias | Enclosure design |
| O-39 | Neither ADR-0003 d7 nor the profile instance states a VPD uncertainty limit; §6.1 gives ±2.0…±4.4 % in band, ±10 % on humid excursion | Profile validity, U1 part grade |
| O-40 | ADR-0003 d7 does not state air VPD or leaf VPD | Profile interpretation |
| O-41 | SCD41's specified range starts at 400 ppm (`verify`); a closed cabinet depletes below it during photoperiod, so depletion is read outside the specified range | CO₂ validity, U3 part selection |

## 13. Maturity

**Pre-schematic.**

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic-frozen** | `verify` resolved, component values computed, footprints checked. `L` releases here | O-32, O-3, O-34 closed; O-33, O-38 fixed as requirements |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0002` fabricated and bench-verified; O-35 measured |

`M05-SAFETY-specification.md` is the as-built form of this document class. The transition is an
edit of this file; no second E-number is issued for the module having been built.
