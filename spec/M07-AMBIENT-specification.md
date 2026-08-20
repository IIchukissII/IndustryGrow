<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M07-AMBIENT — module specification

- **Status:** Working specification, pre-schematic capture. `E0009` not laid out, not fabricated
- **Date:** 2026-08-04 (restructured to specification form; no decision changed)
- **E-number:** `E0009` · module-ID strap `0b111`
- **Governing ADRs:** ADR-0014 (rev 3), ADR-0001, ADR-0002 (rev 3), ADR-0016, ADR-0017 (rev 2), ADR-0018, ADR-0019
- **Companions:** `M01-CLIMATE-specification.md`, `M02-LIGHT-specification.md`, `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M07-AMBIENT sensor module in both deployment variants: sensor complement,
measured quantities, interfaces, environmental, mechanical and firmware requirements, and their
verification.

Not specified here: carrier design (`store/E0001-000003-D-pinmap.md`), field-bus changes for an
outdoor run (ADR-0002, O-14), gateway-side handling of published subjects (ADR-0014 d7).

## 2. Identification

| | Value |
|---|---|
| Module class | M07-AMBIENT |
| Module-ID strap | `0b111` |
| E-number | `E0009` — first assembly. The second variant takes its own assembly E-number, assigned at design commit (ADR-0017 rev 2 d4) |
| Bare design | One layout, one strap, one firmware image, two populated configurations |
| Carrier | `E0001`, sensor-module header pair (ADR-0014 d5) |

### 2.1 Deployment variants

Ambient is the volume the growing enclosure exchanges with, one step out (ADR-0014 d4).

| | **Indoor** | **Outdoor** |
|---|---|---|
| Ambient of | A cabinet or chamber inside a building | A greenhouse, or a building whose interior is the growing space |
| Physical location | The room or hall surrounding the enclosure | Outside the building envelope, typically mast-mounted |
| Wind group (§4.2) | Not populated | Populated at deployment scale and above |
| Radiation shield | Only under glazing in direct sun | Mandatory (E1) |
| Environmental qualification | Indoor, as M01–M05 | Outdoor (§7) |
| Field bus leaves the enclosure | No | Yes — O-14 |

The node does not know which variant it is. Where both boundaries are real, the class is
instantiated twice in two zones (ADR-0014 d1, d7).

## 3. Function

M07 measures the boundary conditions of the growing space. It participates in no control loop.

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| Ambient air temperature | U1 SHT45 | −40…+125 °C (`verify`) | Indoor 15…30 °C; outdoor −20…+45 °C (`verify`) | ±0.1 °C (`verify`) |
| Ambient relative humidity | U1 SHT45 | 0…100 %RH | Indoor 25…65 %RH; outdoor 10…100 %RH incl. condensing | ±1.0 %RH typ (`verify`) |
| Absolute barometric pressure | U2 BMP390 class | 300…1250 hPa (`verify`) | 950…1050 hPa | ±0.5 hPa (`verify`) |
| Ambient CO₂ reference | U3 SCD41 | 400…5000 ppm specified | Outdoor 400…450 ppm; occupied room 450…1200 ppm | ±(50 ppm + 2.5 % of reading) 400–1000 ppm; ±(50 ppm + 3 % of reading) 1001–2000 ppm |
| Irradiance | U4 | `verify` — quantity and range unresolved | Full sun ≈ 1000 W/m² vs room light: orders of magnitude apart | `verify` |
| Wind speed | Purchased anemometer (`SP`), pulse | Per instrument | 0…30 m/s (`verify`) | Per instrument |
| Wind direction | Purchased vane, Gray-coded absolute encoder | 0…360°, resolution per instrument | — | Encoder resolution |
| Heading, tilt | U7 LSM303AGR class | Per part (`verify`) | Deviation from commissioned values | `verify` |

Sampling rate: minutes, all subjects.

Required by: M06 mass-flow density (absolute pressure), M06 envelope-leakage reference
(ambient port), M01 and M06 CO₂ cross-calibration (CO₂ reference).

### 3.1 Exclusions

| Quantity | Owning class / reason |
|----------|------------------------|
| Air state at the canopy | M01-CLIMATE |
| Air transport in the cabinet | M06-VENTILATION |
| Precipitation | Not measured (ADR-0014 d4) |
| Gas / VOC | Not populated (ADR-0014 d4) |
| Angular rate | No rate gyroscope; fixed installation (ADR-0014 d4) |

## 4. Sensor complement

### 4.1 Core — both variants, every scale

| # | Device | Function | Supply | I²C address | Address type | Indoor | Outdoor |
|---|--------|----------|--------|-------------|--------------|--------|---------|
| U1 | Sensirion SHT45 | Ambient air T / RH | 1.08–3.6 V | `0x44` | Fixed | ● | ● |
| U2 | Bosch BMP390 class | Absolute barometric pressure | `verify` | `0x76` / `0x77` (`verify`) | Strap | ● — O-23 | ● |
| U3 | Sensirion SCD41 | Ambient CO₂ reference | 2.4–5.5 V | `0x62` | Fixed | ● | ● |
| U4 | Irradiance sensor | Energy-balance input | `verify` | `verify` | `verify` — O-12 | ● — O-22 | ● |

### 4.2 Wind group — outdoor variant, deployment scale and above

Left unpopulated under the partial-BOM mechanism (ADR-0014 d2) on the indoor variant and at
cabinet scale.

| # | Component | Function | Interface |
|---|-----------|----------|-----------|
| — | Anemometer, purchased | Wind speed | Pulse train (§6.2). SP number per ADR-0019 |
| — | Vane with absolute encoder, purchased | Wind direction | Gray code via shift register on SPI (§6.3) |
| U5 | Pulse conditioning, RC + Schmitt trigger | Anemometer signal path | Timer, external counter mode |
| U6 | 74HC165 class shift register | Parallel Gray code → SPI | In the vane housing, not on the M07 board |
| U7 | LSM303AGR class | Heading reference, installation-integrity monitor | I²C, address `verify`. On the M07 board |

Both instruments are external, in their own weatherproof housings, cabled to the M07 enclosure.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1, U2, U3, U4, U7 |
| Header A pins 9–11, 13/14 — SPI_SCK/MISO/MOSI, CS | U6 shift-register readout |
| Header A GPIO or PWM pin, timer-capable | Anemometer pulse input from U5 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b111` |

