<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Deployment-profile signing and verification

The manual of [ADR-0025](../ADR/ADR-0025-deployment-profile-signing-and-gateway-verification.md): the operator signing tool, the ERP profile store and the gateway pull client that sign, carry and verify a deployment instance profile.

## ADR-0025 dec 1

| Code | Role |
|---|---|
| `signing/sign_profile.py` | Operator-side signer of instance profiles |
| `gateway/profile_client.py` | Gateway-side verifier of instance profiles |

Community template signing (ADR-0009) has no code.

## ADR-0025 dec 2

| Code | Role |
|---|---|
| `gateway/profile_client.py::pull_once` | The one pull-verify-apply path; verifies against the single configured key |
| `erp/app/api/routes.py::gateway_pull_active_profile` | Serves the machine's active instance version; no template route reaches a gateway |

## ADR-0025 dec 3

| Code | Role |
|---|---|
| `signing/sign_profile.py::cmd_sign` | Writes a detached signature on the operator host |
| `erp/app/services/profiles.py::add_version` | Stores the signature it receives; holds no signing key |
| `erp/app/api/routes.py::store_profile_version` | Accepts a version with its signature from the operator |

## ADR-0025 dec 4

| Code | Role |
|---|---|
| `signing/sign_profile.py::cmd_keygen` | Generates the profile-signing keypair, apart from `pki/bootstrap-root.sh` and `firmware/tools/mkimage.py` |

## ADR-0025 dec 5

| Code | Role |
|---|---|
| `signing/sign_profile.py::cmd_keygen` | Refuses to write the key without a passphrase; refuses to overwrite one without `--force` |

## ADR-0025 dec 6

| Code | Role |
|---|---|
| `signing/sign_profile.py::sign_bytes` | Signs the file's bytes as read; never rewrites the document |
| `erp/app/services/profiles.py::add_version` | Stores `document_b64` untouched |
| `erp/app/api/routes.py::gateway_pull_active_profile` | Returns the stored `document_b64` and `signature` as stored |
| `gateway/profile_client.py::document_and_signature` | Decodes `document_b64` to the signed bytes; refuses a reply without it |
| `gateway/profile_client.py::verify_signature` | Verifies over the decoded bytes |

## ADR-0025 dec 7

| Code | Role |
|---|---|
| `signing/sign_profile.py::document_identity` | Refuses to sign a document without `machine_id` and `version_tag` |
| `gateway/profile_client.py::machine_identity` | Reads this gateway's `GBOX_NNNN` from its client certificate CN |
| `gateway/profile_client.py::version_of` | Reads machine and version from the verified bytes |
| `gateway/profile_client.py::pull_once` | Refuses a document addressed to another machine |

## ADR-0025 dec 8

| Code | Role |
|---|---|
| `gateway/profile_client.py::version_ordinal` | Orders version tags by their leading integer; refuses an unorderable tag |
| `gateway/profile_client.py::installed_version` | Reads the running version from the active or last-known-good profile |
| `gateway/profile_client.py::pull_once` | Refuses a version not greater than the running one |

## ADR-0025 dec 9

| Code | Role |
|---|---|
| `signing/sign_profile.py::sign_bytes` | ECDSA P-256 over SHA-256 through `openssl dgst -sha256 -sign` |
| `gateway/profile_client.py::verify_signature` | Verification through `openssl dgst -sha256 -verify` |

## ADR-0025 dec 10

| Code | Role |
|---|---|
| `gateway/profile_client.py::Config.from_env` | Reads the key path from `IGROW_PROFILE_VERIFY_KEY`, default `pki/profile-verify.pub` under the config directory |
| `gateway/profile_client.py::verify_signature` | Fails closed when no key is provisioned |
| `gateway/provision.sh::setup_dirs` | Creates the config `pki` directory the key is copied into at commissioning |

## ADR-0025 dec 11

| Code | Role |
|---|---|
| `erp/app/services/profiles.py::mark_active` | Raises `UnsignedVersionError` for a version without a signature |
| `erp/app/api/routes.py::record_active_profile` | Answers `UnsignedVersionError` with HTTP 409 |
| `gateway/profile_client.py::document_and_signature` | Refuses a pulled profile without a signature |

## ADR-0025 dec 12

Not yet realised.

Links: [signing/README.md](../signing/README.md), [gateway/README.md](../gateway/README.md).
