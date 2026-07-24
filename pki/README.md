<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# The operator CA — bootstrap and ceremony

[ADR-0024](../ADR/ADR-0024-operator-ca-bootstrap-and-key-ceremony.md) decides the
shape of this CA and why. This file is the *procedure* and the *numbers* — the
`what` to that record's `why`, per [ADR-0000](../ADR/ADR-0000-decision-records-and-source-of-truth.md)
decision 2.

[ADR-0007](../ADR/ADR-0007-pki-and-secure-element-identity.md) decision 3 roots
trust **per operator**: there is no IndustryGrow root CA, and nobody can issue
into your deployment but you. That independence is the point, and the price of it
is this procedure.

## The chain

```
  operator root            offline, encrypted, on removable media  (d1, d5, d6)
        │                  signs certificate authorities only      (d2)
        ▼
  issuing CA               on the ERP host; CA:TRUE, pathlen:0     (d1, d2)
        │
        ├──► gateway leaf  CN = GBOX_NNNN, clientAuth              (ADR-0022 d2)
        └──► ERP leaf      SAN required, serverAuth
```

Gateways and the ERP's proxy anchor on the **root** (d3). The intermediate travels
in the chain each peer presents. That is why replacing the intermediate touches no
deployed unit, and replacing the root touches every one of them.

## Lifetimes

| Tier | Validity | Renewed |
|------|----------|---------|
| operator root | 3650 days (10 y) | rotation only — see below |
| issuing CA | 1095 days (3 y) | at ~2 y, from the offline root |
| gateway / ERP leaf | 90 days | routinely, per ADR-0007 d7 |

These are the values; the *decision* is decision 8's relation — root ≫ intermediate
≫ leaf, each issuer outliving what it issues plus a renewal margin. `sign-csr.sh`
and `issue-intermediate.sh` enforce it and refuse to mis-issue, so shortening a tier
without renewing the one above it fails at issuance rather than months later at a
handshake.

## Before you start

You need: `bash`, `openssl` (3.x), removable media ×2, and somewhere to write down
a passphrase that is not those media.

**Do step 1 and 2 on a machine that is offline** (d5). Nothing downstream can tell
whether you did, and no later check will catch it — that is exactly why it is a
ceremony rather than a build step.

## Step 1 — the operator root

```bash
./bootstrap-root.sh --dir ./ca --operator OP-STRAWBERRY-01
```

Prompts for the passphrase that protects the key at rest. Produces
`ca/operator-root.key` (encrypted) and `ca/operator-root.crt` (the trust anchor —
public, copy it freely).

The operator name is yours; it appears in every subject in the estate. Pick it once.

## Step 2 — the issuing CA

```bash
./issue-intermediate.sh --dir ./ca
```

Asks for the root passphrase, then a new one for the issuing key. Produces
`ca/issuing-ca.key` and `ca/issuing-ca.crt`, and verifies the chain before
reporting success.

**This is the only thing the root ever signs** (d2). After it, the root is not
needed again until you renew the intermediate or rotate the root.

## Step 3 — custody (this is the step that matters)

```bash
cp ca/operator-root.key /media/stick-a/
cp ca/operator-root.key /media/stick-b/
shred -u ca/operator-root.key      # or your platform's equivalent
```

Then, per decision 6:

- the two media go in **two different physical places** — co-located copies share
  a fire;
- the **passphrase is held apart from both** — media and secret travelling together
  is one theft, not two;
- `operator-root.crt` stays; it is public.

Nothing in the tooling can verify any of this. If the second copy is in the same
drawer, you have one copy with extra steps.

**Losing every copy, or someone else gaining one, means re-provisioning every unit
in the estate** (d11). ADR-0007 decision 7 deliberately provides no revocation path
that would soften that.

### If you would rather use a hardware token

A PIV token holding a non-exportable root key is stronger custody and is a fully
supported choice, not a future migration (d7). It is not the default because it
would make a hardware purchase a precondition for a self-hosted build, and add a
PKCS#11 dependency to the one procedure that must run on a bare offline machine.

