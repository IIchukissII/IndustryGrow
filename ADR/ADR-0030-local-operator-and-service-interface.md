<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0030: Local operator and service interface

- **ID:** ADR-0030
- **Status:** Proposed
- **Date:** 2026-08-31
- **Project:** IndustryGrow
- **Parent:** ADR-0015
- **Companions:** ADR-0002 (rev 3), ADR-0016, ADR-0017 (rev 2), ADR-0020, ADR-0022, ADR-0027, ADR-0028

## Context and problem

Fixed elsewhere:

| Source | Fixed |
|---|---|
| ADR-0015 d1 | The cultivation profile is the single mutation interface. No remote-command API, no real-time tuning channel, no override outside profile updates |
| ADR-0015 d8, d10 | The gateway runs the control loops and holds a conservative fallback profile |
| ADR-0016 d1 | During survey there is no active control; actuators are operator-controlled |
| ADR-0016 d2, d6 | Identification runs offline; the identified model reaches the cabinet in the profile |
| ADR-0018 d10 | The over-temperature trip is at the heating actuator, independent of MCU, gateway and cloud |
| ADR-0020 | The gateway owns local persistence |
| ADR-0022 d9 | The ERP takes operator decisions, not machine observations |
| ADR-0028 d1, d3, d9 | Commissioning is an ordered sequence; the instance `-CC` is the authoritative trim record; the commissioning tool is an operator tool |

Not fixed:

- whether an operator may be given a local, on-site interface at all, and what it may do;
- how such an interface relates to the profile as the single mutation channel;
- what a service technician may run, and where a result goes;
- what carries a procedure to the person performing it.

The `-M-` protocol documents in the store already say how a bring-up or a
calibration is performed, and the per-instance `QP` / `CP` / `CC` records already
say what a given performance of one produced. Nothing connects them: the
procedure is read off a screen elsewhere and the result is typed in afterwards.

An interface that is not decided is decided by whoever builds one. The failure
this record exists to prevent is a local device that accumulates authority —
holding telemetry nobody reconciles, applying changes nobody recorded, or
becoming the only way to perform an operation, so that the cabinet quietly
depends on an accessory.

## Decision drivers

| # | Driver |
|---|---|
| P1 | The cabinet must operate, control and record identically with the interface absent. It is an accessory, never equipment |
| P2 | Every fact has one authoritative home (ADR-0000 d3). A local interface must not become a second one |
| P3 | The profile stays the single mutation channel for control behaviour (ADR-0015 d1) |
| P4 | Series production requires reproducibility: two technicians following one procedure must produce comparable records |
| P5 | Safety must not depend on an optional device |

## Decision

1. **A local operator and service interface is an optional accessory.** The cabinet
   operates, controls and records identically without it. Removing it changes how an
   operator talks to the cabinet, never what the cabinet does.

2. **It holds no system of record.** No telemetry store, no configuration master, no
   calibration authority. Persistence is the gateway's (ADR-0020); records are the
   ERP's (ADR-0021, ADR-0022).

3. **It is a bus observer by default.** It joins the cabinet bus in listen-only mode,
   publishes no heartbeat and no diagnostics, and nothing on the bus may subscribe to
   it. It takes a Node-ID from the ADR-0027 allocation when it must issue requests, and
   allocates none.

4. **It may command only what is commandable without it.** Every action it offers must
   be equally performable from the gateway or an operator tool. No capability exists
   only here.

5. **It originates operator intent, never system input.** A value it computes may be
   displayed but may not become an input the system consumes, because that input would
   be unreproducible without the accessory. A value a human enters may be carried to the
   component that owns it, through that component's own sanctioned channel. The
   interface is an input *device*, never an input *source*.

6. **Excitation is not a setpoint.** Driving an actuator to observe the plant's response
   is permitted only while the control loops are not in control of that actuator
   (ADR-0016 d1). Changing a target the loops are pursuing is a change of control
   behaviour and remains profile-only (ADR-0015 d1). The two are different operations
   and are not to be merged into one control.

7. **An excitation must not outlive the tool that requested it.** The requesting
   interface may be unplugged at any moment (P1), so it cannot be what ends a test. Every
   excitation is bounded by the component that performs it, which returns to the fallback
   profile (ADR-0015 d10) when the bound expires or the requester goes silent. By P5 no
   interlock is reachable from here; the over-temperature trip stays as ADR-0018 d10
   places it.