Module-ID bit 1 (`STRAP_1`, PA6) reaches the MCU from carrier revision `E0001-000003`.
`E0001-000001` and `E0001-000002` carry it only with the `J6` pad 4 → `J3` pad 15 link added by
hand; without that link M07's pattern reads back as `0b101`.

1-Wire is not part of the ADR-0014 d5 header contract. O-18.

## 6. Measurement requirements

### 6.1 Irradiance

| Item | Requirement |
|------|-------------|
| Quantity | Unresolved: PPFD, broadband irradiance, or illuminance. Not interchangeable; shall not be mixed in one time series. O-12 |
| Range | Unresolved across variants: full sun against room light. O-22 |
| Constraint | M02-LIGHT's part is calibrated for a narrowband luminaire at closed-space levels and shall not be assumed usable under sunlight |

### 6.2 Wind speed

| Item | Requirement |
|------|-------------|
| Transducer | Purchased anemometer with pulse output; frequency proportional to speed |
| Conditioning | RC network and Schmitt trigger in hardware; timer in external counter mode; no software debouncing |
| Conversion constants | Pulses-per-revolution and the speed constant live in the SP record and the deployment profile, not in firmware |
| Starting threshold | Below it the instrument does not turn. Zero shall be published as *unknown*, distinct from *calm* |

### 6.3 Wind direction

| Item | Requirement |
|------|-------------|
| Encoding | Gray-coded absolute encoder (ADR-0014 d4) |
| Transport | Serialized in the vane housing by U6, read over SPI. I²C shall not be used for this link |
| Decode | Gray → binary in firmware (`b = g; while (g >>= 1) b ^= g;`) |
| Azimuth | `U7 heading + constant mounting offset + encoder reading`. Mast guides fix the mounting offset as a constant of the assembly |
| Encoder resolution | A property of the purchased instrument; does not enter the header contract |
| Magnetic compatibility | Where the encoder reads its tracks magnetically, its field shall not reach U7. Check at model selection. O-13 |

### 6.4 Installation integrity

| Item | Requirement |
|------|-------------|
| Inputs | U7 accelerometer and magnetometer, polled at minute scale |
| Heading | Tilt-compensated |
| Calibration | Hard-iron and soft-iron coefficients are deployment data, not firmware constants. Procedure undefined, O-20 |
| Diagnostic | Tilt and heading deviation from commissioned values published, not silently corrected. Thresholds undefined, O-21 |

### 6.5 Vane / anemometer cross-check

| Observation | Published inference |
|-------------|---------------------|
| Speed above the starting threshold, direction static | Vane seized, detached, or cable open |
| Direction changing, speed static above the threshold | Anemometer seized or pulse path broken |
| Both static, other channels indicate air movement | Common cable or conditioning fault |
| Both static, speed below the starting threshold | Calm — not a fault |

The discriminator is measured speed against the starting threshold, never a bare zero.

## 7. Environmental requirements — outdoor variant

The indoor variant inherits the M01–M05 indoor assumptions; nothing in this section gates it.

