<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE bring-up protocol — `E0002-000001-M-bringup-protocol`

- **Type:** HOW document (Manual, document layer **M** — ADR-0017 d9). It owns the *how*; the
  *why* is delegated to the ADRs by number (ADR-0000 d2/d3).
- **Subject:** the design `E0002-000001` (M01-CLIMATE), on an `E0001` carrier.
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
| Board under test | One `E0002-000001`, assembled | — |
| Carrier | One `E0001` instance with a WeAct STM32F405RGT6 core board | ADR-0002 rev 3 |
| Gateway | `SP0004` on `can0` at 500 kbit/s, terminated | `SP0004-M-gateway-bringup` |
| Supply | `+12 V` SELV | ADR-0018 |
| Programmer | ST-Link V3 over SWD | — |
| Firmware | `store/E0001-000001-F.hex` — one image holding every personality; the module-ID strap selects M01 at runtime | ADR-0017 d16 |
| Addresses | `spec/M01-CLIMATE-specification.md` §4. All three devices sit at their part-default addresses; `E0002` has no pin map of its own | — |

**Bench gotchas.** Four, each of which has cost time before:

1. The ST-Link asserts NRST. Unplug it for any soak run, or the board resets under you.
2. **The debug console survives the whole run.** Unlike M05, M01 claims no header GPIO
   (spec §5), so USART1 (PA9/PA10, 115200 8N1, via the ST-Link VCP) is a console through boot
   *and* operation. The M05 habit of printing everything before `sensors_init()` does not apply.
3. `VCC` on the STDC14 header is sense-only. Power the board from the supply, not the probe.
4. Drop SWD to `freq=950` for anything that reads flash. At higher rates `--upload` and `-v`
   fail on a good flash, so a verify failure there is the link, not the image.

---

## 2. Flash and boot

```
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=950 \
    -d store/E0001-000001-F.hex -v
```

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | SWD connect | Core ID read; VTREF reports the board's rail | |
| 2 | Flash write and verify | Verify reports no mismatch | |
| 3 | Boot to application | Application runs, no fault loop | |
| 4 | Strap decode | Boot log reads `module-id strap = 0b001 -> M01-CLIMATE (E0002)` | |

A strap that decodes to anything else is a carrier or header fault, not a module fault. The
personality is selected at runtime; no M01-specific image exists to reflash.

---

## 3. bxCAN and Cyphal bring-up

| # | Check | Pass criterion | Result |
|---|---|---|---|
| 5 | CAN loopback self-test | Boot log reports it passed | |
| 6 | Bit rate | 500 kbit/s, matches the gateway | |
| 7 | Bus errors | Zero over the run | |
| 8 | Heartbeat, subject 7509 | Present and periodic | |
| 9 | Node-ID | 97 | |
| 10 | Reset cause | Heartbeat payload decodes it | |

---

## 4. I²C sensor-presence probe

I²C1 at 100 kHz standard mode, set by the shared carrier driver.

| # | Device | Address | Pass criterion | Result |
|---|---|---|---|---|
| 11 | U1 SHT45, air temperature and humidity | `0x44` | Answers, and its serial read passes CRC | |
| 12 | U2 BME68x, pressure and gas | `0x76` | Answers; variant reads **BME688** | |
| 13 | U3 SCD41, CO₂ | `0x62` | Answers; serial read passes CRC | |

**An address ACK is not a pass.** Two failure modes defeat a bare probe:

- M05's INA226 also answers at `0x44`, so an ACK there does not prove an SHT45 is fitted. The
  CRC-checked serial read is the discriminator.
- A latched I²C fault makes *every* device answer while no transfer completes (spec O-37).
  A device counts as present only once step 14 has published a sample from it.

Confirm U2's variant. `BME680` in the boot log means the §4.2 alternative was fitted; the driver
handles both, the BOM does not.

---

## 5. Functional run against bench stimuli

Ten published subjects, nine of them at 1 Hz or the U3 interval; 4117 arrives at the gas-scan
interval of spec §6.4. Expected operating ranges are in `spec/M01-CLIMATE-specification.md` §3;
a reading is a pass when it falls in the expected range for the stimulus applied.

