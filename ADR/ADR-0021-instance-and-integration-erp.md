<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0021 (rev 1): Instance-and-integration ERP — the pre-cloud system of record

- **ID:** ADR-0021 (rev 1)
- **Status:** Accepted
- **Date:** 2026-07-09 (rev 1: 2026-07-19)
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0000, ADR-0004 (rev 1), ADR-0007, ADR-0015, ADR-0016 (rev 1), ADR-0017 (rev 1), ADR-0019, ADR-0020, ADR-0022 (API contract)
- **Realizes:** ADR-0017 (rev 1) deferred decision "Registry and store location" — the instance/integration layer host
- **Relates to:** ADR-IF-0001 (planned) — the `production_unit` entity this store's foundational part aligns to when IndustryGrow integrates as a layer over the IndustryFlow core at stage 11

## Revision history

- **rev 1 (2026-07-19)** — Resolves the deferred *"ERP product / framework /
  database engine"* decision and refines decision 15 accordingly. The store
  **class** changes from *"relational with history"* to a **compact document
  metadata store over the flat object-store warehouse** (ADR-0017 decision 15).
  The reasoning: the project's storage paradigm is already document/blob-oriented
  — identifiers *are* object keys, the store is flat, listing is a key-prefix
  scan (ADR-0017 decision 15) — and this ERP is the *queryable index over that
  object store* (decision 7), not a relational core imported from IndustryFlow.
  A relational engine would couple the `[D]` domain **layer** to the paradigm of
  the `[F]` **core**, which decision 2 explicitly keeps separate (core-plus-layer,
  not absorption). Where the Context, Decision drivers, and alternatives below say
  *"relational"* (e.g. *"a mutable relational join over time"*), read it as naming
  the **mutable-join-over-time nature** of integration data — the property that
  ruled out the flat keyspace and the PR-gated table — now realized as a document
  store, **not** a commitment to SQL. The concrete engine is resolved to
  **MongoDB** (Deferred decisions, now resolved). No ownership boundary (decisions
  4–11), tenancy decision (16), or profile-channel decision (12–13) changes. The
  stage-11 `[F]` migration (decisions 3, 16) is now a document→`production_unit`
  export-transform keyed on ADR-0017's identifier grammar — still *migration, not
  remodel*, because the load-bearing keys are unchanged.

## Context and problem

ADR-0017 separated identity into two axes and three homes. The **type registry** (`Exxxx`/`SPxxxx` → meaning) is repo-side and public — it is `REGISTRY.md`. The **document store** for type-level and per-instance artifacts is an S3-compatible object store keyed by identifier (decision 15). But the third home — the **instance and integration layer** — was named and then deferred:

> the instance and integration layer — serials, ATECC bindings, the `-QP/-QR/-CP/-CC/-PR` records, and the integration identifier `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` that joins the two trees — is production data created at assembly and lives platform-side (IndustryFlow / `production_unit`, ADR-IF-0001), off the public repo. The precise platform host … remain[s] deferred to ADR-IF-0001 and Phase 2.
> — ADR-0017 (rev 1), Deferred decisions

That deferral is now blocking. Three concrete needs have arrived at once, and none can be met by the type registry (type-level, public, immutable-by-PR) or the object store (flat blobs, one object per identifier) alone:

1. **Serials must be issued and instances tracked.** ADR-0017 decision 8 assigns serials in Production and binds each to an ATECC608 via the `-PR` provisioning record (ADR-0007). Something must *allocate* the next serial per module+version, *hold* the serial↔ATECC binding, and answer "which instances of `E0002` exist, and where is each one?" A flat object store cannot allocate or query this; the repo must not carry per-operator production data.

2. **Integration is inherently mutable and relational.** The integration identifier is a *mutable cross-reference* re-assigned whenever an instance moves, is removed, or is replaced (ADR-0017 decision 13; ADR-0016 mobility). Answering "what is installed at `GBOX_0001` depth `010100` today, and what was there last month?" is a relational join over three moving parts (machine, position, instance) with history — the native shape of a database, not of a flat keyspace or a PR-reviewed markdown table.

3. **There is no cloud yet.** ADR-0020 establishes that IndustryFlow arrives only at roadmap **stage 11**; stages 1–10 (bring-up, survey, first cultivation) run with **no cloud sink**. Every ADR that routes instance/integration data "platform-side / to IndustryFlow" presupposes a platform that does not yet exist. The survey and first-cultivation work of ADR-0016/0020 will *produce* serialized instances, provisioning records, calibration certificates, and deployment-specific profiles during exactly the period when their designated home is absent.

