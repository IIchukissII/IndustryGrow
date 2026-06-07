<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0015: Gateway profile caching and local control loops

- **ID:** ADR-0015
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0003, ADR-0004 (rev 1)

## Context and problem

ADR-0001 framed IndustryGrow as an open-core platform where cultivation profiles describe the operating parameters for a crop. ADR-0003 specified the first reference profile (strawberry day-neutral). ADR-0004 rev 1 defined the gateway as a stateless edge — minimal persistent state, forensic trail on IndustryFlow.

What none of those ADRs explicitly answered is: **where do control decisions get made, and how do they relate to the profile?**

Two plausible architectures exist:

**Architecture A — gateway as passive bridge.** Gateway forwards telemetry to IndustryFlow, IndustryFlow runs control logic, IndustryFlow sends actuator commands back to gateway, gateway forwards them to actuator nodes. This makes the cloud the controller.

**Architecture B — gateway as autonomous edge controller, profile as cloud-to-edge contract.** Gateway holds a local copy of the active profile. Gateway runs control loops *locally* against that profile. IndustryFlow is the source of truth for the profile (storage, history, versioning), but does not send real-time actuator commands. New profile versions are pushed (or pulled), gateway atomically switches to them. Control happens at the edge.

The initial discussion implicitly leaned toward A, which is wrong for several reasons:

- **Network dependence for control is fragile.** A cabinet's climate, light, and dosing loops cannot wait for cloud round-trip latency or survive multi-hour internet outages. Plants do not pause when WiFi goes down.
- **Security surface is larger.** If the cloud can send actuator commands, then compromise of the cloud account leads directly to physical-system compromise (over-heating, over-dosing). With profile-only flow, the worst compromise can do is push a bad profile — which is atomic, signed, versioned, and reversible.
- **Operational coupling.** Architecture A ties the cloud's reliability to the cabinet's reliability. Architecture B decouples them: cloud can fail and the cabinet keeps growing.

This ADR commits to Architecture B and specifies the contract between gateway and IndustryFlow.

## Decision drivers

- **Plants do not wait.** Control must work during network outages.
- **Single mutation channel.** All changes to system behavior must flow through one well-defined path (profile versioning), so they are auditable, signed, reversible.
- **Cloud as observer + profile source, not commander.** The cloud's role is storage, history, ML/analytics, and profile distribution. Real-time control is local.
- **Safety is separate from control.** Hardware interlocks (an analog over-temperature trip at the heating actuator, ADR-0018 decision 10) override profile-derived setpoints. The profile cannot disable safety; the cloud cannot disable safety; even a corrupted control loop cannot disable safety.
- **Simplicity at the cloud-edge boundary.** No bidirectional real-time RPC, no streaming-command protocol, no "cloud sends, gateway acks". Just: profile version pulled by gateway; telemetry pushed by gateway.

## Decision

### Profile as cloud-to-edge contract

1. **Cultivation profile is the single mutation interface.** All changes to gateway control behavior flow through profile versioning. There is no remote-command API, no real-time tuning channel, no override mechanism outside of profile updates. If something needs to change about how the cabinet runs, the change is expressed as a new profile version.

2. **Profile format: JSON document.** Schema specified in ADR-0009 (deferred — profile schema), populated per ADR-0003 (strawberry day-neutral as reference instance). Profile is small (kilobytes), human-readable, version-controlled.

3. **Profile versioning is monotonic per cabinet.** Each cabinet has an associated profile and a version number. New version = new immutable JSON document with a new version identifier. Old versions are retained on IndustryFlow side for rollback and audit.

### Gateway-side profile handling

4. **Gateway holds the active profile in a local file** (e.g., `/etc/industrygrow/active-profile.json`). This file is read by the gateway's control loop on each iteration. The file is part of gateway configuration state (per ADR-0004 rev 1's stateless-edge principle, *configuration* state is allowed; *operational* state is not).

5. **Profile sync: pull from IndustryFlow.** Gateway polls IndustryFlow periodically (default: every 60 seconds, configurable) to check for new profile versions. When a new version is detected:
   - Gateway downloads the new profile document.
   - Verifies signature (per decision 7).
   - Writes to a temporary file.
   - Performs atomic `rename()` to replace the active profile file.
   - Logs the version transition.

   No push mechanism from IndustryFlow is required. Pull avoids opening inbound ports on the gateway.

