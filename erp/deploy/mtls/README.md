<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# mTLS termination for gateway callers

[ADR-0022 decision 2](../../../ADR/ADR-0022-instance-integration-erp-api.md): a
gateway authenticates to the ERP with the ATECC608-bound client certificate it
already holds under [ADR-0007](../../../ADR/ADR-0007-pki-and-secure-element-identity.md),
and its identity `GBOX_NNNN` is derived **from the verified certificate, never
from a request parameter**.

The ERP does not terminate TLS. A reverse proxy does, and this directory holds
the sample configuration plus the tooling to exercise it.

## Two caller classes, two listeners

ADR-0022 keeps machine and human authentication apart, and the proxy is where
that separation becomes physical:

| Port | Callers | Credential | Client cert |
|------|---------|-----------|-------------|
| 443  | operator console, provisioning tooling | scoped token (d3) | not requested |
| 8443 | gateways | client certificate under the operator root (d2) | **required** |

`/api/v1/gateway/` is refused on 443. Not because the app would accept it — it
would not — but because a route whose entire premise is a verified certificate
should not appear to exist on a listener that never asks for one.

## The header contract

The proxy verifies the chain and forwards two facts:

| Header | nginx variable | Meaning |
|--------|----------------|---------|
| `X-Client-Verify` | `$ssl_client_verify` | `SUCCESS`, `FAILED:<reason>`, or `NONE` |
| `X-Client-DN` | `$ssl_client_s_dn` | subject DN of the verified peer |

The app (`erp/app/services/mtls.py`) then, **in this order**:

1. requires the request's transport peer to be in `ERP_GATEWAY_TRUSTED_PROXIES`;
2. requires `X-Client-Verify` to be exactly `SUCCESS`;
3. parses `GBOX_NNNN` out of the DN's **CN** itself.

Step 3 is why there is no `X-Client-Gbox` header. If the proxy sent a ready-made
identifier, there would exist a header whose value becomes an identity, and
decision 2's "never from a request parameter" would hold only by the proxy's good
manners. Forwarding the DN instead means the only thing a caller can influence is
a string that must have come out of a certificate the proxy verified.

Both listeners assign these headers unconditionally, which also overwrites
anything a client sent under those names.

### The certificate's CN carries the machine identifier

ADR-0007 fixes what a gateway certificate *is* but not how it names its unit.
This deployment pins it: **the CN is the ADR-0017 machine identifier verbatim** —
`CN=GBOX_0001`, no prefix, no vendor form, no email. The string the proxy verified
is the string the ERP keys on, with no translation step in between to get wrong.
Everything else in the subject (`O`, `OU`) is organisational and unread.

## Fail-closed by default

`ERP_GATEWAY_TRUSTED_PROXIES` is **empty by default** and the gateway routes then
answer **503**, not 401. With no proxy address configured there is no way to tell
a forwarded header from a forged one, and the honest answer is that the channel
does not exist here — not that the caller presented a bad credential.

Configure it with the address the proxy actually appears as on the wire (IPs or
CIDRs), as in `docker-compose.mtls.yml`.

> **Do not run the app with uvicorn's `--proxy-headers`.** That mode rewrites the
> client address from `X-Forwarded-For`, i.e. lets the request choose the address
> the trusted-peer check inspects. `main.run()` and the container CMD both pass
> `--no-proxy-headers`; keep it that way if you write your own entrypoint.

And close the app's own port. The trusted-peer check makes a directly reachable
app port survivable; it does not make it a good idea.

## Running it

```bash
# a throwaway operator root + a GBOX_0001 client certificate
./deploy/mtls/make-test-ca.sh deploy/mtls/certs

docker compose -f docker-compose.yml -f docker-compose.mtls.yml up

# the gateway pull channel, as a gateway
curl --cacert deploy/mtls/certs/operator-root.crt \
     --cert   deploy/mtls/certs/gateway.crt \
     --key    deploy/mtls/certs/gateway.key \
     --resolve erp.local:8443:127.0.0.1 \
     https://erp.local:8443/api/v1/gateway/active-profile

# without the certificate: refused by nginx, never reaching the app
curl --cacert deploy/mtls/certs/operator-root.crt \
     --resolve erp.local:8443:127.0.0.1 \
     https://erp.local:8443/api/v1/gateway/active-profile
```

`make-test-ca.sh` is **not** a CA bootstrap procedure. A real operator root
(ADR-0007 d3 — trust is rooted per operator; there is no global IndustryGrow
root) is a long-lived secret with custody and renewal ceremony around it, and
standing one up is an ADR-0007 deferred decision, not a shell script. The script
makes disposable, unencrypted keys so this contract can be run end to end on a
laptop. It follows the real shapes where they matter: P-256 keys (the curve the
ATECC608 does), 90-day leaves (ADR-0007 d7, short-lived and renewed), and a
same-CN leaf under a *foreign* root so rejection can be tested rather than
assumed.

Generated certificates are gitignored. Do not commit any.

## What is tested, and what is not

`erp/tests/test_mtls_certs.py` runs `make-test-ca.sh` and performs a **real**
mutually-authenticated TLS handshake: the operator-root leaf verifies, the
same-CN foreign-root leaf does not, and the accepted certificate's DN — encoded
exactly as `$ssl_client_s_dn` encodes it — yields `GBOX_0001`.
`erp/tests/test_mtls.py` covers the app's half: forged headers from a direct
caller, failed and missing verification, the fail-closed default, and a query
parameter failing to override the certificate's identity.

**`nginx.conf` itself is not executed by the suite** — there is no nginx in CI.
It is a declaration of the same contract the tests hold the app to, verified by
reading. Exercise it with the `curl` commands above when standing up a real
deployment.
