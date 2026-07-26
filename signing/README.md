<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# Profile signing — the operator's third key

A gateway will not apply a cultivation profile it cannot verify (ADR-0015 d7).
This directory holds the key that makes a profile verifiable, and the tool that
uses it. **ADR-0025** is the decision record; this file is the runbook, and holds
the values ADR-0025 deliberately does not (ADR-0000 d2).

## The three keys, and why they are three

An operator running IndustryGrow ends up holding three independent signing
authorities. They are separate on purpose (ADR-0025 d4; ADR-0007 d8), and mixing
them up is the most expensive mistake available here.

| Key | Says | Lives | Used |
|-----|------|-------|------|
| **Operator CA root** (`pki/`, ADR-0024) | *who a unit is* | offline, two encrypted copies, separate locations | approximately once, to sign one intermediate |
| **Firmware-signing key** (ADR-0004 d12) | *what code runs on a node* | offline | at firmware release events |
| **Profile-signing key** (here, ADR-0025) | *what a cabinet is instructed to do* | operator workstation, encrypted | every time a setpoint changes |

The custody here is deliberately lighter than the CA root's, and that is a
decision rather than an oversight (ADR-0025 d5): this key is used constantly, and
a ceremony per setpoint edit is a ceremony operators route around. Encrypted at
rest on a host you control is the baseline. A hardware token or a dedicated
offline machine is better and nothing stops you.

**Losing this key** means every future profile must be signed with a new one, and
each gateway re-provisioned with the new public half. It does not invalidate the
running profile — gateways keep controlling their cabinets. **Leaking it** means
someone else can author profiles your gateways will accept; rotate, re-provision,
and note that ADR-0025 leaves the compromise-response procedure deferred.

## Values

| Thing | Value | Why here |
|-------|-------|----------|
| Curve | **NIST P-256** | ADR-0025 d9 / ADR-0007 d1 — the project's one asymmetric family |
| Digest | **SHA-256** | ADR-0025 d9 |
| Key encryption | **AES-256-CBC**, passphrase | ADR-0025 d5's "encrypted at rest" made concrete |
| Signature form | base64 of DER ECDSA-Sig-Value, detached | ADR-0025 leaves the encoding to implementation |
| Key file | `profile-signing.key` (0600) | |
| Public half | `profile-verify.pub` (0644) | provisioned to each gateway |

## One-time setup

```bash
export SIGNPW='…'                     # or use file:/path, see openssl passphrase specs
./sign_profile.py keygen --dir ~/industrygrow-profile-key --pass env:SIGNPW
```

Back up `profile-signing.key` and its passphrase separately. Then provision the
public half onto each gateway — it is public material, so this is a copy, not a
ceremony:

```bash
scp ~/industrygrow-profile-key/profile-verify.pub gbox:/tmp/
ssh gbox 'sudo install -m 0644 /tmp/profile-verify.pub /etc/industrygrow/pki/'
```

Until a gateway has this file it applies nothing at all — the client is
fail-closed, because the alternative to "no key" is not "skip the check"
(ADR-0025 d10, d11).

## Signing a profile

A profile document must name the cabinet it is for and its own version, because
one signature binds content, addressee, and version together (ADR-0025 d7):

```json
{
  "machine_id": "GBOX_0001",
  "version_tag": "v43",
  "setpoints": { "air_c": 21.5 },
  "model": {}
}
```

```bash
./sign_profile.py sign --dir ~/industrygrow-profile-key \
                       --in gbox0001-v43.json --pass env:SIGNPW
# writes gbox0001-v43.json.sig — and does NOT touch gbox0001-v43.json
```

Then store both in the ERP and record the version active. The ERP will refuse to
activate a version with no signature (ADR-0025 d11).

**Do not reformat the document after signing.** Not `jq .`, not an editor that
trims trailing whitespace, not a pipeline that re-serialises JSON. The signature
covers the bytes (ADR-0025 d6), so a change no human would call a change still
breaks it — and it breaks at the cabinet, which will keep running its previous
profile and log a verification failure. `sign_profile.py verify` is the pre-flight
for exactly this:

```bash
./sign_profile.py verify --pubkey ~/industrygrow-profile-key/profile-verify.pub \
                         --in gbox0001-v43.json --sig gbox0001-v43.json.sig
```

## Per cabinet, and forward only

Two consequences worth knowing before you plan a rollout:

- **One signature per cabinet.** A profile signed for `GBOX_0001` is inert at
  `GBOX_0002` (ADR-0025 d7). Rolling one cultivation programme out to nine
  cabinets means nine documents and nine signatures. That is the cost of a stolen
  artifact not being a fleet-wide event.
- **Versions only go up.** A gateway refuses a version that is not greater than
  the one it runs, because a signature never expires and a replayed old artifact
  is otherwise indistinguishable from a current one (ADR-0025 d8). **A rollback is
  a new, higher version carrying the earlier content** — never the old artifact
  re-activated. The version's leading integer is what gets compared, so `v42` →
  `v43` orders and `latest` is refused rather than guessed at.

## Not to be confused with

- [`../pki/README.md`](../pki/README.md) — the operator CA that issues *identity*
  certificates. Different key, different authority, different custody.
- [`../store/SP0004-M-atecc-provisioning.md`](../store/SP0004-M-atecc-provisioning.md)
  — the gateway's own key, on its ATECC. That one proves *who* is asking; this one
  vouches for *what* it is told to do.
- The community profile registry. Template profiles are signed by their authors
  under a scheme ADR-0009 still owes us; templates never reach a gateway directly
  (ADR-0025 d1–d2). This key signs deployment instances only.
