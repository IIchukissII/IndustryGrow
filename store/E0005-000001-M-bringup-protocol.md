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
- **State:** §2 and §3 are executable against loose U1 parts; §4 needs a fabricated board, which
  O-93 and V5 block.

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
| Board under test | One assembled `E0005-000001` — §4 only | — |
| Carrier | One `E0001` instance with a WeAct STM32F405RGT6 core board | ADR-0002 rev 3 |
| Gateway | `SP0004` on `can0` at 500 kbit/s, terminated, time master running | `SP0004-M-gateway-bringup` |
| Supply | `+12 V` SELV, with a current readout to 1 mA | ADR-0018, spec §7.1 |
| Programmer | ST-Link V3 over SWD; console on the VCP, 115200 8N1 | — |
| Firmware | `store/E0001-000001-F-boot.hex` and `-F-slot-a.hex` — bootloader and one application image; the application holds every personality and the strap selects M04 at runtime | ADR-0017 d16, ADR-0029 d1 |
| Reference target | A matte surface filling the field at the test distance, its temperature read by a contact thermometer | §4.5 |
| Heat source | A warm object 10 K or more above the scene, hand-held | §4.5 |
| Meter | DMM at the module's 3V3 pin | spec §11 V11 |

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

Steps 13 onward, on an `E0005-000001` in an `E0001` socket. The personality
(`firmware/nodes/m04_plant/`, strap `0b100`) and the `industryflow.greenhouse.plant` type are in
the carrier image and are datasheet-authored: §4.9 lists what that leaves open.

**Bench gotchas.**

1. **The imager face is the outward one.** U1 stands alone on `B.Cu`, which faces away from the
   carrier once the module is seated. Do not rest the board on it, and keep flux and fingers off
   the window (spec §9 M3, M4).
2. **The obstacle-free cone is 140°** for the `BAA` build, referenced to the external aperture
   (spec §9 M1). A hand, a probe lead or a bench edge inside it is in the scene.
3. **M04 raises I²C1 to 400 kHz** and holds the bus 300 ms of every second (spec §5.2). A bench
   master on the same segment competes with it.
4. The ST-Link asserts NRST — unplug it for any soak longer than a few minutes.
5. USART1 (PA9/PA10) stays free on this module, so the console is live through boot and
   operation (spec §5).
6. **Ta is the die temperature, not air**: the air around the device sits about 8 K below it
   (spec §8 T1). Nothing here compares Ta to a room thermometer and calls it a fault.

### 4.1 Flash and boot

```
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=950 -d store/E0001-000001-F-boot.hex -v
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=950 -d store/E0001-000001-F-slot-a.hex -v
```

Two images, separate sectors (ADR-0029 d1); each carries its own load address and neither
reaches the Node-ID sector. **A mass erase de-provisions the node** (ADR-0027 d4).

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 13 | SWD connect | Core ID read; VTREF reports the board's rail | |
| 14 | Flash write and verify | Verify reports no mismatch | |
| 15 | Strap decode | Boot log reads `module-id strap = 0b100 -> M04-PLANT (E0005)` | |
| 16 | Boot to application | Application runs, no fault loop | |

Step 15 discharges spec **V6**. Bit 2 is `STRAP_2`, routed on every carrier revision, so `0b000`
here is a header or solder fault rather than the `STRAP_1` gap that catches M02.

### 4.2 Node-ID

The first instance takes **99**; 96, 97 and 98 are M05, M01 and M02, and 1 is the gateway's own
time master.

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 17 | Read `uavcan.node.id` | 127 on a virgin carrier — no Node-ID provisioned | |
| 18 | Write `uavcan.node.id` = 99, then restart | Register reads 99 before the restart, transport runs on 99 after it | |

### 4.3 bxCAN and Cyphal enumeration

| # | Check | Pass criterion | Result |
|---|-------|----------------|--------|
| 19 | CAN loopback self-test | Boot log reports it passed | |
| 20 | Heartbeat, subject 7509 | Present and periodic, health NOMINAL | |
| 21 | `GetInfo` name | `org.industrygrow.node.m04` | |
| 22 | `uavcan.node.port.List`, subject 7510 | Publishers list **4144**; servers list **408** (`uavcan.file.Read`) and **409** (`uavcan.file.Write`) | |
| 23 | Bus errors | Zero over the run | |

### 4.4 I²C presence probe and calibration restore

