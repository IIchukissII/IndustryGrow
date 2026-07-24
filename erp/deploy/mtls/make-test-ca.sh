#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generate a THROWAWAY operator PKI and the certificates needed to exercise the
# mTLS gateway channel (ADR-0022 decision 2) locally.
#
#   ./make-test-ca.sh [output-dir] [GBOX_NNNN]
#
# NOT a CA bootstrap procedure — see ../../../pki/ for that. A real operator root
# (ADR-0007 d3) is an offline, encrypted, custody-managed secret, and standing one
# up is ADR-0024's ceremony, not a shell script. This produces disposable keys,
# unencrypted, in one unattended pass, so the proxy config and the app's identity
# extraction can be run end to end on a laptop.
#
# Shapes that are deliberately faithful to the real thing:
#   * The two-tier chain of ADR-0024 d1 — an operator root that signs only the
#     issuing CA, and an issuing CA that signs every leaf. A fixture built on a
#     flat root would be exercising a shape no deployment runs, and would not
#     catch a verifier configured to accept only directly-signed leaves.
#   * P-256 (prime256v1) keys — the curve the ATECC608 does (ADR-0007 d1/d5).
#   * The gateway leaf's CN is the ADR-0017 machine identifier verbatim, which is
#     what the ERP parses out of the forwarded DN.
#   * A leaf issued under a *different* root is included, so "rejects a foreign
#     CA" can be tested rather than assumed.

set -euo pipefail

OUT="${1:-./certs}"
GBOX="${2:-GBOX_0001}"
OPERATOR="OP-STRAWBERRY-01"
DAYS_ROOT=3650
DAYS_CA=1095   # the issuing tier, inside the root (ADR-0024 d8)
DAYS_LEAF=90   # short-lived, per ADR-0007 d7

mkdir -p "$OUT"
cd "$OUT"

key() { openssl ecparam -name prime256v1 -genkey -noout -out "$1"; }

ca_ext() { printf 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n'; }

# ---- the operator root (ADR-0007 d3: rooted per operator, no global root) ----
key operator-root.key
openssl req -x509 -new -key operator-root.key -sha256 -days "$DAYS_ROOT" \
  -out operator-root.crt \
  -subj "/O=${OPERATOR}/OU=pki/CN=${OPERATOR} operator root" \
  -addext 'basicConstraints=critical,CA:TRUE' \
  -addext 'keyUsage=critical,keyCertSign,cRLSign'

# ---- the issuing CA — the only thing the root signs (ADR-0024 d1, d2) -------
key issuing-ca.key
openssl req -new -key issuing-ca.key -out issuing-ca.csr \
  -subj "/O=${OPERATOR}/OU=pki/CN=${OPERATOR} issuing CA"
openssl x509 -req -in issuing-ca.csr -CA operator-root.crt -CAkey operator-root.key \
  -CAcreateserial -days "$DAYS_CA" -sha256 -out issuing-ca.crt \
  -extfile <(ca_ext)

# Everything below is signed by the issuing CA, never by the root.
sign_leaf() { # name, ext-block
  openssl x509 -req -in "$1.csr" -CA issuing-ca.crt -CAkey issuing-ca.key \
    -CAcreateserial -days "$DAYS_LEAF" -sha256 -out "$1.crt" \
    -extfile <(printf '%s' "$2")
  # What a peer presents: its leaf, then the intermediate. Never the root.
  cat "$1.crt" issuing-ca.crt > "$1-chain.crt"
}

# ---- the ERP's server certificate -------------------------------------------
key server.key
openssl req -new -key server.key -out server.csr \
  -subj "/O=${OPERATOR}/OU=erp/CN=erp.local"
sign_leaf server 'subjectAltName=DNS:erp.local,DNS:localhost,IP:127.0.0.1
extendedKeyUsage=serverAuth
'

# ---- the gateway client certificate (stands in for the ATECC-held key) ------
key gateway.key
openssl req -new -key gateway.key -out gateway.csr \
  -subj "/O=${OPERATOR}/OU=gateways/CN=${GBOX}"
sign_leaf gateway 'extendedKeyUsage=clientAuth
'

# ---- a leaf from someone else's root, to prove the boundary is real ---------
# Kept deliberately flat: the point under test is the trust root, not the tier
# count, and a foreign chain that fails for the wrong reason proves less.
key foreign-root.key
openssl req -x509 -new -key foreign-root.key -sha256 -days "$DAYS_ROOT" \
  -out foreign-root.crt -subj "/O=NOT-THE-OPERATOR/CN=foreign root" \
  -addext 'basicConstraints=critical,CA:TRUE'
key foreign-gateway.key
openssl req -new -key foreign-gateway.key -out foreign-gateway.csr \
  -subj "/O=NOT-THE-OPERATOR/OU=gateways/CN=${GBOX}"
openssl x509 -req -in foreign-gateway.csr -CA foreign-root.crt -CAkey foreign-root.key \
  -CAcreateserial -days "$DAYS_LEAF" -sha256 -out foreign-gateway.crt \
  -extfile <(printf 'extendedKeyUsage=clientAuth\n')
cp foreign-gateway.crt foreign-gateway-chain.crt

rm -f ./*.csr ./*.srl
chmod 600 ./*.key

echo "Throwaway PKI in $(pwd):"
echo "  operator-root.crt        the trust anchor the proxy verifies against"
echo "  issuing-ca.crt           the intermediate; travels in every chain below"
echo "  server-chain.crt/.key    the ERP's TLS identity  (present the CHAIN)"
echo "  gateway-chain.crt/.key   ${GBOX}, under the operator root  -> accepted"
echo "  foreign-gateway-*        same CN, someone else's root      -> rejected"
