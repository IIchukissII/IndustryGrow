#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Post-deployment verification — run this ON the host that runs the ERP.
#
#   ./verify-deployment.sh [--host erp.local] [--tls-dir DIR]
#                          [--https-port N] [--mtls-port N]
#
# Every check here exists because the thing it checks is silently wrong when it
# is wrong: the deployment still starts, the console still loads, and the failure
# surfaces later as a gateway that cannot authenticate or a database anyone can
# read. `docker compose ps` says none of that.
#
# The certificate directory and the two published ports are read from the running
# proxy rather than assumed. Both are deployment-set (`ERP_TLS_DIR`,
# `ERP_HTTPS_PORT`, `ERP_MTLS_PORT`) because 443 and 8443 are the ports most
# likely already taken on a host that runs anything else — and a check aimed at a
# port this deployment does not use reports on whatever else answers there, which
# is worse than not checking. Order: an explicit flag, then the running proxy,
# then the environment, then `erp/.env`, then the compose defaults.
#
# Exit status is the number of FAILures, so it is usable in a pipeline — note
# that a pipe replaces it with the last command's. WARNs do not fail the run —
# they are things that are defensible but worth knowing.

set -uo pipefail

HOST="erp.local"
COMPOSE=(docker compose)
# Empty means "not given on the command line" — resolved after the container
# section, once it is known whether there is a proxy to read the answer from.
TLS_DIR=""
HTTPS_PORT=""
MTLS_PORT=""

while [ $# -gt 0 ]; do
  case "$1" in
    --host)     HOST="${2:-}";     shift 2 ;;
    --tls-dir)  TLS_DIR="${2:-}";  shift 2 ;;
    --https-port) HTTPS_PORT="${2:-}"; shift 2 ;;
    --mtls-port)  MTLS_PORT="${2:-}";  shift 2 ;;
    -h|--help)  sed -n '3,25p' "$0"; exit 0 ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$PROJECT_DIR/.env"

