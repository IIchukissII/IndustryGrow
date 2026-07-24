<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0020: Gateway persistence model — local store as lifecycle-dependent data sink

- **ID:** ADR-0020
- **Status:** Proposed
- **Date:** 2026-06-15
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0016 (rev 1), ADR-0018 (rev 1)
- **Supersedes:** ADR-0004 (rev 1) decisions 8–9 and alternative D (partial)
- **Amends:** ADR-0015 decision 4 (configuration-vs-operational-state distinction)

## Revision history

- **(2026-06-15)** — Supersedes ADR-0004 rev 1 decisions 8–9 (the RAM-only runtime-state policy: no persistent telemetry log, in-memory-only ring buffer) and its alternative D (persistent crash-safe local buffer, rejected there). The scope is exactly those two decisions plus alternative D: **decision 10 (platform-side hash chain) is retained and relied upon by this ADR, and decision 11 (system logs) is unaffected.** ADR-0004 rev 1 carries inline supersession notes at decisions 8–9 and alternative D pointing here (no ADR-0004 rev 2 — see Reviewer notes). **Substantive reason:** ADR-0004 rev 1 rejected a persistent local buffer on two independent grounds — SD-card write-amplification/endurance, and a threat-model argument. An external SSD/NVMe-class boot-and-data medium removes the **endurance** ground, which was load-bearing for that rejection. This legitimately reopens the local-storage decision **on the endurance axis only**; the threat-model reasoning is unchanged and the local tamper-evident audit log (ADR-0004 alternative A) **stays rejected** (see decision 9). Also **amends** ADR-0015's configuration-state-allowed / operational-state-forbidden distinction (its decision 4): a *bounded* operational buffer is now permitted within the scope below; previously all operational state on the gateway was forbidden.

## Context and problem

ADR-0004 rev 1 described the gateway as a **stateless edge** whose only persistent state is identity and configuration, with runtime telemetry living in a RAM-only ring buffer (decisions 8–9). That framing implicitly assumed **IndustryFlow already exists as the durable record of history** — every justification for keeping the gateway stateless ("IndustryFlow is the durable history of record", "IndustryFlow records the gap") presupposes a cloud sink that has already seen the data.

On the project roadmap the cloud (IndustryFlow) only arrives at stage 11. Stages 1–10 — bring-up, sensor-platform development, the empirical **survey** and identification campaign (ADR-0016 rev 1), and first cultivation — run with **no cloud sink at all**. During that period there is no other store: survey and identification (ADR-0016 survey phase, decision 1) **cannot happen without local storage**, because the durable record they assume does not yet exist. The gateway is therefore, of necessity, the **primary durable data sink** for the pre-cloud period.

The stateless edge is thus an **end-state, not the bring-up reality**. The role of the local store evolves over the deployment lifecycle:

- **Primary durable sink** — stages 1–10, pre-cloud. The de-facto record of history; survey capture and first cultivation depend on it.
- **Store-and-forward buffer** — stage 11+, once IndustryFlow is present. The local store demotes to a resilience buffer; IndustryFlow reassumes the role ADR-0004 rev 1 assigned it.

The trigger that makes this reframing actionable (rather than merely a documentation fix) is hardware: an external SSD/NVMe-class boot-and-data medium removes the SD-card write-endurance constraint that was load-bearing in ADR-0004 rev 1's decision drivers and in its rejection of alternatives A and D. The endurance objection no longer stands; the threat-model objection does. This ADR records the persistence model that follows.

This ADR carries **decisions and rationale only**. On-disk formats, exact storage-device models and SKUs, retention figures as committed numbers, and pinouts are BOM / implementation concerns per ADR-0000 and are not fixed here.

## Decision drivers

- **No cloud before stage 11.** A durable local sink is not optional during bring-up; it is the only record that exists. Survey/identification (ADR-0016) is blocked without it.
- **Endurance constraint is lifted, threat model is not.** SSD/NVMe removes the write-amplification ground that load-bore ADR-0004 rev 1's stateless-edge rejection of local buffering; nothing about it changes the local-tamper threat model.
- **Replaceability must survive (ADR-0004 driver).** A dead gateway must remain a drop-in swap that loses at most an un-flushed tail, not a data-recovery procedure. The store is **best-effort**, not a durability guarantee against device failure.
- **Stateless-edge end-state is still the target.** Post-cloud, the gateway returns to a thin forwarder; the local store demotes to a buffer. This ADR does not abandon ADR-0004's principle — it scopes its applicability to the post-cloud lifecycle.
- **Scope each storage purpose separately.** Operational buffering, survey capture, and configuration/identity state have different durability semantics, bounds, and lifecycles; they must not be conflated under one "the gateway now has storage" decision.
- **Bus and storage load are modest by construction.** Per-node preprocessing on the STM32F405 means **features, not raw frames, traverse the bus** (e.g. the MLX90640 thermal node computes on-board), so neither CAN load (ADR-0002, classic CAN, 500 kbit/s) nor survey-storage volume is large.

