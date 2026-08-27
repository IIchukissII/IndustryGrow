<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE calibration protocol — `E0002-000001-M-calibration-protocol`

- **Type:** HOW document (Manual, document layer **M** — ADR-0017 d9). It owns the *how*; the
  *why* is delegated to the ADRs by number (ADR-0000 d2/d3).
- **Subject:** the design `E0002-000001` (M01-CLIMATE), on an `E0001` carrier.
- **Identifier:** the filename is the object key; form `Exxxx-VVVVVV-<layer>-<slug>`.
- **Scope:** type-level. This document is the **procedure**. Executing it against one instance
  produces that instance's `-CP` (raw points) and `-CC` (coefficient and validity), ADR-0017 d11.
- **Calibration recurs.** The `-QP` of `E0002-000001-M-bringup-protocol` is issued once; `-CP`
  and `-CC` are dated `-YYYYMMDD` so a later run does not overwrite an earlier record
  (ADR-0017 d11). They key off the instance identifier, never the integration identifier (d13).
- **Result data does not live here.** Serials, instance identity and measured values go to the
  ERP lifecycle-document index and the warehouse object (ADR-0021 d7), never into this repo
  (ADR-0017, *Registry and store location*). The result columns below stay blank in the repo copy.

---

## 1. Trims this covers

| Trim | Device | Written by | Verifies |
|---|---|---|---|
| Temperature offset | U3 SCD41 | §10 command 3 — centi-degrees C, decimal ASCII, bounded 0–2000 | V7, O-45 |

U1 and U2 carry no trim: both are used at their factory calibration, and §4 of the specification
admits only U1 for VPD. A further trim adds a row here and a section to §6; it does not add a
document.

## 2. Prerequisites

| Item | Requirement | Source |
|---|---|---|
| Instance | One `E0002-000001` that has passed `E0002-000001-M-bringup-protocol` | — |
| Firmware | An image carrying §10 command 3 | ADR-0017 d16 |
| Gateway | `SP0004` on `can0` at 500 kbit/s, terminated, node publishing | `SP0004-M-gateway-bringup` |
| Command tool | `gateway/node_command.py`, run against the node's provisioned Node-ID | ADR-0027 |
| Logger | Any consumer recording subjects 4112 and 4120 with timestamps | §10.1 |

**The ST-Link asserts NRST — unplug it.** A reset mid-window restarts the thermal transient and
voids the run. Uptime in the heartbeat is the check.

## 3. The reference

| Source | Role | Basis |
|---|---|---|
| **U1 SHT45, subject 4112** | **The reference** | §10 makes U1 the board's only primary T. It sits in the same air as U3, and its sustained self-heating is 0.004 K (§8.1) |
| M05 TMP117, subject 4099 | Ambient hold only | ADR-0014 alternative G — the TMP117 measures the cabinet, a different volume. It confirms the ambient held; it does not determine a trim |
| U2 BME68x, subject 4118 | Neither | Its own hotplate moves it during a gas scan, and §4 does not admit it for VPD |

The determination is differential and needs no absolute reference. Its dominant uncertainty is
U1's own accuracy, not the statistics of the window.

## 4. Run conditions

The offset compounds measurement mode, neighbouring self-heating, ambient and air flow (§8.1),
so it is determined in the mode the board is used in and is only valid for it.

| # | Condition | Pass criterion | Result |
|---|---|---|---|
| 1 | Application operating mode | U2 gas scan **armed** at its build interval, U3 periodic | |
| 2 | U1 heater | Never commanded during the window | |
| 3 | Still air, board undisturbed | No enclosure change, no handling | |
| 4 | No commands issued | Nothing addresses the node during the window | |
| 5 | No reset | Heartbeat uptime monotonic end to end | |
| 6 | Data arriving | Sample count matches the published rate. `health=NOMINAL` is **not** this check — O-75 | |

## 5. Equilibrium gate

Thermal equilibrium is established from the data, not from elapsed time. All four gates hold on
the window before any value is determined.

| # | Gate | Threshold | Result |
|---|---|---|---|
| 1 | Window length | ≥ 60 min of paired samples, with the first ≥ 20 min after power-on excluded | |
| 2 | Ambient ramp | \|dU1/dt\| ≤ 2 mK/min on block means | |
| 3 | Difference drift | Block-mean drift of (U3 − U1) ≤ 1 mK/min across ≥ 6 blocks | |
| 4 | Lag term collapsed | Plain mean and ramp-rate intercept agree within 20 mK | |

Gate 4 is what separates equilibrium from a steady ramp, and gates 1–3 do not imply it. On a
ramp the slower device lags, so the measured difference carries a term proportional to the ramp
rate. Fit across blocks

```
U3 - U1 = D_ss - dtau * (dU1/dt)
```

and take `D_ss` at the intercept. While a lag term survives, the intercept and the plain mean
disagree; at equilibrium they converge and either may be used.

## 6. Determination — U3 temperature offset

Per the SCD4x datasheet, with `T_offset_previous` read from the boot log:

```
T_offset_new = T_offset_previous + (T_U3 - T_U1)
```

The factory default is 4 °C and is not this board's value. Write in centi-degrees, rounded. The
device encoding is `word = offset_C * 65535 / 175`, so the written value quantizes to 2.67 mK —
below every other term.

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | `T_offset_previous` from the boot log | Read back, not assumed | |
| 2 | `T_U3 - T_U1` over the gated window | §5 passed | |
| 3 | `T_offset_new` in centi-degrees | Within 0–2000 | |

## 7. Write

One command, one EEPROM cycle. The EEPROM is rated for at least 2000 cycles; the write is issued
once per determination and never on a schedule (§10).

```
node_command.py <node-id> 3 --parameter <centi-degrees>
```

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | Command accepted | `SUCCESS`. `BAD_PARAMETER` is a value outside 0–2000, missing or non-decimal; `BAD_STATE` is U3 absent or busy | |
| 2 | Write and persist | Diagnostic Record reads `U3 temperature offset written, centi-C = <n>` | |

`SUCCESS` is the transport's answer and is returned before the device is touched — the node
stops U3, writes, persists in its own pass and reconfigures across several loop passes. Step 2 is
the write's own answer and is the one that counts.

## 8. Verification — V7

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | Discard the settling period | ≥ 15 min after the write. Stop, persist and reconfigure leave U3 transient | |
| 2 | Re-apply §5 to the post-write window | All four gates pass | |
| 3 | Residual | \|U3 − U1\| ≤ 0.1 K | |
| 4 | Persistence | Power-cycle; the boot log reads back the written offset | |

A boot log that reads back 4.000 °C after a successful write means the persist did not stick —
the same EEPROM-persistence failure mode the bring-up protocol checks on ASC.

## 9. Records produced

| Object | Holds |
|---|---|
| `E0002-000001-NNNNNN-CP-YYYYMMDD` | Raw points: window bounds, the block table, the four §5 gate values, ambient, and the §4 run conditions as executed |
| `E0002-000001-NNNNNN-CC-YYYYMMDD` | The coefficient written, its units, the reference used, the ambient it was determined at, and its validity |

**Validity.** The offset is a property of this board in these operating conditions, not of the
device type. It is re-determined after any change to the board's thermal environment — an
enclosure fitted or changed, the mounting changed, or the gas scan interval or setpoint list
changed. Until a `-CC` exists for an instance, that instance's subjects 4120 and 4121 are
invalid (O-45).
