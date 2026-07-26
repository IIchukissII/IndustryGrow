<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Gateway ATECC608 identity provisioning — `SP0004-M-atecc-provisioning`

- **Type:** HOW document (Manual, document layer **M** — ADR-0017 d9). It owns the
  *how* and the ATECC608 *values*; the *why* is delegated to the ADRs by number
  (ADR-0000 d2/d3).
- **Subject:** the gateway Raspberry Pi's on-board **ATECC608B** secure element =
  part of **SP0004** (REGISTRY.md; ADR-0019 d2 — SP0004 is the one SP part with
  per-instance identity, its ATECC-bound certificate). Node-carrier (E0001)
  provisioning is a separate concern (ADR-0007 d5, not built).
- **Identifier:** the filename is the object key; form `SPxxxx-<layer>-<slug>` per
  the SP document-layer convention in `REGISTRY.md`.
- **Companion automation:** the gateway provisioning tool
  [`gateway/provision_identity.py`](../gateway/provision_identity.py), which
  performs §4–§8, and the operator CA in the repo's `pki/` directory
  (`sign-csr.sh` signs the CSR it produces). This document is the tool's
  specification; where the two disagree, this document is right and the tool is a
  bug. It reaches the Pi with the rest of the `gateway/` bundle (`deploy.ps1`) and
  is deliberately *not* installed by `provision.sh`: provisioning is a later phase
  than bring-up and needs a dependency the bring-up venv does not carry (§5).
- **Stage:** roadmap Production / Phase 2 (ADR-0017) — serials and identity are
  assigned here, after the bench bring-up of `SP0004-M-gateway-bringup`.
- **Validation status — READ THIS.** The ATECC608 config words and lock sequence
  below are drawn from Microchip references (ATECC508A datasheet — the config-zone
  layout is identical on the 608B, whose full map is under NDA; cryptoauthlib;
  the TrustFLEX/TrustCUSTOM notes). **No part has been provisioned against this
  document yet.** The config-zone lock is *irreversible*: a wrong value locked into
  a production part scraps it. Confirm every config byte on a sacrificial part
  before locking a real one. Values here are the intended configuration, not a
  hardware-validated one — the same posture `erp/deploy/mtls`'s `nginx.conf` carried
  before it ran on a box.

  What *is* exercised is everything downstream of the chip's signature
  (`erp/tests/test_gateway_provisioning.py`, against the §5 software fallback): the
  CSR the tool assembles verifies and is signed by `pki/sign-csr.sh`, the resulting
  leaf authenticates through the ERP's mTLS identity extraction, renewal
  re-certifies the same key, and the refusals in §6/§8 hold. So the unvalidated
  surface is narrowed to the ATECC conversation itself — the config words, the lock
  sequence, and cryptoauthlib reaching the part.

---

## 1. Conventions

| Thing | Rule | Source |
|-------|------|--------|
| **Key = on-chip, non-exportable** | The device keypair is generated inside the ATECC608 in a non-exportable slot; the private key never leaves the part. Only the public key and the chip serial are exported. | ADR-0007 d1 |
| **Curve = NIST P-256** | secp256r1, the part's native ECC curve and the basis of the serial↔crypto binding. | ADR-0007 d1; ADR-0017 d8 |
| **Identity = `GBOX_NNNN` in the CSR CN** | The CSR's Common Name is the ADR-0017 machine identifier verbatim — `GBOX_0001`, no prefix, no vendor form. This is *not* the ATECC die serial (§7). The ERP derives the caller's identity from this exact string. | ADR-0022 d2; `erp/deploy/mtls/`; ADR-0007 rev 1 d10b |
| **One key, re-certified per operator** | Migration between operators is re-certification of this same key, never a new key. `GBOX_NNNN` stays stable across the self-hosted CN and, at stage 11, IndustryFlow's SAN device segment. | ADR-0007 rev 1 d10 |
| **Key material = per-instance, off-repo** | No key, CSR, or issued cert is committed. Public material is recorded in the gateway's provisioning binding (§8); the private key is on the chip and nowhere else. | ADR-0017 (two-homes); ADR-0019 d2 |

---

## 2. What the ATECC provisioning produces

Three artifacts, in one Production pass per unit:

1. an **on-chip P-256 keypair** in slot 0, private half non-exportable (§3–§4);
2. a **CSR** carrying that public key and `CN=GBOX_NNNN`, signed by the slot-0 key
   itself (§5), which the operator CA turns into a gateway leaf (§6);
3. a **provisioning binding** recorded in the ERP (ADR-0022 d5) tying the gateway's
   machine identity `GBOX_NNNN` and its SP0004 vendor serial to the issued
   certificate — public material only (§8).

The private key is never one of the artifacts — that is the entire point of the
part being on the BOM (ADR-0007 d1).

> **The gateway is a machine, not an E-instance.** It is **SP0004**, keyed by its
> vendor serial plus the ATECC-bound certificate (ADR-0019 d2), and it *is* a
> machine `GBOX_NNNN` (ADR-0022 d1). It has no `Exxxx-VVVVVV-NNNNNN` instance serial,
> and the `-PR` lifecycle-document suffix proper is defined on those E-instances
> (ADR-0022 d7) — node provisioning, not this. This document is the **gateway's**
> ATECC provisioning; its binding is machine-scoped (§8).

---

## 3. Slot map and config zone (the ADR-0007 d9 values)

ADR-0007 d9 leaves the concrete slot numbers, key-config words, and lock sequence
to this document. Here they are.

**Part variant.** Use a **TrustCUSTOM** ATECC608B (blank, config zone unlocked). A
**TrustFLEX** part ships pre-locked with a factory key and Microchip-signed cert —
you cannot set the slot policy below, so it does not fit a self-run operator PKI.
The identity key must be generated under *our* config, on *our* curve, and certified
by *our* CA.

**Identity slot = slot 0.** Slots 0–7 are the only ones that hold ECC private keys;
slot 0 is the cryptoauthlib/atcacert default (`private_key_slot = 0`). Config-zone
words for slot 0 (both stored **little-endian** in the 128-byte config template):

| Word | Value | Raw bytes | Meaning |
|------|-------|-----------|---------|
| **SlotConfig[0]** (bytes 20–21) | `0x2087` | `87 20` | `IsSecret`=1 (no clear-text read of the slot), `WriteConfig`=GenKey (GenKey may write a random on-chip key here; slot not writable in clear), external+internal ECDSA sign permitted. Drop the ECDH bit → `0x2083` for sign-only. |
| **KeyConfig[0]** (bytes 96–97) | `0x0033` | `33 00` | `Private`=1 (ECC private key), `PubInfo`=1 (public key always derivable via GenKey/get_pubkey), `KeyType`=4 (P-256), `Lockable`=1 (slot can be individually locked). Set `ReqRandom` → `0x0073` to force a random nonce before each use (optional hardening). |

`IsSecret=1` + `WriteConfig=GenKey`, with no encrypted-read and no parent write key,
is what makes the private key **non-exportable**: no command path returns bytes 0–31
of the slot. Only the public key leaves, via GenKey(public) / `atcab_get_pubkey`.

> The 608B's full config map is NDA. Read the config zone back after writing and
> diff it byte-for-byte against your template on a **sacrificial part**, then lock
> that sacrificial part and prove the whole §5 flow, before you write config to a
> unit destined for the field.

---

## 4. Provisioning sequence (irreversible — order is load-bearing)

Each `atcab_*` call is cryptoauthlib (§5 has the interface init). Every **Lock** is
one-way: there is no unlock, no factory reset.

1. **Detect.** `i2cdetect -y 1` — a blank TrustCUSTOM answers at 7-bit `0x60`, a
   configured/TFLXTLS part at `0x36`. cryptoauthlib's `atcai2c.address` wants the
   **8-bit** form (`0x6C` for `0x36`).
2. **Write config** — `atcab_write_config_zone(template)` with the §3 words. Only
   writable while the config zone is unlocked.
3. **Lock config** — `atcab_lock_config_zone()`. **The slot policy is now frozen
   forever.** GenKey into a private slot is *rejected while config is unlocked*, so
   this must come before step 4.
4. **Generate the key** — `atcab_genkey(0, pubkey)`. The P-256 private key is
   created inside slot 0 from the on-chip TRNG and never exists off the die; the
   64-byte public key is returned.
5. **Read anchors** — `atcab_get_pubkey(0, pubkey)` and
   `atcab_read_serial_number(serial)` (§7). These feed the CSR (§5) and the
   provisioning binding (§8).