## Decision

### Lifecycle role of the local store

1. **The local store's role is lifecycle-dependent.** Pre-cloud (stages 1–10) it is the **primary durable data sink** and the de-facto record of history. Post-cloud (stage 11+) it demotes to a **store-and-forward buffer**; IndustryFlow becomes the long-term durable record and audit authority (per ADR-0004 rev 1 decision 10). Before first sync, the local store *is* the record.

### Storage purposes (scoped separately)

2. **Store-and-forward operational buffer.** Decoded telemetry is buffered locally and flushed to IndustryFlow when connectivity returns; eviction is oldest-first past a retention bound. The bound is derived from **tolerable outage length, not storage size** — scalar telemetry is on the order of kilobytes per minute (ADR-0002 bus context), so size is not the binding constraint. Proposed retention: **~7 days `[PROPOSED — confirm]`** (rationale: covers a long weekend plus a multi-day connectivity or provider outage with margin, while bounding the worst-case un-flushed tail; it is a time bound, not a capacity bound).

3. **The buffer is best-effort, not a durability guarantee.** It survives reboot and network outage — the common case, and the gap ADR-0004 rev 1 decision 9 explicitly left lossy. It does **not** guarantee durability against failure of the storage device itself. This preserves the ADR-0004 replaceability value: a dead gateway is a drop-in swap that loses at most the un-flushed tail, not a data-recovery exercise. The "best-effort" semantics must be defined precisely enough (what is and is not promised on device failure) that replaceability is not quietly lost — see Negative consequences.

4. **Survey-capture mode.** A distinct, **time-boxed**, higher-rate local capture for an ADR-0016 identification campaign (survey phase, ADR-0016 decision 1), exported off-device for off-line identification (ADR-0016 decisions 2, 13). Because features rather than raw frames traverse the bus, survey-storage volume is modest. This mode is active only during a campaign; it is not a steady-state behaviour.

5. **Configuration and identity state are unaffected.** `active-profile.json` (ADR-0015 decision 4), the monotonic batch sequence number (ADR-0004 rev 1 decision 10), firmware artifacts (ADR-0004 rev 1 decision 13), and the ATECC-bound identity (ADR-0004 / ADR-0007) are already-permitted persistent state and are unchanged by this ADR.

   > **Amended by ADR-0024 (decision 4):** the **operator CA trust anchor** joins this enumeration. It was absent because the PKI that needs one had not been stood up; ADR-0024 stands it up, and the anchor is configuration state of a kind with the identity certificate already listed here. The rest of this decision is unchanged.

### What stays RAM-only

6. **Live decoded telemetry working set stays in RAM.** The current decoded state consumed by control loops and the state estimator (ADR-0015 decision 8; ADR-0016 decision 4) is in-memory, as before.

7. **The in-memory ring buffer for the current upload batch stays in RAM.** The transient batching buffer for the in-flight upload (the spirit of ADR-0004 rev 1 decisions 8–9) remains RAM-resident; the new persistent buffer (decision 2) sits behind it for outages that exceed the in-memory window or cross a reboot, not in place of it.

### What stays rejected

8. *(reserved — see decision 9)*

9. **The tamper-evident local hash-chained audit log stays rejected — do not resurrect.** ADR-0004 alternative A is **not** reopened by this ADR. Its rejection rested on the **threat model** — a compromised gateway already streams false data live, so local tamper-evidence adds no power — which a faster, higher-endurance storage medium does not change. The audit / forensic trail remains platform-side (ADR-0004 rev 1 decision 10, the per-gateway hash chain on IndustryFlow). Only the **endurance** ground for rejecting local buffering (ADR-0004 alternatives A and D) is retired here; the threat-model ground is untouched.

### Boot / storage medium

