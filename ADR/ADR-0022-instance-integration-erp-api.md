<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0022 (rev 3): Instance-and-integration ERP — the machine- and operator-facing API

- **ID:** ADR-0022 (rev 3)
- **Status:** Accepted
- **Date:** 2026-07-19 (accepted 2026-07-21, with decision 1 clarified; rev 1 and rev 2: 2026-07-26; rev 3: 2026-08-30)
- **Project:** IndustryGrow
- **Parent:** ADR-0021 (rev 4)
- **Companions:** ADR-0000, ADR-0004 (rev 1), ADR-0007 (rev 1), ADR-0015, ADR-0016 (rev 1), ADR-0017 (rev 2), ADR-0019, ADR-0020, ADR-0024, ADR-0025, ADR-0029
- **Supersedes:** ADR-0022 rev 2 (2026-07-26), and through it rev 1 (2026-07-26) and the initial record (2026-07-19, accepted 2026-07-21)
- **Realizes:** ADR-0021 deferred decisions *"ERP ↔ object store integration"* and *"ERP ↔ gateway profile push"*; at rev 1, the API surface for ADR-0021 rev 3 decision 17's machine identity binding (decision 12); at rev 3, the API surface for ADR-0021 rev 4 decision 18's intended firmware release, and with it ADR-0029 decisions 14 and 16 (decision 14). ADR-0007's deferred *"`-PR` record format"* is **not** resolved by this — decision 12 records the queryable binding and leaves the blob format and document key deferred
- **Clarified by:** ADR-0023 (decision 1's type-meaning exclusion — the read-through catalog)
- **Relates to:** ADR-IF-0001 (planned) — the `production_unit` core whose API this one's foundational operations align to at stage 11

## Revision history

- **rev 3 (2026-08-30)** — Adds decision 14: the operator route that records a
  machine's intended firmware release, and the gateway route that pulls it and the
  artifact it names. ADR-0029 decisions 14 and 16 put the ERP on the firmware path
  — the operator acts here, the gateway fetches from here — and ADR-0021 rev 4
  decision 18 claims the entity that makes it representable under decision 1. The
  same division as rev 1: ownership there, exposure here. Decisions 8's pure-read
  rule and 9's exclusions are extended to the new routes rather than qualified;
  alternative Q records the pull-confirmation write refused for firmware, as
  alternative M records it for profiles.

- **rev 2 (2026-07-26)** — Amends decision 7. It said the ERP returns the object
  key **or a time-limited retrieval URL**, and nothing else; a read-through that
  streams the bytes is now permitted as a third form, for documents the ERP has
  indexed or the repository owns. The reason the original wording did not survive
  contact: a URL the object store issues is only usable by a browser the object
  store has been configured to accept, so on a deployment whose bucket credentials
  cannot set a CORS policy, an operator could be handed a valid grant for a
  document they still could not read. The rule was stricter than the reasoning
  under it — ADR-0021 decision 7 asks that the ERP not **duplicate** blob content,
  and a stream duplicates nothing. It is the same shape ADR-0023 established for
  `REGISTRY.md`, which decision 1 already serves read-through: read-only, stored
  nowhere, owned by whoever owned it before. What stays forbidden is the ERP
  holding a copy (alternative O).

  *Corrected in place 2026-07-26, after the amendment shipped:* decision 1's
  clarification **enumerated** decision 7's forms — "a time-limited URL, never
  content" — instead of referring to them, so amending decision 7 left decision 1
  stating a rule its own cited source no longer carried, and the two read as a
  contradiction for exactly the documents this revision was written to make
  readable. It defers now. This is the drift ADR-0000 decision 3 predicts for a
  restatement: the copy does not change when the original does.

- **rev 1 (2026-07-26)** — Adds decision 12 and qualifies decision 8, both prompted
  by building the gateway side. **Decision 12** exposes the machine
  identity binding ADR-0021 rev 3 decision 17 gives the ERP to own. Decision 5's binding is keyed by an
  E-instance serial `Exxxx-VVVVVV-NNNNNN`, and a gateway has none — it is SP0004,
  identified as machine `GBOX_NNNN` (ADR-0019 decision 2; decision 1) — so the
  gateway's certificate had nowhere to be recorded and no route to record it
  through. `store/SP0004-M-atecc-provisioning.md` names this gap and declines to
  close it, correctly: it is an API-contract question, and this is the API
  contract. **Decision 8** gains an explicit statement that the profile-pull
  endpoint is a pure read: it was implied by "a store and a cache, not a second
  mutation channel" but never said, and the pull-confirmation write it forbids is
  attractive enough to be worth naming as rejected (alternative M). The
  competing option for decision 12 — a synthetic E-instance serial — is rejected
  where the ownership question lives, in ADR-0021 rev 3 alternative M. No other
  decision changes; no boundary moves.

## Context and problem

ADR-0021 (rev 1) fixes the ERP's role, ownership, and boundaries and the store engine (MongoDB over the flat object-store warehouse), but it deliberately defers *how the ERP is talked to*. Two of its deferred decisions are, in substance, API-contract questions:

- **ERP ↔ object store integration** — who writes a lifecycle-document blob first, and how its key is recorded in the ERP index with referential integrity across the two stores.
- **ERP ↔ gateway profile push** — the concrete mechanism by which a stored profile version reaches the gateway's `active-profile.json`, consistent with ADR-0015's single mutation channel.

Both are resolved only by deciding the API. Serial allocation, provisioning binding, integration tracking, and SP stock likewise need an external interface: production stations issue serials and bind ATECC records, an operator installs and moves instances, and the gateway obtains its active profile version. This ADR decides the **shape, auth model, and hard boundaries** of that interface. Concrete route strings, request/response schemas, and the OpenAPI document are implementation per ADR-0000; this record carries decisions and rationale.

The API has two distinct caller classes with very different standing against the existing PKI: **machine callers** (the gateway, which already holds an ATECC608-bound X.509 client certificate under ADR-0007) and **human/tooling callers** (operators, provisioning stations), which have no hardware identity anchor. Conflating them is the central design error this ADR exists to prevent.

**What deploying it exposed (rev 2).** Decision 7's original wording assumed a
retrieval URL is always spendable. It is not: a presigned URL is honoured only by a
browser the object store has been told to accept, and the first real deployment ran
against a bucket whose credentials could not set that policy. So the API could hand
an operator a correct, unexpired grant for a manual they still could not open — a
boundary that produced no security and one unreadable document. The rule was
stricter than the reasoning under it, and rev 2 loosens the rule to match the
reasoning rather than the other way round.

**What building the gateway side exposed (rev 1).** Two gaps surfaced once the
gateway actually had an identity and a client, and both sit on the caller-class
line above. First, the gateway's certificate had nowhere to live: the provisioning
binding of decision 5 is keyed by an E-instance serial, and a gateway does not have
one — so the one unit whose certificate the whole mTLS channel depends on was the
one unit the API could not record. Second, with a real client pulling on a timer,
the obvious next question is "did it actually pull?", and the obvious next move is
to have the pull say so. That move is the caller-class error in its most tempting
form: it looks like completing a record, and it is a machine reporting an
operational act. Decision 12 closes the first gap; decision 8 now says out loud
that the second is closed deliberately.

## Decision drivers

- **Resolve ADR-0021's two deferred API-shaped decisions** without re-opening its ownership or boundary decisions.
- **Reuse the PKI that already exists (ADR-0007).** The gateway is a machine caller with a first-class, hardware-anchored credential; not using it would be drift, not simplicity.
- **The boundaries ADR-0021 draws must be un-representable in the API, not merely undocumented.** A missing route is not a boundary; an absent capability plus an explicit rejection is.
- **Single mutation channel (ADR-0015).** The gateway's `active-profile.json` is the only thing that mutates a running control loop; the API must be a store and a record, never a second deploy path.
- **Single source of truth (ADR-0000).** The API references type meaning, SKUs, and blobs; it never accepts writes of them.
- **Forward-shaped for stage 11.** Auth and request semantics must map onto IndustryFlow's model (ADR-0004 / ADR-IF-0001) so the transition is a swap of validation, not a redesign.

## Decision

### Surface and resource model

1. **The API exposes exactly the entities ADR-0021 owns, and only those:** machines (`GBOX_NNNN`), E-instances and the serial allocator, provisioning bindings (`-PR`), integration records, the lifecycle-document index, deployment profiles and their active-version records, a machine's intended firmware release (*rev 3*; ADR-0021 decision 18), and SP stock/placement. There is no resource for type meaning, telemetry, an audit trail, SKUs/prices, or community template profiles. It is served by the same single container as the ERP (ADR-0021 decision 15); its OpenAPI document is generated from the implementation.

   *Clarified in place 2026-07-21 by ADR-0023 (ADR-0000 decision 5):* the type-meaning exclusion is of a resource the ERP **owns**. A read-only route serving `REGISTRY.md` as parsed, storing nothing (ADR-0023 decisions 1, 4), is permitted — it is the read-side counterpart of decision 9. Denying the read would only push callers back into keeping their own tables.

   *Clarified in place 2026-07-26, on the same reasoning:* the exclusion likewise does not deny a **read grant for a type-layer document** already in the warehouse — the manuals, pinmaps and schematics `store_sync` mirrors there. Decision 7 keeps them out of the *ingestion* surface and that is unchanged: the ERP still accepts no type-layer upload, indexes none, and owns none. What it may do is what it already does for `REGISTRY.md` — serve, read-only, storing nothing, a thing the repository owns. The operator standing at a cabinet needs the bring-up manual, and refusing to point at a document the ERP is already holding the key for would only push them into keeping a second copy of `store/` somewhere — the dispersion ADR-0000 decision 3 exists to prevent. Two constraints make this a read of the mirror rather than a read of the bucket: the object must correspond to a file in the repository's `store/` directory, and the route offers only the forms decision 7 allows — never a copy the ERP keeps.

### Authentication — two caller classes, two mechanisms

2. **Gateway machine callers authenticate by mTLS using the ADR-0007 PKI, from day one.** The gateway already holds an ATECC608-bound X.509 client certificate; the API validates it against the operator's trust root and derives the machine identity (`GBOX_NNNN`) **from the verified certificate, never from a request parameter**. No bearer token is issued to or accepted from a gateway. The single-tenant wrinkle (ADR-0021 decision 16) removes per-tenant *routing*, not certificate *validation*: there is one operator root, but the certificate is still verified and the identity still extracted.

3. **Human and provisioning-tooling callers authenticate with a scoped operator token — interim, and explicitly a stage-11 migration target.** These callers have no hardware anchor and there is no pre-cloud JWT infrastructure (that is IndustryFlow's, ADR-0004). A token is acceptable **only** if it is *scoped per caller role* (provisioning station vs. operator vs. read-only), so the ERP can attribute who allocated a serial or recorded an activation, and if its subject/role is shaped to map onto a future JWT claim (stage 11 replaces validation, not request semantics). **A single static secret shared across all callers — and especially one shared with gateways — is rejected** (decision 2; alternatives B, C).

### Serial allocation (authoritative)

4. **Serials are issued by the ERP, never supplied by the client.** Allocation is gap-free per `(Exxxx, VVVVVV)` and atomic (a counter document updated in one operation; ADR-0021 decision 4; ADR-0017 decisions 1, 8). The allocation response is final. **Pre-cloud the allocator is single-writer (one issuing station);** gap-free multi-station/offline allocation is deferred (below), and until decided the API must not present a multi-writer guarantee it does not keep.

### Provisioning binding

5. **Provisioning binds a serial to its ATECC608 certificate and writes the structured `-PR` record — public material only.** The binding request carries certificate metadata (public key fingerprint, cert serial, validity), never a private key (which never leaves the ATECC608, ADR-0007). The blob of the `-PR` document follows the lifecycle-document flow (decision 7); the ERP holds the queryable binding and the object key.

   *Scope note (rev 1):* this decision is about **E-instance serials**. A machine has its own binding on its own route — see decision 12.

### Integration operations

6. **Install / move / remove / replace are the integration operations, and the depth code `DDDDDD` is assigned here at integration.** Exactly one current instance may occupy a `(machine, depth)` position; history is preserved (ADR-0021 decision 6; ADR-0017 decision 13). The depth code is **never** written onto the instance record or back into any type registry (ADR-0017 decision 7); the integration identifier `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` is returned only as a derived view of the current placement.

### Lifecycle-document ingestion (index over the warehouse)

7. **The document-ingestion surface is scoped to the instance-lifecycle suffix allowlist `{QP, QR, CP, CC, PR}` on instance identifiers `Exxxx-VVVVVV-NNNNNN`, and that scope is exhaustive.** The blob is written to the object-store warehouse *first*, then its key and metadata (type, status, dates, calibration validity) are recorded in the ERP index (referential integrity — a recorded key always resolves; ADR-0021 decision 7). **The ERP never *stores* blob content, and returns it only by reading through.** It answers with the object-store key, a time-limited retrieval URL, or — *rev 2* — the bytes streamed straight from the object store to the caller, holding none of them. The third form exists because the second is not always usable: a URL the object store issues is only good to a browser that store has been configured to accept, and an operator handed a grant they cannot spend has been given nothing. What remains forbidden is the ERP keeping a copy — ADR-0021 decision 7 asks that it not duplicate the store, and a read-through duplicates nothing (alternative O). Type-layer documents — `-S/-D/-L/-P/-M/-I`, the `-D-fab.zip` fabrication package (ADR-0017 decision 18), SP documents and designed-accessory rollups (`SPxxxx-<layer>`, `<parent>-D-<slug>`, ADR-0019 decisions 8–9) — are **not** acceptable through this API; they are repo/`store_sync` artifacts. The allowlist (not a blocklist) is what makes that boundary hold as the type layer grows.

### Profiles — store and record, never a deploy path

8. **The API stores deployment-specific profile versions and records which version is active on which `GBOX`; it does not deploy.** A profile version is stored as one whole artifact (setpoints + model, atomically; ADR-0021 decision 13; ADR-0016 alternative D) — there is **no** operation that writes model parameters separately. The active-version relationship is a **record write**, not a push: **there is no deploy/push/activate-on-gateway endpoint** (ADR-0015 decision 4; ADR-0021 decision 12). The gateway obtains its active profile version by **pulling** it over its mTLS channel (decision 2); the ERP is where that version is pulled *from* and the record of *which is active where* — a store and a cache, not a second mutation channel.

   **The pull is a pure read (rev 1).** The profile-pull endpoint writes nothing —
   no last-pulled timestamp, no pull counter, no delivery receipt. The two caller
   classes of decision 2 write different things for different reasons: an operator
   records a *decision* (which version is active), and a machine records nothing at
   all. A pull-confirmation write would be a machine reporting an operational act,
   which decision 9 excludes by category, and it would turn the one endpoint the
   gateway calls on a timer into a write channel for operational events. So "has
   this gateway actually pulled?" is deliberately not answerable from the ERP; it
   is answerable from the gateway's own journal, and post-cloud from IndustryFlow
   (ADR-0004 rev 1 decision 10). Consumers of this API — the console included —
   present that as a gap rather than closing it (alternative M).

### Boundaries as hard, un-representable constraints

9. **The following are architectural absences, enforced (absent routes, documented rejections, allowlists), not merely omitted:**
   - **No telemetry / operational / firmware-flash / audit intake** — that stream stays platform-side (ADR-0021 decision 10; ADR-0004 rev 1 decision 10; ADR-0020 decision 9). Unrecognised paths under an operational namespace are rejected, not silently absorbed.
   - **No type-meaning writes** — `Exxxx`/`SPxxxx` are foreign keys into `REGISTRY.md`; the API accepts no description/spec/meaning for them (ADR-0021 decision 11; ADR-0000 decision 3).
   - **No SKU/price/purchase-order writes** — those live in the BOM (ADR-0021 decision 9); SP resources carry stock and placement only. The instance key for a tracked purchased part is its **vendor serial**, never a project `NNNNNN` (ADR-0019 decision 2).

### Tenancy and stage-11 disposition

10. **The API operates single-tenant with no tenancy routing, but carries the tenant dimension implicitly** (the operator root for mTLS callers, the token scope for others), so stage-11 adds tenancy without redesigning requests (ADR-0021 decision 16).

11. **At stage 11 the foundational `[F]` operations (instances, provisioning, integration) align to IndustryFlow's `production_unit` API; the domain `[D]` operations (`GBOX`, profiles) remain the IndustryGrow layer API.** Gateway mTLS is already conformant; the operator token migrates to JWT validation against IndustryFlow's auth service. The concrete re-layering is decided with ADR-IF-0001 (Deferred decisions).

### Machine-scoped provisioning (rev 1)

12. **A machine carries its own provisioning binding, on its own route, keyed by `GBOX_NNNN` — it does not borrow decision 5's.** The entity, and why it cannot be decision 5's, are ADR-0021 rev 3 decision 17's; this decision is how the API exposes it, which is the division decision 1 requires. What the binding holds is the same **public certificate material** decision 5 names (public-key fingerprint, certificate serial, validity) plus the two facts that identify the unit rather than the certificate: the **SP0004 vendor serial** and the **ATECC die serial**. Public material only, as in decision 5 — a private key is not representable in this API.

    Three properties follow, and are decisions rather than implementation notes:

    - **The binding is upserted, latest-wins, not appended.** Gateway certificates are short-lived and auto-renewed (ADR-0007 decision 7), so a binding written once at provisioning describes a certificate that has since been replaced. Re-certification writes the binding again. This is a *configuration record kept current*, not a history: what a machine's certificate *used to be* is not a question this API answers, and making it one would be the audit trail decision 9 excludes.
    - **What identifies the unit survives what identifies the certificate.** `GBOX_NNNN` and the public-key fingerprint are stable across renewal and across re-certification under another operator (ADR-0007 rev 1 decision 10d); the certificate serial and validity window are not. Consumers key on the former and display the latter.
    - **The `-PR` document blob is out of scope here.** Decision 7's allowlist is defined on E-instance identifiers, and whether a machine gets a lifecycle document at all — and under what object key, given the ADR-0017 grammar — is not settled by this ADR. Decision 12 records the *queryable binding*; the blob question stays deferred.

### Identifiers on the wire (added 2026-07-29)

13. **The API is the one place the ADR-0017 / ADR-0019 identifier grammar is parsed, and it returns every identifier read into its fields as well as whole.** A response carrying an identifier also carries its parts — an instance's module, version and serial; an integration record's depth levels beside the module, version and serial it holds; a type-layer object key's root, version, document layer, slug and withdrawal status — including the decoded form of the two encoded fields, `VVVVVV` as `major.minor.patch` and `DDDDDD` as its three levels (ADR-0017 decision 1).

    **Why the parse belongs here and nowhere else.** The scheme is positional, so reading it is a parse and not a match: the same letter is a document layer in the slot after the version and an ordinary word in a slug two segments later, and the same six digits are a position on one axis and a design version on the other. A consumer that re-derived those fields from the string would be a second implementation of the scheme, free to drift from the one the store is filed by — and the drift would be silent, because a wrong parse yields a plausible field rather than an error. One implementation, on the side that already owns the grammar, is what makes ADR-0017's opacity affordable: an identifier can stay meaningless to look at because whatever hands it over also says what its parts are.

    **Two boundaries this does not cross.** It puts no *meaning* in the API — what an `Exxxx` designates still comes from the registry and only from it (ADR-0023 decision 1; ADR-0021 decision 11). Structure is read here; meaning is read there. And it does not promote a parsed field over the key: the identifier **is** the object key (ADR-0017 decision 15) and remains what is stored, compared and copied, the fields being a derived view of it in the same sense decision 6 makes the integration identifier a derived view of a placement.

    Recorded as an additive in-place amendment (ADR-0000 decision 5), not a revision bump: it constrains response shape without changing a decision already on record. It also resolves the *where* of ADR-0017's deferred "identifier validation and parsing tooling" item.

### Firmware — an operator's selection, and a gateway's pull (rev 3)

14. **The API records which firmware release an operator intends for a machine, and serves that release to the machine's gateway; it does not update anything.** The entity is ADR-0021 rev 4 decision 18's; this decision is how it is exposed, which is the division decision 1 requires. It is decision 8's shape applied to firmware, and deliberately so — the two are the same problem, and an operator who has learnt one channel has learnt the other.

    - **The operator writes a selection, keyed by `GBOX_NNNN`.** The request names a released artifact set by its object-key root (`E0001-VVVVVV-F`, ADR-0029 decision 13), which the API validates **against the warehouse** — both slot images must be present there. The warehouse rather than the repository's `store/`, unlike decision 7's read-through routes: those serve documents the repository defines, while this serves the bytes a gateway will fetch, and those come from the bucket (ADR-0021 decision 7). Validating against a checkout would accept a release the gateway then cannot fetch, because mirror and bucket diverge whenever `store_sync` has not run — and it would make a repository mount a precondition for firmware, which it is not. A selection naming a release the bucket cannot serve is refused where an operator can still fix it, rather than becoming a gateway that quietly serves nothing. Upserted, latest-wins, like decision 12's binding: this is a configuration record kept current, and what a machine's intended release *used to be* is not a question this API answers.
    - **The gateway pulls over its mTLS channel (decision 2), and the pull is a pure read.** It returns the intended release and, on a second route, the artifact bytes — streamed through from the warehouse under decision 7's third form, the ERP holding no copy of an image as it holds no copy of any blob (ADR-0021 decision 7). The machine identity comes from the verified certificate, never a request parameter.
    - **There is no update, deploy, or flash endpoint, and no write a gateway can make.** The ERP is what the intent is read *from*; the transfer is the gateway's, on the bus it owns (ADR-0029 decision 16). Decision 8's pure-read rule applies here unchanged and for the same reason: an operator records a decision, a machine records nothing at all. So *"is this machine's firmware up to date?"* is not answerable from the ERP — that comparison is the gateway's, against what it observes on the bus (ADR-0029 decision 15) — and consumers present it as a gap rather than closing it (alternative Q).

    **Why the artifact is served by this API rather than fetched from the repository.** The gateway already holds one credential and speaks to one system of record (decision 2); a second fetch path to a public repository would put a component deliberately denied write access to the ERP into a trust relationship with an unauthenticated source, for the one artifact whose substitution matters most. ADR-0029 decision 6 makes that survivable — a node verifies the signature itself, and an unsigned or altered image is refused at the node — but survivable is not the standard for the path an operator is told to use. Serving it here also makes the release an operator selected and the bytes a gateway receives the same object by construction, rather than two lookups that can disagree.

## Alternatives considered

**A. Amend ADR-0021 instead of a new record.** *Rejected:* an API contract is new material — its own auth model, endpoint semantics, and rejected alternatives — and it *instantiates* two of ADR-0021's deferred decisions rather than qualifying a recorded one. ADR-0000 decision 5's in-place-amendment vehicle is for bounded qualifications, not for a decision surface with its own stage-11 lifecycle.

**B. One shared static bearer token for all callers, gateways included.** *Rejected:* fuses machine and human auth, makes a hardware-anchored gateway present as an anonymous token holder, loses the gateway identity the deployment record depends on, and manufactures stage-11 re-hardening debt. A copyable shared secret defeats the hardware-identity posture ADR-0007 establishes.

**C. Defer gateway mTLS; use a token now, migrate later.** *Rejected:* the PKI already exists, so mTLS for the gateway is zero new infrastructure. Deferring it is drift with no payoff and a migration owed at stage 11 that need not exist.

**D. A profile deploy/push (or gateway-activate) endpoint.** *Rejected:* it is a second mutation channel, which ADR-0015 decision 4 forbids. The gateway pulls; the ERP records. The only permitted effect of an active-version write is a record.

**E. Accept any document for an instance (a blocklist of forbidden types).** *Rejected:* as the type layer grows (ADR-0017 decision 18; ADR-0019 decisions 8–9), a blocklist silently lets new type-level kinds route through the ERP. An exhaustive `{QP,QR,CP,CC,PR}` allowlist keeps the ERP the instance-layer index only (ADR-0021 decision 11).

**F. Store model parameters separately from setpoints.** *Rejected:* exactly ADR-0016 alternative D / ADR-0021 decision 13 — a profile version is one atomically-versioned artifact.

**G. Accept client-supplied serials.** *Rejected:* the ERP is the serial-allocation authority (ADR-0021 decision 4); a client-chosen serial cannot be gap-free-guaranteed and forfeits the authority.

**O. Keep decision 7 as it stood and require a CORS policy on the bucket (rev 2).** The boundary is cleaner: the ERP touches no bytes at all, and one line of bucket configuration makes every grant spendable. *Rejected as a requirement,* and it remains the better arrangement wherever it is available: it makes the deployment depend on a permission the object store may not grant the credentials it was given, and it pushes a configuration step into every future deployment to preserve a property — no duplication — that a read-through does not threaten anyway. An operator who can set the policy should; the API no longer *needs* them to.

**P. Return identifiers as strings and let each consumer parse them (decision 13).** The API stays narrower, the grammar lives in one ADR rather than in a response schema, and any consumer is free to read exactly the fields it needs. *Rejected:* the parse is positional and the failure mode is silent. A consumer that scans a key for "a segment that is `S/D/L/P/M/I/F`" reads a slug word as a document layer and produces a confident wrong answer, and one that assumes the second six-digit field is a version reads a depth as `v2.1.0`. Neither raises anything. Multiplying that across the console, the gateway, and a future IndustryFlow importer buys narrowness at the price of N implementations that can each be wrong differently — and the one place already holding the grammar is the side that stores the identifiers. The narrower surface is real but is the wrong economy: the schema grows by fields that are derived and cheap, the alternative grows by implementations that are load-bearing and silent.

**Q. Let the gateway report the update result back — which nodes took the release, and when (rev 3).** The firmware pull already knows the answer at the moment it matters, the operator has no other way to see whether a selection took effect, and it is one write on a channel the gateway already holds. *Rejected:* it is alternative M with a different payload, and it fails the same caller-class test — the selection is what an *operator decided*, this would be what a *machine and its nodes did*. ADR-0029 decision 15 was corrected for proposing exactly this, and its correction is the governing statement: the observation stays at the gateway, and reaches a system of record at stage 11 with the rest of the operational stream. Accepting it here would also make the firmware routes the only ones where a machine writes, which is the surface decision 9 exists to keep closed. The question is the right question for an operator to ask; the ERP is the wrong place to answer it, and answering it here would cost the property that makes decision 9 checkable — that no route accepts a machine's account of what happened.

**M. Record the gateway's pull — a "last pulled at" timestamp on the machine (rev 1).** One mutable field, overwritten each pull, no history; it would let an operator see at a glance whether a cabinet is actually collecting its profile, which decision 8 otherwise leaves unanswerable. *Rejected:* it is operational intake from a machine caller, which decision 9 excludes by category, and the "it is only one field, not a log" defence does not survive the caller-class test the Context sets out — the active-version record is what an *operator decided*, while this would be what a *machine did*. Its real use is monitoring ("alert me if a gateway has not pulled in an hour"), and monitoring is platform-side (ADR-0004 rev 1 decision 10). It would also convert the single endpoint a gateway calls on a 60-second timer from a read into a write, opening exactly the machine-write surface decision 9 closes. The question is a good one; the ERP is the wrong place to answer it.

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
- **ADR-0017 / ADR-0019** — the API is where the identifier grammar is parsed, and it hands identifiers back read into their fields (decision 13); it assigns depth at integration (decision 6); the document allowlist keeps type-layer artifacts (incl. `-D-fab.zip`, SP docs, accessories) out of the ERP (decision 7).
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

- ADR-0000, ADR-0004 (rev 1), ADR-0007, ADR-0015, ADR-0016 (rev 1), ADR-0017 (rev 2), ADR-0019, ADR-0020, ADR-0021, ADR-IF-0001 (planned), `GLOSSARY.md`.
