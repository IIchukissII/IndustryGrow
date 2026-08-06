<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M06-VENTILATION — module specification

- **Status:** Working specification, pre-schematic capture. `E0008` not laid out, not fabricated
- **Date:** 2026-08-04
- **E-number:** `E0008` · module-ID strap `0b110`
- **Governing ADRs:** ADR-0014 (rev 3), ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 2), ADR-0018
- **Companions:** `M01-CLIMATE-specification.md`, `M05-SAFETY-specification.md`, `M07-AMBIENT-specification.md`
- **History:** Split out of `M01-M06-air-nodes-specification.md` (2026-08-03) on 2026-08-04; M01's half became `M01-CLIMATE-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M06-VENTILATION sensor module: sensor complement, measured quantities and
their derivation, electrical, mechanical and firmware requirements, and their verification.

Not specified here: carrier design (`store/E0001-000002-D-pinmap.md`), fan control (no actuator
module exists), gateway-side handling of published subjects (ADR-0014 d7).

## 2. Identification

| | Value |
|---|---|
| Module class | M06-VENTILATION |
| Module-ID strap | `0b110` |
| E-number | `E0008` — fully-populated assembly |
| Bare design | One layout; each standard populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) |
| Carrier | `E0001`, sensor-module header pair 2×12 + 2×8 (ADR-0014 d5) |

## 3. Function

M06 measures air transport through the cabinet. Installed in the flow path — duct or fan
discharge — and not relocatable outside it.

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| Air velocity, point | U1 FS3000 | 0…7.23 m/s or 0…15 m/s by variant (`verify`) | Cabinet duct velocity, `verify` | `verify` |
| Volumetric flow (derived, §6.1) | U1 | — | `verify` | Dominated by the profile coefficient, O-4 |
| Differential pressure, filter | U2 SDP8xx class | `verify` | Tens to hundreds of Pa (`verify`) | `verify` |
| Differential pressure, envelope ↔ ambient | U3 SDP8xx class | `verify` | Single Pa (`verify`) | `verify` |
| Filter resistance (derived, §6.2) | U1, U2 | — | `verify` | — |
| In-stream temperature / RH | U4 SHT45 | −40…+125 °C, 0…100 %RH (`verify`) | Cabinet air, near the M01 range | ±0.1 °C typ, ±1.0 %RH typ (`verify`) |
| Air density (derived, §6.1) | U4 + external pressure | — | ≈ 1.2 kg/m³ | — |

Publication rate: seconds, all subjects.

### 3.1 Exclusions

| Quantity | Owning class |
|----------|--------------|
| Air state at the canopy — T/RH for VPD, CO₂, VOC | M01-CLIMATE |
| Absolute barometric pressure, ambient reference | M07-AMBIENT |
| Bus voltage and current | M05-SAFETY |

No gas sensor of the BME688 class is populated (ADR-0014 d4). No sealed enclosure is assumed,
required or specified in any phase (ADR-0014 d4).

## 4. Sensor complement

| # | Device | Function | Supply | Rail | I²C address | Address type |
|---|--------|----------|--------|------|-------------|--------------|
| U1 | Renesas FS3000 | Air velocity in the flow path | 3.3 V ±10 % | 3.3 V | `0x28` | Fixed |
| U2 | Sensirion SDP8xx class, higher range | Differential pressure across the filter | `verify` | 3.3 V | `verify` (`0x25` / `0x26` class) | `verify` |
| U3 | Sensirion SDP8xx class, low range | Differential pressure envelope ↔ ambient | `verify` | 3.3 V | `verify`, shall differ from U2 | `verify` |
| U4 | Sensirion SHT45 | In-stream T / RH for density compensation | 1.08–3.6 V | 3.3 V | `0x44` | Fixed |

