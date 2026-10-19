<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0020 — Gateway persistence model

- **Status:** Accepted
- **Date:** 2026-07-26; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0004](ADR-0004-gateway-host-hardening.md) (its RAM-only decisions 8–9 and the local-buffer alternative of its decision 9 are replaced by this record for a gateway with a local store; its decision 10 hash chain is relied upon), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (configuration state of decision 4 beside the operational state permitted here), [ADR-0016](ADR-0016-empirical-survey-and-state-space-modeling.md) (survey phase that needs the store), [ADR-0018](ADR-0018-power-distribution-and-rail-monitoring.md) (supply that powers the storage medium), [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (whether the medium takes an SP number), [ADR-0024](ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) and [ADR-0025](ADR-0025-cultivation-profile-signing.md) (trust material added to decision 5), [ADR-0033](ADR-0033-canopy-imaging-channel.md) (raw imagery as its own storage purpose)

## Context

A stateless gateway presupposes a durable record of history elsewhere. IndustryFlow arrives at roadmap stage 11; bring-up, sensor-platform work, the survey and identification campaign (ADR-0016) and first cultivation precede it with no cloud sink, so before then the gateway is the only place data can be kept.

ADR-0004 rejected a persistent local buffer on two grounds: SD-card write endurance and the threat model. An SSD/NVMe-class medium removes the endurance ground; the threat-model ground is unchanged.

A dead gateway must stay a drop-in swap that loses at most an un-flushed tail, not become a data-recovery procedure. Operational buffering, survey capture and configuration state differ in durability, bound and lifetime. Nodes preprocess on board, so features rather than raw frames cross the bus, and scalar telemetry volume is small (ADR-0002).

## Decision

1. **The local store's role depends on the lifecycle stage.** Before IndustryFlow exists the store is the primary durable record of history; once IndustryFlow exists it is a store-and-forward buffer, and IndustryFlow is the durable record and audit authority (ADR-0004 d10).
2. **Decoded telemetry is buffered locally under a time bound.** It is flushed to IndustryFlow when connectivity returns and evicted oldest-first past the bound. The bound derives from tolerable outage length, not storage size, because scalar telemetry volume does not bind; the figure is configuration (ADR-0000 d2).
3. **The buffer is best-effort.** It survives reboot, power loss and network outage, losing at most the un-flushed tail; it promises nothing against failure of the storage device. That keeps a failed gateway a drop-in swap.
4. **Survey capture is a separate, time-boxed mode.** It records at a higher rate for one identification campaign (ADR-0016 d1) and is exported for off-line identification (ADR-0016 d2, d13); it is not a steady-state behaviour. Feature telemetry keeps its volume modest; raw imagery is a purpose of its own (ADR-0033 d15).
5. **Configuration and identity state persist as before.** The enumeration is: the active profile and its last-known-good copy (ADR-0015 d4), the monotonic batch sequence number (ADR-0004 d10), firmware artifacts (ADR-0004 d13), the secure-element-bound identity (ADR-0007), the operator CA trust anchor (ADR-0024 d4) and the profile-verification public key (ADR-0025 d10).
6. **The live decoded working set stays in RAM.** It is the state control loops and the estimator consume (ADR-0015 d8; ADR-0016 d4).
7. **The in-flight upload batch stays in RAM.** The persistent buffer of decision 2 sits behind it for outages that outlast the in-memory window or cross a reboot.
8. *(withdrawn: see decision 9)*
9. **No tamper-evident local audit log.** A compromised gateway already streams false data live, so local tamper-evidence adds nothing, and a better storage medium does not change that. The forensic trail is the platform-side hash chain (ADR-0004 d10).
10. **A gateway with a local store boots from an SSD/NVMe-class medium.** Bare SD is acceptable only for a deployment with no local store. The device class is the decision; the device is a BOM line.
11. **The gateway resumes without the network.** On cold boot it loads the last-known-good profile and resumes control (ADR-0015 d4), and flushes buffered telemetry when connectivity returns.
12. **Per-node data richness may vary by lifecycle phase; this record fixes no format, rate or trigger for it.** Richer capture during a campaign is the analogue of sensor density varying over time (ADR-0016 d1–d3, d8); ADR-0033 d15 takes it for imagery.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | Restate the replacement inside a revised ADR-0004 | Puts this record's decision in a second home (ADR-0000 d3) |
| 1 | Keep the gateway RAM-only | Presupposes a cloud sink that does not exist before stage 11; survey and first cultivation would have no record |
| 1 | Defer all local persistence until IndustryFlow exists | Alternative A for stages 1–10; blocks the survey campaign those stages exist to run |
| 2 | Bound the buffer by capacity | Scalar telemetry volume does not bind; tolerable outage length does |
| 3 | Guaranteed durability against device failure, also post-cloud | A failed gateway becomes a data-recovery procedure instead of a drop-in swap |
| 9 | Resurrect the local hash-chained audit log (ADR-0004 dec 8 alternatives) | The medium change answers endurance, not the threat model |
| 10 | Persistent buffer on bare SD | The write-endurance problem ADR-0004 cited |

## Consequences

**Positive**

- Survey, identification and first cultivation have a local record before IndustryFlow exists.
- Buffered telemetry survives reboots and multi-day outages, and no cloud round-trip is needed to resume.
- After stage 11 the gateway returns to a thin forwarder with a best-effort buffer.

**Negative**

- An always-on SSD/NVMe draws from the gateway's SELV supply (ADR-0018), whose headroom must be confirmed when the gateway BOM is authored.
- The gateway carries operational state with its own eviction and failure modes to test.

## Deferred

| Open question | Settled by |
|---|---|
| Retention figure per deployment, against tolerable outage length | Deployment configuration |
| Supply headroom for the storage medium | Gateway BOM |
| Own SP number for the storage medium, or an MPN line in the gateway SBC's BOM (ADR-0019 d4) | Registry owner |
| Encryption at rest of the buffer; telemetry is low-sensitivity | Maintainers, if higher-sensitivity data is ever buffered |

## References

- ADR-0004 — decisions 8–11 and 13, and the alternatives of decisions 8 and 9.
- `gateway/files/app/gateway_telemetry.py` — the store, its eviction and its durability settings.
- `gateway/gateway.env.example` — store switch and retention figure.
- `store/SP0004-M-gateway-bringup.md` — SD at bring-up, SSD/NVMe in production.
