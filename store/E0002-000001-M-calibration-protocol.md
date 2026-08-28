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
| 2 | U1 heater | Never commanded during the run | |
| 3 | Still air, board undisturbed | No enclosure change, no handling | |
| 4 | No commands issued | Nothing addresses the node during the run, apart from the §7 write between the determination and verification sets | |
| 5 | No reset | Heartbeat uptime monotonic end to end | |
| 6 | Data arriving | Sample count matches the published rate, with no gap exceeding four publication intervals. `health=NOMINAL` is **not** this check — O-75 | |

## 5. Sampling gate

Thermal equilibrium is established from the data, not from elapsed time — and not from one long
window. The room wanders on an hour timescale at ramp rates far below any per-window threshold,
and that wander moves U1 and U3 by different amounts, because they are different packages at
different points on the board. Inside a single window that error is invisible: it looks like a
steady offset, and the standard error of the window mean understates the true uncertainty by
more than an order of magnitude.

**The determination is a mean over many short windows spread across a full daily cycle**, not one
long window. The spread across those windows *is* the uncertainty, and it is the only honest
estimate of it.

| # | Gate | Threshold | Result |
|---|---|---|---|
| 1 | Window size | ≥ 100 paired U3 samples per window — **≈ 8 min** at U3's 0.2 Hz. Use **10–15 min**; longer is wasted, see below. The first ≥ 20 min after power-on is excluded | |
| 2 | Window count | ≥ 24 accepted windows | |
| 3 | Total span | ≥ 12 h between the first and last accepted window | |
| 4 | Net ambient ramp | \|U1(last window) − U1(first window)\| / span ≤ 2 mK/min | |
| 5 | Ramp balance | Windows of each sign of dU1/dt ≥ ⅓ of the accepted set | |
| 6 | Reproducibility | Standard error of the mean of the window means ≤ 25 mK | |

`D_ss` is the mean of the accepted window means. **Quote gate 6's standard error beside it; a
determination without it is not a result.**

**Windows are not individually gated on ramp.** An earlier revision required \|dU1/dt\| ≤ 2 mK/min
per window. That threshold is only meaningful over an hour or more: the room's instantaneous ramp
rate swings by an order of magnitude more than that as its thermostat cycles, so applied to a
short window the same number rejects nearly everything and leaves too few windows to measure a
spread. Ramp is bounded across the ensemble instead, by gates 4 and 5.

**Why span, not length.** Lengthening a single window is close to useless against correlated
wander — it buys far less than the square root of the added time. For a fixed total observing
time it is actively worse, because it yields fewer independent samples of the wander and so a
*larger* standard error on the result. Prefer many short windows over few long ones.

**The window is already long enough at ten minutes.** The offset is a degree-scale quantity
against per-sample noise of a few tens of mK, so a ten-minute window resolves it to well inside
the centi-degree it is written in — and inside U1's own accuracy, which is the floor no averaging
gets past (§3). Going from ten minutes to an hour changes a window's mean by single-digit mK
while costing six times the observing time. Spend that time on more windows, further apart.

**How gates 4 and 5 bound the lag term.** On a ramp the slower device lags, so each window's
difference carries a term `−dtau · (dU1/dt)`. Averaged over the accepted set that term becomes
`−dtau ·` mean ramp, and the mean ramp over the span is fixed by its endpoints alone. Gate 4
bounds it at 2 mK/min; U3's lag against U1 cannot plausibly exceed a few minutes, so the residual
bias is single-digit mK — below the centi-degree the result is rounded to in §6. **No lag fit is
needed, and none should be used.**

**Retired: the fitted-lag gate.** An earlier revision fitted

```
U3 - U1 = D_ss - dtau * (dU1/dt)
```

across blocks within one window and required the intercept to agree with the plain mean within
20 mK. That gate is withdrawn. It is ill-conditioned exactly where it matters: in a quiet window
the regressor's range is small, the fit has no leverage, and the recovered `dtau` is not
reproducible between adjacent windows — it varies far more widely than any physical thermal lag
and changes sign. It rejected sound windows on a meaningless fit and passed others by luck. Do
not reinstate it as a per-window test.

