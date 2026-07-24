# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Shared helpers for the operator CA scripts (ADR-0024). Sourced, not executed.

die() { printf 'error: %s\n' "$1" >&2; exit 1; }
warn() { printf 'warning: %s\n' "$1" >&2; }

# ADR-0024 decision 8, enforced rather than documented: each tier's validity must
# strictly contain the validity of everything it issues, plus a renewal margin.
#
# The failure this prevents is a quiet one. A certificate that outlives its issuer
# validates fine on the day it is issued and starts failing later, at the issuer's
# expiry, with a chain error that points at the issuer rather than at the leaf that
# was mis-issued. Catching it at issuance is the only cheap moment.
#
#   assert_issuer_outlives ISSUER_CRT DAYS [WHAT]
assert_issuer_outlives() {
  issuer_crt="$1"; days="$2"; what="${3:-certificate}"
  seconds=$(( days * 86400 ))

  # -checkend is used rather than parsing notAfter because date-string parsing
  # differs between GNU and BSD date, and the ceremony machine is deliberately
  # whatever the operator had lying around.
  if ! openssl x509 -in "$issuer_crt" -noout -checkend "$seconds" >/dev/null 2>&1; then
    issuer_end=$(openssl x509 -in "$issuer_crt" -noout -enddate | cut -d= -f2)
    die "issuer expires $issuer_end, before the $days-day $what it would sign.
       ADR-0024 d8: an issuer must outlive what it issues. Renew the issuer first,
       or shorten --days."
  fi

  # A margin of less than one further lifetime means the next routine renewal will
  # be the one that trips the check above.
  if ! openssl x509 -in "$issuer_crt" -noout -checkend "$(( seconds * 2 ))" >/dev/null 2>&1; then
    issuer_end=$(openssl x509 -in "$issuer_crt" -noout -enddate | cut -d= -f2)
    warn "issuer expires $issuer_end — less than one further ${days}-day period of
         headroom. This issuance succeeds; the next one may not. Renew the issuer."
  fi
}
