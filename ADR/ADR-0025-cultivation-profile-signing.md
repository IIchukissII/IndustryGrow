<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0025: Deployment-profile signing and gateway verification

- **ID:** ADR-0025
- **Status:** Accepted
- **Date:** 2026-07-26
- **Project:** IndustryGrow
- **Parent:** ADR-0015
- **Companions:** ADR-0001, ADR-0004 (rev 1), ADR-0007 (rev 1), ADR-0021, ADR-0022, ADR-0024
- **Amends:** ADR-0020 decision 5 (permitted gateway persistent state — adds the profile-verification key)
- **Realizes:** the *instance* half of ADR-0015's deferred *"signature scheme"* (see decision 1)

## Context and problem

ADR-0015 decision 7 requires the gateway to verify a profile's signature against a
trusted public key **before** writing it to the active-profile file, and to reject,
log, and keep running the previous version when verification fails. Nothing
implements it, because there is nothing to implement against: no ADR says what
signs a profile, what the signature covers, or where the gateway's trusted key
comes from. The ERP carries a nullable `signed_hash` that nothing sets and nothing
reads.

That gap now blocks the gateway consumer side. The ERP serves the active profile
over the mTLS channel ADR-0022 decision 2 defines, the channel is built and
fail-closed, and the gateway's identity is provisioned. What is missing between
them is the client — and a client cannot honour decision 7 against an undecided
scheme. Building the pull loop first and adding verification later would ship an
edge that applies whatever the channel hands it, which is the one thing decision 7
exists to forbid.

The surrounding records have already fixed most of this one's boundaries. They are
inherited here, not re-opened:

- **The profile is the single mutation interface**; there is no command channel and
  no real-time tuning path (ADR-0015 decision 1).
- **The gateway pulls; nothing pushes.** There is no deploy endpoint (ADR-0022
  decision 8; ADR-0021 decision 12), and pull is what keeps the gateway's firewall
  outbound-only (ADR-0004 rev 1 decision 5; ADR-0015 alternative C).
- **Verification failure behaviour is already decided** — rejected, logged, previous
  version stays active (ADR-0015 decision 7); with a built-in fallback profile if
  there is no previous version to keep (ADR-0015 decision 10).
- **Template, instance, and cache are three different artifacts** (ADR-0021
  decision 12), and profile versioning is monotonic per cabinet (ADR-0015 decision 3).
- **P-256 keys, ECDSA signatures** (ADR-0007 decisions 1–2).
- **Identity PKI and firmware-signing PKI are distinct trust roots** so that a
  compromise or rotation of one does not implicate the other (ADR-0007 decision 8;
  ADR-0004 rev 1 decisions 12–16).
- **The ERP is a relying party, never an authority** (ADR-0024 decision 13), and
  holds operator-private production data (ADR-0021 decision 14).
- **A gateway trust anchor is provisioned, replaceable configuration** — not a value
  compiled in for the life of the hardware (ADR-0007 rev 1 decision 10d).

What remains genuinely open — and is decided below — is which ADR owns this
question at all, who holds the signing key, what the signature covers, how a
signed profile is bound to the cabinet and version it was authored for, and what
the gateway is provisioned with in order to check it.

## Decisions

### Scope, and which ADR owns it

1. **This ADR owns signing and verification for the deployment-specific *instance*
   profile on the ERP-to-gateway channel. Community *template* profile signing
   remains ADR-0009's.** ADR-0015's deferred-decisions list routes "signature
   scheme" to ADR-0009, and that attribution is narrowed here rather than ignored:
   its own qualifier is *"critical for community-contributed profiles per ADR-0001
   decision 5"* — the contributor-authored template case, which was the only
   profile-signing problem visible when ADR-0015 was written. The ERP did not exist
   then, and neither did the instance channel. ADR-0021 decision 12 subsequently
   split the profile layer into three non-overlapping artifacts — public community
   template, private deployment instance, gateway cache — and the first two are
   signed by different principals, for different audiences, with different privacy
   properties. Keeping them in one ADR would make an operator's private control
   channel depend on the design of a public registry.

2. **The gateway has exactly one verification path, because templates never reach it
   directly.** A template becomes an instance in the ERP, authored and signed by the
   operator; the instance is what is stored, pulled, and verified. So the gateway
   needs no way to tell a template from an instance and no dispatch between two
   trusted keys. A future design that routed template profiles to gateways would
   need that dispatch, and needs its own ADR before it is implemented (ADR-0000
   decision 1) — this decision is what such an ADR would have to amend.