6. **Atomic switch.** The control loop re-reads the profile file at the start of each iteration. Because `rename()` is atomic on POSIX filesystems, the loop never observes a half-written profile. There is no "partial application" of a profile update.

7. **Signature verification before application.** Each profile carries a digital signature. Gateway verifies the signature against a trusted public key (the platform's or the profile author's, per ADR-0001 decision 5 community-contributed model) before writing to the active-profile file. Profiles with invalid signatures are rejected, logged, and not applied — the previous version remains active.

### Control loops on gateway

8. **Gateway runs rule-based control loops locally.** For each functional subsystem with a corresponding actuator module (per future actuator taxonomy), the gateway runs a control loop:
   - Read current telemetry from sensor nodes (via Cyphal subjects, decoded into in-memory state by Pycyphal).
   - Read setpoints from the active profile.
   - Compute control output (e.g., PID for climate temperature, schedule lookup for photoperiod, setpoint-based dosing for EC/pH).
   - Issue actuator command via Cyphal to the appropriate actuator node.

   Control loops are pure functions of (current telemetry, active profile, current time). They have no persistent state across reboots — on restart, they pick up where they are based on current telemetry and current profile.

9. **Advanced control modules generate profile versions, not commands.** Per ADR-0001 decision 4, commercial advanced control modules (MPC, predictive ML, adaptive optimization) are not gateway-side software. They live on the IndustryFlow side, consume telemetry history from the platform's TimescaleDB, and produce **new profile versions** as their output. Their effect on the cabinet flows entirely through the profile mutation channel — same as a human operator manually editing a profile.

   This unifies "human edits profile" and "ML generates new profile" into a single mutation path, with single audit trail, single rollback story.

10. **Conservative fallback profile.** Gateway ships with a built-in, read-only fallback profile baked into the firmware image. If the active profile file is missing, corrupted, or fails signature verification, gateway loads the fallback profile and logs the event. The fallback profile contains minimal-safe setpoints: moderate temperature (no aggressive heating or cooling), conservative photoperiod, EC/pH within a wide safe range, irrigation cycle that prevents drying out but does not over-water. Fallback is not a *good* profile, just a *survival* profile.

### Hardware safety interlocks (carrying ADR-0014 forward)

11. **Hardware interlocks override any profile or control-loop output, and they live at the actuator — not on M05.** The over-temperature cutoff is implemented at the heating-actuator node: an analog thermistor/PT1000 on a lead in the grow volume → comparator → that actuator's relay-enable, co-located with the element it cuts. It trips if the grow-volume temperature exceeds a hard-coded threshold (e.g., 35 °C), independently of the profile, the control loop, the gateway, and the cloud. Even if all software is compromised or malfunctioning, the heater shuts off. M05 is sense-only: it hosts no trip, no comparator, and no relay-enable (ADR-0018); it reports temperatures but switches nothing.

    > **Refined by ADR-0018 decision 10:** the MCU/gateway/cloud-independent trip is an analog thermistor/PT1000 + comparator (not the I²C TMP117, which is reported-temperature only), and it is located **at the heating actuator**, not on M05. The interlock *principle* — a hardware-independent over-temperature cutoff — is unchanged; what is specified differently is both the sensing element and the trip's location (actuator, per the sense/switch separation).

    Leak and door are **report/alert-only** on M05 — no automatic cutoff. M05 senses a leak on a lead to the reservoir/pump zone and a door-open on a GPIO, and reports/alerts; it switches nothing. Because a nutrient leak is a slow, non-fire failure (minutes–hours, not seconds), its response is software-mediated — the gateway commands the pump off over Cyphal — and does not require a hardware-independent interlock.

    The profile defines *operating* parameters. Hardware interlocks define *survival* parameters. These two domains are deliberately non-overlapping.

### What IndustryFlow does (and does not) do

12. **IndustryFlow is profile-source and telemetry-sink.** Its responsibilities:
    - Store profile versions (current and historical).
    - Serve profile versions to gateways on pull request.
    - Accept telemetry uploads from gateways.
    - Run history-based analytics (anomaly detection, predictive ML, etc.).
    - Optionally generate new profile versions via advanced-control modules (commercial).

