<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0015 — Gateway profile caching and local control loops

- **Status:** Accepted
- **Date:** accepted 2026-05-16; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0003 (reference profile content), ADR-0004 (outbound-only firewall decision 5 relies on), ADR-0016 (state estimator and model parameters carried by decisions 6 and 8), ADR-0018 (location of the over-temperature trip of decision 11), ADR-0020 (bounded operational state beside decision 4's configuration state), ADR-0022 (the ERP endpoint decision 5 pulls from), ADR-0025 (the signature scheme decision 7 requires), ADR-0031 (demand form and per-element safe output decision 18 relies on)

## Context

A cultivation profile states a crop's operating parameters (ADR-0001, ADR-0003). Control can be computed in the cloud and sent to the cabinet as commands, or computed on the gateway against a local copy of the profile.

A cabinet's climate, light and dosing loops cannot wait on a cloud round-trip or stop during a multi-hour outage. A cloud that can command actuators turns a compromised cloud account into a physical hazard (over-heating, over-dosing); a cloud that can only publish profiles can at worst publish a bad profile, which is signed, versioned and reversible. Every change to cabinet behaviour is auditable and reversible only if it travels one path.

Hardware survival interlocks sit below all software and are not part of control.

## Decision

1. **The cultivation profile is the single mutation interface.** Every change to gateway control behaviour is a new profile version; there is no remote-command API, no real-time tuning channel and no override outside a profile update. One path gives one audit trail, one rollback story and one signature check.
2. **A profile is a JSON document.** It is small and human-readable; its schema is the deferred profile-schema ADR's (ADR-0009) and its reference content is ADR-0003's.
3. **Profile versions are monotonic per cabinet.** A new version is a new immutable document with a new version identifier; earlier versions are retained by the profile store for rollback and audit.
4. **The gateway holds the active profile as persistent local configuration state.** Control reads it from local storage, so control resumes after a reboot without the network. What else the gateway may persist is ADR-0020 d5's enumeration.
5. **The gateway pulls the profile; nothing pushes it.** The gateway polls the profile store periodically (ADR-0022 d8), downloads a new version, verifies it (decision 7) and installs it. Pull needs no inbound port on the gateway (ADR-0004 d5).
6. **A profile switch is atomic.** The new version replaces the active one in a single atomic step, so the control loop never observes a partially written profile and no profile is ever partially applied.
7. **A profile is verified before it is applied.** A profile whose signature does not verify is rejected and logged, and the previous version stays active. The scheme, and the key the gateway checks against, are ADR-0025's.
8. **The gateway runs rule-based control loops locally.** Each loop reads telemetry from the bus and setpoints from the active profile, computes an output (PID, schedule lookup, setpoint dosing) and issues a demand to the actuator node. A loop is a function of current telemetry, active profile and time, and holds no state across a reboot.
9. **Advanced control modules produce profile versions, not commands.** Commercial modules (ADR-0001 d4: MPC, predictive ML, adaptive optimisation) run platform-side on telemetry history and emit new profile versions. A human edit and a machine-generated optimisation take the same mutation path.
10. **The gateway carries a built-in, read-only fallback profile.** It is loaded when the active profile is missing, unreadable or fails verification with no previous version to keep, and the event is logged. It holds survival setpoints only — moderate temperature, conservative photoperiod, wide EC/pH band, irrigation that prevents drying out.
11. **Hardware interlocks override every profile and control-loop output.** The over-temperature trip is hardware at the heating actuator, independent of MCU, gateway and cloud (ADR-0018 d10; ADR-0031 d7); M05 senses and switches nothing. Leak and door are report-only on M05, and the leak response is software-mediated — the gateway commands the pump off — because a nutrient leak develops over minutes to hours.
12. **The platform side is profile source and telemetry sink, never commander.** The ERP stores instance profile versions and serves them on pull (ADR-0021 d12; ADR-0022 d8); IndustryFlow accepts telemetry, runs history-based analytics and may emit profile versions through advanced-control modules (decision 9).
13. **No platform component exposes an actuator command API.** There is no "set actuator X to Y" or "irrigate now" endpoint, because it would be a second mutation channel bypassing profile versioning.
14. **No remote command channel to the gateway.** No remote execution, no SSH-as-API, no command-and-control side channel; a behaviour change goes through the profile.
15. **No temporary override outside the profile.** A temporary adjustment is a new profile version, and its reversal is another.
16. **No multi-cabinet coordination at gateway level.** Each gateway runs its own profile; cross-cabinet coordination, if needed, is expressed platform-side as coordinated profile versions.
17. **No machine learning on the gateway in phase 1.** Control is rule-based; ML runs platform-side and acts through profile generation (decision 9).
18. **Control is cascaded: the supervisory loop on the gateway, actuator-local conditioning on the node.** Decision 8's loop is the outer loop and emits a demand; an actuator left latched when the bus goes quiet is a hazard, and a bus-rate loop cannot emit a millisecond pulse train.
    - A node receives a normalised demand plus a validity deadline, not a switch state (ADR-0031 d4).
    - The node owns output conditioning — PWM, slew and rate limits, minimum on/off time, saturation — and any single-variable regulator on a state it measures itself (fan speed, pump flow).
    - When the deadline passes without a fresh demand the node drives its safe output (ADR-0031 d6); no gateway liveness signal is needed.
    - All cross-coupling stays in the outer loop, where the multi-sensor view and the model exist (ADR-0016); node regulators are single-variable.
    - No part of the profile reaches a node: outer-loop gains and model parameters are profile content, node conditioning parameters are actuator properties set at commissioning (ADR-0028).

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **B.** Profile for slow changes plus a real-time command API for tuning | Two mutation channels mean two audit trails, two rollback paths and two security models |
| 4 | **D.** Profile held in RAM only | After a reboot with the cloud unreachable the cabinet would run the fallback profile without cause |
| 5 | **C.** Server-initiated profile push | Requires inbound connections to the gateway, which ADR-0004 d5 forbids |
| 8 | **A.** Gateway as passive bridge; the cloud computes control and sends actuator commands | Control fails during outages, a cloud compromise becomes a physical compromise, and cloud reliability couples to cabinet reliability |
| 8 | **E.** Control loops in node firmware, each node holding the profile | Distributes the profile to every node (more state, sync and attack surface); a node sees only its own data, the gateway sees the whole system |
| 18 | Output conditioning computed in the gateway loop and sent as switch states | The bus-rate loop cannot produce the actuator's millisecond timescale, and a lost bus leaves the last switch state latched |
| 18 | Node fail-safe triggered by a gateway liveness signal | The deadline travelling with each demand makes staleness local to the node with no extra signal |

## Consequences

**Positive**

- The cabinet keeps growing on the last synced profile through a cloud or ERP outage; only new profiles stop arriving.
- Human edits, generated optimisations and community updates share one audit trail, one rollback path and one signature check.
- The cloud holds no command authority; the worst a compromise can do is push a signed, versioned, reversible profile.
- Control loops are functions of telemetry, profile and time, so a profile can be simulated and tested before release.
- Operating parameters (profile) and survival parameters (hardware) do not overlap.
- Each actuator node publishes its applied demand as actuator state (ADR-0031 d8), so control decisions reach the platform with telemetry.

**Negative**

- The gateway runs control software whose failures have physical effect and needs testing and operational care beyond a bridge.
- The profile schema is a signed contract, so changing it is a breaking event.
- One universal fallback profile may not suit every crop as the registry grows.
- An operator cannot stop the cabinet remotely except by publishing a corrective profile or intervening at the cabinet.

## Deferred

| Open question | Settled by |
|---|---|
| Profile schema, version-compatibility rules, registry mechanics, and whether community templates carry signatures | ADR-0009 (profile schema) |
| Whether outer-loop gains are profile content, platform defaults or per-cabinet calibration | First tuned outer loop |
| Alerting and trust refresh on extended profile-sync failure | Gateway operations, when implemented |
| A platform-side "revert to version N−1" operation; it must mint a new version (ADR-0025 d8) | ERP roadmap |

## References

- ADR-0001 — open core and advanced control modules (d4), community profiles (d5).
- ADR-0025 — instance-profile signing scheme.
- ADR-0031 — actuator classes, demand form, safe output per element.
- `gateway/profile_client.py` and `gateway/files/systemd/industrygrow-profile-pull.timer` — the pull, verify and install path and its interval.
- `gateway/control_model.py` — outer loop and node-side conditioning model.
