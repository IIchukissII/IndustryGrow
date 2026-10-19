<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0021 — Instance-and-integration ERP as the pre-cloud system of record

- **Status:** Accepted
- **Date:** 2026-07-09; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0004](ADR-0004-gateway-host-hardening.md) (stateless edge that excludes the ERP host), [ADR-0007](ADR-0007-pki-and-secure-element-identity.md) (PKI whose public material the ERP holds), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (single mutation channel the profile store leaves intact), [ADR-0016](ADR-0016-empirical-survey-and-state-space-modeling.md) (profile as one artifact; inventory mobility), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (identity grammar; instance layer hosted here, d21), [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (SP spec and vendor-serial identity), [ADR-0020](ADR-0020-gateway-persistence-model.md) (lifecycle-staged persistence; operational buffer), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (the API that exposes these entities), [ADR-0023](ADR-0023-machine-readable-type-registry.md) (type registry read through, never copied), [ADR-0024](ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) (operator CA that issues and keeps no record), [ADR-0026](ADR-0026-erp-backup-and-restore.md) (backup and restore of this store), [ADR-0029](ADR-0029-node-firmware-update-and-bootloader.md) (firmware path that needs decision 18), ADR-IF-0001 (planned `production_unit` core the `[F]` part aligns to)

## Context

ADR-0017 separates identity into three homes: the public type registry, the flat object-store warehouse keyed by identifier, and an instance-and-integration layer — serials, secure-element bindings, lifecycle records and the integration identifier — that it assigns to the platform and defers. IndustryFlow arrives only at roadmap stage 11 (ADR-0020), while stages 1–10 produce exactly these records: serials must be allocated, bindings held, and installs, moves and replacements tracked with history.

Neither existing home fits. The registry is type-level, public and PR-gated; the warehouse is flat blobs, one object per identifier. Instance and integration data is per-operator, private, mutable and joined over time across machine, position and instance.

The integration end-state is core plus layer: IndustryFlow is the independent multitenant core, IndustryGrow a domain layer over it (ADR-0001 dec 7). The glossary's `[F]`/`[D]` tags draw that line, and this store straddles it.

## Decision

### Positioning and lifecycle role

1. **A compact, self-hostable, containerized ERP is the pre-cloud system of record for the instance and integration layer, run on one operator-chosen host that reaches the gateways it serves, and never on a gateway.** Self-hostability means a single node and no managed service (ADR-0001 dec 6). The gateway is excluded because it is the stateless replaceable edge (ADR-0004; ADR-0020 dec 5), and a system of record there makes a gateway swap a data-loss event.
2. **The ERP is the primary record for stages 1–10; at stage 11 its `[F]` part aligns to IndustryFlow's `production_unit` and its `[D]` part remains the IndustryGrow layer.** Integration is core plus layer, not absorption. Before the core exists the local store is the record, as ADR-0020 dec 1 holds for telemetry.
3. **The schema is a forward-compatible subset of the multitenant `production_unit` model, split along the `[F]`/`[D]` line.** Stage 11 is then a data migration and re-layering, not a remodel. Where ADR-IF-0001 is not yet fixed, the ADR-0017 identifier grammar is the shared anchor on the load-bearing keys.

### What the ERP owns (and only this)

4. **Machines and E-instances, with the ERP as the serial-allocation authority.** It issues the next serial per module and version (ADR-0017 dec 1, 8) and records the instance; the type meaning is referenced from the registry, never copied.
5. **The E-instance identity binding: serial to secure-element certificate, public material only.** It is the structured content of the `-PR` record (ADR-0007; ADR-0017 dec 12); the private key never leaves the part, and the blob stays in the warehouse with the ERP holding its key.
6. **Integration records with their history.** The integration identifier is re-assigned on every install, removal and replacement (ADR-0017 dec 13), so the record keeps the sequence and "where has this serial been" is a query. The depth code is assigned at integration and never written back into a type or instance identifier.
7. **A lifecycle-document index over the warehouse, not the documents.** Per instance it holds status, dates, calibration validity and the object key of each lifecycle record; the blob stays in the warehouse (ADR-0017 dec 15).
8. **Deployment-specific profile versions and the record of which version is active on which machine.** A version is setpoints and identified model parameters as one unit (ADR-0016 dec 6).
9. **Purchased-part stock and placement.** It records quantities on hand and which tracked part instance sits at which machine (ADR-0019 dec 2); the vendor-free spec stays in the registry and the SKU and price in the BOM.

### Boundaries (what the ERP must not own)

10. **Not the operational or forensic trail.** Telemetry, firmware-flash events, control-decision audit and the tamper-evident chain stay platform-side (ADR-0004 dec 10; ADR-0020 dec 9). The ERP records assets, their configuration and history, and buffers nothing operational.
11. **Not the type registry, the warehouse blobs, or the BOM's values.** Each is referenced by key; every fact the ERP stores is one no other home owns (ADR-0000 dec 3).

### Profiles

12. **The gateway remains the single mutation channel; the ERP is the versioned store the active version is pulled from and the record of which is active where.** A profile is deployed only through the gateway (ADR-0015 dec 1, 5; ADR-0020 dec 11). Three roles do not overlap: the community registry holds public templates (ADR-0001 dec 5), the ERP holds private deployment versions, and the gateway holds a cache of the active one.
13. **Model parameters stay inside the profile and are versioned with it.** The ERP stores the whole profile as one atomically versioned artifact, so rollback covers setpoints and model together (ADR-0016 dec 6). It is where the offline identification of ADR-0016 dec 2 emits new versions into, not a second versioning mechanism.

### Licensing and data

14. **The application is open core under AGPL-3.0-or-later; the data it holds is operator-private production data.** The self-build path (ADR-0001 dec 1, 6) covers standing up the store; the records stay off the public repository, as ADR-0017 requires of the instance layer.

### Deployment vehicle

15. **Delivery is a single container over a compact document metadata store that indexes the warehouse; the concrete product is implementation.** A document store follows from the layer being the queryable index over a store whose identifiers are object keys (dec 7; ADR-0017 dec 15); integration history is carried by validity-stamped records with one current occupant per position, not by a relational join engine. *Compact* bounds the entity set — the asset and traceability core — not the image: a printed representation of records the ERP already owns adds no entity, while a second service or a second node breaches *single-node*.

### Tenancy

16. **Single-tenant in operation, multitenant-conformant in schema.** Every `[F]` record carries the tenant dimension as a single-tenant instance of the core model, so stage 11 inserts the estate as one tenant; no isolation, partitioning or shared hosting is built, because that is the core's concern. A second operator before stage 11 runs a second private instance.

### Machine identity

17. **The ERP owns a machine's identity binding: its certificate's public material, recorded against the machine identifier.** The gateway is a purchased part with no E-instance serial (ADR-0019 dec 2), so decision 5 cannot hold it; nothing else pre-cloud does, because the operator CA keeps no record of what it issued (ADR-0024 dec 13). The binding makes expiry of a short-lived certificate (ADR-0007 dec 7) a query; ADR-0022 dec 12 exposes it.

### Intended firmware release

18. **The ERP owns a machine's intended firmware release: the operator's recorded selection of a released artifact set, by its object-key root.** ADR-0029 dec 14 and 16 start a transfer from an operator action recorded here and fetch the artifact from here; the repository publishes a release to every operator alike and says nothing about one cabinet. It is intent only — what a node runs is observed at the gateway (ADR-0029 dec 15) and excluded by decision 10 — and machine-scoped, because firmware identity follows the carrier every node runs (ADR-0017 dec 16). The bytes stay in the warehouse and are read through; ADR-0022 dec 14 exposes the entity.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | Build no ERP; wait for IndustryFlow at stage 11 | Serials, bindings and integration records are produced during stages 1–10 with no cloud present |
| — | Stand up full IndustryFlow now | The platform does not exist end-to-end; a compact store for the instance subset migrates into it later |
| — | Track instances in the type registry or in spreadsheets | The registry is type-level, public and PR-gated; spreadsheets give no allocation authority, no integrity and no history |
| 1 | Run the ERP on the gateway host | The gateway's persistent state is enumerated (ADR-0020 dec 5) and excludes a system of record; a swap would lose serial, binding and integration state |
| 2 | Stage 11 as IndustryFlow absorbing and retiring the ERP | Erases the core-plus-layer architecture; the `[D]` layer remains over the core |
| 3 | A bespoke instance schema unaligned with `production_unit` | Forces a rewrite at stage 11, when the data is most valuable |
| 9 | ERP as source of truth for SP specs, SKUs or prices | The spec is the registry's (ADR-0019) and the price the BOM's (ADR-0000); the ERP owns only stock and placement |
| 10 | Let the ERP hold operational telemetry or an audit trail | A second forensic record of the same events (ADR-0004 dec 10; ADR-0020 dec 9; ADR-0017) |
| 13 | Profiles as a separate subsystem, or model parameters split from setpoints | Rejected by ADR-0016; breaks atomic rollback and adds a second mutation path |
| 15 | A relational store with history as the store class | Couples the `[D]` layer to the core's paradigm; the layer is an index over an object store keyed by identifier |
| 15 | A full enterprise ERP with finance, HR and procurement | Contradicts the compact, self-hostable positioning and buys modules the project does not need |
| 16 | Build multitenant operation into the ERP now | Unused with one operator, and runtime tenancy is a core `[F]` concern the layer would duplicate |
| 16 | A schema with no tenant dimension | Forces a remodel at stage 11 |
| 17 | Give the gateway a synthetic E-instance serial and record it under decision 5 | Puts a value in the identity grammar that names nothing the project manufactures; every consumer learns an exception |
| 18 | Carry the intended firmware release inside the cultivation profile | The two version on unrelated clocks; a firmware selection would mint profile versions and a profile rollback would revert firmware |
| 18 | Record against each node what it runs, to report update progress | Operational intake excluded by decision 10 and ADR-0022 dec 9; filled only by a machine write, unfilled it is a column that lies |

## Consequences

**Positive**

- Serial allocation, provisioning binding and integration tracking have a home for the stage 1–10 campaigns that produce them.
- Instance mobility is answered by the integration history.
- Each boundary leaves the ERP owning only facts no other home owns.
- Stage 11 is a migration and re-layering, with the estate entering the core as one tenant.
- A self-builder can run the store from open artifacts while the data stays private.
- The warehouse keeps its flat-blob role and gains a queryable index.
- Lifecycle blobs are written to the warehouse before their key is indexed, and the gateway pulls the active profile rather than receiving a push (ADR-0022 dec 7, 8).
- Backup and restore of the store are decided in ADR-0026.

**Negative**

- The instance layer is a new critical, mutable store to operate; losing it loses serial-allocation state.
- The migration promise is only as good as ADR-IF-0001's eventual model, and the tenant dimension is carried before anything exercises it.
- The boundaries hold only while review enforces them.
- Three profile homes (template, instance, cache) need teaching; editing a profile in the ERP does not reach the plant until the gateway pulls it.
- The compact scope must be defended against finance and procurement creep.
- The container carries a text-shaping and font stack for printed representations.

## Deferred

| Open question | Settled by |
|---|---|
| Concrete `production_unit`-subset mapping and the stage-11 export transform | ADR-IF-0001 |
| Re-layering shape at stage 11, and whether the ERP console survives as an operator console over the core | ADR-IF-0001 |
| Whether a second operator before stage 11 takes a second private instance or pulls the core timeline forward | Maintainers, when a second operator appears |

## References

- ADR-0022 — the API over these entities; ADR-0026 — backup and restore.
- `erp/README.md` — collections, store engine, and the boundaries as enforced.
- `erp/app/db.py` — `foundation.*` / `domain.*` collections and their indexes.
- `GLOSSARY.md` — `[F]` / `[D]` scope tags.
