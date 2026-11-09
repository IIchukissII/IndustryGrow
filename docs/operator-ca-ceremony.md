<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Operator CA tooling and the root-key ceremony

The manual of [ADR-0024](../ADR/ADR-0024-operator-ca-bootstrap-and-the-root-key-ceremony.md): the scripts that generate the operator root and issuing CA, issue leaves, and hold the firmware-signing key, and where relying parties hold the anchor.

## ADR-0024 dec 1

| Code | Role |
|---|---|
| `pki/bootstrap-root.sh` | Generates the operator root key and self-signed certificate |
| `pki/issue-intermediate.sh` | The root signs the issuing intermediate |
| `pki/sign-csr.sh` | The intermediate signs a gateway or server leaf; refuses to run beside `operator-root.key` |
| `pki/openssl.cnf` | Extension profiles `v3_root`, `v3_intermediate`, `v3_gateway`, `v3_server` |

Node provenance certificates have no issuing profile.

## ADR-0024 dec 2

| Code | Role |
|---|---|
| `pki/openssl.cnf` | `v3_root`: `CA:TRUE`, `keyCertSign`; `v3_intermediate`: `CA:TRUE, pathlen:0`; leaf profiles `CA:FALSE` with one extended key usage each |

## ADR-0024 dec 3

| Code | Role |
|---|---|
| `pki/sign-csr.sh` | Writes `NAME-chain.crt`, leaf plus intermediate, without the root |
| `gateway/provision_identity.py::cmd_install` | Refuses a single-certificate chain; refuses a non-self-issued anchor |
| `gateway/profile_client.py::pull` | Presents the chain; validates the ERP server certificate against `operator-root.crt` |
| `erp/deploy/mtls/nginx.conf` | `ssl_client_certificate` is the operator root; `ssl_verify_depth 2` walks the intermediate |

## ADR-0024 dec 4

| Code | Role |
|---|---|
| `gateway/provision_identity.py::cmd_install` | Installs `operator-root.crt` beside the identity chain in the gateway `pki` directory |
| `gateway/provision.sh::setup_dirs` | Creates that directory |
| `gateway/profile_client.py::Config.from_env` | Reads the anchor from `IGROW_OPERATOR_ROOT` |

## ADR-0024 dec 5

| Code | Role |
|---|---|
| `pki/ceremony.sh` | Runs key generation under `unshare --user --net`; refuses when the namespace has routes; checks the root key is encrypted |
| `pki/bootstrap-root.sh` | Writes the root key AES-256-encrypted; refuses to overwrite a root |
| `pki/sign-csr.sh` | Refuses to issue on a host holding `operator-root.key` |

## ADR-0024 dec 6

| Code | Role |
|---|---|
| `pki/ceremony.sh` | Refuses identical root and issuing passphrases; reports the ceremony incomplete until custody is done |
| `pki/bootstrap-root.sh` | Prints the custody steps |

Custody itself is physical and has no code.

## ADR-0024 dec 7

Not yet realised.

## ADR-0024 dec 8

| Code | Role |
|---|---|
| `pki/_lib.sh::assert_issuer_outlives` | Refuses an issuance the issuer does not outlive; warns below one further period of margin |
| `pki/issue-intermediate.sh` | Applies the check root over intermediate |
| `pki/sign-csr.sh` | Applies the check intermediate over leaf |
| `pki/ceremony.sh` | Verifies the root outlives the issuing CA |

## ADR-0024 dec 9

| Code | Role |
|---|---|
| `pki/issue-intermediate.sh` | Signs a successor once the current `issuing-ca.*` is moved aside |

## ADR-0024 dec 10

| Code | Role |
|---|---|
| `pki/bootstrap-root.sh` | Generates the successor root into a separate directory |
| `gateway/provision_identity.py::cmd_install` | Installs an anchor over the provisioning path |
| `erp/deploy/mtls/nginx.conf` | `ssl_client_certificate`, the proxy's trust set |

## ADR-0024 dec 11

| Code | Role |
|---|---|
| `pki/ceremony.sh` | The new root and intermediate |
| `pki/sign-csr.sh` | Re-issue of every leaf |
| `gateway/provision_identity.py::cmd_install` | Re-installs chain and anchor on each unit |

## ADR-0024 dec 12

Not yet realised.

## ADR-0024 dec 13

| Code | Role |
|---|---|
| `pki/ceremony.sh` | Root and intermediate generation, outside the ERP image |
| `pki/sign-csr.sh` | Leaf issuance, outside the ERP image |
| `erp/deploy/mtls/make-test-ca.sh` | Disposable two-tier test fixture, not a bootstrap procedure |

## ADR-0024 dec 14

| Code | Role |
|---|---|
| `firmware/tools/mkimage.py::new_key` | Generates the bare P-256 firmware-signing key; refuses an unencrypted key unless `--dev`, which marks it a development key |
| `firmware/tools/mkimage.py::public_key_hex` | Derives the public half the bootloader is built with |
| `firmware/tools/release.sh` | Signs a release with that key and builds the bootloader holding its public half |
| `firmware/boot/verify.c` | Bootloader verification against the compiled-in key |

Links: [pki/README.md](../pki/README.md), [erp/deploy/mtls/README.md](../erp/deploy/mtls/README.md), [gateway/README.md](../gateway/README.md).