8. **It executes procedures it did not author.** A `-M-` protocol is carried to the
   interface and performed step by step, each step's precondition being the previous
   step's pass (as ADR-0028 d1 orders commissioning). The interface does not compose,
   edit or improvise a procedure, because by P4 the comparability of two performances
   rests on them being the same procedure.

9. **A performance produces a record it does not keep.** Executing a protocol yields the
   per-instance `QP` / `CP` / `CC`, filed through the existing path; the filed record is
   authoritative (ADR-0028 d3). Within a procedure step the interface measures and
   proposes; the record determines. This is the reading of ADR-0028 d9 that this decision
   adopts.

10. **A gated engineering mode may expose service functions, and creates none.** It offers
    commissioning (ADR-0028) and diagnostics already available elsewhere, subject to
    decision 4. Every action it takes is recorded wherever the normal path records it; an
    action that cannot be recorded is not offered. The gate is an accident guard, not an
    access control — anyone at the interface is already inside the trusted CAN domain
    (ADR-0002 d7) and the record must not claim otherwise. The mode is visibly indicated
    while active and lapses on its own.

11. **Removable media is a courier, never a record.** It may carry a signed firmware
    image (ADR-0029), a signed profile (ADR-0025), a procedure, or an operator-initiated
    export. It may not carry a telemetry log, which by P2 would be a copy of what the
    gateway and the ERP already own, with no rule saying which is right.

12. **The realization is not fixed here.** These constraints bind the role, not a device.
    An installed panel and a gateway-served local page are both permitted realizations,
    as is a portable service instrument, which ADR-0016 d9 already packages with a survey
    kit rather than with a cabinet. Part selection, display size and interfaces belong to
    the document whose job is that value (ADR-0000 d2), not to this record.

## Alternatives considered

**A. A panel-specific record.** *Rejected:* it forecloses the cheaper realization — a page
served by the gateway — and invites a second ADR for it. The constraints are identical;
only the hardware differs, and hardware is not this record's subject.

**B. The interface runs control loops when the gateway is absent.** *Rejected:* ADR-0015 d8
gives the loops to the gateway. Two controllers on one cabinet contend, and the one that
can be unplugged must not be one of them.

**C. Local telemetry logging.** *Rejected:* P2. A third copy alongside the gateway buffer
and the ERP record, with no retention rule and no reconciliation. Removable media also
carries it out of the cabinet, around the boundary ADR-0002 d7 places at the gateway.

**D. The interface as an ERP client.** *Rejected:* ADR-0022 d9, and it duplicates the
gateway identity that ADR-0007 rev 2 d11 makes the site credential.

**E. A local setpoint channel for a running cultivation.** *Rejected:* ADR-0015 d1
directly. Decision 6 grants the tested need — excitation while the loops are not in
control — without opening the channel d1 closes.

**F. An engineering mode with unrecorded full access.** *Rejected:* P4. A fleet in which
any unit may have been silently altered during a service visit is not reproducible, and an
unrecorded change is the silent override ADR-0000 d4 forbids, expressed as a menu.

## Consequences

- An operator may be given local visibility and local manual operation without the
  cabinet acquiring a dependency, which makes the interface a purchasable option rather
  than a line on every BOM.
- The `-M-` protocols gain an executor, and the `QP` / `CP` / `CC` records gain a
  producer, without either end changing owner.
- Decision 7 obliges the actuator command surface to carry a bound and a fallback. That
  surface does not yet exist; the requirement lands on whichever record defines it.
- Decision 5 forbids a class of otherwise attractive features: anything where the
  interface's own computation would feed the system. Such work belongs to the gateway or
  to identification (ADR-0016 d2).
- Nothing here is validated on hardware. This record fixes a role, and by ADR-0000 d7
  acceptance records agreement, not implementation.

## Deferred decisions

- The actuator command surface that decision 7 constrains — the bound, the fallback
  trigger and the liveness condition — belongs with the environmental-actuator record,
  not here.
- Whether a procedure carried to the interface is signed, and against which trust root,
  follows ADR-0025 rather than being settled here.
- Which realization is built first, and its part selection.

## References

- ADR-0015 — profile as the single mutation interface; gateway control loops; fallback profile
- ADR-0016 — survey phase and operator-controlled actuators; offline identification; survey kits
- ADR-0018 — the hardware over-temperature trip at the heating actuator
- ADR-0020 — gateway persistence
- ADR-0022 — what the ERP takes
- ADR-0028 — commissioning sequence; the instance `-CC`; the commissioning tool
