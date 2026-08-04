<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0007 (rev 1): PKI, hardware identity, and provisioning

- **ID:** ADR-0007 (rev 1)
- **Status:** Accepted
- **Date:** 2026-07-25
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0004 (rev 1), ADR-0017 (rev 2), ADR-0021, ADR-0022, ADR-0024
- **Cross-project:** ADR-IF-0007 (IndustryFlow device CA — the SAN-URI tenant identity the stage-11 leaf conforms to)
- **Supersedes:** ADR-0007 (initial, 2026-06-19)

## Revision history

- **rev 1 (2026-07-25)** — Adds decision 10 (identity across operators), resolving
  the identity-envelope half of the deferred *migration / cross-signing* decision
  and, with it, ADR-0024's deferred *"what the self-hosted provisioning pipeline
  must not hard-code"* constraint — both settled here before the gateway-certificate
  pipeline (the work that would otherwise bake in a single operator) is written.
  Decisions 1–9 are substantively unchanged; decision 3 gains an inline bounded
  cross-reference to decision 10 (the trust anchor becomes *replaceable* at an
  operator change, but a gateway still trusts one operator's root at a time — "no
  other" is not relaxed). The trigger was concrete: IndustryFlow's device CA
  authorises on a SAN URI and deprecates CN-as-identity, while this project's ERP
  (ADR-0022 d2) authorises on `CN=GBOX_NNNN`, so the one on-chip key must present
  under two incompatible envelopes. Adds alternatives G–I and a companion/reference
  refresh (ADR-0021/0022/0024 are now Accepted; ADR-0017 is rev 1).

## Context and problem

ADR-0007 is the most-referenced ADR that was never written. ADR-0001 names it as the future PKI architecture; ADR-0002 rev 3 populates an ATECC608B on every board "for hardware identity (PKI per ADR-0007)" and places the security boundary at the gateway with PKI specifics deferred here; ADR-0004 rev 1 assumes gateway↔IndustryFlow mTLS "is in place" and explicitly excludes the PKI — "ADR-0007 covers *who the gateway is*, this ADR covers *how the gateway behaves once it is who it says it is*"; ADR-0017 binds the manufactured serial to "the ATECC608 plus its provisioned certificate (ADR-0007)" and defines the `-PR` provisioning record as that binding's home; ADR-0019 and ADR-0020 both lean on an "ATECC-bound identity". The keystone is missing, and its absence now blocks concrete work — the carrier (E0001) routes I²C2 to an ATECC608 secure element, but the node firmware cannot wire up that identity seam without a decided model for what the secure element *is for*.

The surrounding ADRs have already fixed this record's boundaries. They are inherited here, not re-opened:

- **A secure element on every board.** ATECC608B is populated on every carrier and gateway (ADR-0002 rev 3). Hardware-anchored identity is a given; this ADR decides how it is used, not whether it exists.
- **The gateway authenticates to IndustryFlow via mTLS** (ADR-0004 rev 1 decision 1). The PKI must produce credentials usable for standard mutual-TLS.
- **The CAN domain is trusted; there is no per-node authentication on the bus** (ADR-0002 rev 3; ADR-0004 rev 1 decision 17). Whatever the per-node secure element is for, it is *not* runtime bus authentication.
- **Two deployment models coexist under one architecture** (ADR-0001): community-self-hosted and commercial-managed. The PKI must serve both without forcing self-hosters onto commercial infrastructure.
- **The gateway is a stateless, replaceable edge** (ADR-0004 rev 1): its only persistent state is its identity, and a unit must be re-provisionable in minutes.
- **Serial is the logistics key; the ATECC608 plus its certificate is the cryptographic instance identity**, bound in the `-PR` provisioning record, public material only, private key never leaving the chip; serials are assigned in Production / Phase 2 (ADR-0017 decisions 8, 12).
- **The firmware-signing key is separate from any identity key, held offline** (ADR-0004 rev 1 decision 12). Identity PKI and firmware-signing PKI are distinct trust roots.

What remains genuinely open — and is decided below — is the certificate architecture: key type and format, how trust is rooted across the two deployment models, what the per-node secure element actually does, the provisioning workflow, and revocation/lifetime.

## Decision drivers

- **Hardware-anchored, non-exportable identity.** The point of the ATECC608 is that the private key is generated on-chip and physically cannot leave it. Software-stored keys would discard the reason the part is on the BOM.
- **Both deployment models, first-class.** A community self-hoster must be able to run the whole system without trusting or depending on a commercial CA. A commercial operator must be able to manage a fleet centrally. Neither is a second-class path (ADR-0001).
- **Standards over bespoke crypto.** Lean on X.509 / mTLS / ECDSA so the gateway↔IndustryFlow channel uses ordinary TLS libraries and the certificates interoperate with existing tooling. Invent no protocol that an audit could not check against an RFC.
- **Stateless-edge and replaceability (ADR-0004).** Identity must be re-provisionable; revocation and lifetime must not require heavy gateway-side infrastructure or large persistent local state.
- **Preserve the trusted-CAN boundary.** The model must not quietly reintroduce per-node bus authentication that ADR-0004 decision 17 deliberately excluded.
- **Supply-chain provenance.** The serial↔secure-element binding (ADR-0017) should let a unit's authenticity be checked against its provisioning record — anti-counterfeit and traceability — independently of any runtime use.
- **Containment.** One compromised unit, or one compromised deployment, must not compromise others or the platform (ADR-0004 decision 48 — compromise containment).

## Decision

### Cryptographic primitives

1. **Identity is anchored in the ATECC608B; the device keypair is generated on-chip in a non-exportable slot and the private key never leaves the part.** Curve is **NIST P-256 (secp256r1)** — the part's native ECC curve and the basis of the serial↔crypto binding (ADR-0017 decision 8). The public key and the serial are the only material exported for certification.

2. **Identity credentials are X.509 certificates with ECDSA P-256 signatures.** This is what the gateway↔IndustryFlow mTLS of ADR-0004 decision 1 consumes directly, with ordinary TLS stacks and no bespoke certificate format.

### Trust topology across deployment models

3. **Trust is rooted per operator, not globally — there is no single IndustryGrow root CA.** A *commercial-managed* deployment chains its gateway and node certificates to an **IndustryFlow-operated managed CA**; a *community-self-hosted* deployment chains to a **root CA the self-hoster runs** (tooling and docs provided, see deferred decisions). A gateway trusts the root of its own deployment/operator and no other. Rationale: this is the only topology in which a self-hoster is genuinely independent of commercial infrastructure (driver: both models first-class), and it bounds key-compromise blast radius and revocation authority to a single operator (driver: containment). A global root would couple every deployment to one key and one revocation authority — the opposite of the fleet independence ADR-0001 requires.

   > **Bounded cross-reference (rev 1):** "no other" stays in force at every instant, without exception. Decision 10 lets one hardware key be **re-certified** across an operator change, and requires the gateway's trust anchor to be *replaceable* rather than compiled in for the life of the part — but a gateway trusts exactly one operator's root at any moment, so a migration is a **clean sequential swap**: the outgoing anchor is retired as the incoming one is installed, which honours "no other" without relaxing it. A migration design that instead required a gateway to trust two *different* operators' roots at once would relax this decision, and must bring an ADR amending it **before** implementation (ADR-0000 decision 1) — it is not pre-authorised here. This is unlike ADR-0024 decision 10's rotation overlap, which holds two roots of the **same** operator and so never engages "no other" at all.

### What gets an identity, and what it is for

4. **The gateway holds an ATECC608-bound X.509 client certificate; this is the mTLS identity ADR-0004 assumes** (decision 1) and the identity stamped into each audit batch (ADR-0004 decision 10). It is provisioned at deployment, renewable, and re-provisionable for a replacement unit (ADR-0004 replaceability driver).

5. **Every node's ATECC608 is a hardware-identity anchor, not a runtime bus credential.** It holds the on-chip non-exportable keypair and is the cryptographic half of the serial↔identity binding recorded in the `-PR` provisioning record (ADR-0017 decision 12). A node leaf certificate **may** be issued at provisioning for **provenance and anti-counterfeit** (a unit can be proven genuine against its `-PR`), but the node secure element is **not** used to authenticate frames on the CAN bus — the bus stays a trusted domain (ADR-0004 decision 17). This keeps the per-board ATECC mandated by ADR-0002 meaningful without reopening per-node bus authentication.

### Provisioning workflow

6. **Provisioning happens in Production (Phase 2, ADR-0017) and never exports a private key.** The device generates its keypair on-chip, emits a certificate signing request carrying its serial and public key, the operator's CA (commercial managed CA or self-hosted root) issues the certificate, and the **`-PR` provisioning record binds serial ↔ certificate** holding public material only (ADR-0017 decision 12). The `-PR` is the unit's birth certificate and the authority for later authenticity checks.

### Lifetime and revocation

7. **Gateway certificates are short-lived and auto-renewed against the operator's CA; revocation is "cease renewal + platform-side deny-list", not CRL/OCSP.** A revoked or replaced gateway simply stops being re-issued, and the platform refuses its identity on the deny-list keyed to the gateway's certificate. This fits the stateless-edge model (ADR-0004) — no gateway-side revocation checking, minimal persistent state — and avoids standing up CRL/OCSP distribution. **Node provenance certificates are long-lived**, because they are an identity/traceability statement, not a live access credential, and are validated against the `-PR` rather than a freshness check.

### Boundary with firmware signing

8. **This ADR governs *identity* (who a unit is); it does not own firmware signing.** The firmware-signing key remains the separate, offline key of ADR-0004 rev 1 decisions 12–16, and the node's firmware-verification public key remains burned into the bootloader (ADR-0004 decision 14) — it is **not** the node ATECC identity key. The two trust roots stay distinct so that a compromise or rotation of one does not implicate the other.

### Configuration detail boundary

9. **The ATECC608 slot map and config-zone lock policy are implementation detail, owned by a manufacturing/provisioning document, not this ADR.** Per ADR-0000 decision 2, this ADR records *that* the device key lives in a non-exportable slot and *why*; the concrete slot numbers, key-config words, and lock sequence live in the document whose job is that value.

### Identity across operators (rev 1)

10. **One hardware key, re-certified per operator: migration is re-certification, never re-keying, and `GBOX_NNNN` is the stable identity under every certificate envelope.** Decision 3 roots trust per operator and the deferred *migration / cross-signing* decision left open what becomes of a unit's identity when it moves between them. This decides it for the identity envelope — the part the gateway-certificate pipeline must respect before it is built — and leaves the operational hand-off procedure and the IndustryFlow-side issuance mechanics deferred below.

    - **a. The on-chip key is the fixed point; the certificate is what changes.** The gateway's single non-exportable P-256 key (decision 1) is certified once per trust domain it must authenticate to. Pre-cloud, the self-hosted operator CA (ADR-0024) issues the leaf the ERP consumes; at stage 11 the IndustryFlow managed CA (decision 3) issues, **for the same key**, the leaf the ingestion edge consumes. Re-keying is not a migration path: the key is the hardware anchor the `-PR` binds a serial to (ADR-0017 decision 12), so replacing it discards that binding and demands physical re-provisioning of the secure element. Decision 4's "re-provisionable" is exactly this — a new certificate over a stable key, not a new key.

    - **b. Two trust domains read identity from different fields; the machine identifier survives both.** The pre-cloud ERP derives the machine identity **from the verified certificate, never from a request parameter** (ADR-0022 decision 2); *which* field it reads is not fixed by that ADR — the mTLS termination it ships today reads the Common Name, `CN=GBOX_NNNN`, the ADR-0017 machine identifier verbatim (`erp/app/services/mtls.py`, `erp/deploy/mtls/`), a deployment-owned field choice, not an ADR one. IndustryFlow **deprecates CN-as-identity** and authorises on a SAN URI carrying tenant and device — `industryflow:tenant/<company_uuid>/device/GBOX_NNNN` (ADR-IF-0007 decision 3) — a property of the core platform this layer conforms to (ADR-0001 core-plus-layer), not one re-opened here. So `GBOX_NNNN` is the CN of the self-hosted leaf and the device segment of the IndustryFlow leaf; the tenant UUID is the one field that appears only at stage 11, because pre-cloud operation is single-tenant and operator-private (ADR-0021) and has no tenant to name. The identifier is stable; only the envelope around it changes with the operator.

    - **c. The mechanism is independent re-certification, not cross-signing.** Cross-signing the self-hosted leaf under the managed root would neither produce the SAN URI the ingestion edge requires (the identity lives in a *different field*, not merely under a different signature) nor preserve the root independence that is decision 3's containment. Each operator issues its own leaf for the shared key from its own root, and the incoming operator's root is delivered out-of-band over the **provisioning path** — the channel ADR-0024 decision 10 also uses, deliberately not the profile channel (ADR-0015 decisions 1, 4). A gateway trusts one operator's root at a time (decision 3); the hand-off is a **clean sequential swap** that honours "no other". A migration design that instead found it needed brief concurrent trust of two *different* operators' roots would be relaxing decision 3, and must carry its own ADR amendment first (ADR-0000 decision 1) — it is not granted here. This differs from ADR-0024 decision 10's rotation overlap, which holds two roots of the **same** operator and so never engages "no other" — the mechanism (provisioning-path delivery) is shared, the d3 reasoning is not.

    - **d. What the provisioning pipeline must not hard-code.** Because the envelope is operator-specific but the key and the machine identifier are not, the pipeline and the `-PR` it writes must bind to the stable facts, never to the current operator's envelope: the `-PR` keys on `GBOX_NNNN` and the hardware public-key fingerprint (both stable across renewal *and* across re-certification), not on a leaf's CN or certificate serial as though that were the identity; the gateway trust anchor must be **replaceable** — provisioned configuration the unit can be re-anchored on when its operator changes, not a single value compiled in for the life of the hardware (this is a replaceable anchor, not a mandate to trust two operators at once — see the decision 3 note); and no tenant value, root identity, or CN-versus-SAN assumption is baked into anything a later migration cannot rewrite. This is the constraint ADR-0024 deferred as *"what the self-hosted provisioning pipeline must not hard-code"*, warning that a pipeline which bakes in the self-hosted envelope closes the migration path silently. It is now a requirement, not a caution.

## Alternatives considered

**A. A single global IndustryGrow root CA with per-operator intermediates.** Simpler chain, one place to manage. *Rejected:* it couples every deployment — including independent self-hosters — to one root key and one revocation authority, making a global key compromise catastrophic and contradicting the fleet-independence and self-hosting goals of ADR-0001. Per-operator roots (decision 3) bound the blast radius to one operator.

**B. Per-node runtime authentication on the CAN bus using the node ATECC.** Each node signs or authenticates its frames. *Rejected:* it contradicts the deliberate trusted-CAN decision (ADR-0004 decision 17) and is impractical on classic CAN — a signature consumes most of the 8-byte payload (ADR-0004 alternative F). The node secure element earns its place as an identity anchor (decision 5), not a bus credential.

**C. Long-lived gateway certificates with CRL/OCSP revocation.** The textbook enterprise PKI revocation path. *Rejected for the gateway:* it requires revocation-distribution infrastructure and gateway-side revocation checking, adding persistent state and network dependencies that fight the stateless-edge model (ADR-0004). Short-lived certs with deny-on-non-renewal (decision 7) achieve revocation with far less machinery. (CRL/OCSP is not forbidden for an operator that wants it; it is simply not the architecture's default.)

**D. Software-stored keys; no secure element.** Keep keys in a file on the gateway and in node flash. *Rejected:* ADR-0002 already mandates the ATECC608, and a non-exportable hardware key is the entire security premise — a key that can be copied off a stolen SD card or dumped from node flash defeats hardware identity.

**E. No per-node identity at all (gateway-only PKI).** Issue certificates only to gateways; treat nodes as anonymous bus participants. *Rejected:* ADR-0017 already binds every manufactured instance's serial to its ATECC608 and certificate via the `-PR`; dropping node identity would strand that binding and forfeit supply-chain provenance and anti-counterfeit. Decision 5 keeps node identity as an anchor without making it a runtime credential.

**F. Continue deferring ADR-0007.** Leave it "planned" and implement the firmware seam ad hoc. *Rejected:* it is the missing keystone several Accepted ADRs depend on, and implementing the secure-element firmware without it would invent the PKI in code — exactly the "discussion precedes the ADR" inversion ADR-0000 decision 1 forbids.

**G. One certificate serving both trust domains (CN=GBOX_NNNN *and* the SAN URI in a single leaf).** Provision once, satisfy everyone. *Rejected (decision 10):* the tenant UUID in the SAN does not exist pre-cloud (ADR-0021 is single-tenant/private), so there is nothing to put in the field at provisioning time; and a single leaf chains to exactly one root, while the ERP anchors on the self-hosted root and IndustryFlow on the managed root. Two relying parties on two roots need two leaves, whatever the fields say.

**H. Re-key at migration — a fresh on-chip keypair for commercial operation.** Treat the move as a new enrolment. *Rejected (decision 10a):* the key is the hardware anchor the serial is bound to (ADR-0017 decision 12); re-keying severs that binding, forfeits the provenance the `-PR` exists to carry, and means physically re-provisioning the secure element for what is an operator change, not a hardware change. The whole point of a non-exportable key is that identity travels by re-certification.

**I. Put the machine identifier in the CN of the IndustryFlow leaf too, for symmetry.** Keep one parsing rule across both domains. *Rejected (decision 10b):* ADR-IF-0007 decision 3 deprecates CN-as-identity on the core platform; forcing the CN convention onto its leaf would fork the core rather than conform to it, which ADR-0001's core-plus-layer positioning forbids. The identifier rides the SAN's device segment there instead — same value, the field the core actually reads.

## Consequences

### Positive

- Hardware-anchored, non-exportable identity for every unit, usable directly in the standard mTLS channel ADR-0004 assumes.
- Both deployment models are genuinely first-class: a self-hoster runs a complete, independent system; a commercial operator manages a fleet centrally; neither depends on the other's keys.
- Gateways stay replaceable and stateless — identity is the only persistent secret, re-provisionable in minutes, with revocation handled by non-renewal rather than gateway-side infrastructure.
- The per-board ATECC608 mandated by ADR-0002 has a defined purpose (identity anchor + provenance) without reopening the trusted-CAN boundary.
- Identity PKI and firmware-signing PKI remain cleanly separated, so the two trust roots fail and rotate independently.
- The serial↔certificate binding (ADR-0017) gains a concrete provisioning workflow and authenticity-check basis.
- A deployment's identity survives an operator change by re-certification of a stable key (decision 10), so the migration remains a change of operator — not of key, not of architecture — and the gateway-certificate pipeline can be built now without silently closing that path.

### Negative

- **A CA must exist for each operator.** Commercial deployments need IndustryFlow to operate a managed CA; self-hosters must run their own root. This is real infrastructure and ceremony, mitigated by providing self-hoster CA tooling and documentation (deferred decisions).
- **Provisioning adds a Production step.** On-chip keygen, CSR, issuance, and `-PR` creation become part of manufacturing (Phase 2, ADR-0017).
- **Short-lived gateway certs require renewal connectivity.** A gateway offline past its certificate lifetime must re-provision; the renewal cadence must be chosen so normal outages never strand a unit.
- **Self-hosters take on CA-operator responsibility** (root-key custody, issuance hygiene). Tooling can lower but not remove this burden; documentation must set expectations.
- **A unit's identity is read from two different certificate fields by its two relying parties** (decision 10b): the ERP parses the CN, IndustryFlow parses the SAN URI. Both must keep `GBOX_NNNN` authoritative and in step — a divergence between the two parsers would authenticate a unit as the wrong thing, or as nothing. Across a migration a gateway may also hold two valid certificates for one key at once, which the `-PR` and any status view must expect rather than treat as a duplicate.

## Deferred decisions

- **ATECC608 slot allocation, key-config words, and config-zone lock sequence** — manufacturing/provisioning document (per decision 9).
- **Self-hoster CA bootstrap tooling and documentation** — how a community builder stands up and safeguards a root CA with minimal ceremony.
- **CSR / issuance tooling and the `-PR` record format** — the concrete provisioning pipeline and the provisioning-record schema (the `-PR` layer is named in ADR-0017; its fields are unspecified).
- **Certificate lifetime and renewal cadence values** — the actual validity windows for gateway and node certificates; the renewal trigger and grace policy.
- **Revocation deny-list mechanics on the platform side** — how IndustryFlow stores and checks revoked gateway identities (touches the IndustryFlow roadmap, like the audit-trail schema in ADR-0004).
- **Migration / cross-signing between self-hosted and commercial operation** — what happens to identity when a deployment moves from community-self-hosted to commercial-managed (or vice versa).
  > **Resolved in part by rev 1 (decision 10):** the *identity envelope* is decided — one on-chip key re-certified per operator, `GBOX_NNNN` stable across CN (self-hosted) and the SAN device segment (IndustryFlow), independent re-certification rather than cross-signing, and the pipeline hard-coding constraint ADR-0024 deferred. Still open: the operational hand-off **procedure** (when and how a deployment flips operator — it must honour decision 3 with a **clean sequential swap**; a design that found it needed brief concurrent trust of two *different* operators' roots would relax "no other" and must bring its own ADR amendment first, ADR-0000 decision 1), the IndustryFlow-side tenant assignment and managed-CA issuance of the SAN leaf (ADR-IF- territory), and **reverse** migration (commercial → self-hosted).
- **Node attestation protocol** — if a future feature ever needs a node to prove its identity to the gateway at runtime, the challenge/response protocol is out of scope here (and must not silently breach decision 5 / ADR-0004 decision 17 without its own ADR).
- **Operator root-key ceremony** — generation, custody, backup, and rotation of the per-operator root keys.

## References

- ADR-0001: IndustryGrow framing — names the future PKI architecture; community-self-hosted and commercial-managed fleet models.
- ADR-0002 (rev 3): Field bus architecture — ATECC608B on every board; security boundary at the gateway; trusted-CAN domain.
- ADR-0004 (rev 1): Gateway host hardening — gateway↔IndustryFlow mTLS, ATECC608-bound gateway identity, audit-batch identity, separate offline firmware-signing key, trusted-CAN assumption.
- ADR-0017 (rev 2): Component, document, and instance identification — serial↔ATECC608↔certificate binding; the `-PR` provisioning record; serials assigned in Production; the `GBOX_NNNN` machine identifier decision 10 keeps stable across envelopes.
- ADR-0019: Purchased-part identification — the gateway SBC's ATECC-bound certificate as its instance key.
- ADR-0020: Gateway persistence model — ATECC-bound identity as already-permitted persistent state; the trust anchor decision 10d keeps replaceable.
- ADR-0021: Instance-and-integration ERP — the pre-cloud, single-tenant/operator-private system of record whose ERP consumes the `CN=GBOX_NNNN` leaf (decision 10b).
- ADR-0022: ERP API — decision 2's gateway client certificate identified by CN; the credential the self-hosted leaf is.
- ADR-0024: Operator CA bootstrap and key ceremony — the two-tier operator CA that issues the self-hosted leaf, and the deferred pipeline-hard-coding constraint decision 10d resolves.
- ADR-IF-0007 (IndustryFlow, cross-project): the device CA whose SAN-URI tenant identity and CN-as-identity deprecation the stage-11 leaf conforms to (decision 10b).
- `store/E0001-000001-D-pinmap.md`: carrier pin map — ATECC608 on I²C2 (PB10/PB11).
- RFC 5280 (X.509 / PKIX); RFC 8446 (TLS 1.3, mutual authentication).
