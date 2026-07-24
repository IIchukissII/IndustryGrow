<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0024: Operator CA bootstrap and the root-key ceremony

- **ID:** ADR-0024
- **Status:** Proposed
- **Date:** 2026-07-24
- **Project:** IndustryGrow
- **Parent:** ADR-0007
- **Companions:** ADR-0001, ADR-0004 (rev 1), ADR-0015, ADR-0017 (rev 1), ADR-0022
- **Amends:** ADR-0020 decision 5 (permitted gateway persistent state — adds the operator trust anchor)
- **Realizes:** ADR-0007 deferred decisions *"Self-hoster CA bootstrap tooling and documentation"* and *"Operator root-key ceremony"*

## Context and problem

ADR-0007 decided that trust is rooted **per operator** (decision 3) and then left the
question of how an operator actually stands one up to two deferred decisions — the
self-hoster bootstrap tooling, and the ceremony around generating, holding, backing
up, and rotating the root key.

That gap is now load-bearing. ADR-0022 decision 2 makes a certificate issued under
the operator root the **only** machine credential the ERP accepts, and the mTLS
termination that enforces it is built: the proxy verifies the chain, and the
application derives `GBOX_NNNN` from the verified DN rather than from any request
parameter. So there is a working channel whose entire premise is *"issued under the
operator root"* — and no operator root. The only CA in the repository is
`erp/deploy/mtls/make-test-ca.sh`, which states accurately that it is a throwaway
fixture and **not** a bootstrap procedure.

Three pieces of pending work block on the same missing thing: a real gateway
certificate, the gateway-side client that authenticates with it, and the `-PR`
provisioning record that binds it to a serial. None can be built against a CA that
does not exist, and building any of them against the test fixture would bake a
disposable trust root into the parts of the system hardest to re-provision.

The surrounding records have already fixed this one's boundaries. They are
inherited here, not re-opened:

- **Trust is rooted per operator; there is no global IndustryGrow root** (ADR-0007
  decision 3). This ADR decides how *an* operator root is stood up, never whether
  it is shared.
- **P-256 keys, X.509 certificates, ECDSA signatures** (ADR-0007 decisions 1–2).
- **Gateway certificates are short-lived and auto-renewed; revocation is cease-renewal
  plus a platform-side deny-list, not CRL/OCSP** (ADR-0007 decision 7).
- **A private key is generated where it will live and does not move** — on-chip for
  device identity (ADR-0007 decision 1). The same principle governs the CA keys here.
- **Identity PKI and firmware-signing PKI are distinct trust roots** (ADR-0007
  decision 8; ADR-0004 rev 1 decisions 12–16). Nothing here touches firmware signing.
- **The gateway is a stateless, replaceable edge** whose permitted persistent state is
  enumerated in ADR-0020 decision 5.
- **The self-build path must be completable from open artifacts** (ADR-0001 decision 6).

What remains genuinely open — and is decided below — is the shape of the chain
between root and leaf, what the root key physically is and who holds it, what
happens when it is backed up, lost, rotated, or compromised, and where the
bootstrap tooling is allowed to run.

## Decision drivers

- **A key's exposure should be proportional to its blast radius.** Compromise of an
  issuing key is recoverable — re-issue under the root. Compromise of the operator
  root is not: every certificate in the estate descends from it, and recovery means
  re-provisioning every unit. These two very different risks must not sit on the
  same key.
- **Short-lived leaves force an online issuer.** ADR-0007 decision 7 requires gateway
  certificates to be short-lived and auto-renewed, which makes whatever signs them an
  effectively-online key. Any design that satisfies the renewal cadence *with the root*
  has silently made the root an online key without ever deciding to.
- **A ceremony that is too heavy is a ceremony that is skipped.** ADR-0007's own
  negative consequence asks tooling to *lower* the self-hoster's CA burden, not remove
  it. A procedure demanding dedicated hardware, a second site, and a quorum of
  custodians will be quietly abandoned by a one-person deployment, and a skipped
  ceremony protects nothing.
- **Recoverability, because the failure is silent.** A lost root key announces itself
  not at the moment of loss but at the next issuance, potentially months later. The
  custody model must survive one destroyed copy without ceremony of its own.
- **Both deployment models keep the same topology** (ADR-0001, ADR-0007 decision 3).
  Self-hosted and commercial operators differ in who performs the ceremony and under
  what controls, not in the shape of the chain their gateways verify.
