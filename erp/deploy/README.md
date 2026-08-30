<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# Deploying the ERP

The ordered procedure for standing up the instance-and-integration ERP on a host
you control. **ADR-0021** decides what the ERP is and that it is a self-hosted
single-node container; this file is the runbook and holds the values (ADR-0000
d2).

**Where it runs.** One host of your choosing that can reach the gateways it
serves — a spare box, a NAS, an existing service host. **Not a gateway**: the
gateway is the stateless replaceable edge (ADR-0004; ADR-0020 d5), so a system of
record there turns a gateway swap into a data-loss event (ADR-0021 rev 2
decision 1, alternative L).

On top of that architectural rule there is a practical one worth checking before
you pick a small ARM host: recent MongoDB server builds require **ARMv8.2-A**,
which the Pi 3B+ and Pi 4 do not implement (Pi 5 does). Confirm against the
release notes for the image tag you intend to run rather than trusting this line —
it is a moving, version-dependent fact, which is why it lives here and not in the
ADR.

**Status: this procedure has not been executed on a real host yet.** It is
assembled from parts that have been tested individually — the CA ceremony, the
mTLS handshake, the profile pull — plus a compose file and an `nginx.conf` that
have never run. Expect to find things. `verify-deployment.sh` exists because the
interesting failures here are the silent ones.

---

## 0. Before you start

You need, on the deployment host: Docker with the compose plugin, and a checkout
of this repository. You need, from elsewhere: the operator CA (`pki/`, ADR-0024),
because the gateway channel is certificate-verified and the CA is not stood up
here — the ERP is a relying party, never an authority (ADR-0024 d13).

If the host already runs something else, check what it occupies before you start:

```bash
ss -tlnp | grep -E ':(443|8443|8021|9000|27017)\b'
```

`443` is the one most likely taken. Every port below is overridable.

---

## 1. Configuration

Copy the template and fill it in. Compose reads `.env` from the same directory:

```bash
cd erp
cp deploy/env.example .env
$EDITOR .env
chmod 600 .env      # it holds the database password and the warehouse keys
```

`ERP_MONGO_USER` and `ERP_MONGO_PASSWORD` have **no defaults** and compose will
refuse to start without them. That is deliberate: `mongo:7` runs with
authentication *disabled* unless root credentials are present at first start, and
a default password would be worse than no default because it would work.

> **Set them before the first `up`.** The admin user is created only on an empty
> data volume. Adding credentials to a deployment that has already started once
> without them does nothing — you have to destroy the volume and start over.

---

## 2. Certificates

The proxy needs three files in one directory. On a real deployment these come
from your operator CA (`pki/README.md`), not from the test fixture:

| File | What it is |
|------|-----------|
| `server-chain.crt` | this host's server certificate **plus the issuing CA** |
| `server.key` | its private key |
| `operator-root.crt` | the operator root, as the anchor for gateway certificates |

```bash
# on the host that holds the issuing CA — not here, and not the offline root machine
./pki/sign-csr.sh --dir ./ca --csr erp.csr --profile server \
                  --san "DNS:erp.local,DNS:$(hostname -f),IP:192.168.1.10"
```

Then copy `erp.crt`'s **chain** file and `operator-root.crt` to the deployment
host, and point compose at them:

```bash
ERP_TLS_DIR=/etc/industrygrow/tls    # in .env
```

Two things that are easy to get wrong and produce confusing failures:

- **Serve the chain, not the bare leaf.** With the two-tier CA (ADR-0024 d1) a
  client anchored on the root cannot build a path unless the server sends its
  intermediate too. The symptom is every gateway failing to verify *the server*.
- **The anchor is the root, not the intermediate.** Anchoring on the issuing CA
  trusts whatever that CA signs next rather than the root you hold (d3).
- **Do not copy `operator-root.key` here.** The root key does not belong on an
  online host (ADR-0024 d5). `verify-deployment.sh` fails if it finds one.

For a laptop trial without a real CA, `deploy/mtls/make-test-ca.sh` produces the
same three files as throwaway fixtures. They are not a deployment.

