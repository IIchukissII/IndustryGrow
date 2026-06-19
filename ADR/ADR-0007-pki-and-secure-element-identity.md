<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0007: PKI, hardware identity, and provisioning

- **ID:** ADR-0007
- **Status:** Proposed
- **Date:** 2026-06-19
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0004 (rev 1), ADR-0017

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

## Alternatives considered

**A. A single global IndustryGrow root CA with per-operator intermediates.** Simpler chain, one place to manage. *Rejected:* it couples every deployment — including independent self-hosters — to one root key and one revocation authority, making a global key compromise catastrophic and contradicting the fleet-independence and self-hosting goals of ADR-0001. Per-operator roots (decision 3) bound the blast radius to one operator.

**B. Per-node runtime authentication on the CAN bus using the node ATECC.** Each node signs or authenticates its frames. *Rejected:* it contradicts the deliberate trusted-CAN decision (ADR-0004 decision 17) and is impractical on classic CAN — a signature consumes most of the 8-byte payload (ADR-0004 alternative F). The node secure element earns its place as an identity anchor (decision 5), not a bus credential.

**C. Long-lived gateway certificates with CRL/OCSP revocation.** The textbook enterprise PKI revocation path. *Rejected for the gateway:* it requires revocation-distribution infrastructure and gateway-side revocation checking, adding persistent state and network dependencies that fight the stateless-edge model (ADR-0004). Short-lived certs with deny-on-non-renewal (decision 7) achieve revocation with far less machinery. (CRL/OCSP is not forbidden for an operator that wants it; it is simply not the architecture's default.)

**D. Software-stored keys; no secure element.** Keep keys in a file on the gateway and in node flash. *Rejected:* ADR-0002 already mandates the ATECC608, and a non-exportable hardware key is the entire security premise — a key that can be copied off a stolen SD card or dumped from node flash defeats hardware identity.

**E. No per-node identity at all (gateway-only PKI).** Issue certificates only to gateways; treat nodes as anonymous bus participants. *Rejected:* ADR-0017 already binds every manufactured instance's serial to its ATECC608 and certificate via the `-PR`; dropping node identity would strand that binding and forfeit supply-chain provenance and anti-counterfeit. Decision 5 keeps node identity as an anchor without making it a runtime credential.

**F. Continue deferring ADR-0007.** Leave it "planned" and implement the firmware seam ad hoc. *Rejected:* it is the missing keystone several Accepted ADRs depend on, and implementing the secure-element firmware without it would invent the PKI in code — exactly the "discussion precedes the ADR" inversion ADR-0000 decision 1 forbids.

## Consequences

### Positive

- Hardware-anchored, non-exportable identity for every unit, usable directly in the standard mTLS channel ADR-0004 assumes.
- Both deployment models are genuinely first-class: a self-hoster runs a complete, independent system; a commercial operator manages a fleet centrally; neither depends on the other's keys.
- Gateways stay replaceable and stateless — identity is the only persistent secret, re-provisionable in minutes, with revocation handled by non-renewal rather than gateway-side infrastructure.
- The per-board ATECC608 mandated by ADR-0002 has a defined purpose (identity anchor + provenance) without reopening the trusted-CAN boundary.
- Identity PKI and firmware-signing PKI remain cleanly separated, so the two trust roots fail and rotate independently.
- The serial↔certificate binding (ADR-0017) gains a concrete provisioning workflow and authenticity-check basis.

### Negative

- **A CA must exist for each operator.** Commercial deployments need IndustryFlow to operate a managed CA; self-hosters must run their own root. This is real infrastructure and ceremony, mitigated by providing self-hoster CA tooling and documentation (deferred decisions).
- **Provisioning adds a Production step.** On-chip keygen, CSR, issuance, and `-PR` creation become part of manufacturing (Phase 2, ADR-0017).
- **Short-lived gateway certs require renewal connectivity.** A gateway offline past its certificate lifetime must re-provision; the renewal cadence must be chosen so normal outages never strand a unit.
- **Self-hosters take on CA-operator responsibility** (root-key custody, issuance hygiene). Tooling can lower but not remove this burden; documentation must set expectations.

## Deferred decisions

- **ATECC608 slot allocation, key-config words, and config-zone lock sequence** — manufacturing/provisioning document (per decision 9).
- **Self-hoster CA bootstrap tooling and documentation** — how a community builder stands up and safeguards a root CA with minimal ceremony.
- **CSR / issuance tooling and the `-PR` record format** — the concrete provisioning pipeline and the provisioning-record schema (the `-PR` layer is named in ADR-0017; its fields are unspecified).
- **Certificate lifetime and renewal cadence values** — the actual validity windows for gateway and node certificates; the renewal trigger and grace policy.
- **Revocation deny-list mechanics on the platform side** — how IndustryFlow stores and checks revoked gateway identities (touches the IndustryFlow roadmap, like the audit-trail schema in ADR-0004).
- **Migration / cross-signing between self-hosted and commercial operation** — what happens to identity when a deployment moves from community-self-hosted to commercial-managed (or vice versa).
- **Node attestation protocol** — if a future feature ever needs a node to prove its identity to the gateway at runtime, the challenge/response protocol is out of scope here (and must not silently breach decision 5 / ADR-0004 decision 17 without its own ADR).
- **Operator root-key ceremony** — generation, custody, backup, and rotation of the per-operator root keys.

## References

- ADR-0001: IndustryGrow framing — names the future PKI architecture; community-self-hosted and commercial-managed fleet models.
- ADR-0002 (rev 3): Field bus architecture — ATECC608B on every board; security boundary at the gateway; trusted-CAN domain.
- ADR-0004 (rev 1): Gateway host hardening — gateway↔IndustryFlow mTLS, ATECC608-bound gateway identity, audit-batch identity, separate offline firmware-signing key, trusted-CAN assumption.
- ADR-0017: Component, document, and instance identification — serial↔ATECC608↔certificate binding; the `-PR` provisioning record; serials assigned in Production.
- ADR-0019: Purchased-part identification — the gateway SBC's ATECC-bound certificate as its instance key.
- ADR-0020: Gateway persistence model — ATECC-bound identity as already-permitted persistent state.
- `store/E0001-000001-D-pinmap.md`: carrier pin map — ATECC608 on I²C2 (PB10/PB11).
- RFC 5280 (X.509 / PKIX); RFC 8446 (TLS 1.3, mutual authentication).