| ID | Requirement |
|----|-------------|
| E1 | Radiation shield mandatory for U1 and U3. Unshielded direct sun biases air temperature by several kelvin |
| E2 | U1, U3, U4 require air exchange while the housing provides weather protection; resolved mechanically |
| E3 | Every part's operating range re-checked against the outdoor range; not inherited from M01. O-17 |
| E4 | Conformal coating everywhere except sensor apertures; repeated dew cycling assumed |
| E5 | UV qualification of the housing material |
| E6 | Field bus and `+12 V` leaving the enclosure: surge protection, grounding, topology, cable length. O-14 |

## 8. Mechanical and enclosure

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | Mast guides fixing enclosure-to-vane-base orientation as a constant of the assembly | §6.3 |
| M2 | Cable entries for the anemometer, vane, bus and power leads | O-16 |
| M3 | Breathing vents against diurnal condensation | O-16 |
| M4 | Radiation shield per E1, not obstructing the air exchange of E2 | E1, E2 |
| M5 | Vane housing carries U6: supply, condensation and temperature qualification in its own right | O-19 |

## 9. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b111` |
| Boot probe addresses | `0x44`, BMP390 `verify`, `0x62`, U4 `verify`, U7 `verify` |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only. Neither the unpopulated wind group nor the variant split requires a firmware variant |
| Variant awareness | None. Which ambient is measured is a `zone` and `node_role` fact at the gateway (ADR-0014 d7) |
| Wind speed | Timer, external counter mode; §6.2 |
| Wind direction | SPI read, Gray decode, azimuth per §6.3 |
| Heading and tilt | §6.4 |
| Cross-check | §6.5 |
| Node directory | `firmware/nodes/m07_ambient/` — does not exist |

## 10. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | E1 | U1 in direct sun with and without the shield, against a shaded reference thermometer |
| V2 | §6.2 | Pulse count against instrument speed at ≥ 3 points, including below the starting threshold |
| V3 | §6.3 | Azimuth against a surveyed reference bearing, full 360° sweep |
| V4 | O-13 | U7 heading with the encoder rotating at the installed mast-to-base separation |
| V5 | §3 CO₂ | Against M01's instrument in the same air (O-7) |
| V6 | §6.4 | Heading and tilt recorded at commissioning, re-read after a deliberate mast displacement |

## 11. Open items

Shared `O-` namespace. O-3, O-5, O-7, O-9, O-32 to O-41 and O-43 to O-51 are M01's; O-2, O-4,
O-6, O-8, O-10, O-11, O-42 are M06's; O-25 to O-31 are M05's; O-52 to O-62 are M02's. The
**Variant** column records which deployment variant an item gates.

| ID | Variant | Item | Blocks |
|----|---------|------|--------|
| ~~O-1~~ | both | ~~E-number assignment~~ — closed 2026-08-03, `E0009` | — |
| O-12 | both | Irradiance quantity: PPFD, broadband irradiance, or illuminance | U4 part selection |
| O-22 | both | Irradiance range across variants; whether one part, two parts, or two footprints | U4 part selection, footprint |
| O-23 | both | Barometric duplication across variants: which instance carries U2, whether the other leaves the footprint bare, and which the deployment profile names as the pressure source | Partial-BOM rule, deployment profile, M01 O-34 |
| O-24 | both | ADR-0016's state-vector partition has no category for exogenous inputs, so the gateway-side handling contract for M07 telemetry is unstated | ADR-0016 revision, gateway estimator contract |
| O-18 | both | 1-Wire absent from the ADR-0014 d5 header contract | Header contract review at the next carrier revision |
| O-13 | outdoor | Encoder track readout optical or magnetic; if magnetic, field reaching U7 | Vane model selection |
| O-19 | outdoor | Supply, condensation and temperature qualification for U6 in the vane housing | Vane housing design |
| O-20 | outdoor | Hard-iron and soft-iron calibration procedure for U7 | Commissioning method |
| O-21 | outdoor | Commissioned reference values for tilt and heading, and diagnostic thresholds | Commissioning method, telemetry schema |
| O-14 | outdoor | Field bus leaving the enclosure: surge, grounding, topology, outdoor `+12 V` | ADR-0002 revision, ADR-0018 review |
| O-15 | outdoor | Anemometer and vane model selection, SP records, conversion constants | BOM, deployment profile schema |
| O-16 | outdoor | Cable-entry and breathing-vent design | Enclosure design |
| O-17 | outdoor | Outdoor temperature and condensation qualification for parts inherited from indoor modules | BOM release |

## 12. Maturity and phase position

**Pre-schematic.** M07 is defined, not scheduled; phase-gate order is `E0001` bring-up, `E0006`
fabrication, then any new module.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic-frozen** | `verify` resolved, values computed, footprints checked. `L` releases here | O-12, O-22, O-23 closed; outdoor variant additionally O-13, O-15, O-17 |
| **As-built** | Estimates replaced by measured values; verification §10 executed | `E0009` fabricated and bench-verified |

The indoor variant is gated by none of the outdoor items: O-14, O-16, O-17, O-19, O-20, O-21
and O-13 belong to the outdoor variant and the wind group.