---

## 3. Bring it up

```bash
cd erp
docker compose -f docker-compose.yml -f docker-compose.mtls.yml up -d --build
```

The overlay is not optional for anything reachable beyond localhost: it is what
puts certificate verification in front of the gateway channel (ADR-0022 d2), and
it removes the app's published port so nothing can reach the app around the
proxy. `docker-compose.yml` on its own is a development bring-up.

Mirror the repository `store/` into the warehouse (ADR-0017 d15 object keys):

```bash
docker compose exec erp python -m app.store_sync --prune
```

---

## 3b. Reaching the console before the CA exists

The full mTLS overlay needs an operator CA. Until that ceremony happens
(ADR-0024), the console can still be served over TLS on its own:

```bash
./deploy/console/make-cert.sh --dir ./deploy/console/certs \
                              --name industrygrow.local --ip <this-host-ip>
docker compose -f docker-compose.yml -f docker-compose.console.yml up -d
```

That serves the console and token API on `ERP_HTTPS_PORT`, and returns **403** on
gateway routes — no client certificate is verified there, so no machine identity
can be established (ADR-0022 d2). The app port stays on localhost.

`make-cert.sh` issues **one self-signed leaf with `CA:FALSE`**. It is not the
operator CA and cannot become one, and it is not `deploy/mtls/make-test-ca.sh`
either — that fabricates something shaped like a real trust root, for tests. What
this buys is that the operator token does not cross the LAN in clear text.
Browsers warn once, correctly: nothing signed it.

### Giving the service its own name

If the host already answers to another `.local` name, **`/etc/avahi/hosts` will not
work** — it adds an *address* record, avahi already owns the IP under the host's own
name, and the second one is rejected as a `Local name collision` (visible only in
`journalctl -u avahi-daemon`, which is why this is written down). An alias needs a
**CNAME**:

```bash
sudo install -m 0755 deploy/console/avahi-alias.py /usr/local/bin/
sudo install -m 0644 deploy/console/avahi-alias.service /etc/systemd/system/
sudo systemctl enable --now avahi-alias.service
```

Edit the unit's `ExecStart` for a different name. The records live only while the
process holds the entry group, so stopping the unit withdraws them — which is the
behaviour you want when the service goes away.

## 4. Verify

```bash
./deploy/verify-deployment.sh --tls-dir /etc/industrygrow/tls
```

It checks the things that are wrong *silently*: an unauthenticated Mongo, a
published database or app port, a bare leaf where a chain belongs, an
intermediate posing as the anchor, a root key on an online host, an mTLS listener
that answers without a client certificate, gateway routes reachable on the console
listener, and whether the ERP can actually reach its warehouse bucket. Exit status
is the number of failures.

It deliberately does **not** claim the deployment works end to end. Only a real
gateway pull does that:

```bash
# on a provisioned gateway (store/SP0004-M-atecc-provisioning.md)
sudo -u gateway /opt/industrygrow/venv/bin/python /opt/industrygrow/profile_client.py once
```

That exercises the whole path — client certificate, nginx verification, DN-derived
identity, signed profile, atomic apply. Nothing short of it does.

---

## 5. What to expect when it goes wrong

| Symptom | Usually |
|---------|---------|
| Gateway routes return **503** | `ERP_GATEWAY_TRUSTED_PROXIES` does not contain the proxy's address on the compose network. The app checks the transport peer, not a header (ADR-0022 d2); fail-closed is the intended default |
| Gateway routes return **401/403** | The certificate verified but its CN is not a `GBOX_NNNN` machine identifier |
| Gateway cannot verify *the server* | The proxy is serving the bare leaf instead of `server-chain.crt` |
| Every gateway certificate is rejected | `ssl_verify_depth` too low — the chain is root → issuing CA → leaf, so depth 2 is required, not incidental |
| The app restarts repeatedly at boot | Mongo was not ready; the healthcheck-gated `depends_on` should prevent this, so check the Mongo healthcheck itself |
| `compose up` fails on `ERP_MONGO_USER` | Working as intended — set it in `.env` |
| Profile pull returns 404 | No version is recorded active for that machine, or none is signed (ADR-0025 d11) |

