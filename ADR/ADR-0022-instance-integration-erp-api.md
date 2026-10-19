<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0022 — Machine- and operator-facing API of the instance-and-integration ERP

- **Status:** Accepted
- **Date:** 2026-07-21; changed 2026-10-19
- **Parent:** [ADR-0021](ADR-0021-instance-and-integration-erp.md)
- **Related:** [ADR-0004](ADR-0004-gateway-host-hardening.md) (platform-side audit and the stage-11 auth model), [ADR-0007](ADR-0007-pki-and-secure-element-identity.md) (operator PKI the gateway channel authenticates against), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (single mutation channel the profile routes preserve), [ADR-0016](ADR-0016-empirical-survey-and-state-space-modeling.md) (profile as one artifact), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (identifier grammar parsed here; lifecycle suffixes), [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (SP identity and type-layer document kinds), [ADR-0020](ADR-0020-gateway-persistence-model.md) (operational buffer that stays on the gateway), [ADR-0023](ADR-0023-machine-readable-type-registry.md) (type registry served read-through), [ADR-0025](ADR-0025-cultivation-profile-signing.md) (signed profiles the pull carries), [ADR-0026](ADR-0026-erp-backup-and-restore.md) (backup that inverts decision 7's write order), [ADR-0029](ADR-0029-node-firmware-update-and-bootloader.md) (firmware path decision 14 serves), ADR-IF-0001 (planned `production_unit` API the `[F]` operations align to)

## Context

ADR-0021 fixes what the ERP owns and where its boundaries lie, and leaves open how it is reached: who writes a lifecycle blob first and how its key is indexed, how a stored profile version reaches the gateway, and how serial allocation, provisioning, integration, stock, machine identity and firmware intent are driven from outside.

The API has two caller classes with different standing. Machine callers — gateways — hold an X.509 client certificate under the operator's PKI (ADR-0007 dec 4, 11). Human and tooling callers — operators and provisioning stations — have no hardware anchor and, pre-cloud, no JWT infrastructure. An operator records decisions; a machine reporting what it did is operational intake, which ADR-0021 dec 10 excludes.

A boundary that exists only as a missing route is undocumented, not enforced. The boundaries of ADR-0021 must be un-representable in the API: an absent capability plus an explicit rejection.

## Decision

### Surface and resource model

1. **The API exposes exactly the entities ADR-0021 owns, and only those.** There is no resource for type meaning, telemetry, an audit trail, SKUs or prices, or community template profiles; the API is served by the ERP's single container and its OpenAPI document is generated from the implementation. The exclusion is of resources the ERP owns: a read-only, storing-nothing read of the type registry (ADR-0023 dec 1, 4) and of a type-layer document the repository owns, through decision 7's forms, is permitted, because denying the read pushes callers into keeping their own copies.

### Authentication

2. **Gateways authenticate by mTLS against the operator's trust root, and the machine identity is derived from the verified certificate, never from a request parameter.** No bearer token is issued to or accepted from a gateway. Single tenancy removes per-tenant routing, not certificate validation.
3. **Human and provisioning-tooling callers authenticate with an operator token scoped per role, as an interim stage-11 migration target.** Role scoping (provisioning station, operator, read-only) attributes who allocated a serial or recorded an activation, and the subject and role are shaped to map onto a future JWT claim. A single static secret shared across callers is rejected.

### Serial allocation

4. **Serials are issued by the ERP, never supplied by the client, gap-free per module and version and atomically.** The allocation response is final (ADR-0021 dec 4). Pre-cloud the allocator is single-writer, and the API presents no multi-writer guarantee.

### Provisioning binding

5. **Provisioning binds an E-instance serial to its secure-element certificate and writes the structured `-PR` record, public material only.** The request carries certificate metadata, never a private key (ADR-0007); the `-PR` blob follows decision 7. A machine's binding is decision 12's.

### Integration operations

6. **Install, move, remove and replace are the integration operations, and the depth code is assigned here.** Exactly one current instance occupies a machine position and history is preserved (ADR-0021 dec 6). The depth code is never written onto the instance record; the integration identifier is returned only as a derived view of the current placement (ADR-0017 dec 7, 13).

### Lifecycle-document ingestion

7. **Ingestion is scoped to the instance-lifecycle suffix allowlist on E-instance identifiers, and that scope is exhaustive.** The blob is written to the warehouse first, then its key and metadata are indexed, so a recorded key always resolves (ADR-0021 dec 7). The ERP stores no blob content and answers with the object key, a time-limited retrieval URL, or the bytes read through from the warehouse; type-layer documents (ADR-0017 dec 18; ADR-0019 dec 8–9) are not accepted.

### Profiles

8. **The API stores deployment profile versions and records which is active on which machine; it does not deploy, and the gateway's pull is a pure read.** A version is one whole artifact with no operation that writes model parameters separately (ADR-0021 dec 13), and there is no deploy, push or activate-on-gateway endpoint (ADR-0015 dec 5). The pull writes nothing — no timestamp, counter or receipt — so whether a gateway has pulled is answered by its own journal, and post-cloud by IndustryFlow (ADR-0004 dec 10).

### Boundaries as enforced absences

9. **The following are enforced absences — absent routes, documented rejections, allowlists:**
   - no telemetry, operational, firmware-flash or audit intake (ADR-0021 dec 10; ADR-0020 dec 9); unrecognised paths under an operational namespace are rejected;
   - no type-meaning writes: module and part numbers are foreign keys into the registry (ADR-0021 dec 11);
   - no SKU, price or purchase-order writes; a tracked purchased part is keyed by its vendor serial (ADR-0019 dec 2).

### Tenancy and stage 11

10. **The API operates single-tenant with no tenancy routing and carries the tenant dimension implicitly** — the operator root for mTLS callers, the token scope for others — so stage 11 adds tenancy without redesigning requests (ADR-0021 dec 16).
11. **At stage 11 the `[F]` operations align to IndustryFlow's `production_unit` API and the `[D]` operations remain the IndustryGrow layer API.** Gateway mTLS is already conformant; the operator token migrates to JWT validation.

### Machine-scoped provisioning

12. **A machine carries its own provisioning binding, on its own route, keyed by the machine identifier.** It holds decision 5's public certificate material plus the purchased part's vendor serial and, where the gateway carries a secure element, its die serial (ADR-0007 dec 11); the entity is ADR-0021 dec 17's.
    - The binding is upserted, latest-wins: certificates are short-lived and renewed (ADR-0007 dec 7), and a certificate history would be the audit trail decision 9 excludes.
    - Consumers key on the machine identifier and public-key fingerprint, which survive renewal and re-certification (ADR-0007 dec 10), and display the certificate serial and validity.
    - The machine's `-PR` blob is outside decision 7's allowlist and outside this decision.

### Identifiers on the wire

13. **The API is the one place the identifier grammar is parsed, and every response carrying an identifier also carries its parts, including the decoded version and depth.** The grammar is positional, so a consumer re-deriving fields would be a second implementation that drifts silently. The API adds structure, not meaning — meaning comes from the registry (ADR-0023 dec 1) — and the identifier remains the stored, compared key (ADR-0017 dec 15).

### Firmware

14. **The API records which firmware release an operator intends for a machine and serves that release to the machine's gateway; it updates nothing.**
    - The operator writes a selection keyed by machine, naming a released artifact set by its object-key root (ADR-0029 dec 13); it is validated against the warehouse, which must hold both slot images, and upserted latest-wins.
    - The gateway pulls the intent and the slot images over mTLS as pure reads, the bytes read through from the warehouse; a gateway may fetch only artifacts of the release its own machine intends.
    - There is no update, deploy or flash endpoint and no gateway write; whether a machine's nodes run the release is the gateway's comparison (ADR-0029 dec 15), and the API serves the artifact so the selected release and the received bytes are one object under the gateway's single credential.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | Amend ADR-0021 instead of a new record | An API contract is a decision surface with its own auth model, alternatives and stage-11 lifecycle |
| 1 | Deny every read of type meaning and type-layer documents | Pushes callers into keeping their own tables and copies, the dispersion ADR-0000 dec 3 prevents |
| 1 | Limit type-layer reads to the repository's released-artifact directory | Specifications are repository-owned documents in the same keyspace (ADR-0017) |
| 2 | One shared static bearer token for all callers, gateways included | Fuses machine and human auth, loses the gateway identity the deployment record depends on, and is copyable |
| 2 | Defer gateway mTLS; use a token now and migrate later | The PKI exists; deferring costs a migration at stage 11 for no gain |
| 4 | Accept client-supplied serials | Cannot be guaranteed gap-free and forfeits the allocation authority |
| 7 | Accept any document for an instance, with a blocklist | New type-level kinds would silently route through the ERP as the type layer grows |
| 7 | Return only a key or time-limited URL, and require a CORS policy on the bucket | A deployment's bucket credentials may not grant that policy; a read-through duplicates nothing |
| 8 | A profile deploy, push or gateway-activate endpoint | A second mutation channel (ADR-0015 dec 1, 13) |
| 8 | Store model parameters separately from setpoints | A profile version is one atomically versioned artifact (ADR-0016; ADR-0021 dec 13) |
| 8 | Record the gateway's pull as a last-pulled timestamp | Operational intake from a machine, turning the timer-driven read into a write; monitoring is platform-side |
| 12 | Append a history of machine bindings | A record of what a certificate used to be is the audit trail decision 9 excludes |
| 13 | Return identifiers as strings for each consumer to parse | The parse is positional and fails silently; every consumer becomes a separate implementation of the grammar |
| 14 | Validate a firmware selection against a repository checkout | Accepts releases the gateway cannot fetch whenever mirror and bucket diverge, and makes a repository mount a precondition |
| 14 | Let the gateway fetch firmware from the public repository | Puts the gateway in a trust relationship with an unauthenticated source for the artifact whose substitution matters most |
| 14 | Let the gateway report which nodes took the release | A machine's account of what happened; it opens the only machine-write surface decision 9 keeps closed |

## Consequences

**Positive**

- ADR-0021's object-store-integration and profile-delivery questions are settled by one record.
- The gateway's existing certificate is reused, so there is no gateway auth to migrate at stage 11.
- The boundaries hold by construction: no deploy path, no telemetry, no type-meaning or SKU writes, allowlisted documents.
- The single mutation channel of ADR-0015 is untouched.
- Stage 11 is a swap of token validation and a re-layering of `[F]` operations, not a redesign.
- Lifecycle blobs pass through the ERP on upload, which writes the blob before the index row.

**Negative**

- mTLS termination and operator-CA validation are infrastructure the pre-cloud deployment must run.
- The scoped operator token is interim debt whose claim shape must be carried to stage 11.
- Multi-station or offline serial allocation is unavailable until decided.
- Adding an instance-lifecycle suffix is an ADR-0017 change, not an API convenience.
- Whether a gateway pulled a profile, or whether its nodes run the intended firmware, is not answerable from the ERP.

## Deferred

| Open question | Settled by |
|---|---|
| Offline and multi-station serial-allocation concurrency | Maintainers, before a second issuing station exists |
| Cleanup of orphan blobs left when the index write fails after the blob write | Maintainers, when orphans are observed |
| Operator-token to JWT claim mapping | ADR-IF-0001, at stage 11 |
| API versioning | Maintainers, at the first breaking change |
| Stage-11 re-layering of the API | ADR-IF-0001 |
| Whether a machine gets a `-PR` lifecycle document, and under what object key | ADR-0007 and ADR-0017 |

## References

- `erp/app/api/routes.py`, `erp/app/api/schemas.py` — routes and response shapes.
- `erp/app/services/mtls.py` — identity derivation behind the terminating proxy.
- `erp/app/models/identifiers.py` — the grammar parser of decision 13.
- `erp/deploy/README.md` — mTLS overlay and operator tokens.
