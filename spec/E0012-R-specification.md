<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# A01-CLIMATE tempering unit — assembly specification

- **Status:** Working specification, pre-build. `E0012` not built; the housing bottom half exists as design source, and the duct set is in development and unidentified (§2)
- **Date:** 2026-09-11
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
| Design source | Housing: `store/E0012-000001-D-case-src.zip`, STEP only (ADR-0019 d9). Duct set: `project/mechanical/`, outside the document store while the combination iterates |
| Duct set identity | None. No section, flange or termination of the §5 path takes a number of its own; identification follows at the commit the combination reaches (ADR-0032 d8, ADR-0017 d5, ADR-0019 d9) |

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
| HX1 | `SP0007` — 62 × 74 mm section, 150 mm; the catalogue lists `Rth` 0.175 K/W at 56 m³/h and 0.225 K/W at 46 m³/h for this profile and length (`verify` — distributor listings of the Fischer data, not the Fischer datasheet) | Grow-volume exchanger of the main tract, and the cabinet's circulation | `T3`, `O-112`. The Fischer LA 6/150 carries this section; its 24 V aggregate is the 46 m³/h one and its own fan does not meet `SP0008`, so the pairing is `O-112` |
| E2 | `SP0008` — ebm-papst **614 NHH**: 24 V over 18–26 V, 6850 min⁻¹, 2.9 W, 56 m³/h **free air**, 41 dB(A), ball bearing, L10 60 000 h at 40 °C, ambient −20…+70 °C, locked-rotor and overload protection, 0.066 kg | Main-tract air mover, mounted on HX1 | Fed from `SP0010`; duty from `E0011` U9 channel 0. Shutoff pressure 105 Pa, read from the datasheet curve rather than stated as a figure. The 612 NHH is the 12 V member of the pair and is **not** this part. `E0011`'s `E5` mounts the same part, and neither the duty input nor the alarm output is settled (`E0011`'s `O-128`) |
| WB1 | `SP0009` — liquid cold plate, 40 × 120 mm working face | Rejection face of `E1`, clamping plate of the column, mounting face of U1 and RT1 | Two wet joints, `M6` |
| U1 | DS18B20, 1-Wire, 12-bit, 750 ms conversion (`verify`) | WB1 block temperature | On `E0011`'s 1-Wire bus; derates both tracts through `E0011`'s `D9` |
| U2 | DS18B20, same bus | HX1 base temperature | Floors `E1` cooling through `E0011`'s `D10` |
| RT1 | NTC 10 kΩ B25/85 3435, at WB1 | Trip element of `E0011`'s `T2` | The comparator is on the node, not in this unit |
| CP1 | Clamping plate and disc-spring set | Column clamping load | `M1` |
| TIM1 | Thermal interface material, both faces of each module | Module-to-plate conduction | `M2` |
| HS1 | Printed housing, PETG | Exchanger enclosure, mounting and column location | `M4`; design source per §2 |
| DA1 | Printed fan flange, PETG | Carries `SP0008` on its hole pitch and presents the §5 flange | `M8` |
| DT1 | Commodity tube sections and bends, DN 75 per §5 | The run between HX1 and each termination | Bend sweep `O-130` |
| FC1 | Printed flange collar, PETG, one per tube end | Takes a DN 75 tube end and presents the §5 flange | `M7` |
| IT1 | Printed intake termination, PETG | Floor intake of `M5` | `M9` |
| PL1 | Printed discharge plenum, PETG | Converts the tract bore to the `M5` slot | `M9` |
| SL1 | Printed TPU gasket, one per flange pair | Joint seal of every §5 flange | `M7`, `M4`, `O-130` |
| WH1 | Harness and connector | The four §5 circuits | Unspecified — `O-126` |

U1, U2 and RT1 are ordinary purchased components and stay MPN lines in the `L` document, on the
same footing as the TMP117 counter-example of ADR-0019 d4. Every SP-numbered part above is a unit
of procurement, and `SP0008` is additionally an actuator device (d6). DA1, DT1, FC1, IT1, PL1 and
SL1 carry no number while the combination iterates (§2).

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

