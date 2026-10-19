<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0007 — PKI, hardware identity and provisioning

- **Status:** Accepted
- **Date:** 2026-06-19; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0002](ADR-0002-field-bus-architecture.md) (fits the secure element to every designed board; places the security boundary at the gateway), [ADR-0004](ADR-0004-gateway-host-hardening.md) (consumes the gateway identity over mTLS; keeps the CAN domain trusted and firmware signing separate), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (the serial ↔ identity binding and the `-PR` record), [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (the gateway is a purchased part), [ADR-0021](ADR-0021-instance-and-integration-erp.md) (the single-tenant ERP that relies on the self-hosted leaf), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (the ERP authenticates gateways by certificate), [ADR-0024](ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) (stands up the per-operator CA this record roots trust in), ADR-IF-0007 (IndustryFlow device CA whose SAN-URI identity the stage-11 leaf conforms to)

## Context

Every board the project designs carries an ATECC608B secure element (ADR-0002); the gateway is a purchased single-board computer (ADR-0019 decision 1) and carries none. The gateway authenticates to IndustryFlow and to the operator ERP by mutual TLS (ADR-0004 decision 1; ADR-0022 decision 2), the CAN domain is trusted with no per-node bus authentication (ADR-0004 decision 17), and firmware signing is a separate offline key (ADR-0004 decision 12). A unit's serial is its logistics key and is bound to its cryptographic identity in the `-PR` provisioning record, public material only (ADR-0017 decisions 8, 12).

Community-self-hosted and commercial-managed deployments coexist (ADR-0001), and neither may depend on the other's infrastructure. The gateway is a stateless, replaceable edge (ADR-0004), so identity must be re-provisionable and revocation must need no gateway-side machinery. Credentials must be ordinary X.509 that standard TLS stacks check, and one compromised unit or deployment must not compromise another.

The two relying parties of a gateway read its identity from different certificate fields: the pre-cloud ERP from the subject, IndustryFlow from a SAN URI that carries a tenant (ADR-IF-0007 decision 3).

## Decision

1. **Device identity is anchored in a P-256 key generated on-chip in a non-exportable secure-element slot; the private key never leaves the part.** NIST P-256 is the part's native curve and the basis of the serial ↔ identity binding (ADR-0017 decision 8); only the public key and the serial are exported for certification. This governs every node, and a gateway that carries a secure element; decision 11 covers the gateway that does not.
2. **Identity credentials are X.509 certificates with ECDSA P-256 signatures.** This is what mTLS consumes directly, with ordinary TLS stacks and no bespoke format.
3. **Trust is rooted per operator; there is no global IndustryGrow root CA.** A commercial deployment chains to an IndustryFlow-operated managed CA and a self-hosted deployment to a root the self-hoster runs (ADR-0024), and a gateway trusts the root of its own operator and no other, at every instant. This is the only topology in which a self-hoster is independent of commercial infrastructure, and it bounds key-compromise blast radius and revocation authority to one operator.
4. **The gateway holds an X.509 client certificate over its device key; it is the mTLS identity ADR-0004 decision 1 assumes and the identity stamped into each audit batch (ADR-0004 decision 10).** It is provisioned at deployment, renewable, and re-provisionable for a replacement unit. Its key anchor is decision 1 or decision 11.
5. **A node's secure element is a hardware-identity anchor, not a runtime bus credential.** It holds the node's key and is the cryptographic half of the serial ↔ identity binding in the `-PR` record. A node provenance certificate may be issued at provisioning for anti-counterfeit checks against the `-PR`; the secure element never authenticates CAN frames.
6. **Provisioning happens in Production (ADR-0017 Phase 2) and never exports a private key.** The device generates its key, emits a CSR carrying its serial and public key, the operator's CA issues the certificate, and the `-PR` record binds serial to certificate with public material only. The `-PR` is the authority for later authenticity checks.
7. **Gateway certificates are short-lived and auto-renewed; revocation is cease-renewal plus a platform-side deny-list, not CRL/OCSP.** This needs no gateway-side revocation checking and no revocation distribution infrastructure. Node provenance certificates are long-lived, because they state origin, not live access, and are validated against the `-PR`.
8. **This record governs identity, not firmware signing.** The firmware-signing key and the bootloader's verification key are ADR-0004's (decisions 12, 14) and are not the node identity key, so a compromise or rotation of one trust root does not implicate the other.
9. **The secure-element slot map and config-zone lock policy are owned by a provisioning document, not this record.** This record fixes that the key lives in a non-exportable slot and why (ADR-0000 decision 2).
10. **One device key is re-certified per operator: migration is re-certification, never re-keying, and `GBOX_NNNN` is the stable identity under every certificate envelope.**
    - **a.** The key is the fixed point and the certificate is what changes. Pre-cloud the self-hosted operator CA issues the leaf the ERP consumes; at stage 11 the IndustryFlow managed CA issues, for the same key, the leaf its ingestion edge consumes. Re-keying would discard the binding the `-PR` records.
    - **b.** The self-hosted leaf carries `GBOX_NNNN` in the subject CN, the field the ERP deployment reads; the IndustryFlow leaf carries it as the device segment of the tenant-scoped SAN URI of ADR-IF-0007 decision 3. The tenant exists only at stage 11, because pre-cloud operation is single-tenant (ADR-0021).
    - **c.** Each operator issues its own leaf from its own root; there is no cross-signing. The incoming operator's root reaches the gateway out-of-band over the provisioning path, never the profile channel, as a sequential swap that keeps decision 3.
    - **d.** The provisioning pipeline and the `-PR` bind to stable facts — `GBOX_NNNN` and the device public-key fingerprint — never to a leaf's CN or serial. The gateway trust anchor is replaceable provisioned configuration, and no tenant, root identity or CN-versus-SAN assumption is fixed where a migration cannot rewrite it.
11. **A gateway without a secure element holds a software P-256 key generated on the gateway, protected by host access control and never copied off it.** The reference gateway is a purchased board with no secure element, and generated-where-it-lives is the property decision 1 protects that a host can still keep. The loss is non-exportability against an attacker who is root on the gateway, so the host hardening of ADR-0004 is load-bearing for identity, and short leaf lifetimes (decision 7) and issuing-CA revocation (ADR-0024) bound a stolen gateway's usefulness.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | Continue deferring this record and build the secure-element firmware seam ad hoc | Several accepted records depend on it, and code would invent the PKI, which ADR-0000 decision 1 forbids |
| 1 | Software-stored keys on nodes and on any board that carries a secure element | A key copied off a stolen card or dumped from flash defeats hardware identity, the reason the part is fitted |
| 3 | A single global IndustryGrow root with per-operator intermediates | Couples every deployment, self-hosters included, to one root key and one revocation authority |
| 3 | Concurrent trust of two operators' roots during a migration | Relaxes "no other"; a design that needs it requires its own ADR first |
| 5 | Per-node runtime authentication on the CAN bus with the node secure element | Contradicts the trusted-CAN decision (ADR-0004 decision 17); a signature consumes most of an 8-byte frame |
| 5 | No node identity, gateway-only PKI | Strands the serial ↔ identity binding of ADR-0017 and forfeits provenance and anti-counterfeit |
| 7 | Long-lived gateway certificates with CRL/OCSP revocation | Needs revocation distribution and gateway-side checking, state and network dependencies that fight the stateless edge |
| 10 | One leaf carrying both the CN and the tenant SAN URI | The tenant does not exist pre-cloud, and one leaf chains to one root while the two relying parties anchor on two |
| 10 | Re-key at migration with a fresh device keypair | Severs the binding the `-PR` records and forces physical re-provisioning for an operator change |
| 10 | Cross-sign the self-hosted leaf under the managed root | Produces no SAN URI and removes the root independence decision 3 keeps |
| 10 | Machine identifier in the CN of the IndustryFlow leaf too | ADR-IF-0007 decision 3 deprecates CN-as-identity; forcing it would fork the core platform |
| 11 | Fit a secure element to the gateway as a precondition | Puts a hardware purchase on the critical path of the self-build route (ADR-0001 decision 6) for a part whose form the project does not control |
| 11 | Keep the gateway certificate described as secure-element-bound and treat the software key as an undocumented exception | Leaves the record asserting hardware that is not fitted, so every claim resting on it is unverifiable (ADR-0000 decision 3) |

## Consequences

**Positive**

- Every unit has an identity usable directly in standard mTLS, hardware-anchored wherever a secure element is fitted.
- A self-hoster runs a complete, independent PKI and a commercial operator manages a fleet centrally, neither depending on the other's keys.
- Revocation by non-renewal keeps the gateway stateless and replaceable.
- The node secure element has a defined purpose without reopening the trusted-CAN boundary.
- Identity and firmware-signing roots fail and rotate independently.
- An operator change is a re-certification of a stable key, so the provisioning pipeline does not close the migration path.

**Negative**

- Each operator must run a CA, and a self-hoster takes on root-key custody (ADR-0024).
- Provisioning adds key generation, CSR, issuance and `-PR` creation to Production.
- A gateway offline past its certificate lifetime must be re-provisioned, so the renewal cadence must outlast normal outages.
- The ERP and IndustryFlow parse `GBOX_NNNN` from different fields, and a divergence between the two parsers authenticates a unit as the wrong machine or as none.
- During a migration one key holds two valid certificates, which the `-PR` and any status view must expect.
- A gateway's identity is only as strong as its host: root on the gateway is the private key.
- The secure-element provisioning procedure and tool for the gateway exist but are off the reference deployment's path.

## Deferred

| Open question | Settled by |
|---|---|
| Node secure-element slot map, key-config words and lock sequence (decision 9) | The node provisioning document, when node provisioning is built |
| `-PR` record schema | ADR-0017 / ADR-0022 owners, with node provisioning |
| Gateway leaf renewal trigger and grace policy | The automated renewal implementation |
| Platform-side deny-list storage and check | IndustryFlow roadmap |
| Operator hand-off procedure, IndustryFlow tenant assignment and SAN-leaf issuance, and reverse migration | The first migration between operators; IndustryFlow issuance is ADR-IF- scope |
| A runtime node attestation protocol | Its own ADR, if a feature ever needs a node to prove identity at runtime |

## References

- ADR-0001, ADR-0002, ADR-0004, ADR-0017, ADR-0019, ADR-0021, ADR-0022, ADR-0024; ADR-IF-0007 (IndustryFlow).
- `pki/README.md` — operator CA procedure and certificate lifetimes.
- `gateway/README.md` — gateway key generation and certificate installation.
- `store/SP0004-M-atecc-provisioning.md`, `gateway/provision_identity.py` — secure-element provisioning for a gateway that carries one.
- `erp/app/services/mtls.py`, `erp/deploy/mtls/` — the ERP's CN-based identity extraction.
- `store/E0001-000003-D-pinmap.md` — carrier pin map: the node secure element.
- RFC 5280 (X.509 / PKIX); RFC 8446 (TLS 1.3).
