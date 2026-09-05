<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M04-PLANT bring-up protocol — `E0005-000001-M-bringup-protocol`

- **Type:** Manual, document layer **M** (ADR-0017 d9).
- **Subject:** the design `E0005-000001` (M04-PLANT), on an `E0001` carrier.
- **Scope:** type-level. One execution against one instance produces that instance's `-QP`
  (ADR-0017 d10): serials, device IDs, measured values.
- **Results:** to the ERP lifecycle index as `Exxxx-VVVVVV-NNNNNN-QP` (ADR-0021 d7), not this
  repo. Result columns stay blank here.
- **State:** §2 and §3 executable against loose U1 parts. §4 not written.

---

## 1. Prerequisites

| Item | Requirement | Source |
|---|---|---|
| U1 parts | `MLX90640ESF-BAA-000-TU`. `BAB` is a different assembly E-number | spec §2, §4.1, ADR-0017 rev 2 d4 |
| Footprint print | 1:1 paper plot of `industrygrow:MLX90640_TO-39-4` from `E0005-000001-D-src.zip` | spec §11 V5 |
| Metrology | Calipers to 0.02 mm | §2 |
| Jig supply | 3.3 V, ≥ 50 mA | spec §7.2 |
| Jig bus | I²C master, 2.2 kΩ pull-ups to 3.3 V, ≤ 400 kHz | spec §5.1 |
| Jig fixture | TO-39 4-lead test socket, or flying leads. A 2.54 mm breadboard does not take a Ø5.84 mm lead circle | spec §4.1 |
| Handling | Can held by the rim; aperture clear of flux and skin; part never rested aperture-down | spec §9 M3, M4 |
| Board under test | One assembled `E0005-000001` — §4 only, not yet available | — |

---

## 2. Incoming inspection, before power

Per delivered part, before soldering.

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 1 | Population, from the package | Body height above the seating plane 5.70 ±0.30 mm; aperture Ø2.60 ±0.10 mm, flush. `BAB` is 11.25 mm with Ø3.90 mm in an M5 threaded barrel | |
| 2 | 1:1 print against the part — lead circle | Ø5.84 ±0.18 mm, four leads at 90°, all four coincident with the printed 1.0 mm holes | |
| 3 | 1:1 print against the part — index tab | Tab 0.80 ±0.10 mm at 45° to the lead pattern; width across it 10.03 ±0.20 mm; tab clear of every hole | |
| 4 | Rotational sense of the numbering | 1 SDA, 2 VDD, 3 GND, 4 SCL, clockwise from the tab in bottom view; matches the print's pin-1 marker | |
| 5 | Lead diameter, all four leads | Ø0.45 ±0.05 mm and Ø0.70 mm; the larger clears the 1.0 mm hole | |
| 6 | Can and window | Ø9.30 ±0.15 mm; window clear, uncoated, unmarked | |

Steps 2–5 discharge spec V5.

---

## 3. Device check on a jig, before assembly

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 7 | Power at 3.3 V; first transaction after 80 ms plus one refresh interval | Supply current per spec §7.2 | |
| 8 | Address scan | Answers at `0x33`. Another address means cell `0x240F` has been written | |
| 9 | Device-ID words `0x2407`, `0x2408`, `0x2409` | All three non-zero and not `0xFFFF`; the 48-bit value identical across two consecutive reads | |
| 10 | Calibration EEPROM `0x2400`–`0x273F`, 832 words = 1664 B, ≤ 400 kHz | No NACK; block neither all `0x0000` nor all `0xFFFF` | |
| 11 | Defective-pixel list, from the step-10 block (DS12 §9) | Recorded, not judged; ≤ 4 per device | |
| 12 | Two or more parts delivered: compare the step-9 IDs | The 48-bit IDs differ | |

Step 9 discharges spec V12. Step-9 and step-11 results go to the `-QP` of the instance the part
is fitted to.

Not covered here: radiometric accuracy, non-uniformity, FOV — spec V1, V7, V10, on an assembled
node.

---

## 4. Powered bring-up on the assembled board

Not written. Written when the following exist:

| Missing | Reference |
|---|---|
| `plant` personality in the carrier image, `firmware/nodes/m04_plant/` | spec §10 |
| `industryflow.greenhouse.plant` DSDL types | spec §10.1 |
| A fabricated `E0005-000001` | `-D-fab.zip`, blocked on O-93 and V5 |

Sections owed: flash and boot; bxCAN and Cyphal enumeration; I²C presence probe, U1 `0x33` and
U2 `0x50`; frame capture and statistics; flat-field determination per spec §6.7. First instance
takes Node-ID 99.

---

## 5. References

| Document | Carries |
|---|---|
| `spec/M04-PLANT-specification.md` | Package §4.1, addresses §4 and §5.1, EEPROM and ID §10, flat field §6.7, verification §11 |
| `store/E0005-000001-D-src.zip` | Layout, and the footprint plotted for §2 |
| `store/E0001-000003-D-pinmap.md` | Carrier side of the header contract |
| `store/SP0004-M-gateway-bringup.md` | Gateway and bus, for §4 |
| ADR-0028 | Calibration-record lifecycle; d10 places the flat field in U2 |
| ADR-0017 | Document layers and the identifier form |
