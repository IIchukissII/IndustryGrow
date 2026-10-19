<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0025 — Deployment-profile signing and gateway verification

- **Status:** Accepted
- **Date:** accepted 2026-07-26; changed 2026-10-19
- **Parent:** ADR-0015
- **Related:** ADR-0001 (the community-contributed template model whose signing stays outside this record), ADR-0004 (outbound-only firewall; the firmware-signing root kept separate), ADR-0007 (P-256; distinct trust roots; the CN identity and replaceable-anchor shape), ADR-0020 (decision 5's permitted gateway state, which decision 10 extends), ADR-0021 (the template / instance / cache split; the ERP as store), ADR-0022 (the pull channel and the gateway's certificate identity), ADR-0024 (the ERP as relying party; the custody model decision 5 does not require)

## Context

ADR-0015 decision 7 requires the gateway to verify a profile's signature against a trusted public key before writing it to the active-profile file, and to reject, log and keep the previous version when verification fails. It does not say what signs a profile, what the signature covers, or where the gateway's key comes from.

The profile is the single mutation interface (ADR-0015 decision 1), versioned monotonically per cabinet (ADR-0015 decision 3). The gateway pulls its active profile from the ERP over mTLS and nothing pushes (ADR-0021 decision 12; ADR-0022 decision 8), which keeps the firewall outbound-only (ADR-0004 decision 5). Template, instance and cache are three artifacts (ADR-0021 decision 12): a public community template and a private deployment instance are signed by different principals for different audiences.

The ERP is a relying party, never an authority (ADR-0024 decision 13), and holds operator-private data (ADR-0021 decision 14). Identity and firmware signing are distinct trust roots (ADR-0007 decision 8), and a gateway trust anchor is replaceable provisioned configuration (ADR-0007 decision 10d).

## Decision

1. **This record owns signing and verification of the deployment-specific instance profile on the ERP-to-gateway channel; community template signing remains ADR-0009's.** ADR-0015 routes its "signature scheme" to ADR-0009 for contributor-authored templates, and the instance channel is a different artifact with a different signer and privacy property. One record for both would make an operator's private control channel depend on a public registry's design.
2. **The gateway has exactly one verification path, because templates never reach it directly.** A template becomes an instance in the ERP, authored and signed by the operator, and the instance is what is stored, pulled and verified. A design that routes templates to gateways needs dispatch between trusted keys and its own ADR first (ADR-0000 decision 1).
3. **The operator signs the profile; the ERP stores a signature it did not produce, and the signing key is never in the ERP or on a gateway.** The ERP has no authoring role over what a cabinet runs (ADR-0021 decision 12; ADR-0022 decision 8), and an ERP that signed would hold, in a networked container, the one key that changes cabinet behaviour.
4. **The profile-signing key is a third trust root, distinct from the operator CA (ADR-0024) and the firmware-signing key (ADR-0004 decision 12).** Identity says who a unit is, firmware what code runs, and this key what cultivation instructions a cabinet may execute. Profile authoring is routine, so signing with the release-only firmware key would bring that key online.
5. **Custody is proportionate to cadence: a passphrase-encrypted key on an operator-controlled host is the baseline, and the ADR-0024 root ceremony is not required.** The key is used at every setpoint change, and a control that makes routine work painful is routed around. Stronger custody (token, offline host) is permitted; the procedure and values belong to the signing runbook (ADR-0000 decision 2).
6. **The signature covers the exact bytes of the profile document, and those bytes travel verbatim from signing to verification.** No component re-serialises the document and nothing canonicalises it, so no JSON library's key order, number format or escaping is part of the security boundary. The ERP stores and returns the signed artifact as opaque bytes, since a store that rebuilds the document on read breaks every signature it holds.
7. **The document carries its own machine identifier and version, so one signature binds content, addressee and version.** The gateway rejects a document not addressed to it, learning its own identifier from its client certificate (ADR-0007 decision 10b), the same string the ERP authorised the pull by (ADR-0022 decision 2). A profile signed for one cabinet is inert at any other, so a programme run on several cabinets is signed once per cabinet.
8. **The gateway refuses a version not greater than the one it runs; a rollback is issued as a new, higher version carrying the earlier content.** A signature does not expire, so a replayed old artifact is otherwise indistinguishable from a current one. The deferred platform-side rollback operation of ADR-0015 must mint a new version to work with this.
9. **ECDSA on NIST P-256 with SHA-256.** The curve is the project's (ADR-0007 decision 1), and a second asymmetric family would add library footprint and audit surface to the gateway.
10. **The gateway's profile-verification public key is provisioned, replaceable configuration, permitted persistent state under ADR-0020 decision 5.** It is not compiled in or baked into an image, for the reason ADR-0007 decision 10d gives for the trust anchor: an irreplaceable key turns rotation into a hardware event. It is public material, of a kind with the operator trust anchor (ADR-0024 decision 4).
11. **A signature is mandatory for anything a gateway can pull: the ERP refuses to record a version as active without one.** The absence is caught where an operator can fix it, not at a cabinet that has stopped taking updates; the gateway's behaviour on failure is ADR-0015 decision 7's.
12. **The built-in fallback profile (ADR-0015 decision 10) is outside this scheme and is never verified against the profile-signing key.** It ships inside the gateway software, so its integrity is that software's, not the profile channel's. A build-time operator signature is impossible because the operator key does not exist at build time, and exempting "the fallback" by name would create a class of profile the gateway accepts unverified.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | Instance and template signing in one record (ADR-0009) | Makes an operator's private control channel depend on the design of a public registry |
| 3 | **A.** The ERP signs profiles on the operator's behalf | Puts the cabinet-control key in a networked container holding production data, and an ERP compromise becomes a cultivation-control compromise |
| 4 | **B.** Sign profiles with the firmware-signing key | Routine authoring brings the release-only offline key online and dissolves the separation ADR-0007 decision 8 records |
| 4 | **C.** Sign profiles with the operator CA key | Conflates who a unit is with what it should do; every issuance uses a key that can retune a cabinet, and rotating one forces rotating the other |
| 5 | Require the ADR-0024 root-key ceremony for the profile key | A ceremony per setpoint change is routed around, and a bypassed signature is worth less than a proportionate one |
| 6 | **D.** Canonical JSON (e.g. JCS / RFC 8785) with re-serialisation at each hop | Verification depends on every implementation agreeing on key order, number format and escaping; failures are silent and far from their cause |
| 6 | **E.** Publish a hash and have the gateway compare digests | A hash proves integrity, not authorisation; whoever substitutes the document substitutes the digest |
| 7 | **F.** Sign a constructed pre-image such as machine, version and payload digest | A second byte format both ends must agree on, and a field left out of it is silently unprotected |
| 7 | One signature any cabinet accepts | Theft of one signed artifact becomes a fleet-wide event |
| 8 | **G.** Strict monotonicity with no rollback path | A bad profile becomes unrecoverable except by re-provisioning |
| 9 | A second asymmetric algorithm family | Adds library footprint and audit surface to a gateway that already has P-256 |
| 10 | Verification key compiled into the gateway software or image | Key rotation becomes a reinstall or hardware event |
| 12 | Sign the fallback with an operator key | The operator key does not exist when the gateway software is built |
| 12 | Exempt the fallback from verification by name | Creates a class of profile the gateway accepts unverified |

