<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0004 (rev 1): Gateway host hardening, firmware signing, and stateless-edge operation

- **ID:** ADR-0004 (rev 1)
- **Status:** Accepted
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3)
- **Supersedes:** ADR-0004 (initial draft, same date)

## Revision history

- **rev 1 (2026-05-16)** — Reframed the gateway as a stateless edge. The initial draft's local hash-chained audit log (decisions 8–11) is removed in favour of an in-memory ring buffer plus an IndustryFlow-side audit trail; the firmware-signing decisions (12–16) are preserved with one adjustment. Rationale: the local-tamper threat model is unrealistic (a compromised gateway already streams false data live) and continuous SD-card logging is a write-amplification cost. See decisions 8–11 and alternative A.
- **Amendments** — decision 3 (2026-08-20): fail2ban's strict thresholds gain an `ignoreip` for the management LAN.
- **Amendments** — decision 5 (2026-08-20): the SSH-facing interface is the *set* of LAN management NICs, not one.
- **Amendments** — decision 18 (2026-08-25): every decoded sample carries three provenance stamps; makes the latency between the tiers of decision 10 measurable.
- **Amendments** — decisions 19 and 20 (2026-08-25): the bring-up enforcement state of decisions 2, 4 and 5, and the operator ERP as a second egress destination; the service-user policy of decision 7 binds every unit.
- **Amendments** — decision 21 (2026-08-25): a validity condition on the acquisition-to-receipt latency of decision 18.

## Context and problem

ADR-0002 fixed the gateway (Raspberry Pi 3B+ / 4 / 5 in the reference configuration) as the security boundary between the trusted CAN domain inside the cabinet and the external network leading to IndustryFlow. ADR-0001 expanded the project's scope to a fleet model — community-self-hosted and commercial-managed deployments coexisting under one architecture.

This ADR covers the **operational security disciplines** that apply to every gateway in IndustryGrow, regardless of deployment model. It deliberately excludes the PKI architecture — certificate hierarchies, hardware identity bindings, provisioning workflows, revocation — which is decided in ADR-0007. The two ADRs are companions: ADR-0007 covers *who the gateway is*, this ADR covers *how the gateway behaves once it is who it says it is*.

The initial draft of this ADR proposed a substantial local audit-logging system on the gateway: hash-chained Merkle log of every CAN frame and every IndustryFlow upload, 30–90 day local retention, structured JSONL format, log rotation. That design is over-engineered for the threat model and the operational constraints:

- **The durable record is IndustryFlow's.** Data arrives on CAN and leaves to IndustryFlow. Local logging duplicates what is already persisted centrally.
- **Write-heavy storage on SD card consumes endurance.** Continuous logging of every CAN frame requires industrial-grade cards and periodic replacement, on hardware that runs unattended.
- **The threat model the local hash-chain defends against does not hold.** The defended scenario is a compromised gateway rewriting past frames to hide what happened. Such a gateway is already streaming false data to IndustryFlow in real time, and IndustryFlow holds the originals as received, so rewriting local history grants no further capability. Tamper-evidence therefore sits on the IndustryFlow side of the upload.
- **Forensic tooling sits with the durable record.** IndustryFlow provides indexing, query, correlation, retention policy and immutable storage. The audit trail is held there, fed by signed batch uploads from the gateway.

This revision redefines the gateway as a **stateless edge**: a replaceable bridge whose only persistent state is its identity (certificate, configuration) and whose runtime state lives in RAM, recoverable from the platform side. Decisions 8–11 from the initial draft are removed; firmware-signing decisions (12–16) are preserved with one adjustment.

The threat model otherwise stays:

- The apartment LAN (or commercial site LAN) is hostile-by-default beyond a defined set of devices.
- A compromised gateway can cause real-world harm (overdosing nutrients, overheating the cabinet, faking telemetry to mask issues).
- Physical access to the cabinet implies legitimate operator presence (no separate physical security model for the inside).
- The gateway runs firmware-update operations for CAN nodes; that pathway must be cryptographically integral.

## Decision drivers