### Who signs

3. **The operator signs the profile; the ERP stores a signature it did not
   produce.** The signing key is never present in the ERP container and never on a
   gateway. This is ADR-0024 decision 13's posture applied to a second artifact: the
   ERP is a store and a relying party, and ADR-0021 decision 12 and ADR-0022
   decision 8 already deny it any authoring role over what a cabinet runs. An ERP
   that signed profiles would hold, in a networked container, the one key that can
   make a gateway change behaviour.

4. **The profile-signing key is a third trust root, distinct from both the operator
   CA (ADR-0024) and the firmware-signing key (ADR-0004 rev 1 decision 12).** Three
   authorities, three blast radii: identity says *who a unit is*, firmware says *what
   code runs*, and this one says *what cultivation instructions a cabinet is
   authorised to execute*. ADR-0007 decision 8 already keeps the first two apart for
   exactly this reason. Consolidating profile signing under the firmware root would
   be worse than untidy: that key is deliberately offline and *used only at release
   events*, and profile authoring is a routine operator act, so signing profiles with
   it would put it online in normal operation and destroy the property it exists for.

5. **Custody is proportionate to cadence: a passphrase-encrypted key on an
   operator-controlled host is the baseline, and the ADR-0024 root-key ceremony is
   explicitly *not* required.** The operator root signs an intermediate approximately
   once and can afford an offline ceremony; a profile-signing key is used every time
   anyone changes a setpoint. A control that makes routine work painful is a control
   that gets routed around, and a bypassed signature is worth less than a
   proportionate one. Operators who want the key on a hardware token or an offline
   host may do that; nothing here forbids stronger custody. The concrete procedure
   and any values belong in an implementation document, not here (ADR-0000
   decision 2).

### What the signature covers

6. **The signature covers the exact bytes of the profile document as transmitted,
   and those bytes travel verbatim from signing to verification.** No component
   re-serialises the document, and there is no canonicalisation step anywhere in the
   system. The alternative — parse, re-serialise, and canonicalise at each hop — makes
   every JSON library's key ordering, number formatting, and Unicode escaping part of
   the security boundary. This has a direct consequence the implementation must
   honour: **the ERP stores and returns the signed artifact as opaque bytes.** A store
   that keeps the profile as a parsed document and rebuilds it on read breaks every
   signature it holds, and does so silently at the point of verification rather than
   at the point of storage.

7. **The document carries its own machine identifier and version, so one signature
   over one byte string binds content, addressee, and version together.** No separate
   pre-image is constructed and no field is signed out of band. The gateway rejects a
   document whose machine identifier is not its own, and it learns its own from the
   `CN` of its client certificate (ADR-0007 rev 1 decision 10b) — the same string the
   ERP authorised the pull by (ADR-0022 decision 2), so the two ends agree by
   construction rather than by configuration. A validly signed profile for one cabinet
   is therefore inert at any other, which means a cultivation programme rolled out to
   several cabinets is signed once per cabinet. That is a deliberate cost: cabinets are
   distinct machines with distinct identities, and a profile that any cabinet would
   accept is a profile whose theft is a fleet-wide event.

8. **The gateway refuses a version that is not greater than the one it is running,
   and a rollback is therefore issued as a new, higher version carrying the earlier
   content.** A signature does not expire, so an old artifact stays valid forever and
   a replayed one is otherwise indistinguishable from a current one — the downgrade is
   the attack that a purely signature-based check cannot see. ADR-0015 decision 3
   already makes versioning monotonic per cabinet; this makes the gateway *enforce*
   that rather than assume it. Re-serving an old artifact is not a supported rollback
   mechanism, and the platform-side rollback operation ADR-0015 left deferred must
   mint a new version to stay compatible with this.

### Primitives, and what the gateway is provisioned with

9. **ECDSA on NIST P-256 with SHA-256.** The curve is already the project's, because
   it is the ATECC608B's native one (ADR-0007 decision 1); a second asymmetric family
   would add library footprint and audit surface to a gateway that already has this
   one for its identity.

10. **The gateway's profile-verification public key is provisioned, replaceable
    configuration.** It is not compiled into the firmware and not baked into an image
    — the same shape ADR-0007 rev 1 decision 10d requires of the identity trust
    anchor, for the same reason: a key that cannot be replaced without new hardware
    turns key rotation into a hardware event. It is public material, so it is
    configuration rather than a secret, and it is added to ADR-0020 decision 5's
    enumeration of permitted gateway persistent state — the profile-signing public
    key is of a kind with the operator CA trust anchor already listed there by
    ADR-0024 decision 4. ADR-0020 decision 5 carries the inline note pointing back
    here, so that enumeration keeps one authoritative home.

