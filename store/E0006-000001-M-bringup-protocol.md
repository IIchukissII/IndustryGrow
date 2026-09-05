<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M05-SAFETY bring-up protocol — `E0006-000001-M-bringup-protocol`

- **Type:** Manual, document layer **M** (ADR-0017 d9).
- **Subject:** the design `E0006-000001` (M05-SAFETY), on an `E0001` carrier.
- **Scope:** type-level. One execution against one instance produces that instance's `-QP`
  (ADR-0017 d10): serials, ATECC608 identity, measured values.
- **Results:** to the ERP lifecycle index as `Exxxx-VVVVVV-NNNNNN-QP` (ADR-0021 d7), not this
  repo. Result columns stay blank here.

---

## 1. Prerequisites

| Item | Requirement | Source |
|---|---|---|
| Board under test | One `E0006-000001`, assembled | — |
| Carrier | One `E0001` instance with a WeAct STM32F405RGT6 core board | ADR-0002 rev 3 |
| Gateway | `SP0004` on `can0` at 500 kbit/s, terminated | `SP0004-M-gateway-bringup` |
| Supply | `+12 V` SELV | ADR-0018 |
| Programmer | ST-Link V3 over SWD | — |
| Firmware | The M05 node image built from `firmware/` | — |
| Addresses and values | `E0006-000001-D-pinmap.md`. Neither U1 nor U2 sits at its part-default strap position; firmware constants come from the pin map | — |

**Bench gotchas.** Three, each of which has cost time before:

1. The ST-Link asserts NRST. Unplug it for any soak run, or the board resets under you.
2. The debug console (USART1, 115200 8N1, via the ST-Link VCP) **ends at `sensors_init()`** —
   the leak channel reclaims PA9, which is also USART1_TX. Any `uart_puts()` you need must come
   before that call.
3. `VCC` on the STDC14 header is sense-only. Power the board from the supply, not the probe.

---

## 2. Flash and boot

Connect-under-reset is required. A plain `-c port=SWD` fails with `Unable to get core ID`
against a free-running target.

```
STM32_Programmer_CLI.exe -c port=SWD mode=UR freq=1800 reset=HWrst \
    -w build/m05.hex -v -rst
```

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | SWD connect under reset | Core ID read | |
| 2 | Flash write and verify | Verify reports no mismatch | |
| 3 | Boot to application | Application runs, no fault loop | |

---

## 3. bxCAN and Cyphal bring-up

| # | Check | Pass criterion | Result |
|---|---|---|---|
| 4 | Bit rate | 500 kbit/s, matches the gateway | |
| 5 | Bus errors | Zero over the run | |
| 6 | Heartbeat, subject 7509 | Present and periodic | |
| 7 | Node-ID | As configured for M05 | |
| 8 | Reset cause | Heartbeat payload decodes it | |

---

## 4. I²C sensor-presence probe

I²C1 at 100 kHz standard mode, set by the shared carrier driver.

| # | Device | Pass criterion | Result |
|---|---|---|---|
| 9 | U1 TMP117, bay air temperature | Answers at the pin-map address | |
| 10 | U2 INA226, bus voltage and current | Answers at the pin-map address | |

**Diagnostic order matters.** U1 is the bus reference: its `ADD0` sits on the same net as U2's
`A0`. If U1 answers, the bus, pull-ups and timing are proven, and any other silence is that
part's address or wiring. Probe U1 first.

---

## 5. Functional run against bench stimuli

Eight published subjects. Expected operating ranges are in
`spec/M05-SAFETY-specification.md` §3 and §6.1; a reading is a pass when it falls in the
expected range for the stimulus applied.

| # | Subject | ID | Stimulus | Pass criterion | Result |
|---|---|---|---|---|---|
| 11 | Heartbeat | 7509 | Free run | Periodic | |
| 12 | `+12 V` bus voltage | 4096 | Node powered from the SELV supply | In the spec §3 expected range | |
| 13 | `+12 V` bus current | 4097 | One node on the bus, across R1 = 0.1 Ω | In the spec §3 expected range | |
| 14 | Bus power | 4098 | Derived | Consistent with 4096 × 4097 | |
| 15 | Bay air temperature | 4099 | Ambient bench air | Tracks a reference thermometer in the same air | |
| 16 | Door state | 4100 | Reed actuated by hand | Toggles clean in both directions | |
| 17 | Leak state | 4101 | Electrode dry → wet → dry | Tracks state in both directions | |
| 18 | Actuator energy | 4102 | S0 pulses injected | Counts accumulate, 3600 J/pulse | |

### 5.1 Leak channel — what to confirm

Excitation is gated: `GPIO_1` (PA9) driven 5 ms per 1 Hz sample, 0.5 % duty, released **low**,
so no DC sits across the electrodes. Confirm the release is low; an idle-high excitation drives
the electrode continuously and will still appear to work (ADR-0018 d11).

`LEAK_WET_THRESHOLD` is provisional. Calibrating it needs real water and the reservoir lead, not
a damp finger — record the threshold used, and treat a hand-wetted pass as provisional.

### 5.2 Door channel — expected failure direction

A cut or unplugged reed lead floats PA15 high on the internal pull-up, giving `engaged = 0`,
reported as *door open*. The channel is expected to fail toward the alert, never into silence.
Confirm this by unplugging the lead. The `valid` field is hardcoded per `DoorStatus.1.0.dsdl`
("input configured, not in fault"); the door is report/alert only, with no automatic cutoff
(ADR-0018 d10, d83), so no fault is detectable for `valid` to report. This is not a defect.

---

## 6. Known residuals

Carry these into the instance `-QP` rather than re-deriving them. None blocks a functional pass.

| Item | Detail |
|---|---|
| Energy accumulator is volatile | `s_pulses` in `s0.c` is plain RAM; accumulated energy resets to zero on every reboot or power cycle. Needs a decision before deployment |
| No S0 debounce | No software debounce on the EXTI handler. A real meter's S0 output is a clean opto-isolated pulse; bench bounce comes from hand-held wiring |
| `LEAK_WET_THRESHOLD` provisional | §5.1 |
| Per-device power unmeasured | U1 and U2 individual draw, microamp-class on 3.3 V. Spec O-30 |

Verification items V2, V3 and V4 of the specification are **not** part of this protocol —
V1 is. See `spec/M05-SAFETY-specification.md` §10 and open items O-25, O-27, O-28.

---

## 7. Closing the protocol

1. Record every result column against the instance, not against this document.
2. File the completed protocol as `E0006-000001-NNNNNN-QP` through the ERP
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
| ADR-0017, *Registry and store location* | The instance layer lives platform-side, off this repo |
| ADR-0021 d4, d7 | ERP as serial authority; lifecycle-document index over the warehouse |
| ADR-0018 d10, d11, d83 | Sense-only function class; gated leak excitation; door report/alert only |
| `spec/M05-SAFETY-specification.md` | Module specification — expected ranges (§3, §6.1), verification (§10) |
| `store/E0006-000001-D-pinmap.md` | Addresses and conditioning values |
| `store/SP0004-M-gateway-bringup.md` | The gateway side of the bench setup |