One segment at 400 kHz, two devices, no switch (spec §5.1).

| # | Device | Pass criterion | Result |
|---|--------|----------------|--------|
| 24 | U1 MLX90640, `0x33` | Answers, and vendor command **1** reports the three device-ID words non-zero and equal to the §3 step 9 reading for the part fitted | |
| 25 | U2 M24C64, `0x50` | Answers; vendor command **3** reports the class-ID byte. `0xFF` on a virgin store, `0x04` once programmed, **anything else is another module class** | |
| 26 | Calibration restore | Console reads `calibration restored`; the boot diagnostic reads `M04 up, 1=present, bitmask U1\|restore\|U2\|trim = 7` | |
| 27 | Defective pixels | Subject 4144's `defective_pixels` equals the §3 step 11 count for this part, 0…4 | |

Step 24 discharges spec **V12** on the assembled board; step 27 discharges the published half of
**V9**. A failed restore leaves the node enumerated and silent on 4144 — that is the specified
behaviour, not a fault of this step (spec §10).

### 4.5 Frame capture and statistics

Subject 4144 at 1 Hz, computed on the mean of that second's four complete frames (spec §6.4).

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 28 | Publication rate | 4144 arrives at 1 Hz, `timestamp` synchronized to the gateway base | |
| 29 | Reference target at 0.3 m, settled | `t_mean` within ±2 °C of the contact thermometer, `t_sigma` ≤ 0.3 K. **Not spec V1**: ε is unidentified (O-87) and one temperature is not three | |
| 30 | Warm object moved through the field | `t_max` follows it, `hotspot_mask` sets bits where it is, and clears when it leaves | |
| 31 | Constants in force | `emissivity` = 1.0 and `reflected_temperature` = `ta` − 8 K on an uncommissioned node (spec §10) | |
| 32 | Stabilization | From a cold power-on, `stabilized` false for the first 4 min and set afterwards | |
| 33 | Ta against air | `ta` runs about 8 K above the canopy air an M01 instance in the same volume reports | |
| 34 | 19-bit saturation | Imaging the luminaire at the mounting distance, no pixel rails | |
| 35 | Soak, ≥ 1 h | Heartbeat continuity unbroken, zero I²C errors, and vendor command **2** reports frames dropped ≪ frames accumulated | |

Step 34 discharges spec **V15**'s saturation half; step 35 discharges **V3** and is the evidence
for **O-91**. Steps 32 and 33 discharge **V4** when run from a cold start with the log kept.

### 4.6 The served interval frame

`/plant/frame.bin` over `uavcan.file.Read`, 3904 B, one per minute (spec §10.2).

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 36 | Announcement | `frame_available` set and `frame_seq` advancing once a minute | |
| 37 | Transfer | The gateway reads 3904 B; magic `IGP1`, version 1, 32 × 24, `n_frames` 235…240 | |
| 38 | Header CRC | CRC-32 over bytes 0…59 and 64…3903 matches the header field | |
| 39 | Header identity | Device ID equals step 24's; emissivity, reflected temperature and interval Ta match the records published during that interval | |
| 40 | Agreement with the record | Frame-wide mean of the mean plane equals the arithmetic mean of that interval's 60 `t_mean` values, within one quantization step | |
| 41 | σ plane | Noise floor over a still scene; rises only on pixels a warm object was swept across during that interval | |
| 42 | Event frame | Drive the hotspot count past 8, or `t_max` past `t_mean` + 5 K: one frame arrives with `n_frames` = 1, the event flag set and a zero σ plane, and no second one inside the same interval | |

Steps 37…42 discharge spec **V8**. Step 41 is the executable half of **V13**.

### 4.7 Power and rail

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 43 | Node draw on `+12 V` | Recorded against the 360 mW estimate; closes **O-94** | |
| 44 | U1 current on 3.3 V | ≤ 25 mA (spec §7.2) | |
| 45 | Rail at U1's VDD pin, DC | 3.3 V ±0.05 V. Ripple against the buck's 400 kHz fundamental needs a scope and is not part of this protocol; **O-95** stays open on that half | |

Steps 43 and 44 discharge spec **V7**.

### 4.8 Flat field

Determination is **not specified** — the project owns no uniform reference target and the
one- against two-point question is unsettled (spec §6.7, O-99). What is executable is the
binding, which is what stops another module's field being applied to this imager.