10. **An SSD/NVMe-class medium is the recommended boot-and-data medium** for any gateway that buffers telemetry (decision 2) or runs survey capture (decision 4). Bare SD remains acceptable **only** for minimal rule-based deployments with no local store. The device *class* is the architectural decision; the concrete device (USB-SSD on a Pi 4, NVMe-via-M.2-HAT on a Pi 5, exact SKU) is BOM / hardware detail per ADR-0000.

### Cold-boot recovery without network

11. **The gateway resumes without the network.** On cold boot it loads the last-known-good `active-profile.json` and resumes control (ADR-0015 decision 4; the rationale that rejected ADR-0015 alternative D). Buffered-but-un-flushed telemetry resumes flushing to IndustryFlow when connectivity returns. No cloud round-trip is required to resume operation.

### Forward direction (not a decision)

12. **Data richness per node may be a temporal variable** — richer during a survey campaign, reduced during operation — analogous to sensor density as a temporal variable in ADR-0016 (decision driver; decisions 1–3, 8). A path to fuller thermal capture (e.g. raw or higher-rate MLX90640 frames during a survey campaign, rather than only on-node features) may be wanted later. Recorded as a forward direction; **implementation deferred** — no format, rate, or trigger is specified here.

## Alternatives considered

**A. Keep ADR-0004 rev 1 as-is (RAM-only, no persistent buffer).** *Rejected:* presupposes a cloud durable sink that does not exist before stage 11. Survey and identification (ADR-0016) cannot run with no store, and first cultivation would have no record. The stateless edge is correct as an end-state but not as the bring-up reality.

**B. Defer all local persistence until IndustryFlow exists, then never add it.** *Rejected:* this is alternative A by another name for stages 1–10; it blocks the survey campaign that the pre-cloud stages exist to perform.

**C. Resurrect the local tamper-evident hash-chained audit log (ADR-0004 alternative A) now that storage is cheap and durable.** *Rejected — see decision 9.* The medium change addresses endurance, not the threat model. A compromised gateway streams false data live; local tamper-evidence adds no power. Audit/forensics stay platform-side (ADR-0004 rev 1 decision 10).

**D. Treat the local store as a guaranteed-durable record even post-cloud (full crash-safe durability against device failure).** *Rejected:* would forfeit the ADR-0004 replaceability value — a failed gateway would become a data-recovery procedure rather than a drop-in swap. Best-effort buffering (decision 3) keeps replaceability; IndustryFlow is the durability authority once present (decision 1).

**E. Bare SD card with a persistent buffer (no SSD/NVMe).** *Rejected as the recommended medium for buffering/survey deployments:* this is exactly the write-endurance problem ADR-0004 rev 1 cited. It remains acceptable only for minimal rule-based deployments with no local store (decision 10).

## Consequences

### Positive

- **Survey and identification are unblocked pre-cloud.** ADR-0016's survey phase has the local sink it requires; stages 1–10 can proceed without IndustryFlow.
- **Outage resilience for operational telemetry.** A bounded persistent buffer survives reboots and multi-hour/multi-day outages, closing the loss-window ADR-0004 rev 1 decision 9 left open — without a cloud round-trip to resume.
- **Stateless-edge end-state is preserved.** Post-cloud, the local store demotes to a best-effort buffer and IndustryFlow reassumes the durable-record role; ADR-0004's principle is scoped, not abandoned.
- **Endurance objection is genuinely retired.** SSD/NVMe makes persistent local buffering operationally sound, which the SD-card era did not.

### Negative

- **SSD/NVMe adds power draw on the gateway.** The gateway (SP0004) is fed from the +12 V SELV rail (SP0003, ADR-0018 rev 1 decision 3). An always-on SSD/NVMe is a non-trivial addition to the sensor-side power budget metered by the single INA226 on that bus (ADR-0018 decision 5). The power-budget implication must be checked against the SELV supply sizing when the gateway BOM is authored. **`[PROPOSED — confirm]`** that the chosen `+12 V` supply has headroom for the storage device.
- **"Best-effort buffer" semantics must be defined.** If the precise durability promise (what is lost on device failure vs. on reboot vs. on outage) is left vague, replaceability is quietly lost — a swapped gateway could turn into a data-recovery expectation. The semantics in decision 3 must be written down concretely before implementation.
- **One more piece of state to reason about on the gateway.** The stateless-edge model was simpler. A bounded operational buffer reintroduces operational state (amending ADR-0015 decision 4), with its own lifecycle, eviction, and failure modes to reason about and test.

## Deferred decisions