13. **IndustryFlow does NOT have an actuator command API.** Architecturally, the platform offers no endpoint of the form "set actuator X to value Y" or "trigger irrigation now". Such operations are not exposed because they would create an alternative mutation channel that bypasses the profile-versioning model. The platform can only push a new profile version; the gateway interprets the profile.

    This is a deliberate constraint, not an oversight. It keeps the architecture clean: one mutation channel, one audit story, one rollback path.

### Non-goals explicitly captured

14. **No remote command channel.** No "execute this command on gateway", no SSH-as-API, no command-and-control sidechannel. If a behavior change is needed, it goes through profile.

15. **No "temporary override" outside the profile.** If an operator wants to make a temporary adjustment, they edit the profile, push a new version, and (optionally) push another version later to revert. Profile versioning is the override mechanism.

16. **No multi-cabinet coordination at gateway level.** Each cabinet's gateway runs against its own profile independently. Cross-cabinet coordination (if ever needed) would be a platform-side concern that ultimately expresses itself as coordinated profile versions across cabinets.

17. **No ML on gateway in phase 1.** Rule-based control only. ML lives in platform-side advanced-control modules, which feed back via profile generation. On-edge ML may become relevant in future commercial gateway tiers (e.g., Allwinner T527 with NPU per ADR-0002 rev 3 alternative K) but is out of scope here.

## Alternatives considered

**A. Gateway as passive bridge, cloud as controller.** Gateway forwards telemetry to cloud, cloud computes control output, cloud sends actuator commands. *Rejected:* network-dependent control is fragile (plants die during outages); larger security surface (cloud compromise = physical compromise); operational coupling of cloud reliability to cabinet reliability.

**B. Remote command API with both profile and command channels.** Hybrid: profile for slow changes, real-time command API for tuning. *Rejected:* two mutation channels mean two audit stories, two rollback paths, two security models. Forcing all mutations through profile versioning unifies these and is cleaner.

**C. Profile push (server-initiated) instead of pull (gateway-initiated).** Cloud opens connection to gateway when new profile is available. *Rejected:* requires inbound connections to gateway, which conflicts with ADR-0004 rev 1 decision 5 (firewall: outbound only, no inbound TCP/UDP). Pull is firewall-friendly and security-cleaner.

**D. Profile cached in RAM only, no persistent file.** *Rejected:* on gateway reboot, gateway would need to re-fetch profile from cloud before resuming control. If cloud is unreachable at reboot, cabinet has no profile and goes into fallback unnecessarily. Persistent file lets the gateway resume on the last-known-good profile after reboot even if cloud is unreachable.

**E. Embed control loops in firmware on smart nodes, not on gateway.** Each Cyphal node runs its own control loop against its own copy of the profile. *Rejected:* requires distributing the profile to every node (more state, more sync logic, more attack surface). Smart nodes are intentionally simple — they publish telemetry and react to commands. Centralizing control on the gateway is a smaller surface and the gateway has both the compute and the full system view (multi-sensor reads inform a single climate decision; a sensor node sees only its own data).

## Consequences

### Positive

- **Cabinet survives cloud outages.** Plants continue to be grown according to the most recently synced profile. The cabinet only loses its ability to receive *new* profiles during an outage; existing operation continues unbroken.
- **Single mutation channel.** Everything that changes the cabinet's behavior flows through profile versioning: human edits, ML-generated optimizations, community-contributed updates. One audit trail, one rollback story, one signature verification path.
- **Clean security model.** Cloud has no command authority. Worst-case cloud compromise pushes bad profile, which is signed (must forge author signature) and versioned (rollback is trivial).
- **Architecturally testable.** Control loops are pure functions of (telemetry, profile, time). Easy to unit-test, easy to simulate ("what would happen with this profile under these telemetry conditions"), easy to validate in CI before pushing a new profile.
- **Hardware safety is unambiguous.** Profile defines operating parameters; hardware defines survival parameters. Non-overlapping responsibility, clear precedence.
- **Aligns with the IndustryFlow side as planned.** The platform's role becomes: storage, history, ML/analytics, profile distribution. This matches a centralized industrial-IoT platform's natural strengths.

### Negative