- **Gateway as stateless edge.** Minimize persistent local state. State that need not live on the gateway lives in IndustryFlow.
- **Replaceability.** A gateway unit should be replaceable in minutes by reconnecting wires and provisioning a new identity. No accumulated local state should be lost.
- **Low write amplification on gateway storage.** No write-heavy services. Industrial SD cards are optional, not required.
- **Authentication to IndustryFlow is mandatory.** Mechanism (mTLS) and supporting PKI in ADR-0007.
- **Compromise containment.** One gateway compromise must not enable compromise of other gateways or of IndustryFlow.
- **Forensic capability.** When operational anomalies occur (a dosing pump that delivered the wrong volume, a heater that didn't shut off), reconstructing what happened must be possible; reconstruction is a platform-side activity.
- **Firmware-update integrity.** Firmware flowing through the gateway to CAN nodes must be cryptographically signed end-to-end. This is the highest-power operation in the system.

## Decision

### Authentication boundary

1. **The gateway authenticates to IndustryFlow via mTLS.** Specifics of the PKI — CA hierarchy, certificate issuance, hardware identity binding via ATECC608, provisioning workflows — are decided in ADR-0007. This ADR assumes that mechanism is in place.

### Gateway host hardening

2. **SSH disabled by default.** Re-enabled per-operation when needed, key-only when enabled, root login forbidden permanently. `PasswordAuthentication` off in `sshd_config`.

3. **fail2ban** configured with strict thresholds on SSH and on any exposed services (if any are introduced later).

> **Amendment (2026-08-20, bounded — ADR-0000 d9).** The strict thresholds do not apply to the management LAN. The ban list is persisted and re-applied at service start, and `bantime.increment` escalates ×2 to a one-week ceiling, so a few mistyped key attempts remove the operator's own workstation from the gateway for hours and a reboot does not clear it — the failure presents as an unreachable gateway — ICMP answering while every TCP port is silently dropped by a source-specific rule in a second `f2b-table`, with the ruleset under `inet industrygrow` reading as correct. The jail therefore renders an `ignoreip` covering loopback and the directly-attached subnets of decision 5's management NIC set, taken from the kernel's link routes at provision time and extendable via `IGROW_F2B_IGNOREIP`. Scope is unchanged for every other source: thresholds, escalation, and the jail itself are untouched. The exemption is acceptable only because SSH is key-only (decision 2) — against a key-only sshd, credential brute force is not the threat fail2ban is buying protection from, and log-noise suppression does not justify a self-lockout that is indistinguishable from dead hardware.

4. **Unattended security updates** via `unattended-upgrades`. Automatic install of security patches, daily reboot window scheduled during photoperiod-off hours.

5. **Firewall: outbound to IndustryFlow only.** No inbound TCP/UDP except SSH on the internal apartment/site interface. Outbound restricted to IndustryFlow's endpoints via iptables or nftables. No general internet access from the gateway.

> **Amendment (2026-08-20, bounded — ADR-0000 d9).** "The internal apartment/site interface" is the *set* of LAN-facing management NICs, not one baked interface. A reference Pi has several (Wi-Fi `wlan0`, USB/legacy Ethernet `eth0`, Pi 5 onboard `end0`); binding SSH to a single one — whichever carried the default route at provision time — locks the operator out when the box is later reached over a different NIC. The rendered ruleset therefore accepts SSH on `iifname { wlan0, eth0, end0 }`, always unioned with the live default-route interface. Scope is unchanged: still LAN-facing only (no uplink/WAN interface), still key-only (decision 2), and the CAN interfaces (`can*`/`vcan*`) are excluded. This resolves the singular/plural ambiguity in "interface"; it does not widen the boundary decision 6 draws.

6. **No service exposes data on the apartment/site LAN.** The gateway is not a server to the LAN. Local debug interfaces (if introduced) bind to `127.0.0.1` only and are reached via SSH port-forwarding when needed.

7. **Minimum-privilege service users.** The gateway service (Pycyphal-based) runs as a dedicated unprivileged user with access only to the CAN interfaces and its configuration directory. No root operations during normal runtime.

### Gateway as stateless edge — runtime state policy

8. **No persistent telemetry log on the gateway.** CAN frames are decoded by Pycyphal, packaged, and forwarded to IndustryFlow in real time. There is no local archive of raw frames or upload history beyond what is necessary for resilience (see decision 9).

9. **In-memory ring buffer for transient network resilience.** When the network connection to IndustryFlow is interrupted, the gateway buffers decoded data in a bounded in-memory ring buffer (default cap: 100 MB, configurable). Buffer is *not* persisted across gateway reboots: if the gateway restarts during a network outage, buffered data is lost. The volume of data lost in this scenario is bounded and IndustryFlow records a gap, which is more useful than the operational complexity of persistent crash-safe local storage. On gateway reboot, IndustryFlow's last-seen timestamp for this gateway communicates the gap explicitly.

> **Superseded in part by ADR-0020 (decisions 8–9, and alternative D — endurance axis only).** For a gateway that buffers telemetry or runs an empirical survey campaign, ADR-0020 permits a *bounded, best-effort* persistent store-and-forward buffer on an SSD/NVMe medium: the SD-card write-endurance ground that load-bore the RAM-only policy of decisions 8–9 is retired, while the threat-model ground is not. The scope is exactly decisions 8–9: **decision 10 (platform-side hash chain) is retained and relied upon by ADR-0020, decision 11 (system logs, below) is unaffected, and alternative A (local tamper-evident audit log) stays rejected.** Pre-cloud (roadmap stages 1–10) the local store is the primary durable sink; once IndustryFlow exists (stage 11+) it demotes to a buffer. See ADR-0020.

10. **Audit trail lives in IndustryFlow, not on the gateway.** Each batch of decoded data uploaded by the gateway includes:
    - Gateway identity (from ATECC608-bound certificate per ADR-0007)
    - Batch sequence number (monotonic per gateway, persisted across reboots in a tiny config file)
    - SHA-256 hash of the batch contents
    - The previous batch's hash (forming a per-gateway hash chain on the IndustryFlow side)

    IndustryFlow stores this chain in its immutable audit log. Tamper-evidence is enforced platform-side, where the long-term storage and forensic tooling live. The only gateway-side responsibility is producing the sequence number and hash.

11. **Standard Linux system logs only.** `systemd-journald` with size-limited persistent storage (e.g., `SystemMaxUse=100M`) captures normal Linux system events (kernel, services, security). This is operator-debugging infrastructure, not forensic infrastructure. No custom logging framework, no JSONL pipeline, no rotation tooling beyond what journald provides.

### Firmware update path for CAN nodes

12. **CAN node firmware is signed at build time** by a dedicated firmware-signing key, separate from any gateway-identity key. The signing key is held offline and used only at release events.

13. **Distribution via Cyphal file transfer service.** The gateway holds firmware artifacts (current and one previous version per node type, on disk — these are infrequent writes) and serves them to nodes on request or push.

14. **Each node verifies the signature before flashing.** The public verification key is burned into the node bootloader at first flash. Subsequent updates require valid signature or are refused.

15. **The bootloader itself is signed by a stronger, rarely-rotated key.** Bootloader updates require physical access to the node's SWD pins (one-time provisioning), preventing remote bootloader substitution.

16. **Firmware update events are reported to IndustryFlow as discrete typed events** via the same upload pipeline as telemetry (decision 10). IndustryFlow's audit log captures: gateway identity, target Node-ID, firmware version, hash, signature verification result reported by the node, timestamp. This is the platform-side equivalent of the local audit-log entry that the initial draft of this ADR proposed.

### Documented trust assumption

17. **CAN domain inside the cabinet is trusted.** No per-node authentication, no payload encryption on CAN. Anyone with physical access to bus wires can spoof or inject. This is intentional and acceptable for the deployment context: physical access already implies operator presence.

### Provenance of a decoded sample

18. **A decoded sample carries three provenance stamps, and only the first is on the wire** *(added 2026-08-25)*. Decision 10 fixes what a *batch* carries; this fixes what a *sample* inside it carries.

    | Stamp | Written by | Clock | Origin |
    |---|---|---|---|
    | `t_acq` | the node | bus time base (ADR-0002 decision 11) | `uavcan.time.SynchronizedTimestamp`, on the wire |
    | `t_rx` | the gateway | host `CLOCK_REALTIME`, plus `CLOCK_MONOTONIC` | frame decode |
    | `t_store` | each tier that retains it | that tier's own clock | local buffer write, then platform accept |

    Three rules keep the set honest:

    - **`t_acq = 0` propagates as 0.** An unsynchronized node reports `UNKNOWN`, and no tier may substitute `t_rx` for it. Silent substitution makes `t_rx − t_acq` read zero forever, which turns the one anomaly signal this decision exists to produce into a constant.
    - **Realtime and monotonic are both retained for `t_rx`.** A time step on the gateway host moves `CLOCK_REALTIME` and manufactures a latency spike that never happened; durations are computed from the monotonic reading and only reported against the realtime one.
    - **`t_store` is per tier and may be absent.** On a deployment with no local persistent buffer (decisions 8–9; ADR-0020 decision 10) there is no local durable write to stamp, and the local stamp is absent rather than zero.

    `t_rx` and `t_store` are **gateway and platform record fields, not DSDL**. No project type is minted for them: ADR-0005 decision 2 reuses over minting, and a node cannot know either value. What the deltas are worth — bus latency, gateway write cost, backhaul latency and its behaviour while a buffer drains — is monitoring, which decision 10 already places platform-side.

    Bounded: `t_rx − t_acq` is meaningful only to the accuracy ADR-0002 decision 11 claims for the time base, and near zero it goes negative on sync noise. A consumer treats a small negative delta as noise, not as a fault.

### Enforcement state of the host-hardening decisions

19. **Decisions 2, 4 and 5 state the production posture; each is unenforced at bring-up, and the condition that turns it on is named here** *(added 2026-08-25)*. Each decision stands as written. What this records is *when* it binds, which none of them says, and which the provisioning material had been carrying in comments — the silent-override pattern decision 4 of ADR-0000 forbids.

    | Decision | Production posture | State until then | Condition |
    |---|---|---|---|
    | 2 | sshd disabled, enabled per operation | sshd enabled, key-only, root login refused | bring-up no longer runs over SSH — provisioning itself is carried on it, so the automation never disables it |
    | 4 | reboot window in photoperiod-off hours | automatic reboot disabled | a cultivation profile defines the photoperiod (ADR-0015) |
    | 5 | egress restricted to the IndustryFlow endpoints | egress policy `accept` | IndustryFlow exists (roadmap stage 11); provisioning needs apt, pip, DNS and NTP before it does |

    The key-only and no-root halves of decision 2 are enforced now; only the disabled-by-default half waits. Decision 5's inbound half — no inbound except SSH on the management NIC set — is enforced now; only the outbound half waits.

    **Decision 5's outbound target is IndustryFlow *and* the operator ERP.** The gateway pulls its active cultivation profile from an operator-hosted ERP over mTLS (ADR-0015 decision 5, ADR-0022 decisions 2 and 8), which decision 5 predates and does not name. Both are named endpoints and "no general internet access" is unchanged; there are two, not one.

20. **The service-user policy of decision 7 binds every unit on the gateway, not one** *(added 2026-08-25)*. Decision 7 names "the gateway service (Pycyphal-based)" because that was the only one when it was written. Three units now run under the same unprivileged `gateway` user — the Cyphal edge, the profile pull (ADR-0015 decision 5), and the time master (ADR-0002 decision 11) — and every unit added later inherits the same posture: dedicated unprivileged user, no root at runtime, and no write access to `/etc/industrygrow` except for the single short-lived unit whose job is to write it.

### Validity of the acquisition-to-receipt latency

21. **`t_rx - t_acq` is valid only while the gateway host clock is disciplined and settled; otherwise the sample carries no latency value** *(added 2026-08-25)*. Decision 18 computes durations from the monotonic clock. That covers a delta whose endpoints are both on the gateway — `t_store` against `t_rx` — and cannot cover this one: `t_acq` is written on the node and `t_rx` on the gateway, so the delta spans two machines and has no monotonic form.

    A node re-derives its offset from the gateway's clock at most once every two publication periods (ADR-0002 decision 11). A correction to that clock reaches the two ends of the subtraction at different times, and the difference appears as latency that was never incurred. Clock discipline after a host reboot is the ordinary case, not an exceptional one.

    A consumer records the latency as **absent** — not zero, not clamped — when either holds:

    - the host clock is not synchronized, or
    - it has been stepped within the settling window: two node publication periods plus the disciplining interval.

    Absent matches how decision 18 treats a missing `t_store`. `t_acq` and `t_rx` are both still stored, so only the derived value is withheld.

    Bounded: a validity condition on decision 18, with no change to any stamp or field.

## Alternatives considered

**A. Local hash-chained audit log on the gateway (the initial draft of this ADR).** Tamper-evident local Merkle log of every CAN frame and IndustryFlow upload, 30–90 day retention, structured JSONL, daily rotation, anchoring chain head to IndustryFlow periodically. *Rejected on revision:* the threat model (compromised gateway rewriting past frames) is not realistic given that a compromised gateway is already streaming false data live; SD-card write amplification is a real operational cost; forensic responsibility properly lives on the platform side where the long-term audit trail is queryable and tamper-evidence has appropriate primitives. Decisions 8–11 of the initial draft are replaced by stateless-edge decisions 8–11 above and IndustryFlow-side audit-trail in decision 10. (ADR-0020 later re-examined this alternative when an SSD/NVMe medium removed the endurance objection, and **keeps it rejected** — the threat model, not storage endurance, was the basis for rejection.)

**B. SSH always-on with key auth.** Industry-default convenience. *Rejected:* SSH disabled by default is a small operational cost (re-enable when needed) for a meaningful attack-surface reduction.

**C. Inbound services exposed on the LAN (debug dashboard, local Grafana, etc.).** *Rejected:* turns the gateway into a server, increases attack surface and operational state. Local-only binding plus SSH port-forwarding achieves the same operator experience without persistent server state.

**D. Persistent crash-safe local buffer (SQLite).** Survives gateway reboots so no data is lost on power-cycle during a network outage. *Rejected on revision:* added complexity (crash-safe ordering, fsync overhead, SD wear) for a small gain — the loss-window is bounded by gateway uptime during the outage, and IndustryFlow records the gap explicitly. The volume of data lost is acceptable and the operational simplification is significant. *Reopened in part by ADR-0020 (endurance axis):* an SSD/NVMe boot-and-data medium removes the SD-wear objection, so a *best-effort* persistent buffer is now permitted; a *guaranteed* crash-safe durable record remains rejected (ADR-0020 alternative D, to preserve gateway replaceability). See ADR-0020.

**E. Unsigned firmware updates.** Trust the gateway to push only correct firmware. *Rejected:* gateway compromise becomes full-node-fleet compromise. Signing isolates the trust boundary at the build-time signing key, which can live entirely offline.

**F. Per-node payload encryption on CAN.** *Rejected:* bandwidth cost is prohibitive on classic CAN (signature consumes most of the 8-byte payload). Complexity disproportionate to the physical-access threat model. Documented in decision 17 as a deliberate trust assumption.

## Consequences

### Positive

- **Standard Linux hardening posture.** Operators familiar with Linux server administration recognize these practices.
- **Gateway is replaceable.** Connect cables, restore identity from backup or re-provision. No accumulated local state to migrate.
- **Boot medium is not write-heavy.** Consumer SD cards reach normal service life. Industrial SD adds thermal range and ECC; it is not required.
- **Minimum-spec gateway hardware works.** A Raspberry Pi 3B+ with 1 GB RAM is adequate for the apartment-scale gateway. Pi 4 and Pi 5 are appropriate for higher-traffic or commercial deployments. The binding constraint is real-time Pycyphal decoding and mTLS upload, not log-retention throughput.
- **Audit trail with cryptographic integrity is held platform-side**, where it can be queried, correlated across deployments, and retained for the operationally meaningful timeframe.
- **Firmware integrity end-to-end** from build signing through node verification. Gateway compromise does not propagate to node firmware.

### Negative

- **Network outage at gateway reboot loses in-flight buffered data.** This is the operational cost of dropping persistent local buffer. Mitigation: reboots are rare, and IndustryFlow records the gap, so the loss is recorded rather than silent. The bounded volume is acceptable at sensor sampling rates.
- **IndustryFlow-side audit log infrastructure must exist and be reliable.** This is no longer a gateway-side problem but a platform-side one. The platform must implement immutable audit-trail storage, per-gateway hash-chain validation, and forensic query tooling. IndustryFlow provides the building blocks (immutable event log, query API); the audit-trail work must be explicit in the platform roadmap.
- **Firmware signing infrastructure** (build-time key management, bootloader hashing scheme, key rotation) is unbuilt, and must exist before first commercial deployment.
- **SSH-disabled-by-default adds operational friction for self-builders.** Documentation must state how to enable SSH temporarily for troubleshooting. Not yet in force — decision 19.

## Deferred decisions

- **IndustryFlow-side audit-trail schema.** What exactly the platform stores per upload batch (gateway identity, sequence number, content hash, prev-hash, signature, timestamp); how it indexes for query; how chain validation is performed at query time. Touches IndustryFlow platform roadmap, not this ADR.
- **Firmware signing key rotation schedule and ceremony.** How often, who participates, how the previous key is archived. Operational specification.
- **Incident response playbook.** Separate runbook, not an ADR.
- **What happens on detected hash-chain break.** Alert? Quarantine? Auto-rollback? Likely "alert and require human confirmation to continue", but decide explicitly when implementing the IndustryFlow audit-trail.
- **Gateway state recovery on replacement.** Procedure for re-provisioning a replacement gateway: cert renewal, configuration restore from IndustryFlow, sequence-number continuation. Operational spec.

## References

- ADR-0001: IndustryGrow framing.
- ADR-0002 (rev 3): Field bus architecture (placed the security boundary at the gateway; updated gateway hardware minimum to Pi 3B+).
- ADR-0005: DSDL foundation (the wire types decision 18's `t_acq` comes from).
- ADR-0007: PKI architecture — gateway identity, certificate management, provisioning workflows.
- ADR-0015: Gateway profile and control loops (the profile pull that decision 19 names as the second egress destination).
- ADR-0020: Gateway persistence model (supersedes decisions 8–9 on the endurance axis only).
- ADR-0022: Instance/integration ERP API (the endpoint the profile pull calls).
- Cyphal file transfer service specification (firmware distribution).
- Linux server hardening guides (CIS benchmarks for Debian/Raspberry Pi OS).
- systemd-journald configuration documentation.
