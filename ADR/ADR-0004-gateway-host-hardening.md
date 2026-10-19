<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0004 — Gateway host hardening, firmware signing and stateless-edge operation

- **Status:** Accepted
- **Date:** 2026-05-16; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0002](ADR-0002-field-bus-architecture.md) (places the security boundary at the gateway; the bus time base decision 18 reads), [ADR-0005](ADR-0005-dsdl-foundation.md) (the wire types decision 18 stamps), [ADR-0007](ADR-0007-pki-and-secure-element-identity.md) (who the gateway is; this record is how it behaves), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (the profile pull and the photoperiod decision 19 names), [ADR-0020](ADR-0020-gateway-persistence-model.md) (the local store that bounds decisions 8–9), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (the ERP endpoint decision 19 adds to egress), [ADR-0024](ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) (custody of the firmware-signing key), [ADR-0029](ADR-0029-node-firmware-update-and-bootloader.md) (the node update path that realises decisions 12–16)

## Context

The gateway is the security boundary between the trusted in-cabinet CAN domain and the external network (ADR-0002), in every deployment model ADR-0001 admits. A compromised gateway can cause physical harm — overdosing nutrients, overheating the cabinet, faking telemetry — and it is the component that pushes firmware to the nodes, the highest-power operation in the system.

The site LAN is hostile beyond a defined set of devices. Physical access to the cabinet implies legitimate operator presence. A compromised gateway already streams false data live, so tamper-evidence of its own history grants no further protection; the durable record and its forensic tooling belong to the platform.

The gateway is a stateless edge: a replaceable bridge whose persistent state is its identity and configuration (ADR-0020 decision 5), replaceable in minutes by reconnecting cables and provisioning an identity. Its storage must not carry write-heavy services, and one gateway's compromise must not reach other gateways or the platform.

## Decision

1. **The gateway authenticates to IndustryFlow by mTLS.** The PKI behind it is ADR-0007's.
2. **SSH is disabled by default, key-only when enabled, and root login is refused permanently.** Disabled-by-default removes an attack surface for the cost of re-enabling it per operation; when this binds is decision 19.
3. **fail2ban guards SSH and any exposed service with strict, escalating thresholds, except for loopback and the management LAN.** A persisted escalating ban on the operator's own workstation presents as dead hardware; against a key-only sshd (decision 2) credential brute force from the management LAN is not the threat bought off. The exempt set is derived from the management interfaces' attached subnets at provisioning.
4. **Security updates install unattended, with the automatic reboot inside the photoperiod-off window.** When this binds is decision 19.
5. **The firewall admits no inbound traffic except SSH on the LAN-facing management interfaces, and restricts egress to named platform endpoints.** SSH is accepted on the whole set of management interfaces, plus the live default-route interface, so the operator is not locked out when the box is reached over another NIC; CAN interfaces are excluded. There is no general internet access from the gateway; the endpoints and when the restriction binds are decision 19.
6. **No service exposes data on the site LAN.** A local debug interface binds to loopback only and is reached by SSH port-forwarding.
7. **Gateway services run as a dedicated unprivileged user with access only to the CAN interfaces and the configuration they need.** No root operation runs during normal runtime; decision 20 extends this to every unit.
8. **Telemetry is forwarded, not archived, by the gateway; the durable record and audit authority are platform-side.** Pre-cloud the local store of ADR-0020 is the record, and from IndustryFlow onward it is a buffer (ADR-0020 decision 1); the gateway keeps no archive of raw frames or upload history beyond that store.
9. **A bounded in-memory buffer bridges transient network loss.** It is lost on reboot; a best-effort persistent buffer behind it is ADR-0020's (decisions 2–3, 7), and the platform records any gap that remains.
10. **The audit trail is a per-gateway hash chain held by IndustryFlow.** Each uploaded batch carries the gateway identity, a per-gateway monotonic sequence number persisted across reboots, the batch's SHA-256 hash and the previous batch's hash. Tamper-evidence is enforced where the long-term storage and forensic tooling live; the gateway only produces the sequence number and the hashes.
11. **System logging is the host's standard journal, persistent and size-limited.** It is operator-debugging infrastructure, not forensic infrastructure, and no custom logging pipeline is added.
12. **CAN node firmware is signed at release by a dedicated firmware-signing key, separate from every identity key.** The key is held offline and used only at release events; its custody is ADR-0024 decision 14.
13. **The gateway distributes firmware over the Cyphal file transfer service, from artifacts it holds on disk for the current and previous release.** Firmware writes are infrequent; where the artifacts come from and which one is served are ADR-0029 decisions 14–17.
14. **Each node verifies the signature before an image becomes bootable.** The verification public key is built into the node bootloader and flashed with it; an image without a valid signature is refused (ADR-0029 decision 6).
15. **The bootloader is replaced only over SWD, with physical access to the node.** It is never an update target over the bus (ADR-0029 decision 10), so a remote attacker cannot substitute it.
16. **Firmware update events reach IndustryFlow as typed events on the telemetry upload path.** Each records gateway identity, target Node-ID, firmware version, hash, the node-reported verification result and a timestamp.
17. **The in-cabinet CAN domain is trusted: no per-node authentication and no payload encryption.** Anyone with physical access to the bus wires can spoof or inject, and physical access already implies operator presence.
18. **A decoded sample carries three provenance stamps, of which only the acquisition stamp is on the wire.** Decision 10 fixes what a batch carries; this fixes what a sample carries. The receipt and store stamps are gateway and platform record fields, not DSDL, because a node cannot know either and ADR-0005 decision 2 reuses before it mints.
    - `t_acq` — written by the node on the bus time base (ADR-0002 decision 11), as the DSDL synchronized timestamp. An unsynchronized node's `UNKNOWN` (0) propagates as 0; no tier substitutes `t_rx` for it.
    - `t_rx` — written by the gateway at frame decode, on both the host realtime and monotonic clocks; durations come from the monotonic reading.
    - `t_store` — written by each tier that retains the sample, on that tier's clock; absent, not zero, where a tier has no store.
