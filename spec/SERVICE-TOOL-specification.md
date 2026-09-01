<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# SERVICE-TOOL — specification

- **Status:** Project stage — requirements fixed, no realization committed
- **Date:** 2026-09-01
- **Identifier:** none assigned (ADR-0030 d12)
- **Governing ADRs:** ADR-0030, ADR-0015, ADR-0016, ADR-0028
- **Companions:** ADR-0002 (rev 3), ADR-0005, ADR-0017 (rev 2), ADR-0020, ADR-0022, ADR-0027

Rationale for the role, its bounds and the rejected alternatives is in the governing ADRs and is
not restated. Values marked `verify` are not confirmed against a measurement or a datasheet.

## 1. Scope

Specifies the portable service and diagnostic tool: what it presents, what it executes, what it
emits, and how each is verified.

Does not specify: the realization or its part selection (ADR-0030 d12); the actuator command
surface it requests excitation through (ADR-0030 deferred); the content of any `-M-` protocol;
the `QP` / `CP` / `CC` schemas (ADR-0022); the cultivation profile (ADR-0015).

## 2. Identification

No E-number or SP-number is assigned; the identifier is assigned at design commit and is not
pre-reserved. Whether it takes an E-number (ADR-0017) or rolls up as a designed accessory
(ADR-0019 d9) follows from the realization chosen (O-76).

The tool takes a Cyphal Node-ID from the ADR-0027 allocation and allocates none (ADR-0030 d3).

## 3. Function

| # | Function | Reference |
|---|---|---|
| **F1** | Presents every subject in the type registry, live, with value, unit and age | ADR-0023, ADR-0030 d1 |
| **F2** | Judges and presents per-signal liveness | ADR-0030 d1 |
| **F3** | Executes an `-M-` protocol step by step, each step gated on the previous step's pass | ADR-0028 d1, ADR-0030 d8 |
| **F4** | Emits the per-instance `QP` / `CP` / `CC` a protocol run produces, for filing | ADR-0028 d3, ADR-0030 d9 |
| **F5** | Requests bounded excitation of an actuator, while the loops are not in control of it | ADR-0016 d1, ADR-0030 d6, d7 |
| **F6** | Issues diagnostics that are issuable without it: GetInfo, restart, register access | ADR-0030 d4 |
| **F7** | Imports a procedure and exports a run result on removable media | ADR-0030 d11 |

### 3.1 Exclusions

| Excluded | Owner |
|---|---|
| Telemetry persistence | Gateway (ADR-0020) |
| Records of any kind | ERP (ADR-0021, ADR-0022) |
| Control loops | Gateway (ADR-0015 d8) |
| Setpoints of a running cultivation | Profile (ADR-0015 d1) |
| System identification | Offline (ADR-0016 d2) |
| Procedure authorship | The `-M-` document |
| Trim determination | The filed `-CC` (ADR-0028 d3) |
| Interlocks | Heating actuator (ADR-0018 d10) |

## 4. Interfaces

| # | Interface | Value | Reference |
|---|---|---|---|
| **I1** | Cabinet bus | Classic CAN, 500 kbit/s, extended IDs | ADR-0002 d8 |
| **I2** | Bus entry state | Listen-only until an operator action; publishes no heartbeat, no diagnostics | ADR-0030 d3 |
| **I3** | Application protocol | Cyphal/CAN; `uavcan.*` and `industryflow.greenhouse.*` | ADR-0005 |
| **I4** | Removable media | Read for procedures, signed images and signed profiles; write for run results only | ADR-0030 d11 |
| **I5** | Termination | None fitted; the tool attaches as a stub. The cabinet's two terminated ends are unchanged by attaching or removing the tool | ADR-0002 d8, ADR-0030 d7 |
| **I6** | Filing | `POST /api/v1/instances/{instance}/documents`, multipart form `doc_type`, `doc_date`, `file` | ADR-0022 d7 |
| **I7** | Filing document types | `doc_type` ∈ `{QP, QR, CP, CC, PR}`; the object key is issued by the ERP, not by the tool | ADR-0022 d7 |

## 5. Software requirements