11. **A signature is not optional for anything a gateway can pull.** The ERP must
    refuse to record a version as active without one, so that the absence is caught
    where an operator can fix it rather than at a cabinet that has stopped taking
    updates. What the gateway does when verification fails is ADR-0015 decision 7's
    and is not restated here.

12. **The built-in fallback profile (ADR-0015 decision 10) is out of scope for this
    scheme and must not be verified against the profile-signing key.** It ships inside
    the firmware image and its integrity comes from the firmware signature chain
    (ADR-0004 rev 1 decisions 12–14) — a different trust root, checked at a different
    time, by the node bootloader rather than by the profile client. This is stated
    because both ways of getting it wrong are attractive: signing the fallback with an
    operator key that does not exist at build time is impossible, and exempting "the
    fallback" from verification by name creates a class of profile the gateway accepts
    unverified. It is not an exemption — it is a different chain.

## Alternatives considered

**A. The ERP signs profiles on the operator's behalf.** One less key for the operator
to hold, and signing at upload is easy to automate. *Rejected:* it puts the key that
controls cabinet behaviour inside a networked container holding production data, and
contradicts ADR-0024 decision 13 and ADR-0021 decision 12 — the ERP is a store and a
record, not an authority over what a cabinet runs. It would also make an ERP
compromise a cultivation-control compromise, collapsing two blast radii into one.

**B. Sign profiles with the firmware-signing key.** No third root; the operator
already holds an offline signing key. *Rejected:* ADR-0004 rev 1 decision 12 keeps
that key offline and used only at release events precisely so a gateway compromise
cannot reach node firmware. Profiles are authored routinely, so this would bring the
key online as a matter of course and dissolve the separation ADR-0007 decision 8
records.

**C. Sign profiles with the operator CA key.** The operator already runs a CA with a
custody model and tooling. *Rejected:* it conflates *who a unit is* with *what a unit
should do*. A CA key that also authorises behaviour means every certificate issuance
is performed with a key that can retune a cabinet, and rotating either concern forces
rotation of the other.

**D. Canonical JSON (e.g. JCS / RFC 8785) with re-serialisation at each hop.** Lets
each component hold the profile as a parsed object, which is more natural for a store
and an API. *Rejected:* it makes correct signature verification depend on every
implementation agreeing on key order, number formatting, and escaping. The failure is
silent, appears far from its cause, and would be re-litigated in every language the
project adds. Signing bytes and moving bytes has no such failure mode.

**E. A hash instead of a signature — publish digests and have the gateway compare.**
Simpler, no key management. *Rejected:* a hash establishes that a document was not
corrupted, not that anyone authorised it. Whoever can substitute the document can
substitute the digest, so it does not satisfy ADR-0015 decision 7's "verify against a
trusted public key" and provides no authenticity at all on a compromised channel.

**F. Sign a constructed pre-image such as `GBOX_0001|v42|sha256(payload)`.** Binds
machine and version explicitly without requiring them inside the document.
*Rejected:* the construction becomes a second format that both ends must agree on
byte-for-byte — canonicalisation reintroduced through the back door, with the
additional hazard that a field left out of the pre-image is silently unprotected.
Putting the identifiers inside the signed bytes achieves the same binding with
nothing to agree on.

**G. Enforce strict monotonicity with no rollback path at all.** *Rejected:* it makes
a bad profile unrecoverable except by re-provisioning, and operators need to be able
to go back. Requiring a rollback to be issued as a new higher version (decision 8)
keeps the anti-downgrade property while leaving the operation available.

## Consequences

### Positive

- ADR-0015 decision 7 becomes implementable, which unblocks the gateway profile-pull
  client and the signing path — both of which were waiting on this and neither of
  which now has to guess.
- Three separable authorities mean an operator can rotate the profile key after a
  workstation compromise without touching gateway identity or node firmware, and a
  stolen profile key cannot issue certificates or flash nodes.
- Signing transmitted bytes removes a whole class of interoperability bug before any
  second implementation exists, and makes verification failures mean what they say.
- The per-cabinet binding means a profile captured from one deployment is inert
  everywhere else, so the theft of a signed artifact is not a fleet-wide event.

### Negative

