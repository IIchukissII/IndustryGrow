#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# The operator CA ceremony, as one reproducible run (ADR-0024 decisions 5-8).
#
#   ./ceremony.sh --dir DIR --operator NAME [--days N] [--intermediate-days N]
#
# Composes bootstrap-root.sh and issue-intermediate.sh, enforces the property
# decision 5 requires instead of asking an operator to remember it, and verifies
# the result before reporting success.
#
# WHY THIS EXISTS. Run by hand the ceremony is a sequence of steps whose most
# important property -- that the generating process has no network -- is
# established by the operator disconnecting an interface and re-connecting it
# afterwards. That is not reproducible: it cannot be re-run identically, it
# cannot be tested, and whether it held is unverifiable after the fact. Here the
# isolation is entered by the script, so every run has it and a run that could
# not get it fails instead of proceeding.
#
# ISOLATION. The key material is generated inside `unshare --user --net`: a user
# and network namespace whose only interface is a down loopback. A process there
# has no route to anything, which is checked below before any key is written.
# This needs no privilege. Read decision 5's "offline" note before assuming this
# is equivalent to a disconnected machine -- the host stays connected, and what
# is guaranteed is that the generating process could not reach it.
#
# WHAT THIS DOES NOT DO. Custody (decision 6) is physical: two encrypted copies
# on separate removable media in separate locations, passphrase held apart. No
# script can do it and none can verify it. The run ends by telling you exactly
# what is left and refuses to claim the ceremony is complete.
set -euo pipefail

die() { printf 'ceremony: %s\n' "$1" >&2; exit 1; }
say() { printf '\n== %s\n' "$1"; }

CA_DIR=""; OPERATOR=""; ROOT_DAYS=3650; INT_DAYS=1095
while [ $# -gt 0 ]; do
  case "$1" in
    --dir)                CA_DIR="${2:-}";    shift 2 ;;
    --operator)           OPERATOR="${2:-}";  shift 2 ;;
    --days)               ROOT_DAYS="${2:-}"; shift 2 ;;
    --intermediate-days)  INT_DAYS="${2:-}";  shift 2 ;;
    *) die "unknown argument '$1'" ;;
  esac
done
[ -n "$CA_DIR" ]   || die "--dir is required"
[ -n "$OPERATOR" ] || die "--operator is required"

# No clobber. Re-running into an existing directory would either overwrite a root
# key or silently leave the old one in place; both are worse than stopping. A
# repeat run goes into a fresh directory, which is what makes this reproducible
# rather than destructive.
[ -e "$CA_DIR" ] && die "$CA_DIR exists. A ceremony writes a new CA; move it aside or choose another --dir."

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
command -v openssl >/dev/null || die "openssl is not on PATH"
command -v unshare >/dev/null || die "unshare is not available; this procedure needs it to guarantee isolation"

# The passphrases are read BEFORE isolation, because resolving them needs the
# network (1Password) and the ceremony must not. They are passed to openssl by
# environment, never on a command line, where the process table would show them.
: "${IGROW_ROOT_PASS:?set IGROW_ROOT_PASS (op run --env-file=pki/.env.op.tpl -- ./pki/ceremony.sh ...)}"
: "${IGROW_CA_PASS:?set IGROW_CA_PASS}"
[ "$IGROW_ROOT_PASS" != "$IGROW_CA_PASS" ] || die "the root and issuing passphrases are identical; decision 6 holds them apart"

say "Isolating (unshare --user --net)"
# The whole key-generating body runs in the namespace. Everything it needs is on
# disk already; nothing it does should reach a network, and in here it cannot.
export CA_DIR OPERATOR ROOT_DAYS INT_DAYS HERE
unshare --user --map-root-user --net bash -euo pipefail -c '
  # Prove the isolation before writing anything. A namespace with a route table
  # would mean unshare silently did less than asked.
  if ip route show 2>/dev/null | grep -q .; then
    echo "ceremony: the namespace has routes; refusing to generate a key" >&2; exit 1
  fi
  echo "   no routes, no reachable interface - the generating process is offline"

  "$HERE/bootstrap-root.sh"      --dir "$CA_DIR" --operator "$OPERATOR" \
                                 --days "$ROOT_DAYS" --pass env:IGROW_ROOT_PASS
  "$HERE/issue-intermediate.sh"  --dir "$CA_DIR" --days "$INT_DAYS" \
                                 --root-pass env:IGROW_ROOT_PASS --pass env:IGROW_CA_PASS
'

say "Verifying"
ROOT="$CA_DIR/operator-root.crt"; INT="$CA_DIR/issuing-ca.crt"
for f in "$ROOT" "$INT" "$CA_DIR/operator-root.key" "$CA_DIR/issuing-ca.key"; do
  [ -s "$f" ] || die "$f was not produced"
done

# The root key must be encrypted at rest (d5). An unencrypted key still parses
# without a passphrase, so this is the check that catches a --pass that silently
# did nothing.
grep -q "ENCRYPTED" "$CA_DIR/operator-root.key" || die "the root key is NOT encrypted"
echo "   root key is encrypted at rest"

openssl verify -CAfile "$ROOT" "$INT" >/dev/null || die "the issuing CA does not verify against the root"
echo "   issuing CA verifies against the root"

# d8: the root must outlive the intermediate, or the chain expires from the top
# and every leaf under it dies early.
root_end=$(date -d "$(openssl x509 -in "$ROOT" -noout -enddate | cut -d= -f2)" +%s)
int_end=$(date -d "$(openssl x509 -in "$INT" -noout -enddate | cut -d= -f2)" +%s)
[ "$root_end" -gt "$int_end" ] || die "the root expires before the issuing CA (decision 8)"
echo "   root outlives the issuing CA"

for c in "$ROOT" "$INT"; do
  openssl x509 -in "$c" -noout -text | grep -q "CA:TRUE" || die "$c is not a CA certificate"
done
echo "   both certificates are CAs"

FP=$(openssl x509 -in "$ROOT" -noout -fingerprint -sha256 | cut -d= -f2)

cat <<EOF

== The CA exists. The ceremony is NOT complete.

  root        $ROOT
  issuing CA  $INT
  SHA-256     $FP

Record the fingerprint in 1Password (IndustryGrow/operator-ca, root_fingerprint).

Custody remains, and it is the step that matters (ADR-0024 d6). Until it is done
the root key sits on this host, which is what decision 5 exists to prevent:

  cp $CA_DIR/operator-root.key /media/<a>/
  cp $CA_DIR/operator-root.key /media/<b>/
  shred -u $CA_DIR/operator-root.key

Two media, two physical locations, passphrase held apart from both. Note that
shred does not reliably destroy the original blocks on an SSD or a
copy-on-write filesystem; see pki/README.md.
EOF