| # | Requirement | Value | Reference |
|---|---|---|---|
| **S1** | Receive path buffers frames independently of the display | ≥ 100 ms of a saturated bus, ≥ 400 frames | I1, F1 |
| **S2** | Frames dropped for want of buffer are counted and presented | count, not a flag | S1 |
| **S3** | Default publication period assumed before one is observed | 1 Hz | ADR-0005 d4 |
| **S4** | Liveness `LATE` threshold | age > 2.5 × observed period | F2 |
| **S5** | Liveness `STALE` threshold | age > 6 × observed period | F2 |
| **S6** | Observed period is measured per signal, not assumed per class | smoothed inter-arrival | F2, S3 |
| **S7** | Liveness bounds clamp the observed period | 250 ms ≤ period ≤ 60 s | S4, S5 |
| **S8** | A non-`LIVE` signal is annunciated outside the liveness view | visible from any view | F2 |
| **S9** | Liveness and value states carry a word, never colour alone | — | F1, F2 |
| **S10** | A protocol step refuses to run when its precondition has not passed | refusal is presented, not silent | F3, ADR-0028 d1 |
| **S11** | A protocol is executed as carried; the tool offers no edit, reorder or skip | — | F3, ADR-0030 d8 |
| **S12** | A run result records the protocol identity and version it was produced under | — | F4 |
| **S13** | Excitation is requested with a duration; the tool never terminates it itself | — | F5, ADR-0030 d7 |
| **S14** | Excitation is refused while the loops are in control of that actuator | refusal is presented | F5, ADR-0015 d1 |
| **S15** | Values computed by the tool are presented, never emitted as system input | — | ADR-0030 d5 |
| **S16** | Vendor command version matches the node firmware | `uavcan.node.ExecuteCommand` 1.0 | F6 |
| **S17** | Engineering mode is visibly indicated while active and lapses without action | timeout `verify` | ADR-0030 d10 |
| **S18** | Every engineering-mode action is recorded where the normal path records it | — | ADR-0030 d10 |
| **S19** | No telemetry is written to removable media | — | ADR-0030 d11 |
| **S20** | Subject-ID to type binding is taken from the registry, not restated | generated at build | ADR-0023, ADR-0000 d3 |
| **S21** | An operator's selection survives a data refresh | selection restored by identity, not index | F1, F2 |
| **S22** | Emitted record form | Markdown; the carried document's step tables with an observed column and a per-step `Pass` / `Fail` | F4, I6 |
| **S23** | Emitted record names the procedure performed | the `Exxxx-VVVVVV-M-<slug>` identity, verbatim from the carried document | S12, ADR-0030 d8 |
| **S24** | Instance identity is an input to the run, fixed before step one | the tool allocates no serial and derives none | ADR-0021 d4, ADR-0022 d4 |
| **S25** | A value the tool measured is distinguishable from an operator judgement in the record | per step | ADR-0030 d5, d9 |
| **S26** | Document type by run | bring-up → `QP`; calibration → `CP` and `CC` | I7, ADR-0017 d10, d11 |
| **S27** | The tool emits the record and retains no copy after filing | — | ADR-0030 d2, d9 |

## 6. Presentation requirements

Requirements on any realization. No part is named.

| # | Requirement | Value | Reference |
|---|---|---|---|
| **P1** | Legible at arm's length by a standing technician | `verify` | F1 |
| **P2** | Operable while wearing gloves | touch target ≥ 9 mm | F3 |
| **P3** | Distinguishes state without colour discrimination | word plus colour | S9 |
| **P4** | Presents the bus state from every view | — | S8 |

## 7. Verification

Entries marked *internal loopback* were executed with the interface transmitting
to its own receive path, with no bus attached. That covers everything above the
transceiver — reassembly, decode, model, presentation — and nothing below it.
Entries marked *cabinet bus* were executed against the bench bus of ADR-0002 d8,
attached as a listen-only stub.