- **Exact retention numbers.** The `~7 days` operational-buffer bound (decision 2) is `[PROPOSED — confirm]`; confirm against tolerable outage length per deployment, not storage size.
- **Survey data-richness temporal policy.** Whether and how per-node data richness varies between survey and operation (decision 12) — format, rate, and trigger — is a forward direction; implementation deferred.
- **Whether the storage device earns its own SP number or folds into the gateway BOM (SP0004).** Per ADR-0019, an SSD/NVMe boot-and-data medium is a purchased part; decide whether it crosses the SP granularity threshold (ADR-0019 decision 4) as its own `SPxxxx` or is an MPN line in the gateway (SP0004) BOM. Flagged as an open question.
- **On-disk buffer format.** Append-log vs. SQLite vs. columnar — implementation, not architecture (ADR-0000). Not decided here.
- **Encryption-at-rest of the buffer.** Telemetry is low-sensitivity, so probably not warranted; flagged so the decision is explicit rather than implicit. Revisit if any high-sensitivity data is ever buffered locally.

## References

- ADR-0000: Decision records and single-source-of-truth discipline — SKUs/prices/formats live in the BOM, not here.
- ADR-0001: IndustryGrow framing — open-core platform, roadmap stages.
- ADR-0002 (rev 3): Field bus architecture — classic CAN, 500 kbit/s; telemetry volume context; on-node preprocessing.
- ADR-0004 (rev 1): Gateway host hardening and stateless-edge operation — decisions 8–9 (superseded on the endurance axis), decision 10 (platform-side hash chain, retained and relied upon), decision 11 (system logs, unaffected), decision 13 (firmware artifacts), alternatives A (audit log, stays rejected) and D (persistent buffer, endurance ground retired).
- ADR-0007: PKI — ATECC-bound gateway identity.
- ADR-0015: Gateway profile caching and local control loops — decision 4 (`active-profile.json`, configuration-vs-operational-state distinction, amended here), alternative D (RAM-only profile, rejected there).
- ADR-0016 (rev 1): Empirical survey and state-space modeling — survey phase, sensor-density-as-temporal-variable, off-line identification.
- ADR-0018 (rev 1): Cabinet power distribution — +12 V SELV supply (SP0003), sensor-bus INA226 metering.
- ADR-0019: Purchased-part (SP) identification — SP granularity threshold; gateway SBC is SP0004.

---

## Reviewer notes

Points marked `[PROPOSED — confirm]` or flagged for human resolution, gathered for quick triage:

1. **Operational-buffer retention `~7 days`** (decision 2; Deferred decisions). Proposed on a *tolerable-outage* basis, not capacity. Confirm the number — and confirm it should be expressed as a time bound rather than a size bound.
2. **Gateway power budget for SSD/NVMe** (Negative consequences). Flagged `[PROPOSED — confirm]`: verify the chosen +12 V SELV supply (SP0003, ADR-0018) has headroom for an always-on SSD/NVMe before the gateway BOM is committed. This is the one cross-ADR power-budget interaction; it cannot be resolved inside this ADR.
3. **Storage device SP-numbering** (Deferred decisions). Open question per ADR-0019 decision 4: own `SPxxxx` vs. MPN line in the SP0004 BOM. Needs a registry/ADR-0019 owner to decide.
4. **"Best-effort buffer" semantics** (decision 3; Negative consequences). Not a numeric proposal but a definitional gap that must be closed before implementation, or replaceability (an ADR-0004 value) erodes silently. Flagged for explicit specification.
5. **Encryption-at-rest** (Deferred decisions). Recommendation is "probably not" (low-sensitivity telemetry); flagged so the human confirms the omission deliberately.

### Metadata / supersession convention (resolved 2026-06-15)

- **Partial supersession is expressed by inline note, not an ADR-0004 rev 2.** ADR-0004 rev 1 now carries inline supersession notes at decisions 8–9 and alternative D pointing here; this ADR keeps the `Supersedes: … decisions 8–9 …` form and the `Amends: ADR-0015 decision 4` field. A rev 2 was rejected: it would restate this ADR's decision inside ADR-0004, violating single-source-of-truth (ADR-0000 decision 3), and most of ADR-0004 rev 1 (decisions 1–7, 10–17) remains in force. The supersession scope was also corrected from the originally-drafted *8–11* to *8–9* — decision 10 (platform-side hash chain) is retained and relied upon by this ADR, and decision 11 (system logs) is unaffected.