| # | Subject | ID | Stimulus | Pass criterion | Result |
|---|---|---|---|---|---|
| 14 | Air temperature, U1 | 4112 | Ambient bench air | In the spec §3 expected range; tracks a reference thermometer in the same air | |
| 15 | Air humidity, U1 | 4113 | Ambient bench air | In the spec §3 expected range | |
| 16 | VPD | 4114 | Derived on-node from U1 alone | Consistent with 4112 and 4113 per spec §6.1 | |
| 17 | CO₂ | 4115 | Ambient bench air, then exhaled breath at the sensor | Ambient in range; breath drives it up and it recovers | |
| 18 | Barometric pressure | 4116 | Ambient | Within 10 hPa of the local station reading | |
| 19 | Gas resistance | 4117 | Ambient, heater profile 320 °C | Non-zero, `valid` set. Arrives at the scan interval the boot line reports (10 s), not at the 1 s tick — allow a full interval before calling it absent. A build with `M01_GAS_SCAN` = 0 withholds it entirely and says `gas scan PARKED` | |
| 20 | Air temperature, U2 | 4118 | Ambient bench air | Within the §8 self-heating budget of 4112 | |
| 21 | Air humidity, U2 | 4119 | Ambient bench air | Consistent with 4113 | |
| 22 | Air temperature, U3 | 4120 | Ambient bench air | Within the §8 self-heating budget of 4112, offset uncalibrated | |
| 23 | Air humidity, U3 | 4121 | Ambient bench air | Consistent with 4113, offset uncalibrated | |

Publication rates: 1 Hz for U1 and U2; U3 publishes one sample per 5 s interval, which is the
only interval the device offers.

### 5.1 U3 configuration state — what to confirm

The boot log reports what the configuration did about automatic self-calibration. Record it.

| Log text | Meaning |
|---|---|
| `ASC already off (no EEPROM write)` | Expected on a device this firmware has configured before |
| `ASC was ON -> off, persisted (one EEPROM cycle)` | Expected on first boot of a new device |
| `ASC was ON -> off in RAM only, NOT persisted` | The setting will not survive a power cycle |

ASC found on again after this firmware has already configured the device means the persist is
not sticking — one of the EEPROM's 2000 cycles per boot. Treat it as a defect, not a residual.

ASC is disabled by design (spec §6.3), which makes FRC mandatory. FRC is barred until five days
after the parts were soldered, must run at the 2.8 V application rail, and needs ≥ 3 min of
normal operation first. It has no entry point in the firmware — spec O-5. It is **not** part of
this protocol.

### 5.2 U3 temperature offset

The boot log reports the offset held in the device. `4.000 °C` is the device default and is not
this board's value: until V7 determines one, U3's temperature and humidity carry an
uncalibrated bias (spec O-45). Record the value; do not write one here.

### 5.3 What this protocol does not verify

No reference instrument beyond a thermometer in the same air. The functional run is a population
and publication check against plausible ambient values. U1 and U3 accuracy are the subjects of
V4 and V7, and CO₂ against a reference is V5, blocked on M07 (spec O-7).

---

## 6. Known residuals

Carry these into the instance `-QP` rather than re-deriving them. None blocks a functional pass.

| Item | Detail |
|---|---|
| Node draw unmeasured | Draw on `+12 V`, average and during a U3 burst, needs a meter in the feed. Spec O-35, V3 |
| U3 offset uncalibrated | Device holds the default; U3 T/RH carry a bias. Spec O-45, V7 |
| Boot probe cannot separate absence from fault | *Not fitted*, *failed*, *unpowered* and a latched bus fault are one observation. Spec O-37 |
| Rail ripple unmeasured | The 30 mV limit of spec §7.4.1 needs instrumentation the project does not have. Spec O-44 |
| Footprints never checked against parts | V6, never executed; the board was fabricated ahead of it |
| C8 effective capacitance not read from the DC-bias curve | V8, never executed |

Verification items V1 to V8 of the specification are **not** part of this protocol; none of them
is discharged by executing it. See `spec/M01-CLIMATE-specification.md` §11.

---

## 7. Closing the protocol

1. Record every result column against the instance, not against this document.
2. File the completed protocol as `E0002-000001-NNNNNN-QP` through the ERP
   (`POST /instances/{instance_id}/documents`, `doc_type=QP`). The blob goes to the warehouse;
   the ERP holds the metadata and the object key (ADR-0021 d7).
3. Serials are **server-issued** — allocate through the ERP (`POST /instances`), never by hand
   (ADR-0021 d4, ADR-0022 d4).
4. Acceptance is not part of this protocol. It belongs to the instance's `-QR` (ADR-0017 d10).

---

## 8. References

| Reference | Subject |
|---|---|
| ADR-0017 d9 | Document layers, `M` = Manual |
| ADR-0017 d10 | `-QP` / `-QR` definition |
| ADR-0017 d16 | One image, personality selected by the module-ID strap |
| ADR-0017, *Registry and store location* | The instance layer lives platform-side, off this repo |
| ADR-0021 d4, d7 | ERP as serial authority; lifecycle-document index over the warehouse |
| ADR-0014 | Sensor-node taxonomy, module-ID strap, presence probing |
| `spec/M01-CLIMATE-specification.md` | Module specification — expected ranges (§3), published subjects (§10.1), verification (§11), bring-up record (§11.2) |
| `store/E0001-000003-D-pinmap.md` | Carrier side of the header contract |
| `store/E0006-000001-M-bringup-protocol.md` | The same protocol for M05 |
| `store/SP0004-M-gateway-bringup.md` | The gateway side of the bench setup |
