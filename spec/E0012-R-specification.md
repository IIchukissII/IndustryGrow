<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# A01-CLIMATE tempering unit — assembly specification

- **Status:** Working specification, pre-build. `E0012` not built; the housing bottom half exists as design source
- **Date:** 2026-09-10
- **E-number:** `E0012` · discipline mechanical · no module class, no strap
- **Governing ADRs:** ADR-0032, ADR-0031 (rev 1), ADR-0017 (rev 3), ADR-0016, ADR-0003
- **Companions:** `E0011-R-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the tempering unit: the main-tract assembly of the A01-CLIMATE apparatus, which
exchanges heat between the grow-volume air and the rejection loop in both directions. Complement,
interfaces to the node and to the loop, thermal, mechanical and verification requirements.

Not specified here:

| Subject | Owner |
|---|---|
| Drive, derate, interlock logic, firmware, published quantities | `E0011-R-specification.md` |
| Design basis, loads and setpoints | `E0011-R-specification.md` §2.2, `profiles/strawberry-day-neutral-v1.json` |
| Humidity tract and its assembly | `E0011-R-specification.md`, until that assembly is committed |
| Cabinet wall penetration, its seal and its thermal break | Enclosure — `O-107` |
| Rejection loop as a purchased assembly | ADR-0032, deferred |
| Thermoelectric module selection | `O-103` |

## 2. Identification

| | Value |
|---|---|
| E-number | `E0012` — first assembly (ADR-0017 d3) |
| Discipline | Mechanical, registry metadata rather than a field in the identifier (ADR-0017 d5) |
| Module class | None. The unit carries no node, no MCU and no class ID |
| Class membership | A01-CLIMATE, whose class is the apparatus and spans both E-numbers (ADR-0031 rev 1 d2) |
| Commanded by | `E0011` over the §5 harness. The unit accepts no command of its own |
| Serialized | Yes. `-QP` carries dimensional and clamping-force inspection (ADR-0017 d10) |
| Position | Assigned at integration, never present in the identifier (ADR-0017 d7) |
| Design source | `store/E0012-000001-D-case-src.zip`, STEP only (ADR-0019 d9) |

## 3. Function

![Vertical section through the tempering unit: grow-volume air enters at a floor intake, crosses HX1 with its integral fan E2 and returns through a ceiling discharge slot; below HX1 the series pair TEC1 and TEC2 sits with its cold face to the exchanger and its hot face to the water block WB1, which is also the clamping plate and carries the coolant supply and return; the three parts form one clamped column through disc springs inside a printed housing that carries no clamping load; U2 reads the HX1 base, U1 and the trip element RT1 read WB1, and four circuits cross the boundary to the node E0011](./figures/e0012-tempering-unit-stack.svg)

| Direction | Path |
|---|---|
| Cooling | Grow-volume air → HX1 → `E1` cold face → `E1` hot face → WB1 → rejection loop |
| Heating | The same path with the `E1` current reversed; the loop is then the heat source |

The unit publishes nothing. Every quantity its sensing elements carry is published by the node
(`E0011-R-specification.md` §3.2).

## 4. Complement

| Ref | Device | Function | Notes |
|---|---|---|---|
| TEC1, TEC2 | `SP0006` — 40 × 40 mm, 127 couple, `Imax` 6.4 A, `Qmax` 57 W, `ΔTmax` 66 K at `Th` 25 °C, α 0.0608 V/K, R 2.78 Ω, K 0.864 W/K (all `verify`) | `E1`, series pair | SKU is `O-103`; the values drive `E0011`'s `D1` and `D4` |
| HX1 | `SP0007` — 62 × 74 mm section, 150 mm, `Rth` 0.17 K/W with `SP0008` fitted (`verify`) | Grow-volume exchanger of the main tract, and the cabinet's circulation | `T3`, `O-112`. Fischer LA 6 150 24 satisfies this line and `SP0008` together |
| E2 | `SP0008` — ebm-papst **614 NHH**: 24 V over 18–26 V, 6850 min⁻¹, 2.9 W, 56 m³/h **free air**, 41 dB(A), ball bearing, L10 60 000 h at 40 °C, ambient −20…+70 °C, locked-rotor and overload protection, 0.066 kg | Main-tract air mover, mounted on HX1 | Fed from `SP0010`; duty from `E0011` U9 channel 0. Shutoff pressure ≈ 100 Pa (`verify` — read from the curve, not a stated figure). The 612 NHH is the 12 V member of the pair and is **not** this part. `E0011`'s `E5` mounts the same part, and neither the duty input nor the alarm output is settled (`E0011`'s `O-128`) |
| WB1 | `SP0009` — liquid cold plate, 40 × 120 mm working face | Rejection face of `E1`, clamping plate of the column, mounting face of U1 and RT1 | Two wet joints, `M6` |
| U1 | DS18B20, 1-Wire, 12-bit, 750 ms conversion (`verify`) | WB1 block temperature | On `E0011`'s 1-Wire bus; derates both tracts through `E0011`'s `D9` |
| U2 | DS18B20, same bus | HX1 base temperature | Floors `E1` cooling through `E0011`'s `D10` |
| RT1 | NTC 10 kΩ B25/85 3435, at WB1 | Trip element of `E0011`'s `T2` | The comparator is on the node, not in this unit |
| CP1 | Clamping plate and disc-spring set | Column clamping load | `M1` |
| TIM1 | Thermal interface material, both faces of each module | Module-to-plate conduction | `M2` |
| HS1 | Printed housing, PETG | Duct, mounting and column location | `M4`; design source per §2 |
| WH1 | Harness and connector | The four §5 circuits | Unspecified — `O-126` |

U1, U2 and RT1 are ordinary purchased components and stay MPN lines in the `L` document, on the
same footing as the TMP117 counter-example of ADR-0019 d4. Every other part above is a unit of
procurement, and `SP0008` is additionally an actuator device (d6).

Deliberately absent:

| Not fitted | Reference |
|---|---|
| Any electronics. No MCU, no driver, no comparator, no class-ID EEPROM | ADR-0031 rev 1 d2, §2 |
| Any sensing of grow-volume air state | M01-CLIMATE, `E0011-R-specification.md` §3.3 |
| Coolant pump and flow switch. SF1 belongs to the loop | ADR-0032, deferred |
| Condensate collection. HX1 forms none | `T2` |

## 5. Interfaces

Circuits crossing to the node:

| Circuit | Node side | Note |
|---|---|---|
| `E1` string, two conductors | U5 output | The series pair is made up inside the unit; string limit 2.0 A per `E0011`'s `D2` |
| `E2` fan drive | U9 channel 0 | Duty only. The fan is fed from the `+24 V` actuator section (`SP0010`), never the `+12 V` sensor bus (`E0011`'s `P1`), and runs full when the expander's outputs are de-asserted (`E0011`'s `F11`) |
| 1-Wire data and supply | `OW_DATA` | U1 and U2 share the node's bus with U3 and U4 |
| RT1 pair | U7 input | Carries `E0011`'s `T2` trip; no MCU sits between the element and the comparator (`E0011`'s `T10`) |

Other interfaces:

| Interface | To | Note |
|---|---|---|
| Coolant supply and return | Rejection loop | At WB1, the unit's only wet joints (`M6`) |
| Air intake and discharge | Grow volume | Terminations are this unit's (`M5`); the wall penetration is not (`O-107`) |
| Mounting | Enclosure | Outside the grow volume, in the main-tract shaft |

The unit's sensing serves both tracts. U1 derates every thermoelectric demand and RT1 removes
both driver enables, so the humidity tract's protection depends on two elements mounted on WB1.

## 6. Thermal requirements

| ID | Requirement | Reference |
|---|---|---|
| `T1` | The unit removes ≥ 26 W from the grow-volume air at a module ΔT of 15 K with the string at `i` ≤ 0.30 of `Imax` (`verify`) | ADR-0032 d6, `E0011` §2.2, `O-105` |
| `T2` | No condensate forms on HX1 or inside the duct. The floor that holds HX1 above the grow-volume dew point is commanded by the node (`E0011`'s `D10`) | ADR-0032 d5 |
| `T3` | Effective thermal resistance from the two-module footprint on the HX1 base to the air stream is ≤ 0.17 K/W (`verify`), spreading included | `O-112`, `E0011`'s `D10` |
| `T4` | U1 and RT1 each read WB1 plate temperature within 2 K (`verify`) at the `T1` duty | `E0011`'s `D9` and `T2` |
| `T5` | Conduction from WB1 into the housing does not raise any printed surface past `M4`'s ceiling at the `T1` duty | `M4` |
| `T6` | The tract's volume flow is the value at the fan's operating point with the fin stack, the duct and both terminations fitted, and that value is what `T1` and `T3` are evaluated against. `SP0008`'s free-air figure is not it | `O-127`, `E0011` §3 |

## 7. Mechanical requirements

| ID | Requirement | Reference |
|---|---|---|
| `M1` | TEC1 and TEC2 are each clamped between HX1 and WB1 at the datasheet clamping force, through CP1 with disc springs. No printed part carries clamping load | `O-103` |
| `M2` | Both faces of every module carry a thermal interface material rated for continuous 100 °C (`verify`) | `O-103` |
| `M3` | HX1 and WB1 are degreased before assembly | `T2` |
| `M4` | Printed parts are PETG. No printed part is in direct thermal contact with a module hot face or with WB1, and no printed surface exceeds **60 °C** continuous (`verify` — PETG HDT ≈ 70 °C at 0.45 MPa) | `T5` |
| `M5` | The discharge termination is a horizontal slot, gap ≤ 20 mm, directed at the far wall; the intake termination is at the floor. The two tracts' intakes are separated in height | ADR-0003 air movement |
| `M6` | WB1's supply and return are the unit's only wet joints, and both are outside the housing. The column is serviceable without breaking the coolant circuit | ADR-0032 decision drivers |

## 8. Verification

| ID | Verifies | Method |
|---|---|---|
| `V1` | `T1` | Step `E1` to the limit into a cabinet at a known start temperature; record the pull-down curve, the WB1 rise and the loop supply temperature |
| `V2` | `T3` | Substitute a resistor of known power for the module pair; thermocouples on the HX1 base and in the stream |
| `V3` | `T2` | Run at the night load with the grow volume at the wet edge of the band for 4 h; record U2 against the M01 dew point and confirm no free water on HX1 or in the duct |
| `V4` | `T4` | Compare U1 and RT1 against a reference sensor on the WB1 plate at the `T1` duty |
| `V5` | `M1`, `M2`, `M3` | Record clamping force at assembly against the module datasheet, the TIM temperature rating, and that each exchanger was degreased |
| `V6` | `M4`, `T5` | Thermograph the printed parts at the `T1` duty; confirm no surface exceeds 60 °C |
| `V7` | `M5` | Trace the discharge jet with the cabinet loaded; confirm it reaches the far wall and that intake air is not drawn directly from the discharge |
| `V8` | `M6` | Pressure-test both joints at the loop's working pressure with the column assembled; confirm no wetted part is inside the housing |
| `V9` | `T6` | Measure tract volume flow with the unit assembled and both terminations fitted, at full `E2` duty and at the `E0011` §3.1 minimum |

## 9. Open items

| # | Item | Blocks |
|---|---|---|
| `O-103` | Thermoelectric module not selected; α, R, K, clamping force and TIM are unconfirmed. Owned by `E0011-R-specification.md`, consumed here | §4, `T1`, `M1`, `M2` |
| `O-107` | Wall penetration, seal and thermal break are enclosure design; the unit's terminations meet them at an undrawn boundary. Owned by `E0011-R-specification.md`, consumed here | `M5` |
| `O-112` | HX1's `Rth` is a catalogue figure that excludes spreading from two point-source modules on the base. Bench measurement required | `T3`, and through it `E0011`'s `D10` |
| `O-126` | Harness and connector unspecified: whether the string, the fan drive, the 1-Wire line and the RT1 pair share one shell, and which family carries the string current | §5, `WH1` |
| `O-127` | The main tract's 56 m³/h is `SP0008`'s **free-air** figure. The operating point against the fin stack, the duct and both terminations is unestablished and is necessarily lower, which moves the air-side film coefficient and with it `T1` and `T3`. The fan curve reaches roughly 100 Pa at shutoff, so the working point turns on a pressure drop nobody has computed or measured | `T6`, `T1`, and `E0011` §2.2's air-side terms |

## 10. Maturity

| Rung | Content |
|---|---|
| **Requirements-fixed** | Complement and requirements fixed; values estimated or `verify`; housing bottom half drawn |
| **Parts-committed** | `O-103` closed; housing complete; `L` document released |
| **As-built** | Estimates replaced by measurements; verification executed; open items closed in place |

Current rung: **Requirements-fixed**.

Reaching *Parts-committed* requires `O-103` and `O-126`. Reaching *As-built* additionally
requires `O-112` and `O-127`, whose measurements `V2` and `V9` are, and `O-107`, without which
`V7` has no boundary to run against.
