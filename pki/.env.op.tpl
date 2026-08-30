# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# 1Password secret-injection template for CA operations. The values are op://
# references, not secrets — safe to commit. Populate `operator-ca` in the
# IndustryGrow vault first.
#
# USE THIS FOR ISSUANCE ONLY, NOT FOR THE ROOT CEREMONY.
#
# `op run` resolves references through the 1Password app or a service account,
# which wants the network. The root ceremony must run on a machine that is
# offline at generation time (ADR-0024 d5), so the two cannot be combined:
# generate the root passphrase in 1Password *before* going offline, then read it
# off a phone and type it at the prompt. That keeps the passphrase out of every
# file, which is what d6's "held apart" is protecting.
#
# Issuance is a different matter — it signs a CSR with the *issuing* CA, which
# ADR-0024 d1 deliberately keeps online:
#
#     op run --env-file=pki/.env.op.tpl -- \
#       ./pki/sign-csr.sh --dir ./ca --csr GBOX_0001.csr --profile gateway \
#                         --pass env:IGROW_CA_PASS
#
# The root passphrase is referenced here too, for the one online case that needs
# it: renewing the issuing CA (pki/README "Renewing the issuing CA"), which is
# itself a ceremony and should bring the root back from its media on an offline
# machine. Prefer typing it there.

IGROW_CA_PASS=op://IndustryGrow/operator-ca/intermediate_passphrase
IGROW_ROOT_PASS=op://IndustryGrow/operator-ca/root_passphrase
