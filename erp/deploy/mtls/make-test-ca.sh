#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Generate a THROWAWAY operator CA and the certificates needed to exercise the
# mTLS gateway channel (ADR-0022 decision 2) locally.
#
#   ./make-test-ca.sh [output-dir] [GBOX_NNNN]
#
# NOT a CA bootstrap procedure. A real operator root (ADR-0007 decision 3) is a
# long-lived secret with ceremony around it — key custody, offline storage,
# renewal policy — and standing one up is an ADR-0007 deferred decision, not a
# shell script. This produces disposable keys, unencrypted, so that the proxy
# config and the app's identity extraction can be run end to end on a laptop.
#
# Shapes that are deliberately faithful to the real thing:
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
DAYS_LEAF=90   # short-lived, per ADR-0007 d7

mkdir -p "$OUT"
cd "$OUT"

key() { openssl ecparam -name prime256v1 -genkey -noout -out "$1"; }

# ---- the operator root (ADR-0007 d3: rooted per operator, no global root) ----
key operator-root.key
openssl req -x509 -new -key operator-root.key -sha256 -days "$DAYS_ROOT" \
  -out operator-root.crt \
  -subj "/O=${OPERATOR}/OU=pki/CN=${OPERATOR} operator root"

# ---- the ERP's server certificate -------------------------------------------
key server.key
openssl req -new -key server.key -out server.csr \
  -subj "/O=${OPERATOR}/OU=erp/CN=erp.local"
openssl x509 -req -in server.csr -CA operator-root.crt -CAkey operator-root.key \
  -CAcreateserial -days "$DAYS_LEAF" -sha256 -out server.crt \
  -extfile <(printf 'subjectAltName=DNS:erp.local,DNS:localhost,IP:127.0.0.1\nextendedKeyUsage=serverAuth\n')

# ---- the gateway client certificate (stands in for the ATECC-held key) ------
key gateway.key
openssl req -new -key gateway.key -out gateway.csr \
  -subj "/O=${OPERATOR}/OU=gateways/CN=${GBOX}"
openssl x509 -req -in gateway.csr -CA operator-root.crt -CAkey operator-root.key \
  -CAcreateserial -days "$DAYS_LEAF" -sha256 -out gateway.crt \
  -extfile <(printf 'extendedKeyUsage=clientAuth\n')

# ---- a leaf from someone else's root, to prove the boundary is real ---------
key foreign-root.key
openssl req -x509 -new -key foreign-root.key -sha256 -days "$DAYS_ROOT" \
  -out foreign-root.crt -subj "/O=NOT-THE-OPERATOR/CN=foreign root"
key foreign-gateway.key
openssl req -new -key foreign-gateway.key -out foreign-gateway.csr \
  -subj "/O=NOT-THE-OPERATOR/OU=gateways/CN=${GBOX}"
openssl x509 -req -in foreign-gateway.csr -CA foreign-root.crt -CAkey foreign-root.key \
  -CAcreateserial -days "$DAYS_LEAF" -sha256 -out foreign-gateway.crt \
  -extfile <(printf 'extendedKeyUsage=clientAuth\n')

rm -f ./*.csr ./*.srl
chmod 600 ./*.key

echo "Throwaway PKI in $(pwd):"
echo "  operator-root.crt      the trust root the proxy verifies against"
echo "  server.crt/.key        the ERP's TLS identity"
echo "  gateway.crt/.key       ${GBOX}, issued under the operator root  -> accepted"
echo "  foreign-gateway.*      same CN, someone else's root             -> rejected"