- **Standards over bespoke crypto** (inherited from ADR-0007): ordinary X.509 path
  validation, nothing a TLS stack cannot check against an RFC.

## Decision

### Chain shape

1. **Two tiers: the offline operator root signs exactly one issuing intermediate CA,
   and every leaf is issued by that intermediate — never by the root.** This is what
   makes ADR-0007 decision 7 and an offline root compatible: the intermediate absorbs
   the online exposure that a short renewal cadence demands, and the root signs on the
   order of once per intermediate lifetime. A compromised intermediate is recovered by
   re-issuing under the root and redistributing nothing; a compromised root is the
   unrecoverable event decision 11 describes. Collapsing the tiers would trade that
   asymmetry away for a shorter chain.

   The leaf classes are named, because "all leaves" has hidden one before: the **gateway
   client certificate** (ADR-0022 decision 2), the **ERP server certificate** the gateway
   validates that channel against, and — when node provisioning is built — the **node
   provenance certificate** ADR-0007 decision 5 permits. The first two are short-lived
   and renewed; the third is not, and decision 8 says why that is not a contradiction.

2. **The root signs certificate authorities only — the intermediate, and at rotation
   its successor.** The intermediate is constrained so it cannot mint further
   authorities: it is the terminal CA in the path, and every certificate below it is
   an end-entity certificate. This is enforced in the certificates themselves rather
   than by procedure, so a mistake at issuance time fails path validation instead of
   silently widening the CA.

3. **The trust anchor distributed to relying parties is the root, not the
   intermediate.** The intermediate travels in the chain each peer presents, as
   ordinary X.509 path building expects. Consequently, replacing the intermediate —
   routine, and the whole point of tier separation — requires no trust distribution to
   anyone at all.

   There are **two** relying parties, not one: the **gateway**, which validates the ERP's
   server certificate, and the **ERP's terminating proxy**, which validates gateway
   client certificates (ADR-0022 decision 2). Both hold the same anchor, and anything
   that changes it — decision 10 — changes it in both places, or breaks the channel in
   one direction only.

4. **The operator trust anchor is gateway configuration state, and this ADR adds it to
   the enumeration in ADR-0020 decision 5.** That decision lists what may persist on the
   gateway — `active-profile.json`, the batch sequence number, firmware artifacts, the
   ATECC-bound identity — and names no trust anchor, because the PKI that would need one
   had not been stood up. It is added here, and ADR-0020 decision 5 carries the inline
   note pointing at this decision, so the enumeration keeps one authoritative home
   (ADR-0000 decision 3) rather than being restated in two records.

   The anchor is trust *configuration*, of a kind with the identity certificate beside
   it. ADR-0015 decision 4 drew the configuration/operational line for
   `active-profile.json`, and ADR-0020 has since relaxed that line's *operational* half —
   a bounded operational buffer is now permitted. The half this rests on is untouched:
   configuration state on the gateway is allowed, and a static anchor the unit is
   provisioned with is configuration. What the stateless-edge model still excludes is
   gateway-side *revocation machinery* — CRL fetching, OCSP lookups, a store that grows
   with fleet history — all of which ADR-0007 decision 7 already declined.

### The root key and its custody

5. **The operator root key is generated on an offline machine, is stored
   passphrase-encrypted, and is never present on a networked host.** It is generated
   where it will live and does not move — the same principle ADR-0007 decision 1
   applies to device keys, applied to the key that certifies them. "Offline" is a
   property of the machine at generation time, not a promise about the medium.

6. **Custody is two encrypted copies on separate removable media, stored in separate
   physical locations, with the passphrase held apart from both.** Two copies because a
   single copy makes ordinary media failure an estate-wide event; separate locations
   because co-located copies share a fire; the passphrase apart because media and
   secret travelling together is one theft, not two. This is redundancy, deliberately
   not a threshold scheme (alternative C).

7. **A hardware token is a supported alternative for the root, not a migration target
   the file-based path is waiting on.** A PIV-style token holding a non-exportable root
   key is stronger custody, and is the natural match for a project whose device identity
   rests on exactly that property. It is not the default because it would put a hardware
   purchase on the critical path of the self-build path ADR-0001 decision 6 requires to
   be completable from open artifacts, and would add a PKCS#11 dependency to the one
   procedure that must run on an otherwise-bare offline machine. Both paths are
   permanently supported and the runbook documents both; an operator on the file path is
   not carrying deferred work.

### Validity relation

