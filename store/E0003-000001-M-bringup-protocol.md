<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M02-LIGHT bring-up protocol — `E0003-000001-M-bringup-protocol`

- **Type:** HOW document (Manual, document layer **M** — ADR-0017 d9). It owns the *how*; the
  *why* is delegated to the ADRs by number (ADR-0000 d2/d3).
- **Subject:** the design `E0003-000001` (M02-LIGHT), on an `E0001` carrier.
- **Identifier:** the filename is the object key; form `Exxxx-VVVVVV-<layer>-<slug>`.
- **Scope:** type-level. This document is the **procedure**. Executing it against one instance
  produces that instance's `-QP` (Quality Protocol, ADR-0017 d10), which holds the serials, the
  ATECC608 identity and the measured values.
- **Result data does not live here.** Serials, instance identity and measured values are
  production data; they go to the ERP lifecycle-document index and the warehouse object
  `Exxxx-VVVVVV-NNNNNN-QP` (ADR-0021 d7), never into this repo (ADR-0017, *Registry and store
  location*). The result columns below stay blank in the repo copy.

---

## 1. Prerequisites

| Item | Requirement | Source |
|---|---|---|
| Board under test | One `E0003-000001`, assembled | — |
| Carrier | One `E0001` instance with a WeAct STM32F405RGT6 core board, **STRAP_1 wired through** | ADR-0002 rev 3, gotcha 1 |
| Gateway | `SP0004` on `can0` at 500 kbit/s, terminated | `SP0004-M-gateway-bringup` |
| Supply | `+12 V` SELV | ADR-0018 |
| Programmer | ST-Link V3 over SWD | — |
| Firmware | The bootloader and one application image; the application holds every personality and the module-ID strap selects M02 at runtime | ADR-0017 d16, ADR-0029 d1 |
| Diffuser | Kimoto OptSaver L-57 over U4's aperture, fitted or not — **record which** | spec §9 M1, M2 |
| Optical stimuli | Darkness, a broadband lamp, a red LED (630–660 nm), a blue LED (450–470 nm), a mains-driven lamp and a 365 nm UV-A source | §6 |
| Addresses | `spec/M02-LIGHT-specification.md` §4, §5.1. `E0003` has no pin map of its own | — |

**The released `E0001-000001-F-*` artifacts predate the M02 personality** — their body is
29 752 bytes against the ≈ 90 KB the current tree builds. Cut a fresh pair before running this
protocol: `firmware/tools/release.sh --key <pem>` for an image that may also be sent over the
air, `--unsigned` for a bench board that will only ever be flashed over SWD. An unsigned image
flashes fine and no node will accept it on the bus, which is the intended asymmetry
(ADR-0029 d6).

**Bench gotchas.** Six, and the first is the one that wastes a whole session:

1. **M02 is the first class whose strap bit 1 is set.** `STRAP_1` (PA6) reaches the MCU from
   carrier revision `E0001-000003`; on `E0001-000001` and `-000002` it arrives only with the
   `J6` pad 4 → `J3` pad 15 link added by hand. Without it the strap reads `0b000`, which claims
   no personality, and the node comes up **healthy, enumerated and publishing nothing**. That
   failure looks like a dead module and is a carrier fault. Check step 6 before anything else.
2. The ST-Link asserts NRST. Unplug it for any soak run, or the board resets under you.
3. **The debug console survives the whole run.** M02 claims no header GPIO (spec §5), so USART1
   (PA9/PA10, 115200 8N1, via the ST-Link VCP) is a console through boot *and* operation, as on
   M01 and unlike M05.
4. `VCC` on the STDC14 header is sense-only. Power the board from the supply, not the probe.
5. Drop SWD to `freq=950` for anything that reads flash. At higher rates `--upload` and `-v`
   fail on a good flash, so a verify failure there is the link, not the image.
6. **The sensing face carries U3 and U4 and nothing else** (spec §9 M10). It is the optical
   aperture; do not rest the board on it, and keep fingers and flux off both windows.

---

## 2. Incoming inspection, before power