This ADR gives the deferred instance/integration layer a concrete, buildable home for the pre-cloud period: a **compact, self-hostable, containerized ERP** — a small relational system of record for machines, instances, integration, lifecycle-document metadata, deployment-specific profiles, and purchased-part stock. It **operates single-tenant and private** — one operator, one deployment estate, no tenancy machinery — but its **data model is IndustryFlow-conformant**. IndustryFlow's core is **multitenant**; the ERP is therefore a *single-tenant instantiation of the multitenant core model*, not a model that ignores tenancy. The single-tenant estate is exactly *one tenant* of the IndustryFlow model, so stage-11 migration is a "tenant N" insertion, not a remodel (decision 16). Operating single-tenant (no isolation infrastructure, no shared hosting) and conforming to the multitenant schema are separate axes, and this ADR takes the first without abandoning the second.

The integration end-state is **core plus layer, not absorption.** IndustryFlow is the *independent, multitenant core* industrial-IoT platform (ADR-0001); IndustryGrow is *not standalone* — it is an additional **domain layer** on top of that core, contributing extensions to it (the `production_unit` entity, cultivation DSDL types, plugin interfaces; ADR-0001 decision 7). The glossary already draws this line: platform-foundational concerns are tagged `[F]` and move with IndustryFlow if the foundation is extracted; domain/CEA concerns are `[D]` and stay with IndustryGrow (`GLOSSARY.md`, scope tags). This ERP straddles both: its **foundational** part (generic instance tracking, provisioning, integration history) is the single-tenant realization of what becomes IndustryFlow's multitenant `production_unit`; its **domain** part (`GBOX` machines, cultivation profiles) is the IndustryGrow layer. At stage 11 the two *integrate* — the `[F]` part becomes one tenant in the multitenant core, the `[D]` part remains the grow layer over it — rather than one swallowing the other.

This ADR carries **decisions and rationale only**. Database engine, ERP product/framework, schema DDL, on-disk formats, container base image, and exact retention figures are implementation/BOM concerns per ADR-0000 and are not fixed here.

## Decision drivers

- **The deferred instance layer must be housed now, not at stage 11.** ADR-0020 already established that stages 1–10 need a durable sink because IndustryFlow does not yet exist; instance/integration/provisioning data is exactly such a sink's contents. Deferring further blocks the survey and first-cultivation campaigns that the pre-cloud stages exist to run.
- **Instance/integration data is mutable, relational, and private** — the opposite of what the repo (public, type-level, PR-gated) and the object store (flat, one-object-per-identifier, blob-shaped) are good at. It wants a small store with mutable history — realized (rev 1) as a document index over the object store, not a stretch of the two existing homes.
- **Single source of truth (ADR-0000) is non-negotiable.** The ERP must own facts *no other home owns* and must not mirror the type registry, the object store's blobs, the BOM's SKUs/prices, or IndustryFlow's operational/forensic trail. Each boundary is drawn explicitly below.
- **The stateless-edge and audit-authority boundaries hold.** Operational telemetry, firmware events, and the tamper-evident hash chain stay platform-side (ADR-0004 rev 1 decision 10; ADR-0020 decision 9). The ERP is an *asset/config/traceability* record, never a second forensic trail — the boundary ADR-0017 alternative E and ADR-0020 decision 9 already defend.
- **Core plus layer, not absorption, at stage 11.** IndustryFlow is the independent core; IndustryGrow is an additional domain layer over it (ADR-0001; `GLOSSARY.md` `[F]`/`[D]` tags). The ERP's foundational instance-tracking part is the private realization of what becomes IndustryFlow's `production_unit` (ADR-IF-0001); its grow-domain part stays the IndustryGrow layer. Its schema is a forward-compatible subset so the stage-11 transition is a data migration and a re-layering, not a rewrite.
- **IndustryFlow-conformant, single-tenant in operation.** IndustryFlow's core is multitenant; the ERP must conform to that model so integration is a migration, not a rewrite. But the pre-cloud period is one operator, so the ERP *operates* single-tenant — a single-tenant instantiation of the multitenant schema, with no tenancy-isolation machinery built now. Conformance (a schema property) and single-tenant operation (a deployment property) are separate; take the second without giving up the first.
- **Self-build path and open-core ethos (ADR-0001).** The first deployer and any community member must be able to stand this up from open artifacts. A single self-hostable container matches the self-build path (ADR-0001 decision 6); the *data* it holds is per-operator production data and is not published (ADR-0017).
- **Compact, not enterprise.** The need is a handful of related entities with history, not procurement/finance/HR. "ERP" here means the asset/inventory/traceability core only; scope creep into finance is a smell.

