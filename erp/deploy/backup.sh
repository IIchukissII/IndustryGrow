#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Take an encrypted backup of the ERP (ADR-0026). Run on the deployment host.
#
#   ./backup.sh --out /var/backups/industrygrow --pass-file ~/.igrow-backup-pass [--keep 7]
#
# This script owns exactly one thing the Python does not: the passphrase. The
# archive is captured and verified inside the container, then encrypted here before
# it goes anywhere (ADR-0026 d7). Keeping the passphrase out of the application
# means it never reaches a log, a traceback, or the container's environment.
#
# WHAT THIS DOES NOT DO: put the second copy somewhere else. ADR-0026 d6 requires
# two copies in separate locations, neither co-located with the warehouse's failure
# domain — and only you know where your second location is. This writes one
# encrypted file and then tells you that you are not finished.

set -euo pipefail

OUT=""; PASS_FILE=""; KEEP=""
COMPOSE=(docker compose)
REMOTE_DIR=/tmp/igrow-backup

usage() { sed -n '3,18p' "$0"; exit "${1:-0}"; }
die() { printf 'error: %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --out)       OUT="${2:-}";       shift 2 ;;
    --pass-file) PASS_FILE="${2:-}"; shift 2 ;;
    --keep)      KEEP="${2:-}";      shift 2 ;;
    -h|--help)   usage 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage 2 ;;
  esac
done

[ -n "$OUT" ] || die "--out is required (a directory for the encrypted archive)"
[ -n "$PASS_FILE" ] || die "--pass-file is required. ADR-0026 d7: this archive carries
       operator-private production data and must not leave the host in plaintext."
[ -f "$PASS_FILE" ] || die "no passphrase file at $PASS_FILE"
[ -s "$PASS_FILE" ] || die "$PASS_FILE is empty"

# gpg, because it is the one of the two that encrypts symmetrically from a
# passphrase FILE without a terminal. age is a fine choice for this — ADR-0026 d7
# names it — but `age -p` insists on prompting, so scripting it would mean feeding
# a tty, and a backup job should not need one. An operator who prefers age can run
# this with --keep unset, take the plaintext path below, and encrypt by hand.
command -v gpg >/dev/null 2>&1 \
  || die "gpg is not installed. Install it, or capture the archive by hand with
       'docker compose exec erp python -m app.backup save' and encrypt it yourself
       (ADR-0026 d7 requires encryption, not a particular tool)."

mkdir -p "$OUT"

# --- capture, inside the container where mongodump and the credentials live ----
printf '[backup] capturing (index first, blobs second — ADR-0026 d3)\n'
"${COMPOSE[@]}" exec -T erp mkdir -p "$REMOTE_DIR"
"${COMPOSE[@]}" exec -T erp python -m app.backup save --out "$REMOTE_DIR" \
  || die "the capture failed; nothing was written"

NAME=$("${COMPOSE[@]}" exec -T erp sh -c "ls -1t $REMOTE_DIR/*.tar.gz | head -1" | tr -d '\r')
[ -n "$NAME" ] || die "the capture reported success but produced no archive"

# --- verify BEFORE encrypting -------------------------------------------------
# A corrupt archive that is also encrypted is two problems. ADR-0026 d10's whole
# point is that an unverified backup is not a backup.
printf '[backup] verifying\n'
"${COMPOSE[@]}" exec -T erp python -m app.backup verify --archive "$NAME" \
  || { "${COMPOSE[@]}" exec -T erp rm -f "$NAME" || true
       die "the archive does not verify — discarded, and not counted as a backup"; }

# --- copy out, encrypt, destroy the plaintext ---------------------------------
BASE=$(basename "$NAME")
PLAIN="$OUT/$BASE"
"${COMPOSE[@]}" exec -T erp cat "$NAME" > "$PLAIN" || die "could not copy the archive out"
"${COMPOSE[@]}" exec -T erp rm -f "$NAME" || true

FINAL="$PLAIN.gpg"
gpg --batch --yes --symmetric --cipher-algo AES256 \
    --passphrase-file "$PASS_FILE" --output "$FINAL" "$PLAIN" \
  || { rm -f "$PLAIN" "$FINAL"; die "encryption failed; the plaintext was removed"; }

shred -u "$PLAIN" 2>/dev/null || rm -f "$PLAIN"
chmod 600 "$FINAL"
printf '[backup] wrote %s (%s)\n' "$FINAL" "$(du -h "$FINAL" | cut -f1)"

# --- retention (a runbook value, not an ADR one) ------------------------------
if [ -n "$KEEP" ]; then
  find "$OUT" -maxdepth 1 -name 'erp-*.tar.gz.gpg' -type f -printf '%T@ %p\n' 2>/dev/null \
    | sort -rn | tail -n "+$((KEEP + 1))" | cut -d' ' -f2- \
    | while read -r old; do printf '[backup] pruning %s\n' "$old"; rm -f "$old"; done
fi

cat >&2 <<NOTE

YOU ARE NOT FINISHED. ADR-0026 d6 requires TWO copies in SEPARATE locations, and
neither co-located with the warehouse's failure domain — the same custody rule the
operator CA root already follows (ADR-0024 d5-7). Copy

  $FINAL

somewhere that is neither this host nor the R2 account holding the warehouse.

And the cost d7 accepts: a forgotten passphrase is an unrecoverable backup. Keep the
passphrase apart from both copies.
NOTE
