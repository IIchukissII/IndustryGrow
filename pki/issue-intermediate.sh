#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Ceremony step 2 of 2 — the root signs the issuing intermediate (ADR-0024 d1, d2).
#
#   ./issue-intermediate.sh --dir DIR [--days N] [--root-pass SPEC] [--pass SPEC]
#
# RUN THIS ON THE OFFLINE MACHINE, while the root key is still present. This is
# the only thing the root ever signs (d2), so it is the last time the root key is
# needed until the intermediate is renewed or the root is rotated.
#
# What comes out:
#   issuing-ca.key   encrypted P-256 key — goes to the ERP host, stays there
#   issuing-ca.crt   the intermediate, presented in-chain by every leaf
#
# The intermediate is what absorbs the online exposure of ADR-0007 d7's short
# renewal cadence. Compromise of it is recoverable: re-run this script, re-issue
# leaves, redistribute nothing — gateways anchor on the root (d3), not on this.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=_lib.sh
. "$SCRIPT_DIR/_lib.sh"

CA_DIR=""
DAYS=1095          # 3 years, well inside the root's 10. See d8 — what is decided
                   # is the relation, not this number.
ROOT_PASS=""
PASS=""

while [ $# -gt 0 ]; do
  case "$1" in
    --dir)       CA_DIR="${2:-}";    shift 2 ;;
    --days)      DAYS="${2:-}";      shift 2 ;;
    --root-pass) ROOT_PASS="${2:-}"; shift 2 ;;
    --pass)      PASS="${2:-}";      shift 2 ;;
    -h|--help)   sed -n '5,20p' "$0"; exit 0 ;;
    *)           die "unknown argument: $1" ;;
  esac
done

[ -n "$CA_DIR" ] || die "--dir is required"

CONF="$SCRIPT_DIR/openssl.cnf"
[ -f "$CONF" ] || die "missing $CONF"

ROOT_KEY="$CA_DIR/operator-root.key"
ROOT_CRT="$CA_DIR/operator-root.crt"
KEY="$CA_DIR/issuing-ca.key"
CRT="$CA_DIR/issuing-ca.crt"
CSR="$CA_DIR/issuing-ca.csr"

[ -f "$ROOT_CRT" ] || die "no operator root in $CA_DIR — run bootstrap-root.sh first"
[ -f "$ROOT_KEY" ] || die "operator-root.key is not in $CA_DIR.
       If it is on removable media, bring it back for this step only, then remove
       it again (ADR-0024 d5)."

[ -e "$KEY" ] && die "$KEY exists — move it aside to issue a successor intermediate"
[ -e "$CRT" ] && die "$CRT exists — move it aside to issue a successor intermediate"

# d8: the root must outlive the intermediate it is about to sign.
assert_issuer_outlives "$ROOT_CRT" "$DAYS" "intermediate"

# Inherit the operator's organisation from the root's subject, so the two cannot
# disagree about who this CA belongs to.
ROOT_SUBJ=$(openssl x509 -in "$ROOT_CRT" -noout -subject -nameopt RFC2253 | sed 's/^subject=//')
OPERATOR=$(printf '%s' "$ROOT_SUBJ" | sed -n 's/.*\bO=\([^,]*\).*/\1/p')
[ -n "$OPERATOR" ] || die "could not read O= from the root subject: $ROOT_SUBJ"

root_pass=(); [ -n "$ROOT_PASS" ] && root_pass=(-passin "$ROOT_PASS")
pass_out=();  pass_in=()
if [ -n "$PASS" ]; then
  pass_out=(-pass "$PASS"); pass_in=(-passin "$PASS")
fi

umask 077

# See openssl.cnf: ${ENV::PKI_SAN} is resolved at parse time; unused here.
export PKI_SAN="${PKI_SAN:-DNS:unused.invalid}"

openssl genpkey -algorithm EC \
  -pkeyopt ec_paramgen_curve:P-256 \
  -aes256 "${pass_out[@]}" \
  -out "$KEY"

openssl req -new \
  -key "$KEY" "${pass_in[@]}" \
  -config "$CONF" \
  -subj "/O=${OPERATOR}/OU=pki/CN=${OPERATOR} issuing CA" \
  -out "$CSR"

# v3_intermediate carries basicConstraints CA:TRUE,pathlen:0 — d2's constraint,
# enforced by the certificate rather than by whoever runs the next script.
openssl x509 -req \
  -in "$CSR" \
  -CA "$ROOT_CRT" -CAkey "$ROOT_KEY" "${root_pass[@]}" \
  -CAcreateserial -CAserial "$CA_DIR/root.srl" \
  -sha256 -days "$DAYS" \
  -extfile "$CONF" -extensions v3_intermediate \
  -out "$CRT"

rm -f "$CSR"
chmod 600 "$KEY"
chmod 644 "$CRT"

# Prove the chain before claiming success. A CA that does not verify against its
# own root is worth catching here rather than at the first handshake.
openssl verify -CAfile "$ROOT_CRT" "$CRT" >/dev/null \
  || die "the issued intermediate does not verify against the operator root"

cat >&2 <<EOF

Issuing CA created in $CA_DIR

  issuing-ca.crt   the intermediate — travels in-chain with every leaf
  issuing-ca.key   ENCRYPTED PRIVATE KEY — belongs on the ERP host

Chain verified against the operator root.

The root has now signed the only thing it signs. Remove operator-root.key from
this machine (ADR-0024 d5, d6) — the two removable copies are the ones that
matter from here.

Issue leaves with:  ./sign-csr.sh --dir DIR --csr FILE --profile gateway|server
EOF