## Decision

### Positioning and lifecycle role

1. **A compact, self-hostable, containerized ERP is the pre-cloud system of record for the instance and integration layer.** It is the concrete host ADR-0017's "Registry and store location" deferred decision left open, for roadmap stages 1–10. It is a small relational application delivered as a single container that an operator (including the first deployer and community self-builders) runs on or beside the gateway host.

2. **Its role is lifecycle-dependent, mirroring ADR-0020's local-store framing.** Pre-cloud (stages 1–10) it is the **primary system of record** for machines, instances, integration, lifecycle-document metadata, deployment profiles, and SP stock. At stage 11+, IndustryFlow arrives as the **independent core** and IndustryGrow becomes an **additional layer over it** (ADR-0001): the ERP's **foundational** `[F]` part (generic instance tracking, provisioning binding, integration history) aligns to IndustryFlow's `production_unit` (ADR-IF-0001), and its **domain** `[D]` part (`GBOX` machines, cultivation profiles) remains the IndustryGrow layer over the core. This is *integration as core-plus-layer, not absorption* — IndustryFlow does not swallow and delete the ERP; the two re-layer. Before IndustryFlow exists, the ERP *is* the instance-layer record — the same "the local sink *is* the record before first sync" logic ADR-0020 decision 1 applies to telemetry.

3. **The schema is an IndustryFlow-conformant, forward-compatible subset of the multitenant `production_unit` model (ADR-IF-0001), split along the `[F]`/`[D]` line.** The entities and keys are shaped so that the stage-11 transition is a data migration and re-layering, not a re-modelling: the `[F]` foundational entities map onto IndustryFlow's multitenant `production_unit` as a single tenant (decision 16); the `[D]` domain entities stay in the IndustryGrow layer that plugs into the core. Where ADR-IF-0001's model is not yet fixed, the ERP follows the identifier grammar of ADR-0017 (which ADR-IF-0001 must also honour), so the two cannot diverge on the load-bearing keys.

### What the ERP owns (and only this)

4. **Machines and instances.** The ERP holds the enumerated `GBOX_NNNN` machines (ADR-0017 decision 6) and every manufactured E-instance `Exxxx-VVVVVV-NNNNNN`. **It is the serial-allocation authority** — it issues the next `NNNNNN` per module+version at production (ADR-0017 decisions 1, 8) and records the instance's existence. The type-level meaning of `Exxxx`/`VVVVVV` is *not* copied here; it is referenced from the repo type registry (`REGISTRY.md`, ADR-0000).

5. **Instance identity binding.** The ERP holds the serial↔ATECC608 binding — the structured content of the `-PR` provisioning record (ADR-0017 decision 12; ADR-0007): the public certificate metadata and the serial it belongs to. **Public material only**; the private key never leaves the ATECC608. The `-PR` *document blob* lives in the object store (decision 7 below); the ERP holds the queryable binding and the object key.

6. **Integration records (the mutable cross-reference).** The ERP holds the integration identifier `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` and, crucially, **its history**: it is re-assigned whenever an instance is moved, removed, or replaced (ADR-0017 decision 13), so the ERP records installs, removals, and replacements over time. This operationalizes ADR-0016's inventory-mobility model — "which instance sits where now, and where has this serial been" is a query, not an archaeology exercise. Depth `DDDDDD` is position-only and assigned here at integration; it is never written back into the type registry or the production identifier.

7. **Lifecycle-document index, not the documents.** For each instance the ERP indexes its `-QP/-QR/-CP/-CC/-PR` records — status, dates, calibration validity, and the **object-store key** of each blob — but the blobs themselves stay in the S3-compatible object store keyed by identifier (ADR-0017 decision 15). The ERP is the structured, queryable metadata layer *over* the object store; it does not duplicate blob content. "Show every instance whose calibration `-CC` expires this month" is an ERP query that resolves to object keys.