The air path is modular on one interface, shared with the humidity tract (ADR-0032 d8,
`E0011`'s `M9`):

![The main tract as a chain of flanged parts on one bore: the floor intake termination IT1, a run of tube sections and bends DT1 whose ends are taken by flange collars FC1, the fan flange DA1 carrying the fan E2 on the fan's own hole pitch, the exchanger HX1, a second DT1 run, and the discharge plenum PL1 converting the bore to the M5 ceiling slot; every joint is a bolted face flange with an SL1 gasket, and the only point narrower than the bore is the fan's own opening](./figures/e0012-duct-interface.svg)

| Dimension | Value |
|---|---|
| Tract bore | DN 75 to DIN EN 1451-1: 75 mm outside, 1.9 mm wall, **71.2 mm free bore**, 39.8 cm² |
| Bore basis | HX1's 62 × 74 mm section, 45.9 cm² gross |
| `E2` fan face | 60 × 60 mm frame, 50 × 50 mm hole pitch, 3.7 mm holes, Ø 64 mm panel opening — the narrowest point of the path |
| Flange | Ø 110 outside, Ø 96 bolt circle, 4 × M4, face 5 mm thick |
| Gasket | SL1, flat, Ø 96 outside, Ø 71.2 inside, 2 mm thick |
| Tube to flange | FC1 collar, socket Ø 75.6 × 32 deep, two M4 set screws at 90° |
| Discharge | PL1 to the `M5` slot at the `M9` area |
| Humidity tract | Same bore and same flange; sections, collars, flanges and terminations interchange |

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
| `T7` | The air path outside HX1 — DA1, DT1, FC1, both terminations and every flange — costs ≤ **30 Pa** at the §4 free-air flow. HX1's own drop is outside this budget | `O-127`, `O-112`, `T6` |

Computed at the §4 free-air flow through the §5 bore, mean velocity 3.9 m/s. One metre of run
with two bends totals 26 Pa. A flange carries no term of its own, the bore being continuous
across it:

| Term | Computed |
|---|---|
| DT1 friction | 3.5 Pa per metre of run |
| Swept 90° bend | 4 Pa each; the family's 87° elbow is sharper, two 45° sections hold the term (`O-130`) |
| IT1 plain socket entry | 5 Pa |
| PL1 and the `M5` slot at the `M9` area | 9 Pa |

## 7. Mechanical requirements

| ID | Requirement | Reference |
|---|---|---|
| `M1` | TEC1 and TEC2 are each clamped between HX1 and WB1 at the datasheet clamping force, through CP1 with disc springs. No printed part carries clamping load | `O-103` |
| `M2` | Both faces of every module carry a thermal interface material rated for continuous 100 °C (`verify`) | `O-103` |
| `M3` | HX1 and WB1 are degreased before assembly | `T2` |
| `M4` | Structural printed parts are PETG and printed seals are TPU. No printed part is in direct thermal contact with a module hot face or with WB1, and no printed surface exceeds **60 °C** continuous (`verify` — PETG HDT ≈ 70 °C at 0.45 MPa; TPU hardness and continuous service temperature are `O-130`) | `T5` |
| `M5` | The discharge termination is a horizontal slot, gap ≤ 20 mm, directed at the far wall; the intake termination is at the floor. The two tracts' intakes are separated in height | ADR-0003 air movement |
| `M6` | WB1's supply and return are the unit's only wet joints, and both are outside the housing. The column is serviceable without breaking the coolant circuit | ADR-0032 decision drivers |
| `M7` | Every section, collar, flange and termination of the §5 path carries the tract bore, and every joint between them is a bolted face flange on the §5 pattern with an SL1 gasket. A joint separates by removing its four screws, with no cutting and no bonding. Nothing in the path is narrower than the bore except the `E2` opening and the `M5` slot | ADR-0032 d8, `T7` |
| `M8` | DA1 carries `SP0008` on the §5 hole pitch and presents the §5 flange at the bore. No printed part tapers the bore | ADR-0032 d8, `T7` |
| `M9` | PL1 converts the tract bore to the `M5` slot and IT1 the floor intake to the bore. The slot's open area is not less than the bore's 39.8 cm², which at `M5`'s 20 mm gap is 200 mm of slot width | `M5`, `T7`, `V7` |

## 8. Verification

| ID | Verifies | Method |
|---|---|---|
| `V1` | `T1` | Step `E1` to the limit into a cabinet at a known start temperature; record the pull-down curve, the WB1 rise and the loop supply temperature |
| `V2` | `T3` | Substitute a resistor of known power for the module pair; thermocouples on the HX1 base and in the stream |
| `V3` | `T2` | Run at the night load with the grow volume at the wet edge of the band for 4 h; record U2 against the M01 dew point and confirm no free water on HX1 or in the duct |
| `V4` | `T4` | Compare U1 and RT1 against a reference sensor on the WB1 plate at the `T1` duty |
| `V5` | `M1`, `M2`, `M3` | Record clamping force at assembly against the module datasheet, the TIM temperature rating, and that each exchanger was degreased |
| `V6` | `M4`, `T5` | Thermograph the printed parts at the `T1` duty; confirm no surface exceeds 60 °C |
| `V7` | `M5`, `M9` | Measure the slot's open area against the `M9` figure, then trace the discharge jet with the cabinet loaded; confirm it reaches the far wall and that intake air is not drawn directly from the discharge |
| `V8` | `M6` | Pressure-test both joints at the loop's working pressure with the column assembled; confirm no wetted part is inside the housing |
| `V9` | `T6` | Measure tract volume flow with the unit assembled and both terminations fitted, at full `E2` duty and at the `E0011` §3.1 minimum |
| `V10` | `T7` | Measure static pressure from the IT1 inlet to the PL1 slot with the path assembled at full `E2` duty, then repeat with one bend added; the difference is the per-bend term |
| `V11` | `M7`, `M8` | Unbolt every flange of the assembled path and re-assemble it; traverse each joint with the tract running and confirm no leakage, and repeat on one joint ten times against the same check |

## 9. Open items

| # | Item | Blocks |
|---|---|---|
| `O-103` | Thermoelectric module not selected; α, R, K, clamping force and TIM are unconfirmed. Owned by `E0011-R-specification.md`, consumed here | §4, `T1`, `M1`, `M2` |
| `O-107` | Wall penetration, seal and thermal break are enclosure design; the unit's terminations meet them at an undrawn boundary. Owned by `E0011-R-specification.md`, consumed here | `M5` |
| `O-112` | HX1's catalogue `Rth` excludes spreading from two point-source modules and misses `T3`: 0.175 K/W at 56 m³/h before spreading, against `T3`'s 0.17 K/W with it. The 24 V aggregate is 0.225 K/W at 46 m³/h and its fan misses `SP0008`; the pairing is unchosen. HX1's air-side pressure drop is unpublished. Bench measurement required | `T3`, `T1`, `T7`, `O-127`, `E0011`'s `D10` |
| `O-126` | Harness and connector unspecified: whether the string, the fan drive, the 1-Wire line and the RT1 pair share one shell, and which family carries the string current | §5, `WH1` |
| `O-127` | The fan's operating point is unestablished: HX1's air-side drop is unpublished (`O-112`) and the duct's is computed, not measured. It lies below `SP0008`'s 56 m³/h free-air figure, which moves the air-side film coefficient and with it `T1` and `T3` | `T6`, `T1`, `O-112`, `E0011` §2.2's air-side terms |
| `O-130` | The duct combination is unfixed: run length and bend count per tract; whether DN 75 carries a bend of the sweep `T7`'s 4 Pa term assumes; the `M5` slot's width at the `M9` area; SL1's TPU hardness, continuous service temperature and compression set over the `V11` cycles; whether FC1 holds a tube end on set screws alone | `T7`, `M4`, `M7`, `M9` |

## 10. Maturity

| Rung | Content |
|---|---|
| **Requirements-fixed** | Complement and requirements fixed; values estimated or `verify`; housing bottom half drawn; duct set in development |
| **Parts-committed** | `O-103` closed; housing complete; duct combination fixed and identified; `L` document released |
| **As-built** | Estimates replaced by measurements; verification executed; open items closed in place |

Current rung: **Requirements-fixed**.

Reaching *Parts-committed* requires `O-103`, `O-126` and `O-130`. Reaching *As-built*
additionally requires `O-112` and `O-127`, whose measurements `V2`, `V9` and `V10` are, and
`O-107`, without which `V7` has no boundary to run against.