## Consequences

**Positive**

- ADR-0015 decision 7 is implementable, with the gateway pull client and the operator signing tool built on it.
- A stolen profile key cannot issue certificates or sign firmware, and rotating it touches neither gateway identity nor node firmware.
- Signing transmitted bytes removes a class of interoperability bug before a second implementation exists.
- A profile captured from one cabinet is inert at every other.
- The signature is a detached, base64-encoded ECDSA signature travelling beside the base64 document; the runbook and the ERP schema own the encoding.
- The gateway orders versions by the leading integer of the version tag and refuses a tag it cannot order; the tag's form is the profile schema's (ADR-0009).

**Negative**

- A third secret joins the operator's custody obligations, after the CA root and the firmware-signing key, each with different handling rules.
- The ERP cannot query or validate profile contents server-side; a search by setpoint must index beside the bytes.
- Several cabinets on one programme need one signature each, and tooling must make that ergonomic.
- Rollback mints a version; re-activating an old version is refused by the gateway.
- Profile signing is split across two records by artifact until ADR-0009 exists.

## Deferred

| Open question | Settled by |
|---|---|
| Community template signing: who signs, how contributor keys are published and trusted, whether templates carry signatures | ADR-0009 |
| Profile-key rotation and compromise response: wholesale invalidation versus ageing out by decision 8, and how a replacement public key reaches deployed gateways | Maintainers, before the first profile-key rotation |
| Signing at scale: tooling for per-cabinet passes, an intermediate authoring key for a large estate | An estate that needs it |

## References

- ADR-0001, ADR-0004, ADR-0007, ADR-0015, ADR-0020, ADR-0021, ADR-0022, ADR-0024; ADR-0009 (future: profile schema, contribution workflow, registry).
- `signing/README.md`, `signing/sign_profile.py` — the signing runbook, key handling and signature encoding.
- `gateway/profile_client.py` — pull, verify, address and version checks.
- `erp/app/services/profiles.py` — opaque storage and the signature requirement at activation.