6. **Build and export the CSR** (§5), `CN=GBOX_NNNN`.
7. **Lock data/OTP** — `atcab_lock_data_zone()`. Freezes data-slot writability;
   conventionally done after the key is captured.
8. **(Optional) lock slot 0** — `atcab_lock_data_slot(0)` (needs `Lockable`=1). A
   one-way burn-in of *this* key, blocking any future GenKey overwrite. Do this
   **only after the CA round-trip (§6) has succeeded** — it prevents regenerating
   the key if issuance failed.

Field robustness: the two irreversible locks mean provisioning must survive power
loss between steps. Write and lock the config in one reliable pass; defer the
optional slot lock (step 8) until the CA has returned a working certificate.

---

## 5. Generating the CSR (key stays on the chip)

The CSR is the only thing that travels to the CA — the private key cannot, so
`pki/sign-csr.sh` signs a CSR it did not generate; producing it is this tool's job.

Three supported routes to a CSR whose signature is produced *by the slot-0 key*.
**`provision_identity.py csr` takes the third**, for a testability reason given
below; the first two remain valid and are what a C or OpenSSL-centric
implementation should use.

- **cryptoauthlib atcacert (preferred, no OpenSSL needed on the Pi).** Fill an
  `atcacert_def_t` with `private_key_slot = 0` and a `public_key_dev_loc` pointing
  at slot 0 with `is_genkey = TRUE` (derive the pubkey via `atcab_get_pubkey`, not a
  public slot), then `atcacert_create_csr_pem(&def, pem, &len)`. It builds the CSR
  body, hashes it, calls `atcab_sign(0, …)` internally, and inserts the signature.
- **PKCS#11 provider (if you want OpenSSL in the flow).** Use cryptoauthlib's
  PKCS#11 module with `pkcs11-provider`/`libp11` and OpenSSL 3.x:
  `openssl req -new -provider pkcs11 -propquery 'provider=pkcs11' -key 'pkcs11:…;object=device;type=private' -subj '/CN=GBOX_0001' -out GBOX_0001.csr`.
  **Do not** reach for the old `ateccx08` OpenSSL *engine* — Microchip has
  deprecated it and it is OpenSSL-1.1.x-only, which current Raspberry Pi OS is not.
- **PKCS#10 assembled against the raw chip primitives (what the tool does).** Build
  the `CertificationRequestInfo` DER directly, hash it, sign the digest with
  `atcab_sign(0, …)`, and wrap the returned 64-byte `R||S` as a DER
  `Ecdsa-Sig-Value`. Needs only the `cryptoauthlib` Python package — no atcacert
  definition to fill, no PKCS#11 provider on the Pi, no OpenSSL in the signing path.
  **Why this one:** it reduces what cannot be tested without hardware to three calls
  (`genkey`, `get_pubkey`, `sign`). Both routes above bury CSR construction behind a
  call that only a real part can execute, so a mistake in it is discoverable only in
  Production; assembling it here makes the CSR path exercisable with a software key
  that presents the same contract (a 64-byte public point, a 64-byte signature over
  a supplied digest). The cost is owning ~100 lines of DER, of which the `R||S` →
  DER conversion would have been required anyway.

Interface init (Raspberry Pi I²C, cryptoauthlib):

```c
ATCAIfaceCfg cfg = cfg_ateccx08a_i2c_default;
cfg.iface_type      = ATCA_I2C_IFACE;
cfg.devtype         = ATECC608;
cfg.atcai2c.address = 0x6C;   // 8-bit form of 7-bit 0x36
cfg.atcai2c.bus     = 1;      // /dev/i2c-1
atcab_init(&cfg);
```

The CSR subject is `CN=GBOX_NNNN` (§1) and no SAN — `sign-csr.sh --profile gateway`
rejects any other CN and issues `clientAuth` (ADR-0007 rev 1 d10b; the field choice
is the deployment's, not an ADR's). The operator name may ride the subject `O=` for
estate consistency (`pki/` bootstrap `--operator`), but the ERP reads only the CN.

### Software-ATECC fallback (CI / laptop only)

Where no chip is present, stand in a **software** P-256 key so the CSR→sign→install
→renew flow can be exercised end to end. Every subcommand takes `--software-key`,
which generates the key if it is absent and otherwise reuses it:

```bash
./provision_identity.py csr --gbox GBOX_0001 --software-key /tmp/gbox0001.key \
                            --out GBOX_0001.csr
```

Equivalently by hand, which is what the tool's fallback signer wraps:

```bash
openssl ecparam -name prime256v1 -genkey -noout -out /tmp/gbox0001.key
openssl req -new -key /tmp/gbox0001.key -subj '/CN=GBOX_0001' -out GBOX_0001.csr
```

This is a **fixture**, exactly like `erp/deploy/mtls/make-test-ca.sh` — the key is
exportable and on disk, which is the whole property the ATECC exists to prevent. It
proves the pipeline, never a real unit. Do not let a software-key CSR reach a
production binding.

---

## 6. Issuing the certificate

Hand the CSR to the operator issuing CA (see `pki/README.md`; runs where the issuing
key lives, never on the offline root machine, never in the ERP container):

```bash
./sign-csr.sh --dir ./ca --csr GBOX_0001.csr --profile gateway
# writes GBOX_0001.crt and GBOX_0001-chain.crt (leaf + issuing intermediate)
```

**Install the chain (`…-chain.crt`), not the bare leaf and not the root** — a peer
anchored on the root still needs the intermediate to build a path (ADR-0024 d1/d3).

---

## 7. The ATECC serial vs. `GBOX_NNNN`

Two different identifiers, deliberately not conflated:

- **`GBOX_NNNN`** — the ADR-0017 **machine** identifier for the cabinet this gateway
  serves (ADR-0022 d1). It goes in the certificate CN. It is **not** assigned by this
  step: it must already exist for the CN, so the gateway is registered as machine
  `GBOX_NNNN` in the ERP *before* provisioning (§8).
- **ATECC 9-byte die serial** — `atcab_read_serial_number()`, factory-unique per
  chip (fixed markers `01 23 …` and `… EE`). It is the *hardware* anchor, recorded in
  the gateway's provisioning binding (§8), never placed in the CN. Its public-key
  fingerprint is what the binding and any revocation deny-list key on, because it is
  stable across renewal and re-certification (ADR-0007 rev 1 d10d; ADR-0024 deferred
  deny-list note).

---

## 8. The provisioning binding (handoff, not schema)

Provisioning ends by recording the gateway's "birth certificate" — the
machine ↔ ATECC ↔ certificate binding, **public material only** (ADR-0007 d6;
ADR-0017 d12). `provision_identity.py binding` collects those inputs into a JSON
file for submission; it names the inputs and does **not** define the record, which
is why it refuses to emit one for a software key at all (a binding asserts a
hardware anchor an exportable key does not have). It uses the **same
certificate-metadata inputs ADR-0022 d5 names** — public-key fingerprint, cert
serial, validity, never a private key — referenced here rather than restated
(ADR-0000 d3). But d5's binding route is written as binding *a
serial* (`Exxxx-VVVVVV-NNNNNN`) and d7's document allowlist is E-instance-scoped, so
**whether the gateway's machine-scoped binding reuses d5's existing route or needs a
distinct machine-binding route is not settled in any ADR** — this document does not
decide it. It owns only *when* in the flow the binding is written and *which local
values* feed it; it does **not** define the record's fields — the record schema is
deferred (ADR-0007 / ADR-0024) and lands with the ERP (board card 17).

**How the tool groups the values.** ADR-0007 rev 1 d10d fixes which facts a `-PR`
may key on and why; that requirement is not restated here (ADR-0000 d3). What
follows from it for this step is the shape of the handoff: `GBOX_NNNN`, the SP0004
vendor serial, the ATECC die serial and the public-key fingerprint sit at the top
level, and the issued certificate's serial, validity and issuer sit in a nested
`certificate` object. The nesting is how the tool records which group is which, so
that the d10d distinction is still legible when the record schema is settled.

**Ordering (a workflow constraint, not an axis redefinition).** The CSR's CN is the
machine identifier `GBOX_NNNN` (§1), so that machine must be registered in the ERP
*before* the CSR is generated. This says nothing about ADR-0017's identity/position
axes — it is only that a gateway is commissioned as its machine, then provisioned
with a certificate that names it.