- **A third offline-ish secret joins the operator's custody obligations**, after the
  CA root and the firmware-signing key. Three keys with three different handling
  rules is a real burden on a self-hoster, and a key an operator does not know they
  have is a key that is poorly held. Decision 5 deliberately keeps this one's custody
  lighter than the CA root's for that reason, and the runbook has to be explicit
  about which key is which.
- **The ERP must store profile artifacts as opaque bytes**, which is a change to how
  it holds them today and costs it the ability to query or validate profile contents
  server-side. That loss is mostly acceptable — ADR-0021 decision 13 already treats a
  version as one whole artifact — but any future "search profiles by setpoint"
  feature must index alongside the bytes rather than parse them on read.
- **One signature per cabinet per version.** An operator running several cabinets on
  one cultivation programme signs it once for each, and tooling has to make that
  ergonomic or operators will look for a way around it.
- **Rollback requires minting a version, not selecting one.** This is a change in how
  the deferred platform-side rollback operation must work, and anyone who implements
  it as "re-activate version N-1" will find gateways refusing the result.
- **This ADR takes a number rather than writing ADR-0009**, so the profile signature
  scheme is now split across two records by artifact. The boundary is stated in
  decisions 1 and 2, but a reader looking for "how are profiles signed" has two
  places to look until ADR-0009 exists.

## Deferred decisions

- **Community template signing** — who signs a registry template, how a contributor's
  key is published and trusted, and whether templates carry signatures at all.
  ADR-0009's, per decision 1.
- **Profile-signing key rotation and compromise response.** What an operator does
  when the profile key is lost or exposed, whether previously signed versions are
  invalidated wholesale or aged out by decision 8's monotonic counter, and how the
  replacement public key reaches already-deployed gateways.
- **The signature's encoding and the artifact's field layout** — DER or raw `R||S`,
  its transport encoding, where it sits relative to the document, and the name of the
  field that carries it. Implementation detail, owned by the document that owns the
  profile artifact's format (ADR-0000 decision 2); this ADR fixes only that a
  detached ECDSA signature over the exact bytes must be present.
- **Whether the version counter is the existing version tag or a separate monotonic
  number.** Decision 8 requires a totally ordered version the gateway can compare;
  ADR-0015 decision 3 already requires monotonic versioning per cabinet, and whether
  today's tag satisfies that ordering is a schema question for ADR-0009 and the
  implementation.
- **Signing at scale for many cabinets** — whether a per-cabinet signing pass is
  driven by tooling, and whether an operator managing a large estate needs an
  intermediate authoring key. Out of scope until an estate exists that needs it.

## References

- ADR-0015: Gateway profile caching and local control loops — decision 7 (verify
  before apply, the requirement this ADR makes implementable), decision 3 (monotonic
  versioning), decision 4 (`active-profile.json`), decision 5 (pull), decision 10
  (the firmware-borne fallback profile decision 12 scopes out), and the deferred
  *"signature scheme"* this ADR narrows in decision 1.
- ADR-0021: Instance-and-integration ERP — decision 12's template/instance/cache split
  (the basis for decision 1's boundary), decision 13 (a version is one whole
  artifact), decision 14 (operator-private production data).
- ADR-0022: ERP API — decision 2 (the gateway's identity comes from its certificate,
  never a parameter), decision 8 (the API stores and records, it does not deploy).
- ADR-0007 (rev 1): PKI, hardware identity, and provisioning — decision 1 (P-256),
  decision 8 (distinct trust roots), decision 10b (`CN=GBOX_NNNN` verbatim, which
  decision 7 here reads), decision 10d (the replaceable-anchor shape decision 10
  here follows).
- ADR-0024: Operator CA bootstrap and the root-key ceremony — decision 13 (the ERP is
  a relying party, never an authority), decision 4 (the amendment pattern decision 10
  here reuses), decisions 5–7 (the custody model decision 5 here deliberately does
  not require).
- ADR-0004 (rev 1): Gateway host hardening — decision 5 (outbound-only firewall, why
  pull), decisions 12–16 (the firmware-signing trust root kept separate here).
- ADR-0020: Gateway persistence model — decision 5's enumeration of permitted
  persistent state, amended by decision 10 here.
- ADR-0001: IndustryGrow framing — decision 5 (the community-contributed model whose
  signing stays with ADR-0009).
- ADR-0009 (future): cultivation profile schema, contribution workflow, registry
  design — retains template signing, the profile document schema, and version
  compatibility rules.