8. **Deployment-specific profiles (as unified versioned artifacts).** The ERP stores the **deployment-specific** cultivation profile versions — setpoints **and** identified model parameters serialized together as one unit (ADR-0016 decision 6), never split into a parallel subsystem — and the **deployment record**: which profile version is active on which `GBOX`. The reconciliation with ADR-0015 and ADR-0016 is decisions 12–13 below.

9. **Purchased-part (SP) stock and instances.** The ERP holds SP stock and location — how many `SP0003` supplies are on hand, which `SP0004` gateway (with its vendor serial / gateway identity, ADR-0019 decision 2) is installed at which machine. The **vendor-free spec** of each `SPxxxx` stays in the type registry (ADR-0019); the **live SKU and price** stay downstream in the BOM (ADR-0000). The ERP holds neither the spec nor the price — it holds *stock and placement*.

### Boundaries (what the ERP must NOT own)

10. **Not the operational/forensic trail.** Telemetry, firmware-flash events, control-decision audit, and the per-gateway tamper-evident hash chain stay platform-side (ADR-0004 rev 1 decision 10; ADR-0020 decision 9). The ERP records *assets and their configuration and history*, never the operational event stream — the two-sources-of-truth failure ADR-0017 alternative E and ADR-0020 decision 9 exist to prevent. Pre-cloud, ADR-0020's local store buffers operational telemetry; the ERP buffers nothing operational.

11. **Not the type registry, the object store's blobs, or the BOM's values.** The ERP references `Exxxx`/`SPxxxx` meaning (repo `REGISTRY.md`), lifecycle-document blobs (object store), and SKUs/prices (BOM) by key or reference; it never copies them. Every fact the ERP stores is one no other home already owns (ADR-0000 decision 3).

### Profiles — reconciliation with ADR-0015 and ADR-0016

12. **The gateway remains the single mutation channel; the ERP is the versioned store, not a second deploy path.** ADR-0015 decision 4 makes the gateway's `active-profile.json` the one channel that mutates the running control loop, and ADR-0020 decision 11 keeps it the cold-boot source of truth for control. This ADR does not touch that: a profile is *deployed* only through the gateway's single channel. The ERP is the durable, versioned **home the deployed version is pushed from** and the record of **which version is active where** — a store and a cache, not two mutation channels. Three non-overlapping roles: community registry = published *template* profiles (ADR-0001 decision 5, public); ERP = deployment-specific *instance* profile versions (production data, private); gateway `active-profile.json` = *cache* of the currently-active version. Template ≠ instance ≠ cache — the same type-vs-instance split ADR-0017 already draws, applied to profiles.

13. **Model parameters stay *in* the profile, versioned with it — the ERP does not split them out.** ADR-0016 alternative D rejected a model subsystem with its own versioning and a parallel audit trail. The ERP honours this: it stores the whole profile (setpoints + state-space matrices + Kalman gains + identification metadata) as one atomically-versioned artifact, so rollback covers everything together exactly as ADR-0016 decision 6 requires. The ERP is where deployment-specific profile *versions* durably live pre-cloud — the operator-side analytics environment ADR-0016 decision 2 emits new profile versions *into* — not a second versioning mechanism over the model.

### Licensing and data

14. **The ERP application is open-core (AGPL-3.0-or-later); the data it holds is operator-private production data.** The software is part of the open core so the self-build path (ADR-0001 decisions 1, 6) covers standing up the instance-layer store; the machines, serials, ATECC bindings, integration history, deployment profiles, and SP stock it holds are per-operator production data and are **not** published — the "off the public repo" property ADR-0017 assigns the instance/integration layer. Open code, private data.

### Deployment vehicle

15. **Delivery is a single self-hostable container over a compact document store that indexes the flat object-store warehouse (ADR-0017 decision 15); the concrete product is implementation.** The architectural commitments are: *containerized* (a drop-in the operator runs on or beside the gateway host, matching the self-build path), *single-node and compact* (asset/traceability core, not a finance/HR suite), and *a document metadata store with mutable history over time*. The last is the rev-1 change (see Revision history): the instance/integration layer is the *queryable index over the object store* (decision 7), and the project's storage paradigm is already document/blob-oriented — identifiers are object keys (ADR-0017 decision 15) — so the layer is a **document store, not a relational core**. Integration remains a mutable cross-reference over time (decision 6); in a document store that history is carried by validity-stamped records (an `installed`/`removed` timestamp pair) with a uniqueness constraint per position, not by a relational join engine. Which document database realizes this — the specific product — is an implementation choice per ADR-0000 (Deferred decisions, now **resolved to MongoDB** over the object-store warehouse), recorded with the implementation. This mirrors ADR-0020 fixing the storage *class* (SSD/NVMe) while leaving the SKU to the BOM.

