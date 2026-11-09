#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Restore an ERP backup (ADR-0026). Run on the deployment host, from erp/.
#
#   ./deploy/restore.sh --archive erp-….tar.gz.gpg --pass-file ~/.igrow-backup-pass [--force]
#   ./deploy/restore.sh --archive erp-….tar.gz.gpg --pass-file ~/.igrow-backup-pass \
#                       --into industrygrow_rehearsal --bucket industrygrow-rehearsal
#
# The Python is the one implementation of the archive check, the serial-counter
# refusal (ADR-0026 d8), the rehearsal-bucket rule (d10), the blob pass and the
# cross-store check (d9). This script adds what the erp container cannot do: it
# decrypts the archive, and it replays the index in the MONGO container, the one
# that carries mongorestore. Order: verify and refuse, blobs, index, finish (d4).
# A failed run is recovered by re-running it.

set -euo pipefail

ARCHIVE=""; PASS_FILE=""; INTO=""; BUCKET=""; FORCE=""
COMPOSE=(docker compose)
REMOTE_DIR=/tmp/igrow-restore

usage() { sed -n '3,16p' "$0"; exit "${1:-0}"; }
die() { printf 'error: %s\n' "$1" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --archive)   ARCHIVE="${2:-}";   shift 2 ;;
    --pass-file) PASS_FILE="${2:-}"; shift 2 ;;
    --into)      INTO="${2:-}";      shift 2 ;;
    --bucket)    BUCKET="${2:-}";    shift 2 ;;
    --force)     FORCE=1;            shift ;;
    -h|--help)   usage 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage 2 ;;
  esac
done

[ -n "$ARCHIVE" ] || die "--archive is required (an erp-….tar.gz.gpg or erp-….tar.gz)"
[ -f "$ARCHIVE" ] || die "no archive at $ARCHIVE"

# The store arguments every Python step takes; the Python decides whether they are
# allowed (a rehearsal needs a bucket that is not the live one, ADR-0026 d10).
STORE_ARGS=()
[ -n "$INTO" ] && STORE_ARGS+=(--into "$INTO")
[ -n "$BUCKET" ] && STORE_ARGS+=(--bucket "$BUCKET")
RESTORE_ARGS=("${STORE_ARGS[@]}")
[ -n "$FORCE" ] && RESTORE_ARGS+=(--force)

# Every plaintext this script creates is removed on every exit, success or `die`:
# the decrypted archive on this host and its copy in the erp container. The
# encrypted archive is the operator's and is left alone.
PLAIN=""
cleanup() {
  "${COMPOSE[@]}" exec -T erp rm -rf "$REMOTE_DIR" >/dev/null 2>&1 || true
  if [ -n "$PLAIN" ] && [ -e "$PLAIN" ]; then shred -u "$PLAIN" 2>/dev/null || rm -f "$PLAIN"; fi
}
trap cleanup EXIT

case "$ARCHIVE" in
  *.gpg)
    [ -n "$PASS_FILE" ] || die "--pass-file is required for an encrypted archive"
    [ -f "$PASS_FILE" ] || die "no passphrase file at $PASS_FILE"
    [ -s "$PASS_FILE" ] || die "$PASS_FILE is empty"
    command -v gpg >/dev/null 2>&1 || die "gpg is not installed"
    PLAIN=$(umask 077; mktemp "${TMPDIR:-/tmp}/igrow-restore.XXXXXX")
    printf '[restore] decrypting\n'
    gpg --batch --yes --quiet --decrypt --passphrase-file "$PASS_FILE" \
        --output "$PLAIN" "$ARCHIVE" \
      || die "could not decrypt $ARCHIVE"
    SRC="$PLAIN"
    ;;
  *) SRC="$ARCHIVE" ;;
esac

IN_ERP="$REMOTE_DIR/restore.tar.gz"
"${COMPOSE[@]}" exec -T erp sh -c "umask 077; mkdir -p $REMOTE_DIR && cat > $IN_ERP" < "$SRC" \
  || die "could not copy the archive into the erp container"

# --- verify, refuse, blobs FIRST (ADR-0026 d4, d8, d10) --------------------------
"${COMPOSE[@]}" exec -T erp python -m app.backup restore --archive "$IN_ERP" \
  --objects-only "${RESTORE_ARGS[@]}" \
  || die "the restore stopped before the index; nothing was replayed into Mongo"

# --- the index SECOND, in the mongo container ------------------------------------
# The erp container knows the archive's source database and the target one; the
# mongo container knows the credentials. --drop replaces each restored collection.
NS=$("${COMPOSE[@]}" exec -T erp python -m app.backup namespaces --archive "$IN_ERP" \
  "${STORE_ARGS[@]}" | tr -d '\r') || die "could not read the archive's namespaces"
NS_FROM=$(printf '%s\n' "$NS" | sed -n 1p)
NS_TO=$(printf '%s\n' "$NS" | sed -n 2p)
[ -n "$NS_FROM" ] && [ -n "$NS_TO" ] || die "could not read the archive's namespaces"

printf '[restore] replaying the index (%s -> %s)\n' "$NS_FROM" "$NS_TO"
# Straight from the erp container into the mongo container, never onto this host
# (the decrypted archive above is the only plaintext here).
"${COMPOSE[@]}" exec -T erp python -c \
  'import shutil, sys, tarfile; shutil.copyfileobj(tarfile.open(sys.argv[1], "r:gz").extractfile("mongo.archive"), sys.stdout.buffer)' \
  "$IN_ERP" \
  | "${COMPOSE[@]}" exec -T mongo sh -c \
      'mongorestore --uri="mongodb://$MONGO_INITDB_ROOT_USERNAME:$MONGO_INITDB_ROOT_PASSWORD@localhost:27017/?authSource=admin" --archive --drop --nsFrom="$1" --nsTo="$2"' \
      sh "$NS_FROM" "$NS_TO" \
  || die "the index replay failed. The objects are restored and the index is not — re-run."

# --- counter tenant, then the cross-store check (ADR-0026 d9) --------------------
"${COMPOSE[@]}" exec -T erp python -m app.backup finish "${STORE_ARGS[@]}" \
  || die "the restored store does not pass check-live; it is not restored (ADR-0026 d9)"

printf '[restore] done: %s\n' "${INTO:-the configured database}"