The tooling here does not drive a token. Generate and self-sign the root on the
token with your vendor's tooling, export `operator-root.crt`, and use
`openssl ... -engine pkcs11` in place of step 2's signing call. Everything from
step 2's output onward is unchanged — the intermediate and every leaf are the same
certificates either way.

## Routine issuance

Move `ca/issuing-ca.*` to the host that will issue (not the offline machine, and
not inside the ERP container — d13). Leave the root key behind; `sign-csr.sh`
refuses to run beside it.

```bash
# a gateway. The CSR comes from the unit — its key is generated on the ATECC608
# and never leaves it (ADR-0007 d1), so a CSR is the only thing that can travel.
./sign-csr.sh --dir ./ca --csr GBOX_0001.csr --profile gateway

# the ERP's own certificate
./sign-csr.sh --dir ./ca --csr erp.csr --profile server \
              --san 'DNS:erp.local,IP:127.0.0.1' --out ./ca/server.crt
```

Each run writes `NAME.crt` and `NAME-chain.crt`. **Present the chain**, not the
bare certificate and not the root: a peer that ships its own root is asking the
other side to trust it on its own say-so, and a peer that ships only its leaf gives
the verifier no path to the anchor it holds.

The gateway CN must be the ADR-0017 machine identifier verbatim — `GBOX_0001`,
no prefix, no vendor form. The ERP derives the caller's identity from that exact
string, so the script rejects anything else rather than issuing a certificate that
would authenticate as nothing.

## Renewing the issuing CA

At roughly two years, before the margin warning becomes a refusal:

```bash
mv ca/issuing-ca.crt ca/issuing-ca.crt.old
mv ca/issuing-ca.key ca/issuing-ca.key.old
# bring the root back from its media, on the offline machine
./issue-intermediate.sh --dir ./ca
```

Nothing is redistributed. Gateways anchor on the root, so they neither notice nor
need to (d9). Re-issue leaves as they expire; keep the old intermediate until the
last certificate it signed has rolled over.

## Rotating the root

Rare, planned, and it touches every unit (d10).

1. Bootstrap a **new** root on the offline machine, into a separate directory.
2. Issue a new intermediate under it.
3. Distribute the new root into the trust set of **every gateway *and* the ERP's
   proxy** — both are relying parties (d3), and updating only one breaks the
   channel in one direction. During the overlap each trusts *both* roots; both are
   yours, so ADR-0007 d3's "the root of its own operator and no other" still holds.
4. Only once every unit is confirmed on the new anchor: stop issuing from the old
   root and retire it.

Distribution is the **provisioning path** — the same hands-on route by which a unit
got its identity — deliberately not the profile channel (d10, alternative F). A
gateway that stays offline across the whole window is stranded and needs
re-provisioning by hand.

## If the root is compromised

This is not a rotation. Every certificate beneath that root must be treated as
forged, and there is no mechanism to disown it in the field. New root, new
intermediate, re-provision the estate.

## What this does not do

- **Generate device keys.** A gateway's key is born on its ATECC608 and cannot
  leave it. Producing that CSR is the gateway provisioning tool's job.
- **Write the `-PR` provisioning record.** The serial ↔ certificate binding of
  ADR-0017 decision 12 has no schema yet; this CA supplies the chain it will
  reference.
- **Issue node provenance certificates.** ADR-0007 decision 5 permits them and they
  are long-lived, which sits awkwardly with decision 8's containment rule; open
  until node provisioning exists.
- **Automate renewal.** At fleet scale an ACME issuer would take the intermediate's
  place without the root or any trust anchor changing (ADR-0024 alternative E).

## Not to be confused with

[`../erp/deploy/mtls/make-test-ca.sh`](../erp/deploy/mtls/README.md) builds a
**throwaway** PKI so the proxy config and the ERP's identity extraction can be
exercised on a laptop. It is a fixture, its keys are unencrypted, and it is not
this. Use it for tests; use this for a deployment.