| # | Step | Pass criterion | Result |
|---|------|----------------|--------|
| 46 | Uncommissioned state | `flat_field` false in the record and in the frame header; vendor command **3** reports `absent: no record` | |
| 47 | Offer a record carrying another imager's device ID over `uavcan.file.Write` to `/plant/flatfield.bin` | **Refused** with `INVALID_VALUE`, reported as `refused: another imager`, nothing written to U2, `flat_field` still false | |
| 48 | Offer a record whose CRC-32 is wrong by one bit | **Refused** as `refused: CRC` | |

Steps 47 and 48 discharge the refusal half of spec **V16**. The accept half — a field written,
read back after a power cycle and confirmed applied — waits on O-99: a record with no
determination behind it would set `flat_field` on an instance that carries no trim.

### 4.9 What this protocol does not verify

| Item | Reason |
|---|---|
| Subpage read time, **V3** second half | 37.5 ms at 400 kHz measured on SCL; needs a logic analyser on the header |
| Excluded-pixel behaviour, **V9** second half | A part with a known defective pixel, confirmed absent from the statistics and interpolated in the served plane |
| Frame accuracy, **V1** | Three temperatures spanning 10…30 °C against a target of **known** emissivity, settled and filling the field. ε is unidentified (O-87) and the project owns no such target |
| Installed footprint, **V2** | Needs the installed mounting height, which no record fixes (O-86) |
| ε and Tr identification, **V10** | Commissioning against a reference target at canopy distance (O-87) |
| Rail ripple, **V11** | Scope on the 3.3 V rail (O-95) |
| Raw-frame reproduction and per-pixel noise, **V13** | 240 consecutive device frames logged raw and the mean plane reproduced offline; the node serves the product, not the raw images |
| Compensation timing and the numerics check, **V14** | Cycle-counter timing on target, and a synthetic constant-temperature sequence through the accumulator |
| 18-bit against 19-bit comparison, **V15** second half | Needs a build that changes the resolution field |
| Flat-field accept path, **V16** | O-99 |

### 4.10 Known residuals

Carry these into the instance `-QP` rather than re-deriving them. None blocks a functional pass.

| Item | Detail |
|---|---|
| **The firmware has never met the hardware** | Written against DS12. Most likely wrong first, in order: the chunked read against the 8 Hz refresh (step 35); the 400 kHz bus timing; the 2 K Ta-jump frame-validity rule rejecting good frames; and the trim commit path (steps 47, 48) |
| Statistics are frame-wide | No segmentation separates leaf from structure, medium or luminaire, so every statistic includes them (O-88) |
| Non-uniformity uncorrected | ±0.5 K fixed-pattern in zone 1 stands until a flat field is determined (O-99) |
| Long-term drift | An additional ±3 °C over years around room temperature, with no re-referencing scheme (O-89) |
| Deployment constants are volatile | The four `industryflow.greenhouse.plant.*` registers have no store behind them (ADR-0005 d7), so a commissioned node re-takes them at every restart |
| Assembly route | The through-hole hermetic can on an otherwise SMT board is not fixed (O-93) |

Executing §4 discharges spec **V4**, **V6**, **V7**, **V8** and **V12**, the continuity half of
**V3**, the published half of **V9**, and the saturation half of **V15**; §2 discharges **V5**.
See `spec/E0005-R-specification.md` §11.

---

## 5. References

| Document | Carries |
|---|---|
| `spec/E0005-R-specification.md` | Package §4.1, addresses §4 and §5.1, EEPROM and ID §10, flat field §6.7, verification §11 |
| `store/E0005-000001-D-src.zip` | Layout, and the footprint plotted for §2 |
| `store/E0001-000003-D-pinmap.md` | Carrier side of the header contract |
| `store/SP0004-M-gateway-bringup.md` | Gateway and bus, for §4 |
| ADR-0028 | Calibration-record lifecycle; d10 places the flat field in U2 |
| ADR-0017 | Document layers and the identifier form |
| ADR-0005 d7, d11, d12 | Subject set, the served frame, and the register store the deployment constants do not have |
| ADR-0014 rev 6 d6, d8 | Module-ID strap and presence probing |
| ADR-0027 d4 | Node-ID store, and why a mass erase de-provisions a node |
| ADR-0029 d1 | Bootloader and application in separate sectors |
| `firmware/README.md` | Default subject-ID map and the Node-ID provisioning steps |