### Tenancy and integration model

16. **Single-tenant in operation, multitenant-conformant in schema.** IndustryFlow's core is multitenant, and this ADR must be IndustryFlow-conformant (integration is core-plus-layer, decision 2). Two separate axes:
    - **Schema (conform now).** The ERP's data model is an IndustryFlow-conformant schema — foundational `[F]` entities are keyed and shaped exactly as a *single-tenant instantiation* of the multitenant core model. Every foundational record belongs, at least implicitly, to one tenant (this operator), so migrating the estate into the multitenant core at stage 11 is inserting it as "tenant N", not remodelling. The ERP does not invent a keying or entity shape that the multitenant core cannot ingest.
    - **Operation (single-tenant now).** The running instance serves one operator with **no tenancy machinery** — no tenant isolation, no per-tenant partitioning at runtime, no shared-hosting surface. That machinery is a core-platform `[F]` concern supplied by IndustryFlow when it arrives; building it in this domain-layer bring-up store would be premature (there is one tenant) and would duplicate what the core will provide.

    So the ERP conforms to multitenancy in its model while declining to *operate* multitenant. Should a second operator appear before stage 11, the answer is a second private single-tenant instance (each still conformant), not runtime tenancy bolted into this store — or, if it is material enough, pulling the IndustryFlow-core timeline forward (Deferred decisions).

## Alternatives considered

**A. Do not build an ERP; wait for IndustryFlow at stage 11.** *Rejected:* this is ADR-0020 alternative A/B by another name for the instance layer. Serials must be issued, provisioning records bound, and integration tracked *during* stages 1–10 — the survey and first-cultivation campaigns (ADR-0016, ADR-0020) produce exactly these records with no cloud present. The end-state home being IndustryFlow does not remove the bring-up-era need for a home.

**B. Stand up full IndustryFlow now instead of a compact ERP.** *Rejected:* IndustryFlow is a heavyweight telemetry/audit platform that does not yet exist end-to-end (ADR-0001 notes IndustryGrow is its first real deployment; ADR-0020 puts it at stage 11). A compact single-container asset store is the right-sized bring-up realization of just the instance-layer subset, designed (decision 3) to migrate into IndustryFlow when it arrives — not a premature build-out of the whole platform.

**C. Track instances in the repo (extend `REGISTRY.md`) or in spreadsheets.** *Rejected:* the type registry is deliberately type-level, public, and PR-gated (ADR-0017; `REGISTRY.md` header). Instance/integration data is per-operator, private, mutable, high-churn, and relational-with-history — it would corrupt the type registry's scope (an act of the dispersion ADR-0000 opposes) and cannot express the mobility joins of decision 6. Spreadsheets give no serial-allocation authority, no referential integrity, and no history.

**D. Let the ERP also hold operational telemetry / an audit trail.** *Rejected:* ADR-0004 rev 1 decision 10 and ADR-0020 decision 9 route operational events and the tamper-evident hash chain platform-side; ADR-0017 alternative E rejects a second forensic record of the same events. The ERP is asset/config/traceability only (decision 10). Pre-cloud operational buffering is ADR-0020's local store, a separate concern with separate durability semantics.

**E. Store profiles as a separate subsystem with independent versioning, or split model parameters out of the profile.** *Rejected:* exactly ADR-0016 alternative D. The ERP stores the whole profile as one atomically-versioned artifact (decision 13) and is not a second mutation channel (decision 12); the gateway's single-mutation-channel (ADR-0015 decision 4) is untouched.

**F. Make the ERP the source of truth for SP specs and/or SKUs and prices.** *Rejected:* the vendor-free SP spec is the type registry's (ADR-0019); the live SKU and price are the BOM's (ADR-0000). The ERP holds SP *stock and placement* only (decision 9) — the fact no other home owns.

**G. Build a bespoke instance schema unaligned with `production_unit`.** *Rejected:* decision 3 requires IndustryFlow conformance so stage-11 integration is a migration. A schema that ignored ADR-0017's identifier grammar and ADR-IF-0001's multitenant `production_unit` shape would force a rewrite at the exact moment the data becomes most valuable.

