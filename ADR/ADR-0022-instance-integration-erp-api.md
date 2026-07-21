<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0022: Instance-and-integration ERP — the machine- and operator-facing API

- **ID:** ADR-0022
- **Status:** Proposed
- **Date:** 2026-07-19
- **Project:** IndustryGrow
- **Parent:** ADR-0021
- **Companions:** ADR-0000, ADR-0004 (rev 1), ADR-0007, ADR-0015, ADR-0016 (rev 1), ADR-0017 (rev 1), ADR-0019, ADR-0020
- **Realizes:** ADR-0021 deferred decisions *"ERP ↔ object store integration"* and *"ERP ↔ gateway profile push"*
- **Clarified by:** ADR-0023 (decision 1's type-meaning exclusion — the read-through catalog)
- **Relates to:** ADR-IF-0001 (planned) — the `production_unit` core whose API this one's foundational operations align to at stage 11

## Context and problem

ADR-0021 (rev 1) fixes the ERP's role, ownership, and boundaries and the store engine (MongoDB over the flat object-store warehouse), but it deliberately defers *how the ERP is talked to*. Two of its deferred decisions are, in substance, API-contract questions:

- **ERP ↔ object store integration** — who writes a lifecycle-document blob first, and how its key is recorded in the ERP index with referential integrity across the two stores.
- **ERP ↔ gateway profile push** — the concrete mechanism by which a stored profile version reaches the gateway's `active-profile.json`, consistent with ADR-0015's single mutation channel.

Both are resolved only by deciding the API. Serial allocation, provisioning binding, integration tracking, and SP stock likewise need an external interface: production stations issue serials and bind ATECC records, an operator installs and moves instances, and the gateway obtains its active profile version. This ADR decides the **shape, auth model, and hard boundaries** of that interface. Concrete route strings, request/response schemas, and the OpenAPI document are implementation per ADR-0000; this record carries decisions and rationale.

The API has two distinct caller classes with very different standing against the existing PKI: **machine callers** (the gateway, which already holds an ATECC608-bound X.509 client certificate under ADR-0007) and **human/tooling callers** (operators, provisioning stations), which have no hardware identity anchor. Conflating them is the central design error this ADR exists to prevent.

## Decision drivers

- **Resolve ADR-0021's two deferred API-shaped decisions** without re-opening its ownership or boundary decisions.
- **Reuse the PKI that already exists (ADR-0007).** The gateway is a machine caller with a first-class, hardware-anchored credential; not using it would be drift, not simplicity.
- **The boundaries ADR-0021 draws must be un-representable in the API, not merely undocumented.** A missing route is not a boundary; an absent capability plus an explicit rejection is.
- **Single mutation channel (ADR-0015).** The gateway's `active-profile.json` is the only thing that mutates a running control loop; the API must be a store and a record, never a second deploy path.
- **Single source of truth (ADR-0000).** The API references type meaning, SKUs, and blobs; it never accepts writes of them.
- **Forward-shaped for stage 11.** Auth and request semantics must map onto IndustryFlow's model (ADR-0004 / ADR-IF-0001) so the transition is a swap of validation, not a redesign.

## Decision

### Surface and resource model

1. **The API exposes exactly the entities ADR-0021 owns, and only those:** machines (`GBOX_NNNN`), E-instances and the serial allocator, provisioning bindings (`-PR`), integration records, the lifecycle-document index, deployment profiles and their active-version records, and SP stock/placement. There is no resource for type meaning, telemetry, an audit trail, SKUs/prices, or community template profiles. It is served by the same single container as the ERP (ADR-0021 decision 15); its OpenAPI document is generated from the implementation.

   *Clarified 2026-07-21, while still Proposed (ADR-0000 decision 5), by ADR-0023:* what the type-meaning exclusion forbids is a resource the ERP **owns** — a place where a designation is stored, edited, or authored. It does not forbid **reading the registry through** the API. A single read-only catalog route serves `REGISTRY.md` exactly as the ERP parses it, holding nothing and deciding nothing (ADR-0023 decisions 1, 4); it is the read-side counterpart of decision 9's prohibition on type-meaning writes. The route exists so that consumers stop carrying their own copy of the registry, which is the outcome the exclusion was written to protect — a caller that cannot read the registry through the API keeps a table instead, and the boundary is lost in the place it was meant to hold.

### Authentication — two caller classes, two mechanisms

2. **Gateway machine callers authenticate by mTLS using the ADR-0007 PKI, from day one.** The gateway already holds an ATECC608-bound X.509 client certificate; the API validates it against the operator's trust root and derives the machine identity (`GBOX_NNNN`) **from the verified certificate, never from a request parameter**. No bearer token is issued to or accepted from a gateway. The single-tenant wrinkle (ADR-0021 decision 16) removes per-tenant *routing*, not certificate *validation*: there is one operator root, but the certificate is still verified and the identity still extracted.

3. **Human and provisioning-tooling callers authenticate with a scoped operator token — interim, and explicitly a stage-11 migration target.** These callers have no hardware anchor and there is no pre-cloud JWT infrastructure (that is IndustryFlow's, ADR-0004). A token is acceptable **only** if it is *scoped per caller role* (provisioning station vs. operator vs. read-only), so the ERP can attribute who allocated a serial or recorded an activation, and if its subject/role is shaped to map onto a future JWT claim (stage 11 replaces validation, not request semantics). **A single static secret shared across all callers — and especially one shared with gateways — is rejected** (decision 2; alternatives B, C).

### Serial allocation (authoritative)

4. **Serials are issued by the ERP, never supplied by the client.** Allocation is gap-free per `(Exxxx, VVVVVV)` and atomic (a counter document updated in one operation; ADR-0021 decision 4; ADR-0017 decisions 1, 8). The allocation response is final. **Pre-cloud the allocator is single-writer (one issuing station);** gap-free multi-station/offline allocation is deferred (below), and until decided the API must not present a multi-writer guarantee it does not keep.

### Provisioning binding

5. **Provisioning binds a serial to its ATECC608 certificate and writes the structured `-PR` record — public material only.** The binding request carries certificate metadata (public key fingerprint, cert serial, validity), never a private key (which never leaves the ATECC608, ADR-0007). The blob of the `-PR` document follows the lifecycle-document flow (decision 7); the ERP holds the queryable binding and the object key.

### Integration operations

6. **Install / move / remove / replace are the integration operations, and the depth code `DDDDDD` is assigned here at integration.** Exactly one current instance may occupy a `(machine, depth)` position; history is preserved (ADR-0021 decision 6; ADR-0017 decision 13). The depth code is **never** written onto the instance record or back into any type registry (ADR-0017 decision 7); the integration identifier `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` is returned only as a derived view of the current placement.

### Lifecycle-document ingestion (index over the warehouse)

7. **The document-ingestion surface is scoped to the instance-lifecycle suffix allowlist `{QP, QR, CP, CC, PR}` on instance identifiers `Exxxx-VVVVVV-NNNNNN`, and that scope is exhaustive.** The blob is written to the object-store warehouse *first*, then its key and metadata (type, status, dates, calibration validity) are recorded in the ERP index (referential integrity — a recorded key always resolves; ADR-0021 decision 7). **The ERP never returns blob content**; it returns the object-store key or a time-limited retrieval URL. Type-layer documents — `-S/-D/-L/-P/-M/-I`, the `-D-fab.zip` fabrication package (ADR-0017 decision 18), SP documents and designed-accessory rollups (`SPxxxx-<layer>`, `<parent>-D-<slug>`, ADR-0019 decisions 8–9) — are **not** acceptable through this API; they are repo/`store_sync` artifacts. The allowlist (not a blocklist) is what makes that boundary hold as the type layer grows.

### Profiles — store and record, never a deploy path

8. **The API stores deployment-specific profile versions and records which version is active on which `GBOX`; it does not deploy.** A profile version is stored as one whole artifact (setpoints + model, atomically; ADR-0021 decision 13; ADR-0016 alternative D) — there is **no** operation that writes model parameters separately. The active-version relationship is a **record write**, not a push: **there is no deploy/push/activate-on-gateway endpoint** (ADR-0015 decision 4; ADR-0021 decision 12). The gateway obtains its active profile version by **pulling** it over its mTLS channel (decision 2); the ERP is where that version is pulled *from* and the record of *which is active where* — a store and a cache, not a second mutation channel.

### Boundaries as hard, un-representable constraints

9. **The following are architectural absences, enforced (absent routes, documented rejections, allowlists), not merely omitted:**
   - **No telemetry / operational / firmware-flash / audit intake** — that stream stays platform-side (ADR-0021 decision 10; ADR-0004 rev 1 decision 10; ADR-0020 decision 9). Unrecognised paths under an operational namespace are rejected, not silently absorbed.
   - **No type-meaning writes** — `Exxxx`/`SPxxxx` are foreign keys into `REGISTRY.md`; the API accepts no description/spec/meaning for them (ADR-0021 decision 11; ADR-0000 decision 3).
   - **No SKU/price/purchase-order writes** — those live in the BOM (ADR-0021 decision 9); SP resources carry stock and placement only. The instance key for a tracked purchased part is its **vendor serial**, never a project `NNNNNN` (ADR-0019 decision 2).

### Tenancy and stage-11 disposition

10. **The API operates single-tenant with no tenancy routing, but carries the tenant dimension implicitly** (the operator root for mTLS callers, the token scope for others), so stage-11 adds tenancy without redesigning requests (ADR-0021 decision 16).

11. **At stage 11 the foundational `[F]` operations (instances, provisioning, integration) align to IndustryFlow's `production_unit` API; the domain `[D]` operations (`GBOX`, profiles) remain the IndustryGrow layer API.** Gateway mTLS is already conformant; the operator token migrates to JWT validation against IndustryFlow's auth service. The concrete re-layering is decided with ADR-IF-0001 (Deferred decisions).

## Alternatives considered

**A. Amend ADR-0021 instead of a new record.** *Rejected:* an API contract is new material — its own auth model, endpoint semantics, and rejected alternatives — and it *instantiates* two of ADR-0021's deferred decisions rather than qualifying a recorded one. ADR-0000 decision 5's in-place-amendment vehicle is for bounded qualifications, not for a decision surface with its own stage-11 lifecycle.

**B. One shared static bearer token for all callers, gateways included.** *Rejected:* fuses machine and human auth, makes a hardware-anchored gateway present as an anonymous token holder, loses the gateway identity the deployment record depends on, and manufactures stage-11 re-hardening debt. A copyable shared secret defeats the hardware-identity posture ADR-0007 establishes.

**C. Defer gateway mTLS; use a token now, migrate later.** *Rejected:* the PKI already exists, so mTLS for the gateway is zero new infrastructure. Deferring it is drift with no payoff and a migration owed at stage 11 that need not exist.

**D. A profile deploy/push (or gateway-activate) endpoint.** *Rejected:* it is a second mutation channel, which ADR-0015 decision 4 forbids. The gateway pulls; the ERP records. The only permitted effect of an active-version write is a record.

**E. Accept any document for an instance (a blocklist of forbidden types).** *Rejected:* as the type layer grows (ADR-0017 decision 18; ADR-0019 decisions 8–9), a blocklist silently lets new type-level kinds route through the ERP. An exhaustive `{QP,QR,CP,CC,PR}` allowlist keeps the ERP the instance-layer index only (ADR-0021 decision 11).

**F. Store model parameters separately from setpoints.** *Rejected:* exactly ADR-0016 alternative D / ADR-0021 decision 13 — a profile version is one atomically-versioned artifact.

**G. Accept client-supplied serials.** *Rejected:* the ERP is the serial-allocation authority (ADR-0021 decision 4); a client-chosen serial cannot be gap-free-guaranteed and forfeits the authority.

## Consequences

### Positive

- **ADR-0021's two API-shaped deferred decisions are resolved** by one record with its own drivers and alternatives.
- **The gateway's existing hardware identity is reused,** so the deployment record is authentic and there is no gateway auth to migrate at stage 11.
- **The boundaries hold by construction** — no deploy path, no telemetry, no type-meaning/SKU, allowlisted documents — rather than by reviewer vigilance alone.
- **The single mutation channel (ADR-0015) is untouched;** the profile API is a store and a record.
- **Stage 11 is a swap, not a redesign:** operator-token validation → JWT, foundational ops → `production_unit`, gateway mTLS already conformant.

### Negative

- **mTLS termination and per-operator CA validation are needed even pre-cloud** for gateway callers — real infrastructure, justified because the PKI already exists.
- **The scoped operator token is interim debt** with a migration owed at stage 11; its claim shape must be carried carefully so the swap stays a swap.
- **Offline/multi-station serial allocation remains open** (below); the API must not over-promise concurrency until it is decided.
- **The document allowlist must be maintained** as the instance-layer document set evolves; adding a suffix is an ADR-0017 change, not an API convenience.

## Relationship to other ADRs

- **ADR-0021** — this API is the external interface to exactly what ADR-0021 owns; it resolves that ADR's object-store-integration and profile-push deferred decisions.
- **ADR-0015** — the gateway's single mutation channel is preserved; the profile API is store-and-record, not a deploy path (decision 8).
- **ADR-0007 / ADR-0004** — gateway mTLS uses the ATECC-anchored PKI; human/tooling auth is a scoped interim token shaped toward the JWT model (decisions 2, 3).
- **ADR-0017 / ADR-0019** — the API speaks the identifier grammar precisely and assigns depth at integration; the document allowlist keeps type-layer artifacts (incl. `-D-fab.zip`, SP docs, accessories) out of the ERP (decisions 6, 7).
- **ADR-0020** — operational buffering is the gateway's local store, never the ERP API (decision 9).
- **ADR-0023** — the type registry is read through this API, never restated by it; it clarifies decision 1's exclusion and supplies decision 9's read-side counterpart.
- **ADR-0000** — decisions and rationale only; route strings, schemas, and the OpenAPI document are implementation.
- **ADR-IF-0001 (planned)** — the `production_unit` core API the foundational operations align to at stage 11 (decision 11).

## Deferred decisions

- **Offline / multi-station serial-allocation concurrency** (shared with ADR-0021 reviewer note 2) — the pre-cloud single-writer position (decision 4) must be lifted here before a second issuing station exists.
- **Blob upload mechanism** — whether the ERP proxies the lifecycle-document blob to the object store or issues a pre-signed upload URL (decision 7), and the exact write-ordering/cleanup for referential integrity.
- **Operator-token → JWT claim mapping** — the concrete subject/role shape (decision 3) that stage 11 swaps validation for.
- **API versioning and the OpenAPI contract** — implementation per ADR-0000.
- **Stage-11 re-layering of the API** (decision 11) — decided with ADR-IF-0001.

## References

- ADR-0000, ADR-0004 (rev 1), ADR-0007, ADR-0015, ADR-0016 (rev 1), ADR-0017 (rev 1), ADR-0019, ADR-0020, ADR-0021, ADR-IF-0001 (planned), `GLOSSARY.md`.