V6 has never been executed on any board in this project, and M01's `-QP` records it as a
residual because the parts arrived after fabrication. M02's two optical parts make it cheap and
consequential: neither aperture is concentric with its package, so a footprint that is right for
the pads can still be wrong for the window.

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | 1:1 paper printout against the physical U4 | Pads coincide; the Ø0.900 mm aperture sits **0.609 mm from the package centre** along the 3.10 mm axis, and lands where the board expects it | |
| 2 | 1:1 paper printout against the physical U3 | Pads coincide with the OLGA-6 land pattern | |
| 3 | Measure U3's photodiode-group offset from the package centre | **Recorded, not judged.** The datasheet does not dimension it (spec O-70); this measurement is what closes the item and what an enclosure window is aligned to | |

Steps 1 and 2 discharge spec **V6**. Step 3 closes **O-70**. Record the U3 offset in the `-QP`
and raise it into the specification; it is a property of the part, not of the instance.

---

## 3. Flash and boot

```
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=950 -d store/E0001-000001-F-boot.hex -v
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=950 -d store/E0001-000001-F-slot-a.hex -v
```

Two images, because the bootloader and the application occupy separate sectors (ADR-0029 d1).
Each carries its own load address, so neither needs one on the command line, and neither reaches
the Node-ID sector.

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 4 | SWD connect | Core ID read; VTREF reports the board's rail | |
| 5 | Flash write and verify | Verify reports no mismatch | |
| 6 | Strap decode | Boot log reads `module-id strap = 0b010 -> M02-LIGHT (E0003)` | |
| 7 | Boot to application | Application runs, no fault loop | |

Step 6 discharges spec **V7**. `0b000` there is gotcha 1, not a module fault. Any other value is
a carrier or header fault. The personality is selected at runtime; no M02-specific image exists
to reflash.

---

## 4. bxCAN and Cyphal bring-up

| # | Check | Pass criterion | Result |
|---|---|---|---|
| 8 | CAN loopback self-test | Boot log reports it passed | |
| 9 | Bit rate | 500 kbit/s, matches the gateway | |
| 10 | Bus errors | Zero over the run | |
| 11 | Heartbeat, subject 7509 | Present and periodic | |
| 12 | Node-ID | 98 | |
| 13 | `uavcan.node.port.List`, subject 7510 | Lists 4128, 4129, 4130 and 4131 | |
| 14 | `GetInfo` name | `org.industrygrow.node.m02` | |
| 15 | Reset cause | Heartbeat payload decodes it | |

98 is this class's bring-up assignment (spec §10). 96 and 97 are taken by M05 and M01, and 1 is
the gateway's own time master — not a stray board.

---

## 5. I²C presence probe through the bus switch

I²C1 at 100 kHz standard mode, set by the shared carrier driver. **U3 and U4 are both fixed at
`0x39`** and are separated topologically by U1, so this probe has a shape no other module's has:
the switch is checked first, and each sensor is reached with exactly one channel selected
(spec §5.2).

| # | Device | Where | Pass criterion | Result |
|---|---|---|---|---|
| 16 | U1 TCA9543A | `0x70`, master segment | ACKs, and its control register reads back the value written to it | |
| 17 | U4 AS7343 | `0x39`, channel 0 | `ID` (`0x5A`) reads `0x81` | |
| 18 | U3 TSL2585 | `0x39`, channel 1 | `ID` (`0x92`) reads `0x5C`, `REV_ID` (`0x91`) reads `0x11` | |

Step 18 discharges spec **V12**.

The boot diagnostic states the population directly:
`M02 up, 1=present, bitmask U1|U4|U3 = 7` for a full board.

**An address ACK is not a pass**, and on this module the reason is sharper than elsewhere: at
`0x39` an ACK could be either sensor, reached through the wrong channel. The device-specific ID
read is the only discriminator.

**U1 powers up with both channels deselected**, so `0x39` is unreachable until firmware has
written the control register. `0x39` silent with no channel selected is a U1 fault, not two
absent sensors (spec O-65). A failure of U2's 1.8 V rail removes U4, U3 **and** U1 together, so
the board then presents as one absent device at `0x70` — *not fitted*, *failed* and *unpowered*
stay indistinguishable (spec O-37).

### 5.1 Both channels selected — the fault the topology exists to prevent

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 19 | Write U1's control register to `0x03` from a bench I²C master, then address `0x39` | The bus fault is observed and recorded | |