- **Gateway is no longer "just a bridge".** Control loops on gateway are real software with real consequences (an actuator command can scorch a plant or flood a reservoir). Gateway firmware needs proper testing, error handling, and operational care. This is more engineering than a pure bridge would require.
- **Gateway hardware minimum increases for full deployment.** Pi 3B+ remains adequate for phase 1 bring-up (single cabinet, rule-based loops). For commercial deployments with more loops or planned headroom, Pi 4 or Pi 5 becomes preferable. Updated in ADR-0002 rev 3 gateway specification — Pi 3B+ minimum for apartment, Pi 4/5 recommended for higher-traffic.
- **Profile schema becomes a critical contract.** Once profiles are signed and versioned, changing the schema is a breaking event. ADR-0009 (profile schema) needs careful design and versioning rules of its own.
- **Fallback profile is a maintenance item.** It lives in firmware and must be kept reasonable. As cultivar variety grows in the community registry, "one universal fallback" may not be appropriate; per-crop fallbacks may eventually be needed. Out of scope for now.
- **No remote operator-override is intentional but operationally constraining.** An operator who realizes "the cabinet is dosing wrong, I need to stop it NOW" cannot send a stop command via IndustryFlow — they must push a corrective profile, or physically intervene at the cabinet. For phase 1 (apartment-scale, operator close to cabinet), this is fine. For commercial managed deployments, this may justify additional design work — but the answer should be "make profile updates faster and reliable", not "add a command channel".

## Relationship to other ADRs

This ADR adds clarity to several existing decisions without changing them:

- **ADR-0001 decision 4** (advanced control modules) — these modules are now explicitly **profile generators**, not command emitters. No structural change to ADR-0001 needed, but the operating model is sharpened.
- **ADR-0004 rev 1** stateless-edge principle — profile cache is *configuration* state, which is permitted. *Operational* state (telemetry log) remains forbidden on gateway. The distinction is preserved.
- **ADR-0002 rev 3 decision 6** (gateway service) — clarified that the gateway service does more than decode+forward: it also runs control loops. Pi 3B+ remains the minimum; control-loop CPU load is small (PID + lookup tables) and fits in 1 GB RAM with comfortable margin.
- **ADR-0014 M05-SAFETY / ADR-0018** — the over-temperature hardware interlock remains the survival layer, independent of profile or control loops. It lives at the heating actuator (ADR-0018 decision 10), not on M05, which is sense-only. This ADR explicitly affirms the boundary.

## Deferred decisions

- **ADR-0009 — profile schema, contribution workflow, registry design.** Profile JSON schema, version compatibility rules, signature scheme, registry mechanics. Critical for community-contributed profiles per ADR-0001 decision 5.
- **Actuator-module taxonomy** — which Cyphal actuator nodes exist, what commands they accept, how PWM/setpoint flows work. Separate future ADR.
- **Control-loop tuning and PID gain storage** — are PID gains part of the profile, or part of platform-default tunings, or per-cabinet calibration? Likely part of the profile, but needs explicit decision.
- **What happens on profile-sync failure for extended period.** Gateway continues with last-known-good profile indefinitely? Triggers an alert? Triggers a refresh of CA trust? Operational concern, decide when implementing.
- **Profile rollback API on platform side.** Does IndustryFlow expose "revert this cabinet to profile version N-1" as a UI/API operation? Recommended yes, but specifics belong to platform roadmap.
- **Audit-trail of control decisions.** When the gateway issues an actuator command, is this captured in the telemetry stream? Recommended: yes, treat actuator commands as a publication channel on Cyphal that gets forwarded to IndustryFlow same as telemetry. Confirm in actuator-taxonomy ADR.

## References

- ADR-0001: IndustryGrow framing — advanced control modules.
- ADR-0002 (rev 3): Field bus architecture — gateway hardware and service.
- ADR-0003: Strawberry day-neutral cultivation profile — reference profile content.
- ADR-0004 (rev 1): Gateway host hardening — stateless-edge principle, distinction between configuration state (allowed) and operational state (not allowed).
- ADR-0014: Sensor node taxonomy — M05-SAFETY (sense-only); the over-temperature hardware interlock lives at the heating actuator per ADR-0018 decision 10.
- `industryflow-platform-dependencies.md` (planned; not yet in repo) — tracking document for IndustryFlow-side work needed to support this ADR (profile distribution API, signature scheme, etc.).