U2 and U3 are different ranges and shall present two distinct addresses on one segment; if the
family does not support that, a second bus or a different part is required. O-10.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1…U4 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b110` |

Module-ID bit 1 (`STRAP_1`, PA6) is unrouted on carrier revision `E0001-000001`
(`firmware/common/carrier/e0001.h`); M06's pattern has bit 1 = 1, so it reads back as `0b100`
on that revision. M06 requires a carrier that routes PA6, or a firmware override. O-42.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, single local segment, all devices board-mounted |
| Speed | 100 kHz standard mode, set by U1 (`verify`) |
| Segment capacitance | Four devices on board traces; below the 400 pF limit |

## 6. Measurement requirements

### 6.1 Volumetric and mass flow

```
Q = A · v̄            v̄ = k · v_point
```

| Item | Requirement |
|------|-------------|
| `A` | Duct cross-section, a deployment constant |
| `v_point` | U1 reading |
| `k`, velocity profile coefficient | Commissioning parameter identified per installation, carried in the deployment profile. ≈ 0.8–0.85 fully developed turbulent, 0.5 laminar (`verify`), not predictable for a disturbed profile. Identification procedure undefined, O-4 |
| Mass flow | `Q · ρ`, with `ρ` from U4 temperature and absolute pressure. Pressure source is M07 (§3.1) |
| Straight-run length upstream | Property of the host installation, not assumed; its effect is absorbed into `k` |
| Sensor orientation | Fixed by the FS3000 datasheet flow direction; constrains board orientation (M2) |

### 6.2 Filter resistance

Filter Δp carries both loading and flow. With flow measured independently by U1, resistance is
recovered from `Δp` and `Q`.

| Item | Requirement |
|------|-------------|
| Inputs | U2 Δp and derived `Q` |
| Published | Filter resistance, plus the raw Δp |

### 6.3 Envelope leakage characteristic

| Item | Requirement |
|------|-------------|
| Method | Fan pressurization: flow stepped across several operating points, envelope-to-ambient pressure recorded at each, power-law characteristic fitted (ISO 9972 class) |
| Reference port | U3 shall reference ambient outside the enclosure, not another point in the duct |
| Range | Single-Pa resolution; a different part from U2 |
| Flow excitation | Several fan operating points. No fan control exists in Phase 1; manual stepping unconfirmed as a commissioning procedure, O-11 |
| Output | Leakage characteristic parameters, held in the deployment profile, not in firmware |

## 7. Power

| # | Device | Typical | Peak | Source |
|---|--------|---------|------|--------|
| U1 | FS3000 | ≈ 10 mA | `verify` | `verify` |
| U2 | SDP8xx | `verify` | `verify` | `verify` |
| U3 | SDP8xx | `verify` | `verify` | `verify` |
| U4 | SHT45 | ≈ 320 µA during conversion | ≈ 60 mA (heater, optional) | `verify` |

No burst load of the SCD41 class; no bulk-capacitor requirement identified, pending `verify` on
U2 and U3. Node draw on `+12 V` not measured; the reference figure is M05's 0.25 W per node
(`M05-SAFETY-specification.md`).

## 8. Mechanical and enclosure

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | U1 in the flow path at fan discharge or duct | ADR-0014 d4 |
| M2 | U1 orientation per the datasheet flow direction; constrains board orientation in the enclosure | §6.1 |
| M3 | U2 pressure taps upstream and downstream of the filter | §6.2 |
| M4 | U3 one port referenced to ambient outside the enclosure; routing shall not pick up duct dynamic pressure | §6.3 |
| M5 | U4 in the flow path, shielded from direct impingement heating | §6.1 |

## 9. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b110`, subject to O-42 |
| Boot probe addresses | `0x28`, `0x44`, two SDP addresses `verify` |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| Derived | `Q` per §6.1; filter resistance per §6.2; mass flow from density |
| Deployment constants | `A`, `k`, leakage parameters read from the deployment profile, not compiled in |
| Role and zone | Assigned by the gateway (ADR-0014 d7). Role values depend on the ventilation / pollination boundary, O-6 |
| Node directory | `firmware/nodes/m06_ventilation/` — does not exist |

## 10. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.1 | `k` identified at commissioning against a reference; method undefined, O-4 |
| V2 | §6.3 | Leakage characteristic fitted over ≥ 3 fan operating points |
| V3 | §4 | U2 and U3 addressable simultaneously on one segment (O-10) |
| V4 | §5 | Module-ID readback on the carrier revision in use (O-42) |

## 11. Open items

Continues the `O-` namespace shared with the M01, M05 and M07 specifications. O-3, O-5, O-7,
O-9 and O-32 to O-41 are M01's; O-12 to O-24 are M07's; O-25 to O-31 are M05's.

| ID | Item | Blocks |
|----|------|--------|
| ~~O-1~~ | ~~E-number assignment for M01 and M06~~ — closed 2026-08-03; M06 assigned `E0008` | — |
| O-2 | `verify` values in this document unconfirmed against datasheets, including SDP8xx part selection within the family. Scope narrowed to M06 on 2026-08-04; the M01 half is O-32 | `L` release |
| O-4 | Identification procedure for the velocity profile coefficient at commissioning, without permanently altering the air path | Commissioning method, deployment profile |
| O-6 | Ventilation / pollination subsystem boundary, hence M06's `node_role` values | Role assignment, gateway profile |
| O-8 | Functional-subsystem enumeration has no canonical home — ADR-0001 d7 contains no list, yet ADR-0002, ADR-0014 and ADR-0017 each restate one | Adding `ventilation` to the enumeration |
| O-10 | SDP8xx part selection: two ranges, two distinct I²C addresses on one bus | Schematic capture |
| O-11 | Flow excitation for leakage identification in Phase 1 — no fan control exists | Commissioning method |
| O-42 | Module-ID bit 1 is unrouted on `E0001-000001`, so M06 reads back as `0b100`. Whether `E0001-000002` routes PA6 is not recorded in the store documents | Bring-up, carrier revision selection |

## 12. Maturity

**Pre-schematic.** M06 is defined, not scheduled; ROADMAP scopes Phase 1 to M01–M05.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic-frozen** | `verify` resolved, component values computed, footprints checked. `L` releases here | O-2, O-10 closed |
| **As-built** | Estimates replaced by measured values; verification §10 executed | `E0008` fabricated and bench-verified |