Step 19 discharges spec **V11**. It is **not reachable through the shipped firmware**, which
selects exactly one channel and never both; it needs a bench master on the header or a temporary
build. Seeing it once deliberately is the point — an unexplained `0x39` fault in the field is
otherwise a mystery.

---

## 6. Functional run against optical stimuli

Four published subjects at 1 Hz. Expected ranges are in `spec/M02-LIGHT-specification.md` §3; a
reading is a pass when it falls in the expected range for the stimulus applied.

| # | Subject | ID | Stimulus | Pass criterion | Result |
|---|---|---|---|---|---|
| 20 | Spectral counts | 4128 | Darkness (sensor covered) | All twelve bands and `clear` near zero; `saturation` clear | |
| 21 | Spectral counts | 4128 | Broadband lamp at working distance | Every visible band non-zero; `gain` and `integration_seconds` present in the same record | |
| 22 | Spectral counts | 4128 | A red LED, 630–660 nm, alone in darkness | **F6 (index 8, 615–665 nm) is the largest band**, F7 (index 9) carries its wing, and the blue end stays near dark | |
| 23 | Spectral counts | 4128 | A blue LED, 450–470 nm, alone in darkness | **FZ (index 2, 423–478 nm) is the largest band** and the red end stays near dark | |
| 24 | Spectral counts | 4128 | Either LED | `clear` exceeds every individual band | |
| 25 | Autorange | 4128 | Cover, then illuminate | `gain` climbs toward 2048× under cover and falls under the lamp, one step per second; the brightest band settles between 10 % and 90 % of full scale under steady light | |
| 26 | PPFD | 4129 | Any | **`valid` = false, value 0.** This is the correct result on an uncommissioned node, not a failure — the coefficients do not exist yet (§7) | |
| 27 | Flicker | 4130 | DC bench supply driving the lamp | `valid` set; `detected_100hz` and `detected_120hz` both clear | |
| 28 | Flicker | 4130 | A lamp on mains | `detected_100hz` set with `valid_100hz` on a 50 Hz supply (`detected_120hz` on 60 Hz). This is the positive control for the flicker path | |
| 29 | UV-A irradiance | 4131 | Darkness | Near zero, `valid` set | |
| 30 | UV-A irradiance | 4131 | 365 nm UV-A source | Responds; the value is **nominal**, from a typical-only responsivity (spec §6.4) | |
| 31 | UV-A irradiance | 4131 | Warm-white lamp only, no UV | Stays at dark level. This is the visible-rejection claim of spec §6.4, measured rather than assumed | |
| 32 | Bus soak | — | Normal operation, ≥ 1 h | Both sensors addressed through their own channels, zero bus errors | |

Step 32 discharges the **second half of spec V5**. The first half — SCL and SDA measured idling
at 1.8 V rather than 3.3 V, and the low level presented to the carrier against the 76 mV `R_ON`
budget — needs a scope and is not part of this protocol.

### 6.1 Vendor commands

Two, over `uavcan.node.ExecuteCommand`. Both exist because the values they report are not
derivable from the published subjects.

| Command | Reports |
|---|---|
| 1 | The gain and integration time the autorange settled on, and the brightest band as permille of full scale |
| 2 | U3's raw UV counts and the gain behind them — the figure V4 compares against dark |

### 6.2 Diffuser state

Record whether the L-57 was fitted for this run. It transmits 60 %, and that constant enters
every band (spec §9.1). Counts taken with and without it are not comparable, and the PPFD
coefficients of §7 are only valid for the optical stack they were identified through.

---

## 7. What this protocol does not verify

| Item | Why not |
|---|---|
| PPFD absolute (**V1**) | Needs a reference quantum sensor in the canopy plane at ≥ 3 output levels, per profile phase spectrum, with the diffuser and enclosure window fitted. **The project owns no quantum sensor** (spec O-52). Until it does, 4129 publishes `valid` = false |
| A 24 h photoperiod (**V2**) | One full cycle logged, no channel saturated and none below 10 % across either 30 min ramp. Hours, not minutes, and it needs the fixture |
| Per-channel spectrum (**V3**) | Each fixture channel driven alone at full output; 730 nm far-red shall register on F8. Needs a multi-channel luminaire. **This is what confirms the band map** across all twelve; steps 22–24 only rule out a gross error |
| UV absolute (**V4**) | Needs a UV-A source of known irradiance |
| Rail measurements (**V9**, **V10**) | U4's and U3's `V_DD` at the pin under load and through U2's start-up transient, against the 1.98 V absolute maximum. Needs a scope on a 1.8 V rail with 180 mV of headroom |
| Diffuser transmission (**V8**) | Band counts with and against the fitted diffuser under one fixture setting |

