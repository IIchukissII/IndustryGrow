<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0004 (rev 1): Gateway host hardening, firmware signing, and stateless-edge operation

- **ID:** ADR-0004 (rev 1)
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3)
- **Supersedes:** ADR-0004 (initial draft, same date)

## Revision history

- **rev 1 (2026-05-16)** — Reframed the gateway as a stateless edge. The initial draft's local hash-chained audit log (decisions 8–11) is removed in favour of an in-memory ring buffer plus an IndustryFlow-side audit trail; the firmware-signing decisions (12–16) are preserved with one adjustment. Rationale: the local-tamper threat model is unrealistic (a compromised gateway already streams false data live) and continuous SD-card logging is a write-amplification cost. See decisions 8–11 and alternative A.

## Context and problem

ADR-0002 fixed the gateway (Raspberry Pi 3B+ / 4 / 5 in the reference configuration) as the security boundary between the trusted CAN domain inside the cabinet and the external network leading to IndustryFlow. ADR-0001 expanded the project's scope to a fleet model — community-self-hosted and commercial-managed deployments coexisting under one architecture.

This ADR covers the **operational security disciplines** that apply to every gateway in IndustryGrow, regardless of deployment model. It deliberately excludes the PKI architecture — certificate hierarchies, hardware identity bindings, provisioning workflows, revocation — which is decided in ADR-0007. The two ADRs are companions: ADR-0007 covers *who the gateway is*, this ADR covers *how the gateway behaves once it is who it says it is*.

The initial draft of this ADR proposed a substantial local audit-logging system on the gateway: hash-chained Merkle log of every CAN frame and every IndustryFlow upload, 30–90 day local retention, structured JSONL format, log rotation. On review this was over-engineered for the actual threat model and operational realities:

- **Gateway is a bridge, not a store.** Data arrives on CAN, leaves to IndustryFlow. IndustryFlow is the durable history of record. Local logging duplicates what is already centrally persisted.
- **Write-heavy storage on SD card is a recurring operational problem.** Continuous logging of every CAN frame chews through SD card endurance, requiring industrial-grade cards and periodic replacement. This is a poor operational story for unattended gateways.
- **The threat model the local hash-chain defends against is not realistic.** The defended scenario is: gateway is compromised, attacker rewrites past frames to hide what happened. But a compromised gateway is *already streaming false data to IndustryFlow in real time*; rewriting local history adds no power because IndustryFlow saw the originals as they happened. Tamper-evidence belongs on the IndustryFlow side of the upload, not on the gateway side of capture.
- **Forensic responsibility belongs where the durable record lives.** IndustryFlow is the platform; it has indexing, query, correlation, retention policy, immutable storage primitives. Audit-trail on IndustryFlow with signed batch uploads from the gateway is the architecturally correct location.

This revision redefines the gateway as a **stateless edge**: a thin, durable, replaceable mostly-stateless bridge whose only persistent state is its identity (certificate, configuration) and whose runtime state lives in RAM and is recoverable at any moment from the platform side. Decisions 8–11 from the initial draft are removed; firmware-signing decisions (12–16) are preserved with one adjustment.

The threat model otherwise stays:

- The apartment LAN (or commercial site LAN) is hostile-by-default beyond a defined set of devices.
- A compromised gateway can cause real-world harm (overdosing nutrients, overheating the cabinet, faking telemetry to mask issues).
- Physical access to the cabinet implies legitimate operator presence (no separate physical security model for the inside).
- The gateway runs firmware-update operations for CAN nodes; that pathway must be cryptographically integral.

## Decision drivers

