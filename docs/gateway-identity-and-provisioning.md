<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Device identity and gateway provisioning

The manual of [ADR-0007](../ADR/ADR-0007-pki-hardware-identity-and-provisioning.md): the provisioning tool, the CA scripts and the ERP mTLS seam that create, certify and read a gateway identity.

## ADR-0007 dec 1

| Code | Role |
|---|---|
| `gateway/provision_identity.py::AteccSigner.genkey` | Generates the P-256 key in secure-element slot 0 |
| `gateway/provision_identity.py::cmd_keygen` | Writes and locks the slot-0 policy, then generates the key |
| `gateway/provision_identity.py::AteccSigner.sign_digest` | Signs on-chip; the private key is never read |

Node secure-element key generation has no code.

## ADR-0007 dec 2

| Code | Role |
|---|---|
| `gateway/provision_identity.py::build_csr` | Builds an ECDSA P-256 CSR with `CN=GBOX_NNNN` |
| `pki/sign-csr.sh` | Issues the X.509 leaf, ECDSA with SHA-256 |
| `pki/openssl.cnf` | Leaf profiles `v3_gateway` (clientAuth) and `v3_server` (serverAuth) |

## ADR-0007 dec 3

| Code | Role |
|---|---|
| `pki/bootstrap-root.sh` | Generates the operator's own root |
| `erp/deploy/mtls/nginx.conf` | The ERP proxy trusts the operator root alone |
| `gateway/profile_client.py::pull` | The gateway validates the ERP against the operator root alone |

## ADR-0007 dec 4

| Code | Role |
|---|---|
| `gateway/provision_identity.py::cmd_csr` | Builds the CSR for issuance and renewal over the same key |
| `gateway/provision_identity.py::cmd_install` | Installs the leaf chain; refuses a leaf whose CN is not the unit's |
| `gateway/profile_client.py::pull` | Presents the client chain on the mTLS channel |
| `erp/app/services/mtls.py::gateway_identity` | Derives the caller from the proxy-verified certificate |
| `erp/app/api/deps.py::gateway_identity` | Applies that check to the gateway routes |

## ADR-0007 dec 5

Not yet realised.

## ADR-0007 dec 6

| Code | Role |
|---|---|
| `gateway/provision_identity.py::cmd_csr` | Emits the CSR; no private key leaves the device |
| `pki/sign-csr.sh` | The operator CA issues the certificate |
| `gateway/provision_identity.py::cmd_binding` | Emits the public binding inputs: machine id, vendor serial, secure-element serial, key fingerprint, certificate envelope |
| `erp/app/api/routes.py::bind_machine_provisioning` | Records the machine binding |

## ADR-0007 dec 7

| Code | Role |
|---|---|
| `pki/sign-csr.sh` | Issues short-lived leaves; the lifetime is the runbook's |
| `gateway/provision_identity.py::cmd_csr` | Renewal CSR over the unchanged key |
| `erp/deploy/mtls/nginx.conf` | No CRL or OCSP configuration |

Automated renewal and the platform-side deny-list have no code.

## ADR-0007 dec 8

| Code | Role |
|---|---|
| `firmware/tools/mkimage.py::new_key` | The firmware-signing key, generated apart from `pki/` and the device key |
| `firmware/boot/verify.c` | The bootloader verification key, compiled in, not the identity key |

## ADR-0007 dec 9

| Code | Role |
|---|---|
| `gateway/provision_identity.py::patch_config_zone` | Applies the slot-0 `SlotConfig` and `KeyConfig` words of store document SP0004-M-atecc-provisioning |
| `gateway/provision_identity.py::cmd_lock_slot` | The optional irreversible slot lock |

The node slot map has no code.

## ADR-0007 dec 10

| Code | Role |
|---|---|
| `gateway/provision_identity.py::public_key_fingerprint` | SHA-256 over the SubjectPublicKeyInfo, the stable key anchor |
| `gateway/provision_identity.py::cmd_binding` | Keys the binding on `GBOX_NNNN` and the fingerprint; the certificate serial and validity are per issuance |
| `erp/app/api/routes.py::bind_machine_provisioning` | Upserts on the machine identifier across re-certification |
| `erp/app/services/mtls.py::gbox_from_dn` | Reads `GBOX_NNNN` from the subject CN |
| `gateway/profile_client.py::machine_identity` | Reads the gateway's own `GBOX_NNNN` from its leaf CN |
| `gateway/provision_identity.py::cmd_install` | Installs a replaceable trust anchor |

The IndustryFlow SAN-URI leaf has no code.

## ADR-0007 dec 11

| Code | Role |
|---|---|
| `gateway/provision.sh::setup_dirs` | Creates the gateway `pki` directory `0750 root:gateway` that holds the software key |
| `gateway/profile_client.py::Config.from_env` | Loads the key file `gateway.key` beside the chain |
| `gateway/provision_identity.py::SoftwareSigner` | Signs a CSR with a file key |

Links: [pki/README.md](../pki/README.md), [gateway/README.md](../gateway/README.md), [erp/deploy/mtls/README.md](../erp/deploy/mtls/README.md).
