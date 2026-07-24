#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Routine issuance — the intermediate signs a leaf (ADR-0024 d1).
#
#   ./sign-csr.sh --dir DIR --csr FILE --profile gateway|server \
#                 [--out FILE] [--days N] [--san SPEC] [--pass SPEC]
#
# Runs wherever the issuing CA lives — NOT on the offline ceremony machine, and
# not inside the ERP container (d13). The operator root is not involved and must
# not be present.
#
# This signs a CSR; it does not generate keys. That is the point: a gateway's key
# is generated on its ATECC608 and never leaves it (ADR-0007 d1), so the only
# thing that can travel to the CA is a CSR. Producing the ATECC CSR is the
# gateway provisioning tool's job, not this script's.
#
# Profiles (see openssl.cnf):
#   gateway   clientAuth. CN must be the ADR-0017 machine identifier verbatim —
#             GBOX_0001 — because the ERP parses the identity out of the DN.
#   server    serverAuth. Requires --san; a server certificate without a SAN is
#             one that no modern TLS stack will accept.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=_lib.sh
. "$SCRIPT_DIR/_lib.sh"

CA_DIR=""; CSR=""; PROFILE=""; OUT=""; SAN=""; PASS=""
DAYS=90            # ADR-0007 d7: short-lived and renewed. The runbook owns the value.

while [ $# -gt 0 ]; do
  case "$1" in
    --dir)     CA_DIR="${2:-}";  shift 2 ;;
    --csr)     CSR="${2:-}";     shift 2 ;;
    --profile) PROFILE="${2:-}"; shift 2 ;;
    --out)     OUT="${2:-}";     shift 2 ;;
    --days)    DAYS="${2:-}";    shift 2 ;;
    --san)     SAN="${2:-}";     shift 2 ;;
    --pass)    PASS="${2:-}";    shift 2 ;;
    -h|--help) sed -n '5,25p' "$0"; exit 0 ;;
    *)         die "unknown argument: $1" ;;
  esac
done

[ -n "$CA_DIR" ]  || die "--dir is required"
[ -n "$CSR" ]     || die "--csr is required"
[ -n "$PROFILE" ] || die "--profile is required (gateway|server)"
[ -f "$CSR" ]     || die "no such CSR: $CSR"

CONF="$SCRIPT_DIR/openssl.cnf"
[ -f "$CONF" ] || die "missing $CONF"

CA_CRT="$CA_DIR/issuing-ca.crt"
CA_KEY="$CA_DIR/issuing-ca.key"
[ -f "$CA_CRT" ] || die "no issuing CA in $CA_DIR — run issue-intermediate.sh first"
[ -f "$CA_KEY" ] || die "no issuing-ca.key in $CA_DIR"

# d1: leaves are issued by the intermediate, never by the root. If the root key is
# sitting here, something has gone wrong with the ceremony rather than with this
# invocation, and saying so is more useful than signing anyway.
[ -e "$CA_DIR/operator-root.key" ] && die "operator-root.key is present in $CA_DIR.
       The root does not issue leaves (ADR-0024 d1) and does not belong on an
       online host (d5). Remove it before issuing."

case "$PROFILE" in
  gateway) EXT=v3_gateway ;;
  server)  EXT=v3_server
           [ -n "$SAN" ] || die "--profile server requires --san (e.g. 'DNS:erp.local,IP:127.0.0.1')" ;;
  *)       die "unknown profile: $PROFILE (expected gateway or server)" ;;
esac

# d8: the intermediate must outlive the leaf it is about to sign.
assert_issuer_outlives "$CA_CRT" "$DAYS" "leaf"

SUBJ=$(openssl req -in "$CSR" -noout -subject -nameopt RFC2253 | sed 's/^subject=//')
CN=$(printf '%s' "$SUBJ" | sed -n 's/.*\bCN=\([^,]*\).*/\1/p')
[ -n "$CN" ] || die "the CSR has no CN: $SUBJ"

# The gateway CN is an identity, not a label: the ERP derives GBOX_NNNN from this
# exact string. A CSR whose CN is a hostname, a description, or a typo produces a
# certificate that authenticates as nothing, and the failure appears in the ERP as
# a rejected caller rather than here.
if [ "$PROFILE" = gateway ] && ! printf '%s' "$CN" | grep -Eq '^GBOX_[0-9]{4}$'; then
  die "gateway CN must be an ADR-0017 machine identifier (GBOX_NNNN), got: $CN"
fi

[ -n "$OUT" ] || OUT="$CA_DIR/issued/${CN}.crt"
mkdir -p "$(dirname "$OUT")"

pass_in=(); [ -n "$PASS" ] && pass_in=(-passin "$PASS")

umask 022
export PKI_SAN="$SAN"

openssl x509 -req \
  -in "$CSR" \
  -CA "$CA_CRT" -CAkey "$CA_KEY" "${pass_in[@]}" \
  -CAcreateserial -CAserial "$CA_DIR/issuing.srl" \
  -sha256 -days "$DAYS" \
  -extfile "$CONF" -extensions "$EXT" \
  -out "$OUT"

# The chain a peer actually presents: leaf first, then the intermediate. The root
# is the anchor (d3) and is deliberately not in here — a peer that ships its own
# root is asking the other side to trust it on its own say-so.
CHAIN="${OUT%.crt}-chain.crt"
cat "$OUT" "$CA_CRT" > "$CHAIN"

openssl verify -CAfile "$CA_DIR/operator-root.crt" -untrusted "$CA_CRT" "$OUT" >/dev/null \
  || die "the issued leaf does not verify against the operator root"

printf 'issued %s (%s, %s days)\n' "$OUT" "$PROFILE" "$DAYS" >&2
printf '  chain: %s   (present this, not the root)\n' "$CHAIN" >&2