19. **Decisions 2, 4 and 5 state the production posture; each binds only once its condition holds.** Until then the provisioning material carries the bring-up posture below, and the rest of each decision is enforced from the first boot.
    - Decision 2: sshd stays enabled (key-only, no root) until bring-up no longer runs over SSH.
    - Decision 4: automatic reboot stays disabled until a cultivation profile defines the photoperiod (ADR-0015).
    - Decision 5: egress stays open until IndustryFlow exists (roadmap stage 11). The restricted egress names two endpoints, IndustryFlow and the operator ERP the gateway pulls its profile and firmware from (ADR-0015 decision 5; ADR-0022 decisions 2, 8).
20. **Decision 7's service-user policy binds every gateway application unit.** The telemetry edge, the profile pull, the firmware client and the bus time master run as the one unprivileged service user, as does any later unit. Configuration is read-only to each of them except the unit whose job is to write it.
21. **`t_rx − t_acq` is recorded only while the gateway host clock is disciplined and settled.** The difference spans two machines and has no monotonic form, and a node re-derives its offset at most once every two publication periods (ADR-0002 decision 11), so a host clock correction reaches the two ends at different times. The latency is absent — not zero, not clamped — while the host clock is unsynchronized or within two node publication periods plus the disciplining interval of a step; both stamps stay stored.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 2 | SSH always on with key authentication | Disabling it by default removes an attack surface for a small operational cost |
| 3 | Strict thresholds for every source, the management LAN included | A few mistyped key attempts ban the operator's workstation for hours across reboots, indistinguishable from dead hardware, and buy nothing against a key-only sshd |
| 5 | SSH bound to the one interface that carried the default route at provisioning | Locks the operator out when the box is later reached over another management NIC |
| 5 | Egress to IndustryFlow only | The gateway pulls profiles and firmware from the operator ERP, a second named endpoint |
| 6 | Inbound services on the LAN (debug dashboard, local Grafana) | Turns the gateway into a server and adds attack surface and state; loopback binding plus port-forwarding gives the same access |
| 8 | Local tamper-evident hash-chained audit log of every frame and upload, with 30–90 day retention and rotation | A compromised gateway already streams false data live, so local tamper-evidence adds no protection; forensics belong where the record is queryable. An endurance-rich medium does not change this (ADR-0020 decision 9) |
| 9 | Guaranteed crash-safe persistent local buffer | A guarantee turns a failed gateway into a data-recovery exercise, against replaceability; a best-effort persistent buffer is ADR-0020's |
| 12 | Unsigned firmware updates, trusting the gateway | Gateway compromise becomes compromise of every node; signing moves the trust boundary to an offline key |
| 15 | Bootloader signed by a separate, stronger, rarely-rotated key | Nothing on the node verifies a bootloader; SWD-only replacement makes physical access the control |
| 17 | Per-node authentication or payload encryption on CAN | A signature consumes most of an 8-byte classic-CAN payload; the cost is disproportionate to a physical-access threat |
| 18 | Substitute `t_rx` for an unsynchronized node's `t_acq` | The acquisition-to-receipt difference then reads zero permanently |
| 21 | Record the latency as zero or clamp it while the clock is unsettled | A clock correction reads as latency never incurred; an absent value is detectable, a clamped one is not |

## Consequences

**Positive**

- The host posture is standard Linux hardening, recognisable to any server administrator.
- A gateway is replaced by reconnecting cables and restoring or re-provisioning its identity, with no accumulated local state to migrate.
- A gateway with no local store runs from an SD card at normal service life (ADR-0020 decision 10).
- A minimum-spec single-board computer suffices; the binding load is real-time decoding and mTLS upload, not log retention.
- The audit trail with cryptographic integrity sits platform-side, where it is queried, correlated across deployments and retained.
- Firmware integrity runs from release signing to node verification, so gateway compromise does not reach node firmware.

**Negative**

- An outage spanning a gateway reboot loses the in-memory buffer, narrowed where ADR-0020's persistent buffer is fitted.
- IndustryFlow must provide immutable audit storage, per-gateway chain validation and forensic queries before the audit trail exists.
- The verification key is fixed in the bootloader, so replacing the firmware-signing key means re-flashing every bootloader over SWD (ADR-0024 decision 14).
- SSH disabled by default adds friction for self-builders, and the documentation must state how to enable it temporarily.

## Deferred

| Open question | Settled by |
|---|---|
| IndustryFlow audit-trail schema: what is stored per batch, how it is indexed, how the chain is validated at query time | IndustryFlow platform roadmap |
| Response to a detected hash-chain break (alert, quarantine, human confirmation) | The IndustryFlow audit-trail implementation |
| Re-provisioning a replacement gateway: certificate renewal, configuration restore, sequence-number continuation | An operational specification, before the first gateway replacement in the field |
| Incident response playbook | Maintainers, as a runbook |

## References

- ADR-0002, ADR-0005, ADR-0007, ADR-0015, ADR-0020, ADR-0022, ADR-0024, ADR-0029.
- `gateway/provision.sh`, `gateway/files/` — the hardening, firewall, journal and unit material that realises decisions 2–7, 11, 19 and 20.
- `gateway/files/app/gateway_telemetry.py` — the provenance stamps and latency validity of decisions 18 and 21.
- `firmware/tools/release.sh`, `firmware/README.md` — release signing (decisions 12, 14).
- Cyphal file transfer service specification; CIS benchmarks for Debian / Raspberry Pi OS; systemd-journald documentation.
