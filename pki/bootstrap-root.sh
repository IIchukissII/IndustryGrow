#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Ceremony step 1 of 2 — generate the operator root (ADR-0024 decisions 5, 6).
#
#   ./bootstrap-root.sh --dir DIR --operator NAME [--days N] [--pass SPEC]
#
# RUN THIS ON AN OFFLINE MACHINE. Decision 5 is that the root key is generated
# where it will live and is never present on a networked host; this script
# cannot check that for you, and nothing downstream can tell the difference
# afterwards. That is why it is a ceremony and not a build step.
#
# What comes out:
#   operator-root.key   encrypted P-256 private key  -> removable media, x2 (d6)
#   operator-root.crt   the trust anchor (public)    -> copy freely
#
# --pass takes an OpenSSL passphrase spec (pass:, env:, file:). Omit it and
# OpenSSL prompts, which is what a real ceremony should do; it exists so the
# tests and a scripted rehearsal can run unattended.

set -euo pipefail

CA_DIR=""
OPERATOR=""
DAYS=3650          # 10 years. What is *decided* is the d8 relation (root >>
                   # intermediate >> leaf); this number is the runbook's to pick.
PASS=""

die() { printf 'error: %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --dir)      CA_DIR="${2:-}";   shift 2 ;;
    --operator) OPERATOR="${2:-}"; shift 2 ;;
    --days)     DAYS="${2:-}";     shift 2 ;;
    --pass)     PASS="${2:-}";     shift 2 ;;
    -h|--help)  sed -n '5,22p' "$0"; exit 0 ;;
    *)          die "unknown argument: $1" ;;
  esac
done

[ -n "$CA_DIR" ]   || die "--dir is required"
[ -n "$OPERATOR" ] || die "--operator is required (e.g. OP-STRAWBERRY-01)"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF="$SCRIPT_DIR/openssl.cnf"
[ -f "$CONF" ] || die "missing $CONF"

mkdir -p "$CA_DIR"
KEY="$CA_DIR/operator-root.key"
CRT="$CA_DIR/operator-root.crt"

# Refuse to overwrite. Regenerating a root over a live one silently orphans every
# certificate beneath it, and it surfaces later as unexplained handshake failures
# rather than as anything pointing back here.
[ -e "$KEY" ] && die "$KEY exists — refusing to overwrite an operator root"
[ -e "$CRT" ] && die "$CRT exists — refusing to overwrite an operator root"

# With no --pass, OpenSSL prompts on the terminal (twice: once to set, once to
# use). An encrypted key whose passphrase came from a flag in someone's shell
# history is an unencrypted key with extra steps, so prompting is the default.
pass_out=(); pass_in=()
if [ -n "$PASS" ]; then
  pass_out=(-pass "$PASS")
  pass_in=(-passin "$PASS")
else
  printf 'A passphrase protects the root key at rest (ADR-0024 d6).\n' >&2
  printf 'Hold it separately from BOTH copies of the media.\n\n' >&2
fi

umask 077

# Placeholder for openssl.cnf's ${ENV::PKI_SAN}: resolved at parse time even
# though only the server profile uses it, and unused here.
export PKI_SAN="${PKI_SAN:-DNS:unused.invalid}"

# P-256 per ADR-0007 d1 — the curve the ATECC608 does, so the chain is one curve
# from the root down to the on-chip device key.
openssl genpkey -algorithm EC \
  -pkeyopt ec_paramgen_curve:P-256 \
  -aes256 "${pass_out[@]}" \
  -out "$KEY"

openssl req -x509 -new \
  -key "$KEY" "${pass_in[@]}" \
  -sha256 -days "$DAYS" \
  -config "$CONF" -extensions v3_root \
  -subj "/O=${OPERATOR}/OU=pki/CN=${OPERATOR} operator root" \
  -out "$CRT"

chmod 600 "$KEY"
chmod 644 "$CRT"

cat >&2 <<EOF

Operator root created in $CA_DIR

  operator-root.crt   the trust anchor — gateways and the ERP proxy verify
                      against this; public material, copy freely
  operator-root.key   ENCRYPTED PRIVATE KEY — the estate depends on it

Next: ./issue-intermediate.sh --dir "$CA_DIR"

Then the step that makes decision 6 true rather than merely written down:

  1. copy operator-root.key to TWO removable media
  2. store them in TWO separate physical locations
  3. keep the passphrase apart from both
  4. remove operator-root.key from this machine

Losing every copy means re-provisioning every unit in the estate. So does
someone else gaining one (ADR-0024 d11) — there is no revocation path that
saves you.
EOF