8. **This ADR fixes the *relation* between tier lifetimes, not the values: root ≫
   intermediate ≫ leaf, and each tier's validity must strictly contain the validity of
   everything it issues, plus a renewal margin.** The concrete windows are values, and
   values live in the document whose job they are (ADR-0000 decision 2) — here the
   bootstrap runbook, alongside the numbers ADR-0007 already deferred for leaf lifetime
   and renewal cadence. What belongs on the record is the invariant: a leaf that
   outlives its issuer is a certificate that stops validating for a reason no log will
   explain clearly.

   The invariant binds certificates that are checked by **path validation at connect
   time** — the gateway client and ERP server certificates. It does not bind the
   long-lived node provenance certificate of ADR-0007 decision 5, which that ADR's
   decision 7 already exempts from freshness: it is validated against the `-PR` record,
   as a statement about a unit's origin, not as a live credential. How such a
   certificate is issued without either shortening its life or outliving its issuer is
   left open below, because node provisioning does not exist yet and guessing at it here
   would fix a shape nothing has tested.

### Rotation and compromise

9. **Intermediate rotation is routine and invisible to relying parties.** The root comes
   out of custody, signs a successor intermediate, and returns. Because both relying
   parties anchor on the root (decision 3), nothing is redistributed and no unit is
   touched.

10. **Root rotation is a planned overlap, not a cutover, and it is distributed
    out-of-band.** The successor root is placed in each relying party's trust set —
    every gateway *and* the ERP's proxy (decision 3) — *before* the outgoing root stops
    issuing, and the outgoing root is retired only once every unit is confirmed to carry
    the new anchor. During the overlap a gateway trusts two roots; both are roots of the
    **same operator**, so ADR-0007 decision 3's "the root of its own operator and no
    other" is satisfied, not bent.

    Distribution is the operator provisioning path — the same hands-on route by which a
    unit gets its identity in the first place — and explicitly **not** the profile
    channel. Routing trust material through the single mutation interface of ADR-0015
    decisions 1 and 4 would make the control-plane path a trust-distribution path, and
    the verification that
    would have to secure it is itself unwritten (the profile signature scheme is
    deferred). Root rotation is rare, planned, and already touches every unit; it does
    not justify widening the one channel ADR-0015 deliberately kept narrow.

11. **Compromise of the operator root is a re-provisioning event, not a rotation.**
    ADR-0007 decision 7 deliberately provides no CRL/OCSP path, so there is no
    mechanism by which a compromised root is disowned in the field; every certificate
    beneath it must be treated as forged. The honest response is a new root and a
    re-provisioned estate. Recording this plainly is the point: it is the consequence
    that justifies decisions 1, 5, and 6, and an operator who does not know it will
    under-invest in exactly those.

### Scope and trust boundary of the tooling

12. **This ADR governs the self-hosted operator root.** The IndustryFlow-managed CA of
    ADR-0007 decision 3 adopts the same chain shape, validity relation, and rotation
    ordering — a gateway must not be able to tell which kind of operator certified it —
    but its custody controls, staffing, and audit obligations are commercial-operations
    concerns this ADR does not decide. They are listed as deferred below rather than
    assigned to a record that does not exist, so the gap stays visible if that record is
    never written.

13. **The bootstrap tooling lives outside the ERP and never runs inside it.** The root
    ceremony runs on an offline machine with no application runtime, and the ERP is a
    networked service holding operator-private production data (ADR-0021 decision 14) —
    precisely the host decision 5 keeps the root away from. Shipping CA tooling inside
    the ERP would place the bootstrap procedure in the artifact least entitled to
    perform it, and would suggest a trust relationship the chain does not have: the ERP
    is a *relying party* on this PKI, never an authority within it.

## Alternatives considered

**A. One tier — the operator root issues leaves directly.** The shortest chain and the
least tooling. *Rejected:* ADR-0007 decision 7 requires short-lived, auto-renewed
gateway certificates, so the issuing key must be reachable on the renewal cadence. A
one-tier CA therefore puts the operator root online — the single outcome the entire
custody model exists to prevent — and does so as a side effect of a lifetime decision
rather than as a decision anyone made. It also makes issuing-key compromise and
root compromise the same event, discarding the recoverability asymmetry of decision 1.