**The span is a property of the room, not of the measurement.** The offset itself is a large
signal — of order a degree against per-sample noise of a few tens of mK — and the *precision*
needed to resolve it is reached in a couple of minutes. Gates 2 and 3 are not there for
precision. They are there because the ambient is uncontrolled: the room's wander biases any short
stretch, and only spanning a large fraction of its daily cycle averages that bias out. **In an
ambient that is actively held, the requirement collapses.**

| Path | Ambient | Gates 2 and 3 |
|---|---|---|
| **A — uncontrolled** | Bench, room air, no active control | As tabled above: ≥ 24 windows, ≥ 12 h |
| **B — controlled** | Chamber or held enclosure, setpoint maintained across the run | ≥ 6 windows over ≥ 1 h, *provided* the hold is demonstrated: U1's own drift across the run ≤ 50 mK, and no monotonic trend in U1 across the accepted windows |

Path B is admissible only with the hold demonstrated from U1's own record, not asserted from the
chamber's setpoint. Gates 1, 4, 5 and 6 apply unchanged on both paths. Which path was used is
recorded in the `-CP`.

**Rejected: stopping when the windows agree.** A self-terminating rule — accumulate short windows,
stop once the standard error of their mean falls below a threshold — is the obvious way to make
path A cheap. It does not work, and it fails in the dangerous direction. A locally quiet stretch
has small internal scatter while sitting at a biased offset, so the rule terminates early,
reports a small standard error, and is wrong: tried across this class of run it converged in
well under an hour and still landed of order 100 mK from the multi-hour value, with a tail
several times worse. **Scatter within a stretch does not measure the offset of that stretch.**
Do not implement it. If the run must be short, control the ambient (path B) rather than trusting
an agreement test.

**Excluding a disturbance.** A window covering a step change in the air — a door or vent opened,
the board handled — fails §4 condition 3 and is dropped before gating, not argued about
afterwards. Such an event is visible as a coincident step in CO₂ and in U3 − U1. Dropping one is
recorded in the `-CP` with its reason; gate 6 is the backstop if one is missed.

## 6. Determination — U3 temperature offset

Per the SCD4x datasheet, with `T_offset_previous` read from the boot log:

```
T_offset_new = T_offset_previous + (T_U3 - T_U1)
```

The factory default is 4 °C and is not this board's value. Write in centi-degrees, rounded. The
device encoding is `word = offset_C * 65535 / 175`, so the written value quantizes to 2.67 mK —
below every other term.

Rounding to a centi-degree is coarser than gate 6's standard error, so a determination that
passes §5 will not be improved by a longer run. **U1's own absolute accuracy is the binding
term** and is not reduced by any amount of averaging (§3).

| # | Step | Pass criterion | Result |
|---|---|---|---|
| 1 | `T_offset_previous` from the boot log | Read back, not assumed | |
| 2 | `T_U3 - T_U1` as `D_ss`, the mean of the accepted window means | §5 passed, all six gates | |
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
| 2 | Re-apply §5 to the post-write windows | All six gates pass | |
| 3 | Residual | \|D_ss\| ≤ 0.1 K, quoted with gate 6's standard error | |
| 4 | Persistence | Power-cycle; the boot log reads back the written offset | |

A boot log that reads back 4.000 °C after a successful write means the persist did not stick —
the same EEPROM-persistence failure mode the bring-up protocol checks on ASC.

## 9. Records produced

| Object | Holds |
|---|---|
| `E0002-000001-NNNNNN-CP-YYYYMMDD` | Raw points: every accepted window with its bounds and mean, every window rejected and by which gate, the six §5 gate values, ambient, and the §4 run conditions as executed |
| `E0002-000001-NNNNNN-CC-YYYYMMDD` | The coefficient written, its units, the reference used, the ambient it was determined at, and its validity |

**Validity.** The offset is a property of this board in these operating conditions, not of the
device type. It is re-determined after any change to the board's thermal environment — an
enclosure fitted or changed, the mounting changed, or the gas scan interval or setpoint list
changed. Until a `-CC` exists for an instance, that instance's subjects 4120 and 4121 are
invalid (O-45).