**Machine-scoped, not an `Exxxx-…-PR`.** The gateway is SP0004, keyed by its vendor
serial and the ATECC-bound certificate (ADR-0019 d2); the `-PR` lifecycle-document
suffix proper is defined on E-instances `Exxxx-VVVVVV-NNNNNN` (ADR-0022 d7), which a
gateway is not. So the local values this step captures are `GBOX_NNNN`, the SP0004
**vendor serial**, and the ATECC die serial + public-key fingerprint — the stable
anchor a renewal or migration re-certification does not disturb (ADR-0007 rev 1 d10d;
ADR-0024 deferred deny-list note). Whether the ERP records a gateway binding by
reusing the E-instance `-PR` record shape or a machine-scoped one is not fixed in the
ADRs; that is part of the deferred record schema, not something this document
decides.

---

## 9. Renewal and migration

- **Renewal** (short-lived leaves, ADR-0007 d7; 90 days, `pki/` runbook): re-CSR and
  re-issue over the **same** slot-0 key — the key is generated once and never
  regenerated on renewal, only re-certified. `atcab_get_pubkey(0)` → new CSR →
  `sign-csr.sh` → install the new chain. No lock, no key change. This is
  `provision_identity.py csr` again, unchanged: the tool has no path that
  regenerates the key, so renewal cannot accidentally become re-keying.
- **Migration to IndustryFlow (stage 11)**: the same key is re-certified under
  IndustryFlow's managed CA, whose leaf carries the identity in a **SAN URI**
  (`industryflow:tenant/<uuid>/device/GBOX_NNNN`) rather than the CN, and the
  gateway's trust anchor is swapped from the self-hosted root to the managed one — a
  **clean sequential swap**, one operator's root trusted at a time (ADR-0007 rev 1
  d10, d3). The migration *procedure* itself, and IndustryFlow's tenant assignment,
  are ADR-deferred — do not build them here.

---

## 10. What this does not do

- **Generate the operator CA or its keys.** That is `pki/` (ADR-0024). This tool is
  a relying party's provisioning client, not an authority.
- **Define the `-PR` schema.** Deferred (ADR-0007 / ADR-0024); §8 lists the inputs
  it hands off, not its fields.
- **Provision node (E0001) secure elements.** Node provenance certificates are
  ADR-0007 d5, long-lived, and open until node provisioning exists.
- **Run on validated hardware yet.** The §3 config words and §4 lock order are
  reference-derived and unexercised — see the validation note at the top.

## 11. Not to be confused with

- [`../pki/README.md`](../pki/README.md) — the operator CA that *signs* the CSR this
  tool produces. The `what`/`why` split: that runbook owns CA custody and lifetimes;
  this one owns the on-chip keygen and the CSR.
- [`../erp/deploy/mtls/make-test-ca.sh`](../erp/deploy/mtls/README.md) and the §5
  software-key fallback — **throwaway fixtures** for exercising the flow on a laptop,
  not a provisioning procedure. Unencrypted, exportable keys; never a production
  binding.

## References

- ADR-0007 (rev 1): PKI, hardware identity, and provisioning — on-chip non-exportable
  P-256 key (d1), the slot-map/lock-sequence delegation to this document (d9), and
  identity across operators (d10) — one key re-certified per operator, `GBOX_NNNN`
  stable across CN and SAN, the `-PR`/anchor bound to stable facts.
- ADR-0017 (rev 1): identification scheme — `GBOX_NNNN` machine identifier, the
  serial↔ATECC↔certificate binding, and the `-PR` provisioning record (d12).
- ADR-0021 / ADR-0022: the ERP as serial-allocation authority and the API the `-PR`
  binding is submitted through; the CN-derived caller identity (ADR-0022 d2).
- ADR-0024: the two-tier operator CA that issues the gateway leaf; the deny-list
  keying on the stable public-key fingerprint / `GBOX_NNNN`.
- ADR-0019: SP0004 as the one SP part with per-instance ATECC-bound identity.
- `SP0004-M-gateway-bringup.md`: the host bring-up this provisioning follows.
- `store/E0001-000001-D-pinmap.md`: carrier ATECC on I²C2 (the node side, separate).
- Microchip ATECC508A datasheet (config-zone/SlotConfig/KeyConfig/GenKey/Lock, layout
  identical on 608B); cryptoauthlib (`atcab_genkey`/`get_pubkey`/`sign`/
  `read_serial_number`, `atcacert_create_csr`); RFC 5280 (X.509 / PKIX).
