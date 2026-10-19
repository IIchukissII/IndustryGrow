<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0030 — Local operator and service interface

- **Status:** Proposed
- **Date:** 2026-08-31; changed 2026-10-19
- **Parent:** [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md)
- **Related:** [ADR-0002](ADR-0002-field-bus-architecture.md) (trusted CAN domain bounded at the gateway), [ADR-0007](ADR-0007-pki-and-secure-element-identity.md) (gateway identity as the site credential), [ADR-0016](ADR-0016-empirical-survey-and-state-space-modeling.md) (survey phase, offline identification, survey kits), [ADR-0018](ADR-0018-power-distribution-and-rail-monitoring.md) (hardware over-temperature trip), [ADR-0020](ADR-0020-gateway-persistence-model.md) (gateway persistence), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (ERP takes operator decisions, not machine observations), [ADR-0025](ADR-0025-cultivation-profile-signing.md) (signed profiles a courier may carry), [ADR-0027](ADR-0027-node-identity-model.md) (Node-ID allocation), [ADR-0028](ADR-0028-commissioning-sequence-and-trim-custody.md) (commissioning sequence and the authoritative `-CC`), [ADR-0029](ADR-0029-node-firmware-update-and-bootloader.md) (signed firmware a courier may carry), [ADR-0031](ADR-0031-actuator-node-taxonomy.md) (actuator demand surface decision 7 constrains)

## Context

The cultivation profile is the single mutation interface, with no remote-command or real-time tuning channel (ADR-0015 dec 1); the gateway runs the control loops and holds a conservative fallback profile (ADR-0015 dec 8, 10). During survey actuators are operator-controlled (ADR-0016 dec 1). The over-temperature trip sits at the heating actuator, independent of MCU, gateway and cloud (ADR-0018 dec 10). Commissioning is an ordered sequence whose instance `-CC` is the authoritative trim record (ADR-0028 dec 1, 3).

The `-M-` protocol documents say how a bring-up or calibration is performed, and the per-instance `QP` / `CP` / `CC` records say what a performance produced; nothing connects the two at the cabinet. An interface that is not decided is decided by whoever builds one, and the failure to prevent is a local device that accumulates authority — telemetry nobody reconciles, changes nobody recorded, or an operation only it can perform.

| # | Property |
|---|---|
| P1 | The cabinet operates, controls and records identically with the interface absent; it is an accessory, never equipment |
| P2 | Every fact has one authoritative home (ADR-0000 dec 3); a local interface must not become a second one |
| P3 | The profile stays the single mutation channel for control behaviour (ADR-0015 dec 1) |
| P4 | Series production requires two technicians following one procedure to produce comparable records |
| P5 | Safety does not depend on an optional device |

## Decision

1. **A local operator and service interface is an optional accessory.** Removing it changes how an operator talks to the cabinet, never what the cabinet does (P1).
2. **It holds no system of record.** No telemetry store, configuration master or calibration authority: persistence is the gateway's (ADR-0020), records are the ERP's (ADR-0021, ADR-0022).
3. **It is a bus observer by default.** It joins listen-only, publishes no heartbeat and no diagnostics, and nothing on the bus subscribes to it; it takes a Node-ID from the ADR-0027 allocation when it must issue requests, and allocates none.
4. **It may command only what is commandable without it.** Every action it offers is equally performable from the gateway or an operator tool.
5. **It originates operator intent, never system input.** A value it computes may be displayed but not become an input the system consumes, because that input would be unreproducible without the accessory. A value a human enters may be carried to the component that owns it through that component's sanctioned channel.
6. **Excitation is not a setpoint.** Driving an actuator to observe the plant is permitted only while the control loops are not in control of that actuator (ADR-0016 dec 1); changing a target the loops pursue is a change of control behaviour and stays profile-only (P3). The two are separate operations and separate controls.
7. **An excitation does not outlive the tool that requested it.** Every excitation is bounded by the component that performs it, which returns to the fallback profile (ADR-0015 dec 10) when the bound expires or the requester goes silent, because the requester may be unplugged at any moment (P1). No interlock is reachable from the interface (P5).
8. **It executes procedures it did not author.** A carried `-M-` protocol is performed step by step, each step gated on the previous step's pass (ADR-0028 dec 1); the interface does not compose, edit or improvise one, because comparability rests on the procedure being the same (P4).
9. **A performance produces a record it does not keep.** The resulting `QP` / `CP` / `CC` is filed through the existing path and the filed record is authoritative (ADR-0028 dec 3); within a step the interface measures and proposes, and the record determines. This is the reading of ADR-0028 dec 9 this record adopts.
10. **A gated engineering mode may expose service functions, and creates none.** It offers commissioning and diagnostics available elsewhere (decision 4), each action recorded where the normal path records it; an action that cannot be recorded is not offered. The gate is an accident guard, not access control — anyone at the interface is inside the trusted CAN domain (ADR-0002 dec 7) — and the mode is visibly indicated and lapses on its own.
11. **Removable media is a courier, never a record.** It may carry a signed firmware image (ADR-0029), a signed profile (ADR-0025), a procedure, or an operator-initiated export; it may not carry a telemetry log, which would be a copy with no rule for which is right (P2).
12. **The realization is not fixed here.** An installed panel, a gateway-served local page and a portable service instrument — which ADR-0016 dec 9 packages with a survey kit — are all permitted; part selection and interfaces belong to the document whose job is that value (ADR-0000 dec 2).

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 2 | Local telemetry logging | P2: a third copy beside the gateway buffer and the ERP, with no retention rule, carried by media around the gateway boundary (ADR-0002 dec 7) |
| 2 | The interface as an ERP client | ADR-0022 dec 9, and it duplicates the gateway identity ADR-0007 dec 11 makes the site credential |
| 4 | The interface runs control loops when the gateway is absent | ADR-0015 dec 8 gives the loops to the gateway; two controllers contend, and the removable one must not be one |
| 6 | A local setpoint channel for a running cultivation | ADR-0015 dec 1; decision 6 grants excitation without opening that channel |
| 10 | An engineering mode with unrecorded full access | P4, and an unrecorded change is the silent override ADR-0000 dec 4 forbids |
| 12 | A panel-specific record | Forecloses the gateway-served page; the constraints are identical and hardware is not this record's subject |

## Consequences

**Positive**

- Local visibility and manual operation come without a cabinet dependency, so the interface is a purchasable option rather than a BOM line.
- The `-M-` protocols gain an executor and the `QP` / `CP` / `CC` records a producer, without either changing owner.

**Negative**

- Decision 7 obliges the actuator command surface to carry a bound and a fallback on whichever record defines it.
- Decision 5 forbids features where the interface's own computation feeds the system; such work belongs to the gateway or to identification (ADR-0016 dec 2).
- Nothing here is validated on hardware; acceptance records agreement, not implementation (ADR-0000 dec 7).

## Deferred

| Open question | Settled by |
|---|---|
| The excitation request path: how decision 7's bound, fallback trigger and requester liveness map onto the actuator demand surface | ADR-0031 |
| Whether a carried procedure is signed, and against which trust root | ADR-0025 |
| Which realization is delivered first, and its part selection | E0010 specification |

## References

- `spec/E0010-R-specification.md` — the service-tool realization's requirements.
- `service-tool/` — the service-tool firmware.