**H. Adopt a full enterprise ERP (finance/HR/procurement suite).** *Rejected:* the need is a handful of related entities with history (decision 15). A heavyweight suite contradicts the compact, self-hostable, self-build positioning and buys modules the project does not need. "Compact" is load-bearing.

**I. Build multitenant *operation* into the ERP now.** *Rejected — but note the schema still conforms (decision 16).* Runtime tenancy (isolation, per-tenant partitioning, shared hosting) is unused with one operator and is a foundational `[F]` concern the IndustryFlow core supplies, not this `[D]` domain store; building it now duplicates the core and blurs the core/layer line (decision 2). This rejects tenancy *machinery*, not tenancy *conformance*: the ERP's schema is a single-tenant instantiation of the multitenant core model, so it ingests cleanly at stage 11.

**K. Build a schema that ignores tenancy entirely (non-conformant single-tenant model).** *Rejected:* IndustryFlow is multitenant and this ADR must be IndustryFlow-conformant. A model with no tenant dimension would force a remodel at stage 11, defeating the migration-not-rewrite goal (decisions 2, 3). Conform to the multitenant model *and* operate single-tenant — do not conflate the two axes (decision 16).

**J. Treat stage 11 as IndustryFlow absorbing and retiring the ERP.** *Rejected:* IndustryGrow is not standalone but also is not disposable — it is an additional domain layer over an independent core (ADR-0001; `GLOSSARY.md` `[F]`/`[D]`). At stage 11 the foundational `[F]` data aligns to `production_unit` in the core while the grow-domain `[D]` layer remains over it (decisions 2, 3). Absorption would erase the layered architecture the project is built on.

## Consequences

### Positive

- **The instance/integration layer finally has a home**, unblocking serial allocation, provisioning binding, and integration tracking for the stage 1–10 survey and first-cultivation work that produces those records (ADR-0016, ADR-0020).
- **Mobility becomes a query, not archaeology.** ADR-0016's inventory model — an instance moving between positions and deployments — is answered by relational history (decision 6), which is what the model always needed and what neither the flat store nor the repo could give.
- **Single source of truth is preserved by construction.** Each boundary (decisions 10–11) leaves the ERP owning only facts no other home owns; type meaning, blobs, SKUs, and the forensic trail stay where they already live.
- **Stage-11 transition is a migration and re-layering, not a rewrite**, because the schema is an IndustryFlow-conformant subset of the multitenant `production_unit` model and integrates as core-plus-layer (decisions 2, 3).
- **Simple to run now, ready to scale later.** Operating single-tenant keeps the bring-up store trivial (no tenancy machinery), while the conformant schema (decision 16) means the estate becomes "tenant N" in the multitenant core without a remodel — both benefits at once.
- **Self-builders can run it**, matching the open-core self-build path; the data stays private (decision 14).
- **The object store keeps its clean flat-blob role**; the ERP adds the queryable index over it (decision 7) without duplicating blob content.

### Negative

- **A new store to operate and back up.** The instance layer becomes a critical, mutable asset — losing it loses serial-allocation state, integration history, and deployment profiles. Backup/restore and (per decision 14) private-data handling must be specified before first production use. ADR-0017 already flagged the *type* registry as a critical asset; this adds a *second*, higher-churn one.
- **A migration is owed at stage 11, and conformance must be carried before it pays off.** The IndustryFlow-conformant-subset promise (decisions 3, 16) is only as good as ADR-IF-0001's eventual multitenant `production_unit` model, which is not yet fixed. The schema must carry the tenant dimension from day one even though a single-tenant deployment never exercises it — an up-front cost paid for a later benefit, and a cross-ADR dependency if the core model diverges from ADR-0017's identifier grammar.
- **Boundary discipline must be held continuously.** The temptation to copy a type meaning, a SKU, or a telemetry sample into the ERP "for convenience" is exactly the drift ADR-0000 warns of; the boundaries (decisions 10–11) must be enforced in review, not just stated here.
- **Profile roles must be taught.** Three profile homes (template/instance/cache, decision 12) is more nuance than "profiles live in one place"; an operator who mutates a profile directly in the ERP expecting it to reach the plant would be surprised — deployment is still the gateway's single channel.
- **Compact-scope must be defended.** "ERP" invites finance/procurement/HR creep (alternative H); keeping it to the asset/traceability core is an ongoing judgment.

## Relationship to other ADRs

