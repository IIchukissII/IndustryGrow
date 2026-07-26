#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# A self-signed server certificate for the operator console, so the token does not
# cross the LAN in clear text.
#
#   ./make-cert.sh --dir ./certs --name industrygrow.local [--ip 192.168.178.55]
#
# THIS IS NOT A CA, AND NOT `pki/`. It issues one self-signed leaf for one nginx
# listener. It creates no trust root, signs nothing else, and no gateway will ever
# chain to it — ADR-0007 decision 3 roots gateway trust per operator, and ADR-0024
# decides how that root is created. Nothing here touches either.
#
# It is also not `deploy/mtls/make-test-ca.sh`, which fabricates a whole two-tier
# CA for exercising the mTLS path in tests. That one produces something shaped like
# the real trust root and must never be deployed; this produces something that
# could not be mistaken for one.
#
# Browsers will warn on first visit because nothing signed it. That is honest: the
# certificate asserts only "this connection is encrypted", which is what it is for.

set -euo pipefail

DIR="./certs"; NAME="industrygrow.local"; IP=""; DAYS=825

usage() { sed -n '3,24p' "$0"; exit "${1:-0}"; }
die() { printf 'error: %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dir)  DIR="${2:-}";  shift 2 ;;
    --name) NAME="${2:-}"; shift 2 ;;
    --ip)   IP="${2:-}";   shift 2 ;;
    --days) DAYS="${2:-}"; shift 2 ;;
    -h|--help) usage 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage 2 ;;
  esac
done

command -v openssl >/dev/null || die "openssl is not on PATH"
mkdir -p "$DIR"

# SANs, not just a CN: no modern TLS stack has looked at the Common Name for host
# verification in years, and a certificate without a SAN is one browsers reject
# outright rather than merely warn about.
SAN="DNS:${NAME},DNS:localhost,IP:127.0.0.1"
[ -n "$IP" ] && SAN="${SAN},IP:${IP}"

openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes \
  -keyout "$DIR/console.key" -out "$DIR/console.crt" \
  -days "$DAYS" -subj "/CN=${NAME}" -addext "subjectAltName=${SAN}" \
  -addext "basicConstraints=critical,CA:FALSE" \
  -addext "keyUsage=critical,digitalSignature,keyEncipherment" \
  -addext "extendedKeyUsage=serverAuth" 2>/dev/null \
  || die "openssl failed to generate the certificate"

chmod 600 "$DIR/console.key"
chmod 644 "$DIR/console.crt"

printf 'wrote %s/console.crt and console.key\n' "$DIR"
printf '  names: %s\n' "$SAN"
printf '  expires: %s\n' "$(openssl x509 -in "$DIR/console.crt" -noout -enddate | cut -d= -f2)"
# CA:FALSE is asserted above and worth saying out loud: this certificate cannot
# sign anything, which is the property that keeps it from drifting into the role
# pki/ owns.
printf '\nThis is a leaf, not a CA (CA:FALSE). The gateway channel still needs the\n'
printf 'operator CA — see pki/README.md and ADR-0024.\n'