---

## 8. Known residuals

Carry these into the instance `-QP` rather than re-deriving them. None blocks a functional pass.

| Item | Detail |
|---|---|
| **The firmware has never met the hardware** | Written against DS001046 v6-00 and DS001043 v5-00. Four things are most likely to be wrong on first contact, in order: the `DATA_0..17` → band assignment under `auto_smux` = 3; the AS7343 register-bank exit through CFG0 (`0xBF`), used once for the `ID` probe; the TSL2585 photodiode → modulator map; and the UV scaling constants. Steps 17, 18 and 22–24 are the cheap screens for the first three |
| `AVALID` is not a freshness flag | It stays asserted, so a stalled spectral engine would republish its last set with the node reporting NOMINAL. There is no sample counter on the part. Check the **data** across V2's 24 h cycle, never the node's health — the same distinction M01 spec O-75 was learned on |
| PPFD uncommissioned | No coefficients, no reference instrument. Spec O-52, V1 |
| Node power unmeasured | Draw on `+12 V`. Spec O-59 |
| U1 has no reset line | `RESET` tied to `V_CC`; a stuck channel is recoverable only by a node power cycle. Firmware reports it rather than attempting a reset it has no line for. Spec O-65 |
| Post-excursion validity undefined | Condensation places U4 outside its 5–85 %RH non-condensing operating conditions. Spec O-57 |
| `GND` pour on `F.Cu` only | The sensor face has no plane behind it, one board thickness from the luminaire's driver. Spec O-69 |
| Deployment constants are volatile | `industryflow.greenhouse.light.ppfd_coeff` has no store behind it (ADR-0005 d7), so a commissioned node re-takes its coefficients at every restart |

Verification items **V6**, **V7**, **V11**, **V12** and the second half of **V5** are discharged
by executing this protocol; **V1**, **V2**, **V3**, **V4**, **V8**, **V9** and **V10** are not.
See `spec/M02-LIGHT-specification.md` §11.

---

## 9. Closing the protocol

1. Record every result column against the instance, not against this document.
2. File the completed protocol as `E0003-000001-NNNNNN-QP` through the ERP
   (`POST /instances/{instance_id}/documents`, `doc_type=QP`). The blob goes to the warehouse;
   the ERP holds the metadata and the object key (ADR-0021 d7).
3. Serials are **server-issued** — allocate through the ERP (`POST /instances`), never by hand
   (ADR-0021 d4, ADR-0022 d4).
4. Acceptance is not part of this protocol. It belongs to the instance's `-QR` (ADR-0017 d10).

---

## 10. References

| Reference | Subject |
|---|---|
| ADR-0017 d9 | Document layers, `M` = Manual |
| ADR-0017 d10 | `-QP` / `-QR` definition |
| ADR-0017 d16 | One image, personality selected by the module-ID strap |
| ADR-0017, *Registry and store location* | The instance layer lives platform-side, off this repo |
| ADR-0021 d4, d7 | ERP as serial authority; lifecycle-document index over the warehouse |
| ADR-0014 rev 6 | Sensor-node taxonomy, module-ID strap, sensor complement, presence probing |
| ADR-0029 d1, d6 | Bootloader and application in separate sectors; image signing |
| `spec/M02-LIGHT-specification.md` | Module specification — expected ranges (§3), acquisition (§6.1), PPFD (§6.2), optical requirements (§9), firmware (§10), verification (§11) |
| `store/E0001-000003-D-pinmap.md` | Carrier side of the header contract, and `STRAP_1` |
| `store/E0002-000001-M-bringup-protocol.md` | The same protocol for M01 |
| `store/E0006-000001-M-bringup-protocol.md` | The same protocol for M05 |
| `store/SP0004-M-gateway-bringup.md` | The gateway side of the bench setup |