- **ADR-0000** — the ERP owns only facts no other home owns; type meaning, blobs, SKUs/prices, and the forensic trail are referenced, never mirrored.
- **ADR-0001** — open-core self-build path: the ERP application is AGPL open core (decision 14), the data is per-operator private.
- **ADR-0004 (rev 1)** — the operational/forensic boundary: the ERP is not a second audit trail (decisions 10; ADR-0004 decision 10).
- **ADR-0007** — the serial↔ATECC binding the ERP holds *is* the structured `-PR` provisioning content (decision 5).
- **ADR-0015** — the gateway's single mutation channel is untouched; the ERP is the versioned profile store, not a deploy path (decision 12).
- **ADR-0016 (rev 1)** — the ERP is where deployment-specific profile versions (setpoints + model, one artifact) durably live pre-cloud, and it operationalizes the inventory-mobility model as integration history (decisions 6, 8, 13).
- **ADR-0017 (rev 1)** — this ADR realizes its deferred "Registry and store location" instance/integration host; it holds serials, ATECC bindings, the lifecycle-document index, and the integration identifier, referencing the repo type registry and the object store.
- **ADR-0019** — the ERP holds SP stock and placement; the SP spec stays in the type registry and the SKU/price in the BOM (decision 9).
- **ADR-0020** — lifecycle-staged, pre-cloud-primary / post-cloud-integrated-as-a-layer framing (decision 2); the ERP is asset state, distinct from ADR-0020's operational buffer.
- **ADR-IF-0001 (planned)** — the `production_unit` entity in the IndustryFlow core that the ERP's foundational `[F]` part aligns to at stage 11; integration is core-plus-layer, not absorption (decisions 2, 3).
- **`GLOSSARY.md`** — the `[F]` (platform-foundational) / `[D]` (domain/CEA) scope tags that draw the core/layer line this ADR's integration model (decisions 2, 3, 16) follows.

## Deferred decisions

- **ERP product / framework / database engine — resolved (rev 1).** A compact, open-core (AGPL-3.0-or-later) bespoke application over **MongoDB** as the document metadata store, with the existing S3-compatible object store as the blob **warehouse** (ADR-0017 decision 15). Rationale: the layer is a queryable index over a document/blob store (decision 7), so a document database matches the paradigm rather than importing the relational core's shape (decision 2). It carries the ADR's obligations natively — gap-free serial allocation (decision 4) is an atomic counter update per module+version; integration history (decision 6) is validity-stamped documents (`installed_at`/`removed_at`) with a per-position uniqueness constraint; a deployment profile version (decisions 8, 13) is one whole document (setpoints + model), never split; the lifecycle-document index (decision 7) holds metadata plus the warehouse object key, never the blob. The `[F]`/`[D]` split (decisions 2, 3, 16) is the collection-group boundary (`foundation.*` carrying a constant tenant field; `domain.*` referencing it). The specific application framework, driver, and container image remain implementation detail per ADR-0000. *Still open:* serial-allocation concurrency/offline behaviour (below) and the stage-11 document→`production_unit` export-transform.
- **Schema DDL and the exact `production_unit`-subset mapping.** The concrete entities, keys, and the migration path to ADR-IF-0001 (decision 3) — pending ADR-IF-0001's model.
- **Serial-allocation concurrency and offline behaviour.** How the ERP issues gap-free serials per module+version (decision 4) under multiple production stations and while offline. Needs specification before production use. **Partially resolved by ADR-0022 (decision 4):** the pre-cloud allocator is single-writer (one issuing station); gap-free multi-station/offline allocation remains deferred there.
- **Backup, restore, and private-data handling** for the instance layer (decision 14; Negative consequences) — the critical-asset protection ADR-0017 flags for the type registry, now owed for this higher-churn store.
- **ERP ↔ object store integration** — how lifecycle-document blobs are written to the S3 store and their keys recorded in the ERP index (decision 7): who writes first, referential integrity across the two stores. **Resolved by ADR-0022 (decision 7):** blob to the warehouse first, then the ERP records the key; the ingestion surface is allowlisted to instance-lifecycle suffixes `{QP,QR,CP,CC,PR}`. (Presigned-upload vs. proxy is left open in ADR-0022's Deferred decisions.)
- **ERP ↔ gateway profile push** — the concrete mechanism by which a stored profile version becomes the gateway's `active-profile.json` (decision 12), consistent with ADR-0015's single mutation channel. **Resolved by ADR-0022 (decision 8):** the ERP stores versions and records which is active; the gateway *pulls* the active version over its mTLS channel — there is no push/deploy endpoint, so ADR-0015's single mutation channel is preserved.
- **Re-layering shape at stage 11** (decisions 2, 3) — the concrete split of the ERP's `[F]` foundational data (migrating into IndustryFlow's `production_unit` core) from its `[D]` domain data (remaining the IndustryGrow layer), and whether the ERP UI survives as an operator-side console over the core or is retired in favour of IndustryFlow's own. Decide with ADR-IF-0001.
- **Multi-operator before stage 11** (decision 16) — confirm the "second private single-tenant instance, not multitenancy" answer holds if a second operator appears early, or whether that event should instead pull the IndustryFlow-core timeline forward.