**B. Three tiers — root, policy CA, issuing CA.** The conventional enterprise layout.
*Rejected:* the middle tier earns its keep where there are multiple issuing domains,
delegated issuance, or distinct certificate policies to separate. A self-hosted
operator has one estate and one issuance policy, so the third tier adds a key to
protect, a lifetime to track, and a rotation to perform, in exchange for a separation
nothing in the deployment uses.

**C. Split the root key across custodians with a Shamir / M-of-N scheme.** The textbook
answer for high-value root custody. *Rejected:* a threshold scheme presumes a quorum of
custodians exists, and the self-hosted deployment ADR-0007 targets is frequently one
person. It converts a recoverable loss (one medium destroyed, the second copy intact)
into an unrecoverable one (shares below quorum), and reconstruction gathers the full key
onto one machine anyway — the moment of peak exposure — for a threat model that plain
redundancy plus a separately-held passphrase (decision 6) already addresses.

**D. Require a hardware token for the root.** *Rejected as the default, adopted as a
supported alternative (decision 7):* requiring it makes a hardware purchase a
precondition for standing up an open-core self-hosted deployment, which contradicts
ADR-0001 decision 6, and adds a PKCS#11 dependency to a procedure whose defining
constraint is that it runs on a machine with nothing installed on it. The custody *goal*
— a non-exportable root — is right; mandating the mechanism is what fails the deployment
model.

**E. Run an ACME issuance service (e.g. step-ca) as the operator CA.** Automates
issuance and renewal end to end. *Rejected for the bootstrap:* it is a service — a
running process, a database, an operational surface — and it does not remove the
problem this ADR exists to solve, it relocates it, because such a service still needs a
root and that root still needs this ceremony. For an estate of one to a handful of
gateways it is more machinery than the renewal cadence justifies, and ADR-0007 decision
7's revocation model needs none of what ACME provides. It remains the natural upgrade
once fleet size makes manual issuance the bottleneck, and decisions 1–3 leave room for
it: an ACME issuer would take the intermediate's place without the root or any relying
party's trust anchor changing.

**F. Distribute a rotated root through the profile channel.** Reuses the one path that
already reaches every gateway. *Rejected (decision 10):* it makes the single mutation
interface of ADR-0015 decisions 1 and 4 carry trust material, and the signature scheme that would
have to authenticate such a delivery is itself unwritten — so the mechanism would rest on
a verification step that does not exist. The bootstrapping objection is not fatal (the
firmware-signing key is a separate root, ADR-0007 decision 8, so it *could* verify a
bundle), but a rare, planned, already-hands-on event is a poor reason to widen a channel
ADR-0015 deliberately kept to one purpose.

**G. Treat the test fixture as the bootstrap procedure — harden `make-test-ca.sh` in
place.** *Rejected:* its purpose is to be disposable so that the proxy configuration and
the identity-extraction seam can be exercised offline, and that purpose is served by
unencrypted keys. Growing custody, encryption, and rotation into it would produce a
script that is neither a good fixture nor a trustworthy ceremony, and would leave the
tests depending on the real procedure's tooling. It does, however, now have to mirror
the real chain's *shape* — a fixture that issues from a flat root would no longer be
exercising what deployments run.

## Consequences

### Positive

- The operator root can be genuinely offline while ADR-0007 decision 7's short renewal
  cadence is still met, because the two roles sit on different keys.
- Intermediate rotation and issuing-key compromise both become recoverable without
  touching a single deployed gateway (decisions 3, 9).
- ADR-0022's mTLS channel gains the trust root it has been assuming since the day it was
  built, and the pending gateway-certificate, profile-pull, and `-PR` work stops being
  blocked on a CA that does not exist.
- The gateway's trust anchor is now named persistent state (decision 4) rather than an
  unrecorded assumption, closing a gap between ADR-0020's enumeration and what the PKI
  has always required.
- The self-build path stays completable from open artifacts and a machine the builder
  already owns (decision 7).
- The commercial and self-hosted paths remain indistinguishable to a gateway (decision
  12), so the migration ADR-0007 defers stays a change of operator, not of architecture.

### Negative

- **The operator now runs a two-key PKI.** Two keys to protect, two lifetimes to track,
  and a rotation procedure per tier — more than the one-tier alternative, and the cost
  of the recoverability decision 1 buys.
- **Root compromise remains unrecoverable** (decision 11). This ADR bounds how likely it
  is; it cannot bound the consequence, because ADR-0007 decision 7 deliberately declined
  the revocation infrastructure that would.