---

## Backup and restore

Decided in **ADR-0026**. This section holds the values and the procedure; the ADR
holds the reasoning.

### Why this one matters more than most backups

The ERP is the **serial-allocation authority** (ADR-0021 d4). Losing its counter is
not a loss of history — the next allocation re-issues a number already stamped on a
board, and two physical parts then claim one identity. Everything else in the store
could be reconstructed by someone with good notes. That cannot.

### Taking one

```bash
cd erp
./deploy/backup.sh --out /var/backups/industrygrow \
                   --pass-file ~/.igrow-backup-pass --keep 7
```

It captures **the index first and the blobs second** — the inverse of the write
order, so the archive can never contain an index naming an object it does not have
(ADR-0026 d3). It verifies before encrypting, encrypts under your passphrase, and
shreds the plaintext.

Then do the half it cannot do for you: **copy the result somewhere that is neither
this host nor the R2 account holding the warehouse.** Two copies, separate
locations — the same custody rule the operator CA root already follows (ADR-0024
d5–7).

| Value | Setting | Why here |
|---|---|---|
| Copies | **2**, separate locations | ADR-0026 d6 |
| Retention | **7** archives (`--keep 7`) | a runbook value; raise it if you allocate serials rarely |
| Cadence | **weekly**, and **before any upgrade or restore** | the store is low-churn; the risk is upgrades, not drift |
| Restore rehearsal | **quarterly**, into a throwaway database | ADR-0026 d10 |
| Encryption | gpg symmetric, AES-256, passphrase file | ADR-0026 d7 |

**A forgotten passphrase is an unrecoverable backup** (ADR-0026 d7 accepts this in
exchange for not adding a fourth long-lived key to custody). Keep the passphrase
apart from both copies.

### What is in it, and what is not

In: the Mongo store, and the warehouse objects that exist nowhere else — the
`-QP/-QR/-CP/-CC/-PR` lifecycle documents.

Not in, each for a reason (ADR-0026 d1): the `store/` warehouse mirror (repository
content — `store_sync` restores it), the operator CA and the profile-signing key
(their own custody, ADR-0024 d5–7 and ADR-0025 d5), the gateway (stateless; its
`active-profile.json` is a re-pullable cache), and `REGISTRY.md` (git).

### Rehearsing a restore

Do this on a schedule, not when you need it:

```bash
gpg --decrypt --passphrase-file ~/.igrow-backup-pass \
    -o /tmp/rehearsal.tar.gz /var/backups/industrygrow/erp-….tar.gz.gpg

docker compose exec -T erp python -m app.backup verify  --archive /tmp/rehearsal.tar.gz
docker compose exec -T erp python -m app.backup restore --archive /tmp/rehearsal.tar.gz \
                                                        --into industrygrow_rehearsal
```

`--into` restores to a throwaway database, which also skips the serial-counter
check below — a rehearsal database has no hardware behind it.

### Restoring for real

```bash
docker compose exec -T erp python -m app.backup restore --archive /tmp/erp-….tar.gz
docker compose exec -T erp python -m app.backup check-live
```

Blobs go back first, then the index (ADR-0026 d4), so no moment exists where the
index references an object that is not there. `check-live` confirms it afterwards.

**It may refuse, and that refusal is the point.** If the live store has allocated
serials the archive does not know about, restoring re-arms those numbers for
re-issue. The tool stops and prints which counters are ahead. Before passing
`--force`, establish **which of those serials reached hardware** — the ERP cannot
tell you, and nothing afterwards can detect the mistake.

### If a backup fails to verify

It is not a backup. `backup.sh` discards it rather than encrypting it, and the most
common cause is worth knowing: `save` refuses when the **live** store has indexed a
document whose object is missing from the warehouse. That is a fault in the running
system (ADR-0021 d7 says a recorded key always resolves), not in the backup — run
`check-live` and investigate before trusting either.