# One variable out of the deployment's own .env — the file compose reads. Parsed,
# not sourced: it holds the database and warehouse credentials, and running it as
# shell to read a port number would be a needless way to execute them.
from_env_file() {
  [ -f "$ENV_FILE" ] || return 1
  local v
  v=$(sed -n "s/^[[:space:]]*$1[[:space:]]*=//p" "$ENV_FILE" | tail -1)
  case "$v" in *[[:space:]]\#*) v="${v%%[[:space:]]\#*}" ;; esac
  v=$(printf '%s' "$v" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
  v="${v%\"}"; v="${v#\"}"; v="${v%\'}"; v="${v#\'}"
  [ -n "$v" ] || return 1
  printf '%s' "$v"
}

# The proxy container, found by compose label rather than by asking compose for
# it. `compose ps -q proxy` answers only when the overlay file that defines the
# service is on the command line, and this script is run without it; the running
# container carries the project label either way. The project name comes off a
# container from the base file, so it follows a renamed project too.
proxy_container() {
  local base_id project
  base_id=$("${COMPOSE[@]}" ps -q mongo 2>/dev/null | head -1)
  [ -n "$base_id" ] || return 1
  project=$(docker inspect "$base_id" \
    --format '{{index .Config.Labels "com.docker.compose.project"}}' 2>/dev/null)
  [ -n "$project" ] || return 1
  docker ps -q --filter "label=com.docker.compose.project=$project" \
                --filter "label=com.docker.compose.service=proxy" 2>/dev/null | head -1
}

# What the proxy actually mounts and publishes. This is the ground truth: it
# holds however the deployment was configured, including a value exported into
# the shell that started it and never written down anywhere.
resolve_addresses() {
  local id src port
  id=$(proxy_container)
  if [ -n "$id" ]; then
    src=$(docker inspect "$id" --format \
      '{{range .Mounts}}{{if eq .Destination "/etc/nginx/tls"}}{{.Source}}{{end}}{{end}}' 2>/dev/null)
    [ -n "$TLS_DIR" ] || TLS_DIR="$src"
    port=$(docker port "$id" 443/tcp 2>/dev/null | head -1)
    [ -n "$HTTPS_PORT" ] || HTTPS_PORT="${port##*:}"
    port=$(docker port "$id" 8443/tcp 2>/dev/null | head -1)
    [ -n "$MTLS_PORT" ] || MTLS_PORT="${port##*:}"
  fi
  [ -n "$TLS_DIR" ]    || TLS_DIR="${ERP_TLS_DIR:-$(from_env_file ERP_TLS_DIR)}"
  [ -n "$HTTPS_PORT" ] || HTTPS_PORT="${ERP_HTTPS_PORT:-$(from_env_file ERP_HTTPS_PORT)}"
  [ -n "$MTLS_PORT" ]  || MTLS_PORT="${ERP_MTLS_PORT:-$(from_env_file ERP_MTLS_PORT)}"
  [ -n "$TLS_DIR" ]    || TLS_DIR="$SCRIPT_DIR/mtls/certs"
  [ -n "$HTTPS_PORT" ] || HTTPS_PORT=443
  [ -n "$MTLS_PORT" ]  || MTLS_PORT=8443
  # A relative path in .env is relative to the compose project directory, which
  # is where compose resolves it — not to wherever this was invoked from.
  case "$TLS_DIR" in /*) ;; *) TLS_DIR="$PROJECT_DIR/${TLS_DIR#./}" ;; esac
}

fails=0
pass() { printf '  \033[32mPASS\033[0m  %s\n' "$1"; }
fail() { printf '  \033[31mFAIL\033[0m  %s\n' "$1"; fails=$((fails + 1)); }
warn() { printf '  \033[33mWARN\033[0m  %s\n' "$1"; }
note() { printf '        %s\n' "$1"; }
section() { printf '\n\033[1m%s\033[0m\n' "$1"; }

have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
section "Containers"
# ---------------------------------------------------------------------------

if ! have docker; then
  fail "docker is not on PATH — this script runs on the deployment host"
  exit 1
fi

running() { "${COMPOSE[@]}" ps --status running --format '{{.Service}}' 2>/dev/null | grep -qx "$1"; }

for svc in mongo erp; do
  if running "$svc"; then pass "$svc is running"; else fail "$svc is not running"; fi
done
if running proxy; then
  pass "proxy is running (mTLS overlay is in use)"
  PROXY=yes
else
  PROXY=no
  warn "no proxy container — the mTLS overlay is not in use"
  note "the gateway channel needs it; without it the ERP has no certificate"
  note "verification in front of it (ADR-0022 d2). Deploy with:"
  note "  docker compose -f docker-compose.yml -f docker-compose.mtls.yml up -d"
fi

resolve_addresses
note "console $HTTPS_PORT · gateway $MTLS_PORT · certificates $TLS_DIR"

# ---------------------------------------------------------------------------
section "The database is not open"
# ---------------------------------------------------------------------------

# mongo:7 runs with authentication DISABLED unless root credentials were set at
# first start. Setting them later does nothing: the admin user is created only on
# an empty data volume, so a deployment that started once without them stays open
# until the volume is recreated.
if "${COMPOSE[@]}" exec -T mongo mongosh --quiet --eval 'db.adminCommand({listDatabases:1})' \
     >/dev/null 2>&1; then
  fail "Mongo accepts UNAUTHENTICATED admin commands"
  note "the ERP's operator-private production data (ADR-0021 d14) is readable by"
  note "anyone who reaches this container. Credentials must be set BEFORE the first"
  note "start — adding them now will not create the user. Recreate the volume:"
  note "  docker compose down && docker volume rm industrygrow-erp_mongo-data"
else
  pass "Mongo refuses unauthenticated access"
fi

if docker port "$("${COMPOSE[@]}" ps -q mongo 2>/dev/null)" 2>/dev/null | grep -q .; then
  fail "Mongo publishes a port to the host"
  note "nothing outside the compose network needs it; use 'compose exec mongo mongosh'"
else
  pass "Mongo publishes no host port"
fi

# ---------------------------------------------------------------------------
section "The app port is not a bypass"
# ---------------------------------------------------------------------------

app_ports=$(docker port "$("${COMPOSE[@]}" ps -q erp 2>/dev/null)" 2>/dev/null || true)
if [ -z "$app_ports" ]; then
  pass "the app port is not published (the proxy is the only way in)"
elif printf '%s' "$app_ports" | grep -q '0\.0\.0\.0\|\[::\]'; then
  fail "the app port is published on ALL interfaces: $(printf '%s' "$app_ports" | tr '\n' ' ')"
  note "a caller reaching it directly bypasses certificate verification entirely."
  note "The app answers 503 on gateway routes for an untrusted peer, so this is a"
  note "bypass of the boundary rather than of authentication — but the containment"
  note "is not publishing it. The mTLS overlay removes this port."
else
  warn "the app port is published on localhost: $(printf '%s' "$app_ports" | tr '\n' ' ')"
  note "fine for an admin shell on this host; the overlay removes it entirely"
fi

# ---------------------------------------------------------------------------
section "TLS material"
# ---------------------------------------------------------------------------

if [ ! -d "$TLS_DIR" ] && [ "$PROXY" = no ]; then
  # Not a failure when the overlay is not in use: without a proxy there is nothing
  # to serve a certificate, and the deployment is legitimately incomplete rather
  # than broken. Failing here anyway would report two problems for one absence.
  warn "no certificate directory at $TLS_DIR"
  note "expected while the mTLS overlay is not deployed; the gateway channel needs"
  note "certificates from the operator CA before it can come up (pki/README.md)"
elif [ ! -d "$TLS_DIR" ]; then
  fail "no certificate directory at $TLS_DIR, but the proxy is running"
else
  for f in server-chain.crt server.key operator-root.crt; do
    if [ -f "$TLS_DIR/$f" ]; then pass "$f present"; else fail "$f missing from $TLS_DIR"; fi
  done

  # The two-tier fallout ADR-0024 turned up the hard way: nginx serving the bare
  # leaf leaves a client anchored on the root unable to build a path (d1/d3).
  if [ -f "$TLS_DIR/server-chain.crt" ]; then
    n=$(grep -c 'BEGIN CERTIFICATE' "$TLS_DIR/server-chain.crt" || true)
    if [ "$n" -ge 2 ]; then
      pass "server-chain.crt carries $n certificates (leaf + issuing CA)"
    else
      fail "server-chain.crt holds $n certificate — it is the bare leaf, not a chain"
      note "a client anchored on the operator root cannot build a path from it"
      note "(ADR-0024 d1/d3). Serve the *-chain.crt sign-csr.sh writes."
    fi
  fi

  # The anchor must be the root, not the intermediate: anchoring on the issuing
  # CA trusts whatever that CA signs next rather than the root the operator holds.
  if [ -f "$TLS_DIR/operator-root.crt" ] && have openssl; then
    subj=$(openssl x509 -in "$TLS_DIR/operator-root.crt" -noout -subject -nameopt RFC2253 2>/dev/null)
    issu=$(openssl x509 -in "$TLS_DIR/operator-root.crt" -noout -issuer  -nameopt RFC2253 2>/dev/null)
    if [ "${subj#subject=}" = "${issu#issuer=}" ]; then
      pass "operator-root.crt is self-issued (it is a root, not an intermediate)"
    else
      fail "operator-root.crt is NOT self-issued — that is an intermediate"
      note "anchoring here trusts whatever its issuer signs next (ADR-0024 d3)"
    fi

    if openssl x509 -in "$TLS_DIR/operator-root.crt" -noout -checkend 2592000 >/dev/null 2>&1; then
      pass "the trust anchor is valid for at least 30 more days"
    else
      warn "the trust anchor expires within 30 days"
    fi
  fi

  if [ -e "$TLS_DIR/operator-root.key" ]; then
    fail "operator-root.key is present in $TLS_DIR"
    note "the root key does not belong on an online host (ADR-0024 d5), and it is"
    note "mounted into the proxy container from here"
  else
    pass "no operator root KEY on this host"
  fi
fi

# ---------------------------------------------------------------------------
section "The gateway channel"
# ---------------------------------------------------------------------------

if [ "$PROXY" = no ]; then
  warn "skipped — no proxy container"
elif ! have curl; then
  warn "skipped — curl is not on PATH"
else
  # nginx must reject an anonymous caller before the request reaches the app.
  # `ssl_verify_client on` does not abort the handshake — it completes it and
  # answers 400 "No required SSL certificate was sent" (495/496 where the config
  # maps them), so the refusal *is* an HTTP status. A connection that never got
  # that far is the same refusal one layer down. Any other status means the
  # request reached the app with no certificate behind it.
  code=$(curl -sS --max-time 10 -k -o /dev/null -w '%{http_code}' \
         "https://127.0.0.1:${MTLS_PORT}/api/v1/gateway/active-profile" 2>/dev/null)
  rc=$?
  if [ "$rc" -ne 0 ]; then
    pass "the mTLS port refuses a caller with no client certificate (no TLS session)"
  else
    case "$code" in
      400|495|496) pass "the mTLS port refuses a caller with no client certificate ($code)" ;;
      *) fail "the mTLS port answered $code WITHOUT a client certificate"
         note "ssl_verify_client is not on — every caller would be unauthenticated" ;;
    esac
  fi

  # The console listener must not carry the gateway channel: an identity cannot
  # be established there, so the routes must not appear to work.
  code=$(curl -sS --max-time 10 -k -o /dev/null -w '%{http_code}' \
         "https://127.0.0.1:${HTTPS_PORT}/api/v1/gateway/active-profile" 2>/dev/null || echo 000)
  case "$code" in
    403) pass "the console listener refuses gateway routes (403)" ;;
    000) fail "the console listener did not answer on ${HTTPS_PORT}" ;;
    *)   fail "the console listener answered $code on a gateway route, expected 403" ;;
  esac

  code=$(curl -sS --max-time 10 -k -o /dev/null -w '%{http_code}' \
         "https://127.0.0.1:${HTTPS_PORT}/" 2>/dev/null || echo 000)
  case "$code" in
    200) pass "the console is served on ${HTTPS_PORT}" ;;
    000) fail "nothing answered on ${HTTPS_PORT}" ;;
    *)   warn "the console returned $code on ${HTTPS_PORT}" ;;
  esac
fi

# ---------------------------------------------------------------------------
section "The warehouse"
# ---------------------------------------------------------------------------

# Through the public async surface, not boto3 internals. The first version of this
# check poked at Warehouse().client and .bucket — which are _client and _bucket —
# so it raised AttributeError, caught it, and reported the warehouse unreachable
# on a deployment where it was working. A check that fails when the thing it
# checks is fine is worse than no check: it teaches you to ignore it.
if "${COMPOSE[@]}" exec -T erp python -c '
import asyncio, sys
from app.services.warehouse import Warehouse
try:
    asyncio.run(Warehouse().list_prefix(""))
except Exception as exc:
    print(exc); sys.exit(1)
' >/dev/null 2>&1; then
  pass "the ERP can reach its warehouse bucket"
else
  fail "the ERP cannot reach its warehouse bucket"
  note "check ERP_WAREHOUSE_ENDPOINT / _BUCKET / _ACCESS_KEY / _SECRET_KEY"
  note "  docker compose exec erp env | grep ERP_WAREHOUSE_ | sed 's/=.*KEY=.*/=<set>/'"
fi

# ---------------------------------------------------------------------------
printf '\n'
if [ "$fails" -eq 0 ]; then
  printf '\033[32mall checks passed\033[0m\n'
  printf 'Not covered here: a real gateway pull. Run one from a provisioned gateway\n'
  printf '(gateway/profile_client.py once) — that is the only thing that exercises the\n'
  printf 'whole path end to end.\n'
else
  printf '\033[31m%d check(s) failed\033[0m\n' "$fails"
fi
exit "$fails"