- **The ceremony depends on the operator's discipline, not on enforcement.** Nothing in
  the tooling can verify that the second copy really went to a second location, or that
  the passphrase is not on the same USB stick. The file-based default (decision 7)
  accepts this in exchange for a procedure that gets performed at all.
- **Root rotation requires reaching every unit during the overlap window** (decision 10),
  by hand, because the delivery path is deliberately the provisioning path. A gateway
  offline across the whole window is stranded and must be re-provisioned — the
  replaceability path ADR-0004 already assumes, but a path someone has to walk.
- **A second offline secret joins the firmware-signing key** (ADR-0004 rev 1 decision
  12). The two are deliberately separate trust roots (ADR-0007 decision 8), so the
  operator now has two independent custody obligations rather than one.
- **The existing mTLS deployment material describes a flat chain.**
  `erp/deploy/mtls/` and its test fixture were written against a single root issuing
  leaves directly; both must be brought to the two-tier shape, or the configuration
  shipped to operators will not match the CA they were told to build.

## Deferred decisions

- **Concrete validity windows and renewal cadence.** ADR-0007 already defers these;
  decision 8 fixes only the ordering they must satisfy. The numbers are settled in the
  bootstrap runbook and with the gateway-certificate work.
- **Node provenance certificate issuance** (decision 8). ADR-0007 decision 5 permits a
  long-lived node leaf while decision 8 here requires an issuer to outlive what it
  issues. Whether such certificates come from the same intermediate, a longer-lived
  sibling under the same root, or the root itself as a narrow exception is open until
  node provisioning is built.
- **The revocation deny-list key.** ADR-0007 defers the platform-side mechanics, but the
  short-lived-leaf model makes one constraint concrete: the deny-list must key on
  something **stable across renewal** — the hardware's public-key fingerprint or its
  `GBOX_NNNN` — because a list keyed on leaf certificate serial would need re-entering
  every time the revoked unit's certificate rolled over.
- **What the self-hosted provisioning pipeline must not hard-code.** ADR-0007 defers
  self-hosted ↔ commercial migration and cross-signing. Decision 12 keeps the topologies
  identical, which is a precondition for that migration but not a design for it; the
  constraint on what the pipeline may bake in (a specific root identity in the `-PR`, a
  single-anchor trust store) has to be fixed before the pipeline is written, or the
  migration path closes quietly.
- **Commercial-operator CA custody, staffing, and audit obligations** (decision 12).
  Named here rather than assigned, because the record that would own them does not exist.
- **Automated issuance.** If fleet size makes manual issuance the bottleneck, an ACME
  issuer in the intermediate's position (alternative E) is the intended path; the
  trigger and the choice of implementation are open.
- **Where the intermediate key lives operationally** — which host holds it, under what
  file ownership and disk protection — is a deployment concern, settled with the
  single-tenant container deployment.
- **The `-PR` provisioning record's certificate fields.** ADR-0007 defers the record
  format; this ADR supplies the issuing chain it will reference but does not fix its
  schema.

## References

- ADR-0000 (rev 1): Decision records — the why/what split that keeps validity *values*
  out of decision 8.
- ADR-0001: IndustryGrow framing — the two deployment models, and the self-build path
  that decision 7 protects.
- ADR-0004 (rev 1): Gateway host hardening — the stateless edge, and the separate
  offline firmware-signing key that stays out of this PKI.
- ADR-0007: PKI, hardware identity, and provisioning — the parent record; per-operator
  rooting (d3), P-256/X.509 (d1–2), the node provenance certificate (d5), short-lived
  auto-renewed leaves and no CRL/OCSP (d7), and the two deferred decisions this ADR
  realizes.
- ADR-0015: Gateway profile and control loops — the single mutation interface
  (decisions 1, 4) that decision 10 declines to reuse, and decision 4's
  configuration-vs-operational state line that decision 4 here relies on.
- ADR-0017 (rev 1): Component, document, and instance identification — the serial ↔
  certificate binding and the `-PR` record the issued leaves will be recorded in.
- ADR-0020: Gateway persistence model — decision 5's enumeration of permitted persistent
  state, which decision 4 adds the trust anchor to.
- ADR-0021: Instance-and-integration ERP — the operator-private production data (its
  decision 14) that makes the ERP a relying party rather than an authority (decision 13
  here).
- ADR-0022: ERP API — decision 2's gateway client certificate, the credential this CA
  exists to issue.
- RFC 5280 (X.509 / PKIX): path validation, basic constraints, key usage.