| # | Verifies | Method | State |
|---|---|---|---|
| **V1** | S1, S2 | Saturate the bus at 500 kbit/s while forcing a worst-case repaint; confirm zero drops and that a forced drop is counted | **S2 passed 2026-09-01**: drain stalled until the ring overflowed, loss counted as a count and the path recovered. **S1 partial**: 46 145 frames over 97 s under continuous full-screen repaint, 0 lost, CPU 96 % idle — but at 949 frames/s, 24 % of line rate, which is the bench ceiling (O-83) |
| **V2** | S4, S5, S6, S7, F2 | Stop one publisher; confirm `LATE` then `STALE` at the specified multiples of its observed period | **Passed 2026-09-01**, internal loopback. Observed period 999 ms; `LATE` at 5991 ms (S4 bound 2498 ms, S5 bound 5994 ms), `STALE` at 10992 ms |
| **V3** | S8, P4 | Repeat V2 while on an unrelated view; confirm annunciation | Not executed |
| **V4** | S10, S11 | Attempt a step out of order and an edit; confirm both refused and the refusal presented | **Passed 2026-09-01** against `E0002-000001-M-bringup-protocol`, parsed as carried into 23 steps. A step two ahead was refused "the previous step has not passed"; revising a recorded outcome was refused "a recorded outcome is not revised here". Both refusals presented. The executor exposes no call that edits, reorders or skips |
| **V5** | F3, F4, S12, S22, S23, S26 | Execute one `-M-` protocol end to end; file the emitted record through I6 and read it back; confirm the blob is unchanged | Not executed |
| **V6** | F5, S13, ADR-0030 d7 | Request excitation, then remove the tool; confirm the excitation lapses and the fallback profile resumes | Not executed |
| **V7** | S14 | Request excitation while the loops control that actuator; confirm refusal | Not executed |
| **V8** | S16, F6 | Issue GetInfo and restart to a node; confirm both answered | **Partial 2026-09-01**, cabinet bus: GetInfo issued by operator action and answered by both nodes, which named them. Restart not issued, so S16's command version is unexercised |
| **V9** | S17, S18 | Enter engineering mode, act, leave it idle; confirm indication, record and lapse | Not executed |
| **V10** | I2, I5 | Attach to a live bus and observe for 10 min; confirm no frame is transmitted and no termination is fitted | **I2 passed 2026-09-01**, cabinet bus: 10 min listen-only, 22 078 frames received, 0 transmitted, TEC and REC 0. **I5 FAILED**: a termination is fitted on the tool, and the bench compensated by removing M01's — see O-84 |
| **V11** | S19 | Exercise every export path; confirm no telemetry reaches the media | Not executed |
| **V12** | F1, S20, I1, I3 | Publish every subject in the registry; confirm each renders with value, unit and age, and that the binding is generated at build rather than hand-listed | **Partial 2026-09-01**, cabinet bus, 10 min: 17 subjects from two nodes rendered with value, unit and age — 22 830 frames, 9 929 transfers, 0 dropped, 0 unknown, TEC and REC 0. Every subject the bench publishes; the registry's remaining classes have no publisher here |
| **V13** | S9, P3 | Render every state in greyscale; confirm each remains distinguishable | Not executed |
| **V14** | S3, S6 | Publish one signal at a rate other than 1 Hz; confirm the observed period replaces the assumption and the liveness thresholds follow it | **Passed 2026-09-01**, cabinet bus. Three rates learned per signal from live publishers: 999 ms, 4577 ms, 9998 ms |
| **V15** | S15 | Exercise every emitted artifact; confirm no tool-computed value appears in any of them | Not executed |
| **V16** | F7, I4 | Import a procedure and export its run result; confirm the round trip | Not executed |
| **V17** | P2 | Measure the smallest interactive target | **Passed 2026-09-01** by calculation on a 5.7 in 640 × 480 panel: pitch 0.181 mm, smallest interactive height 50 px = 9.05 mm. Not a physical measurement |
| **V18** | S21 | Select a signal, then let the data refresh; confirm the selection is still the one selected | **Passed 2026-09-01**, internal loopback |
| **V19** | S24, S27 | Complete a run with no instance fixed; confirm no record is emitted, and that a filed run leaves no copy on the tool | Not executed |
| **V20** | S25 | Complete a step whose observed value the tool measured and one judged by the operator; confirm the record distinguishes them | Not executed |

## 8. Open items

| # | Item | Blocks |
|---|---|---|
| **O-76** | Realization not chosen — installed panel, gateway-served page, or portable instrument | Identification, P1, and any part selection |
| **O-77** | The actuator command surface carrying the excitation bound does not exist | F5, S13, S14, V6, V7 |
| **O-78** | Whether a carried procedure is signed, and against which trust root | F7, I4, S11 |
| **O-80** | S17 engineering-mode timeout not chosen | S17, V9 |
| **O-81** | P1 legibility not expressed as a testable figure, so it has no verification entry | P1 |
| **O-82** | ADR-0030 d9 adopts a reading of ADR-0028 d9; not confirmed by the maintainers | F4, V5 |
| **O-83** | The bench cannot saturate the bus: the gateway's MCP2515 tops out at 949 frames/s, 24 % of line rate, however many generators run. V1's S1 half needs a generator that reaches line rate. V8's restart half needs a node the bench can afford to take off the bus | V1, V8 |
| **O-84** | The bench carries its termination on the tool and not on M01, so removing the tool leaves the bus with one terminated end | I5, V10 |

## 9. Maturity

| Rung | Content |
|---|---|
| **Requirements-fixed** | Functions, interfaces and software requirements fixed; realization open; verification unexecuted |
| **Realization-committed** | O-76 closed; identifier assigned; presentation requirements resolved to figures |
| **As-built** | Estimates replaced by measurements; verification executed; open items closed in place |

Current rung: **Requirements-fixed**.

Reaching *Realization-committed* requires O-76 and O-81. Reaching *As-built* additionally
requires O-77, without which V6 and V7 cannot run, and O-83, which holds the two entries a
real bus has not yet exercised.