- **Gateway as stateless edge.** Minimize persistent local state. Anything that doesn't *have* to live on the gateway lives in IndustryFlow.
- **Replaceability.** A gateway unit should be replaceable in minutes by reconnecting wires and provisioning a new identity. No accumulated local state should be lost.
- **SD-card friendly.** Write-amplification on gateway storage should be minimal. No write-heavy services. Industrial SD cards remain a *nice-to-have*, not a *requirement*.
- **Authentication to IndustryFlow is non-negotiable.** Mechanism (mTLS) and supporting PKI in ADR-0007.
- **Compromise containment.** One gateway compromise must not enable compromise of other gateways or of IndustryFlow.
- **Forensic capability.** When operational anomalies occur (a dosing pump that delivered the wrong volume, a heater that didn't shut off), reconstructing what happened must be possible — but reconstruction is a platform-side activity, not a gateway-side activity.
- **Firmware-update integrity.** Firmware flowing through the gateway to CAN nodes must be cryptographically signed end-to-end. This is the highest-power operation in the system.

## Decision

### Authentication boundary

1. **The gateway authenticates to IndustryFlow via mTLS.** Specifics of the PKI — CA hierarchy, certificate issuance, hardware identity binding via ATECC608, provisioning workflows — are decided in ADR-0007. This ADR assumes that mechanism is in place.

### Gateway host hardening

2. **SSH disabled by default.** Re-enabled per-operation when needed, key-only when enabled, root login forbidden permanently. `PasswordAuthentication` off in `sshd_config`.

3. **fail2ban** configured with strict thresholds on SSH and on any exposed services (if any are introduced later).

4. **Unattended security updates** via `unattended-upgrades`. Automatic install of security patches, daily reboot window scheduled during photoperiod-off hours.

5. **Firewall: outbound to IndustryFlow only.** No inbound TCP/UDP except SSH on the internal apartment/site interface. Outbound restricted to IndustryFlow's endpoints via iptables or nftables. No general internet access from the gateway.

6. **No service exposes data on the apartment/site LAN.** The gateway is not a server to the LAN. Local debug interfaces (if introduced) bind to `127.0.0.1` only and are reached via SSH port-forwarding when needed.

7. **Minimum-privilege service users.** The gateway service (Pycyphal-based) runs as a dedicated unprivileged user with access only to the CAN interfaces and its configuration directory. No root operations during normal runtime.

### Gateway as stateless edge — runtime state policy

8. **No persistent telemetry log on the gateway.** CAN frames are decoded by Pycyphal, packaged, and forwarded to IndustryFlow in real time. There is no local archive of raw frames or upload history beyond what is necessary for resilience (see decision 9).

9. **In-memory ring buffer for transient network resilience.** When the network connection to IndustryFlow is interrupted, the gateway buffers decoded data in a bounded in-memory ring buffer (default cap: 100 MB, configurable). Buffer is *not* persisted across gateway reboots: if the gateway restarts during a network outage, buffered data is lost. The volume of data lost in this scenario is bounded and IndustryFlow records a gap, which is more useful than the operational complexity of persistent crash-safe local storage. On gateway reboot, IndustryFlow's last-seen timestamp for this gateway communicates the gap explicitly.

10. **Audit trail lives in IndustryFlow, not on the gateway.** Each batch of decoded data uploaded by the gateway includes:
    - Gateway identity (from ATECC608-bound certificate per ADR-0007)
    - Batch sequence number (monotonic per gateway, persisted across reboots in a tiny config file)
    - SHA-256 hash of the batch contents
    - The previous batch's hash (forming a per-gateway hash chain on the IndustryFlow side)

    IndustryFlow stores this chain in its immutable audit log. Tamper-evidence is enforced platform-side, where the long-term storage and forensic tooling live. The only gateway-side responsibility is producing the sequence number and hash; this is microscopic write load.

11. **Standard Linux system logs only.** `systemd-journald` with size-limited persistent storage (e.g., `SystemMaxUse=100M`) captures normal Linux system events (kernel, services, security). This is operator-debugging infrastructure, not forensic infrastructure. No custom logging framework, no JSONL pipeline, no rotation tooling beyond what journald provides.

### Firmware update path for CAN nodes

12. **CAN node firmware is signed at build time** by a dedicated firmware-signing key, separate from any gateway-identity key. The signing key is held offline and used only at release events.

13. **Distribution via Cyphal file transfer service.** The gateway holds firmware artifacts (current and one previous version per node type, on disk — these are infrequent writes) and serves them to nodes on request or push.

14. **Each node verifies the signature before flashing.** The public verification key is burned into the node bootloader at first flash. Subsequent updates require valid signature or are refused.

15. **The bootloader itself is signed by a stronger, rarely-rotated key.** Bootloader updates require physical access to the node's SWD pins (one-time provisioning), preventing remote bootloader substitution.

16. **Firmware update events are reported to IndustryFlow as discrete typed events** via the same upload pipeline as telemetry (decision 10). IndustryFlow's audit log captures: gateway identity, target Node-ID, firmware version, hash, signature verification result reported by the node, timestamp. This is the platform-side equivalent of the local audit-log entry that the initial draft of this ADR proposed.

### Documented trust assumption

17. **CAN domain inside the cabinet is trusted.** No per-node authentication, no payload encryption on CAN. Anyone with physical access to bus wires can spoof or inject. This is intentional and acceptable for the deployment context (physical access already implies operator presence). The assumption is documented here so the boundary is explicit and reviewers know what is in scope and what is not.

## Alternatives considered

**A. Local hash-chained audit log on the gateway (the initial draft of this ADR).** Tamper-evident local Merkle log of every CAN frame and IndustryFlow upload, 30–90 day retention, structured JSONL, daily rotation, anchoring chain head to IndustryFlow periodically. *Rejected on revision:* the threat model (compromised gateway rewriting past frames) is not realistic given that a compromised gateway is already streaming false data live; SD-card write amplification is a real operational cost; forensic responsibility properly lives on the platform side where the long-term audit trail is queryable and tamper-evidence has appropriate primitives. Decisions 8–11 of the initial draft are replaced by stateless-edge decisions 8–11 above and IndustryFlow-side audit-trail in decision 10.

**B. SSH always-on with key auth.** Industry-default convenience. *Rejected:* SSH disabled by default is a small operational cost (re-enable when needed) for a meaningful attack-surface reduction.

**C. Inbound services exposed on the LAN (debug dashboard, local Grafana, etc.).** *Rejected:* turns the gateway into a server, increases attack surface and operational state. Local-only binding plus SSH port-forwarding achieves the same operator experience without persistent server state.

**D. Persistent crash-safe local buffer (SQLite).** Survives gateway reboots so no data is lost on power-cycle during a network outage. *Rejected on revision:* added complexity (crash-safe ordering, fsync overhead, SD wear) for a small gain — the loss-window is bounded by gateway uptime during the outage, and IndustryFlow records the gap explicitly. The volume of data lost is acceptable and the operational simplification is significant.

**E. Unsigned firmware updates.** Trust the gateway to push only correct firmware. *Rejected:* gateway compromise becomes full-node-fleet compromise. Signing isolates the trust boundary at the build-time signing key, which can live entirely offline.

**F. Per-node payload encryption on CAN.** *Rejected:* bandwidth cost is prohibitive on classic CAN (signature consumes most of the 8-byte payload). Complexity disproportionate to the physical-access threat model. Documented in decision 17 as a deliberate trust assumption.

## Consequences

### Positive

- **Standard Linux hardening posture.** Operators familiar with Linux server administration recognize these practices.
- **Gateway becomes truly replaceable.** Drop-in replacement: connect cables, restore identity from backup or re-provision, done. No accumulated local state to migrate.
- **SD card / boot medium is no longer write-heavy.** Inexpensive consumer SD cards last for years. Industrial SD is a *nice-to-have* (better thermal range, ECC), not a *requirement* for service life.
- **Minimum-spec gateway hardware works.** A Raspberry Pi 3B+ with 1 GB RAM is adequate for the apartment-scale gateway. Pi 4 and Pi 5 are appropriate for higher-traffic or commercial deployments. The constraint moved from "what compute can sustain log retention" to "what compute can sustain real-time Pycyphal decoding and mTLS upload" — much lower.
- **Audit trail with cryptographic integrity exists**, but on the platform side, where it can be queried, correlated across deployments, and retained for the operationally meaningful timeframe.
- **Firmware integrity end-to-end** from build signing through node verification. Gateway compromise does not propagate to node firmware.
- **Trust assumptions are explicit and documented.**

### Negative

- **Network outage at gateway reboot loses in-flight buffered data.** This is the operational cost of dropping persistent local buffer. Mitigation: gateway reboots are rare; gateway power is uninterruptible at the LAN side; IndustryFlow records the gap so it's not silent data loss, it's known data loss. The bounded volume is acceptable for a sensor-data platform (where the *next* sample is moments away).
- **IndustryFlow-side audit log infrastructure must exist and be reliable.** This is no longer a gateway-side problem but a platform-side one. The platform must implement immutable audit-trail storage, per-gateway hash-chain validation, and forensic query tooling. This is *expected* of an industrial-IoT platform — IndustryFlow already has the building blocks (immutable event log, query API) — but it must be explicit in the platform roadmap.
- **Firmware signing infrastructure** (build-time key management, bootloader hashing scheme, key rotation) is real engineering work. Reserve a focused sprint before first commercial deployment.
- **SSH-disabled-by-default adds operational friction for self-builders.** Documentation must clearly explain how to enable SSH temporarily for troubleshooting.

## Deferred decisions

- **IndustryFlow-side audit-trail schema.** What exactly the platform stores per upload batch (gateway identity, sequence number, content hash, prev-hash, signature, timestamp); how it indexes for query; how chain validation is performed at query time. Touches IndustryFlow platform roadmap, not this ADR.
- **Firmware signing key rotation schedule and ceremony.** How often, who participates, how the previous key is archived. Operational specification.
- **Incident response playbook.** Separate runbook, not an ADR.
- **What happens on detected hash-chain break.** Alert? Quarantine? Auto-rollback? Likely "alert and require human confirmation to continue", but decide explicitly when implementing the IndustryFlow audit-trail.
- **Gateway state recovery on replacement.** Procedure for re-provisioning a replacement gateway: cert renewal, configuration restore from IndustryFlow, sequence-number continuation. Operational spec.

## References

- ADR-0001: IndustryGrow framing.
- ADR-0002 (rev 3): Field bus architecture (placed the security boundary at the gateway; updated gateway hardware minimum to Pi 3B+).
- ADR-0007: PKI architecture — gateway identity, certificate management, provisioning workflows.
- Cyphal file transfer service specification (firmware distribution).
- Linux server hardening guides (CIS benchmarks for Debian/Raspberry Pi OS).
- systemd-journald configuration documentation.