## References

- ADR-0000: Decision records and single-source-of-truth discipline.
- ADR-0001 (rev 1): IndustryGrow framing — open-core, self-build path, community profile registry, roadmap stages.
- ADR-0004 (rev 1): Gateway host hardening — stateless edge, platform-side hash chain (audit-authority boundary).
- ADR-0007: PKI and secure-element identity — ATECC608 binding, provisioning.
- ADR-0015: Gateway profile caching and local control loops — `active-profile.json`, single mutation channel.
- ADR-0016 (rev 1): Empirical survey and state-space modeling — deployment-specific profiles/models, inventory mobility.
- ADR-0017 (rev 1): Component, document, and instance identification — two-axis model, deferred instance/integration layer host, object store.
- ADR-0019: Purchased-part (SP) identification — SP spec in registry, SKU/price in BOM.
- ADR-0020: Gateway persistence model — lifecycle-staged store, pre-cloud/post-cloud roles, IndustryFlow at stage 11.
- ADR-IF-0001 (planned): IndustryFlow multitenant `production_unit` entity — the stage-11 core the ERP's foundational data aligns to as one tenant.
- `GLOSSARY.md`: `[F]`/`[D]` scope tags — the core/layer line.

---

## Reviewer notes

Points flagged for human resolution or marked open, gathered for quick triage:

1. **Forward-compatibility dependency on ADR-IF-0001** (decision 3; Negative consequences). The "migration, not rewrite" promise is only as strong as `production_unit`'s eventual model, which is not yet fixed. Confirm the ERP should track ADR-0017's identifier grammar as the stable anchor until ADR-IF-0001 lands.
2. **Serial-allocation authority in the ERP** (decision 4; Deferred decisions). The ERP as the gap-free serial issuer per module+version is proposed here; confirm this is the right home vs. a separate allocator, and specify offline/multi-station behaviour before production use.
3. **Profile store in the ERP** (decisions 8, 12–13). Confirmed with the user as "also a profile store," reconciled as *store + deployment record*, not a second mutation channel, with model-in-profile preserved (ADR-0015 / ADR-0016 alt D). Flagged so the reconciliation is reviewed against those ADRs explicitly.
4. **Re-layering shape at stage 11** (decisions 2, 3; Deferred decisions). Open: what exactly migrates (the `[F]` foundational data, as one tenant) vs. stays the `[D]` layer, and whether the ERP UI survives as an operator console over the core or is retired. Not absorption — core-plus-layer. Decide with ADR-IF-0001.
5. **Multitenant conformance vs. single-tenant operation** (decisions 3, 16). Confirmed with the user: IndustryFlow is multitenant, so the ERP schema must be IndustryFlow-conformant (a single-tenant instantiation of the multitenant model) while the running instance operates single-tenant with no tenancy machinery. Flagged so the schema work explicitly carries the tenant dimension even though nothing exercises it yet.
6. **ADR number / IF-vs-IG scope — resolved.** Filed as an IndustryGrow **ADR-0021**: this is a domain-layer, pre-cloud bring-up decision, the same pattern as ADR-0001/0014/0017 (all IG records that reference the planned IF-side ADR-IF-0001). The end-state core entity remains **ADR-IF-0001** in the IndustryFlow repo; no ADR-IF record is created here. The previously-implicit `ADR-IF-`(IndustryFlow) vs. `ADR-`(IndustryGrow) namespace convention is now a recorded decision — **ADR-0000 (rev 1) decision 8** — with `GLOSSARY.md` binding the tokens and ADR-0001 as origin of the core/layer split; this closes the ADR-0000 "unwritten discipline" gap that made the scope feel open.
