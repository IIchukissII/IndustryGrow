<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0024 — Operator CA bootstrap and the root-key ceremony

- **Status:** Accepted
- **Date:** accepted 2026-07-24; changed 2026-10-19
- **Parent:** ADR-0007
- **Related:** ADR-0001 (the self-build path decisions 5 and 7 keep completable), ADR-0004 (the firmware-signing key decision 14 takes into custody), ADR-0015 (the profile channel decision 10 declines to reuse), ADR-0017 (the `-PR` record issued leaves are bound in), ADR-0020 (decision 5's permitted gateway state, which decision 4 extends), ADR-0021 (the operator-private data that keeps the ERP a relying party), ADR-0022 (the gateway client certificate this CA issues), ADR-0029 (the bootloader that verifies against the firmware-signing key)

## Context

ADR-0007 roots trust per operator (decision 3), with P-256 keys and X.509 certificates (decisions 1–2), and makes gateway certificates short-lived and auto-renewed with revocation by cease-renewal and deny-list, not CRL/OCSP (decision 7). A certificate issued under the operator root is the only machine credential the ERP accepts (ADR-0022 decision 2), so a self-hosted deployment needs a real operator CA before any gateway certificate, gateway client or `-PR` record can be built against it.

A key's exposure must be proportional to its blast radius. Compromise of an issuing key is recovered by re-issuing under the root; compromise of the root is not recoverable, because every certificate in the estate descends from it. Short-lived leaves force an online issuer, so a design that renews leaves with the root makes the root an online key.

A ceremony too heavy for a one-person deployment is skipped, and a skipped ceremony protects nothing. A lost root key is noticed only at the next issuance, so custody must survive one destroyed copy. Self-hosted and commercial operators differ in who performs the ceremony, not in the chain their gateways verify, and the self-build path must stay completable from open artifacts (ADR-0001 decision 6).

## Decision

1. **The chain has two tiers: the offline operator root signs one issuing intermediate, and the intermediate issues every leaf.** The intermediate absorbs the online exposure a short renewal cadence demands, and the root signs about once per intermediate lifetime, so an intermediate compromise is recovered by re-issue and a root compromise stays the rare event decision 11 describes. The leaf classes are the gateway client certificate (ADR-0022 decision 2), the ERP server certificate the gateway validates, and the node provenance certificate ADR-0007 decision 5 permits.
2. **The root signs certificate authorities only, and the intermediate is constrained to issue end-entity certificates only.** The constraint is in the certificates, so an issuance mistake fails path validation instead of widening the CA.
3. **Relying parties anchor on the root, not the intermediate; the intermediate travels in each peer's presented chain.** There are two relying parties — the gateway, validating the ERP's server certificate, and the ERP's terminating proxy, validating gateway client certificates — and both hold the same anchor. Replacing the intermediate therefore redistributes nothing, and changing the anchor (decision 10) changes it in both places or breaks the channel in one direction.
4. **The operator trust anchor is gateway configuration state, permitted persistent state under ADR-0020 decision 5.** A static anchor the unit is provisioned with is configuration of a kind with the identity certificate beside it (ADR-0015 decision 4). Gateway-side revocation machinery stays excluded (ADR-0007 decision 7).
5. **The root key is generated on a host with no network path, stored passphrase-encrypted, and never present on a networked host.** The offline property is obtained by tooling: the generating process runs in an isolated network namespace that is checked to have no routes before a key is written, which is reproducible and verifiable where an operator unplugging a cable is not. The host itself may stay connected; a dedicated offline machine runs the same tooling, with the namespace then redundant.
6. **Custody is two encrypted copies on separate removable media in separate physical locations, with the passphrase held apart from both.** One copy makes media failure an estate-wide event, co-located copies share a fire, and media travelling with the passphrase is one theft, not two. This is redundancy, not a threshold scheme.
7. **A hardware token holding a non-exportable root key is a supported alternative, not a migration target.** It is not the default because it puts a hardware purchase on the self-build path and a PKCS#11 dependency into the one procedure that runs on a bare machine. Both paths are permanently supported and documented.
8. **Lifetimes satisfy root ≫ intermediate ≫ leaf, and each tier's validity strictly contains everything it issues plus a renewal margin.** The values live in the CA runbook (ADR-0000 decision 2), and the tooling refuses an issuance that breaks the relation, because a leaf that outlives its issuer fails validation for a reason no log states. The relation binds certificates checked by path validation at connect time; the long-lived node provenance certificate is validated against the `-PR` instead (ADR-0007 decision 7).
9. **Intermediate rotation is routine and invisible to relying parties.** The root comes out of custody, signs a successor and returns; because relying parties anchor on the root, no unit is touched.
10. **Root rotation is a planned overlap distributed out-of-band over the provisioning path, never the profile channel.** The successor root enters every gateway's and the ERP proxy's trust set before the outgoing root stops issuing, and the outgoing root retires once every unit carries the new anchor; both roots belong to the same operator, so ADR-0007 decision 3 holds. Carrying trust material on the single mutation interface (ADR-0015 decisions 1, 4) would widen a channel kept to one purpose for a rare, already hands-on event.
11. **Compromise of the operator root is a re-provisioning event, not a rotation.** With no CRL/OCSP path (ADR-0007 decision 7) a compromised root cannot be disowned in the field, so every certificate beneath it is treated as forged and the estate is re-provisioned under a new root. This consequence is what justifies decisions 1, 5 and 6.
12. **This record governs the self-hosted operator root; the IndustryFlow-managed CA adopts the same chain shape, validity relation and rotation ordering.** A gateway cannot tell which kind of operator certified it. The managed CA's custody, staffing and audit obligations are commercial-operations concerns not decided here.
13. **The bootstrap tooling lives outside the ERP and never runs inside it.** The ERP is a networked service holding operator-private data (ADR-0021 decision 14), the host decision 5 keeps the root away from, and it is a relying party on this PKI, never an authority within it.
14. **The firmware-signing key is a second root under this ceremony: a bare P-256 keypair, not a CA, kept separate from the operator root (ADR-0007 decision 8).** The bootloader holds its public half and verifies each image directly (ADR-0029 decision 6), so decisions 5 and 6 apply unchanged and rotation additionally re-flashes every bootloader over SWD (ADR-0029 decision 10). Development keys are outside this decision — marked where created, not custodied, not deployed — and a bootloader built with one refuses production images and the reverse; single-copy custody is a deployment choice whose loss leaves the estate accepting no further signed image until every bootloader is re-flashed by hand.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **A.** One tier: the root issues leaves directly | Short-lived leaves put the issuing key online, so the root goes online, and issuing-key and root compromise become one event |
| 1 | **B.** Three tiers: root, policy CA, issuing CA | A middle tier separates issuance domains or policies a single-estate operator does not have, at the cost of another key, lifetime and rotation |
| 1 | **E.** An ACME issuance service (e.g. step-ca) as the operator CA | A running service that still needs a root and this ceremony; more machinery than a small estate's renewal cadence justifies. It can take the intermediate's place later without changing the root or any anchor |
| 5 | Offline by procedure: the operator disconnects the network before generating | Cannot be re-run, tested or verified afterwards |
| 6 | **C.** Root split across custodians by Shamir / M-of-N | Presumes a quorum a one-person deployment lacks, turns one lost medium into an unrecoverable loss, and reassembles the key on one machine anyway |
| 7 | **D.** Require a hardware token for the root | Makes a hardware purchase a precondition of self-hosting (ADR-0001 decision 6) and adds PKCS#11 to the bare-machine procedure |
| 10 | **F.** Distribute a rotated root through the profile channel | Puts trust material on the single mutation interface for a rare, planned, hands-on event |
| 13 | **G.** Harden the test-CA fixture into the bootstrap procedure | A fixture's purpose is disposable, unencrypted keys; grown into a ceremony it is neither a good fixture nor a trustworthy ceremony. It mirrors the two-tier shape instead |
| 14 | Firmware-signing key as a CA issuing signing certificates | The bootloader verifies each image directly against one public key and does no path validation |

## Consequences

**Positive**

- The operator root stays offline while ADR-0007 decision 7's renewal cadence is met, because the two roles sit on different keys.
- Intermediate rotation and issuing-key compromise are recovered without touching a deployed gateway.
- The ERP's mTLS channel has a real trust root, and the gateway-certificate, profile-pull and `-PR` work builds on it.
- The gateway trust anchor is named persistent state rather than an unrecorded assumption.
- The self-build path stays completable from open artifacts and a machine the builder already owns.
- Self-hosted and commercial chains look identical to a gateway, so an operator change is not an architecture change.

**Negative**

- The operator runs a two-key PKI: two keys to protect, two lifetimes to track, a rotation per tier.
- Root compromise stays unrecoverable without re-provisioning the estate.
- Custody depends on the operator's discipline; no tooling verifies that the second copy reached a second location or that the passphrase is held apart.
- Root rotation must reach every unit by hand within the overlap window, and a gateway offline throughout is re-provisioned.
- The issuing-CA key lives on a networked host named in the runbook, exposure decision 1 accepts as recoverable.
- The operator holds the firmware-signing key as a second, independent custody obligation.

## Deferred

| Open question | Settled by |
|---|---|
| How a long-lived node provenance certificate is issued without outliving its issuer: same intermediate, a longer-lived sibling, or the root as a narrow exception | Node provisioning, when it is built |
| The deny-list key, which must be stable across renewal (device public-key fingerprint or `GBOX_NNNN`), not a leaf serial | ADR-0007's deny-list mechanics |
| Commercial-operator CA custody, staffing and audit obligations | The record that owns the IndustryFlow-managed CA |
| Trigger and implementation for automated issuance in the intermediate's position | Fleet size making manual issuance the bottleneck |
| The certificate fields of the `-PR` record | ADR-0007's `-PR` schema |

## References

- ADR-0001, ADR-0004, ADR-0007, ADR-0015, ADR-0017, ADR-0020, ADR-0021, ADR-0022, ADR-0029.
- `pki/README.md` — the ceremony runbook and the lifetime values; `pki/` — the ceremony tooling.
- `erp/deploy/mtls/make-test-ca.sh` — the two-tier test fixture, not a bootstrap procedure.
- `figures/adr0024-ceremony.svg` — the ceremony sequence.
- RFC 5280 (X.509 / PKIX): path validation, basic constraints, key usage.
