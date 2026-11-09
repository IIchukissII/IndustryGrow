<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Component, document and instance identifier handling

The manual of [ADR-0017](../ADR/ADR-0017-component-document-and-instance-identification.md): how the identifier grammar, the document store, the ERP and the firmware release tooling realise type, instance and position identification.

## ADR-0017 dec 1

The grammar is one module of field patterns and anchored regular expressions; every other module imports it. Instance and integration identifiers are formatted and validated there, versions and depth codes decoded there. The ERP issues serials up to six digits and refuses past the last one rather than carrying into the version.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::E_MODULE` | `E` plus four digits |
| `erp/app/models/identifiers.py::VERSION` | Six-digit version code |
| `erp/app/models/identifiers.py::SERIAL` | Six-digit serial |
| `erp/app/models/identifiers.py::DEPTH` | Six-digit depth code |
| `erp/app/models/identifiers.py::INSTANCE_RE` | `Exxxx-VVVVVV-NNNNNN` |
| `erp/app/models/identifiers.py::INTEGRATION_RE` | `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` |
| `erp/app/models/identifiers.py::instance_id` | Formats and validates an instance identifier |
| `erp/app/models/identifiers.py::integration_id` | Formats and validates an integration identifier |
| `erp/app/models/identifiers.py::decode_version` | `VVVVVV` to major, minor, patch |
| `erp/app/models/identifiers.py::decode_depth` | `DDDDDD` to three levels |
| `erp/app/services/serials.py::allocate` | Refuses a serial past `999999` |
| `erp/app/api/routes.py::_integration_out` | Returns both axes of an installation, each read into its own fields |
| `erp/tests/test_identifiers.py` | Grammar round trips and rejections |

### Fields as the ERP returns them

| Field | Returned as |
|---|---|
| `VVVVVV` | `version` and `version_label` (`v2.1.3`) |
| `DDDDDD` | `depth_code`, `depth_levels` and `depth_label` (`01.02.00`) |
| Lifecycle key | `suffix` and `stamp` beside the instance fields |

## ADR-0017 dec 2

The ERP indexes lifecycle documents and holds their blobs in the warehouse. It has no firmware-event, telemetry or control-audit collection, and a test asserts no instance field carries telemetry.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::LifecycleDoc` | Index row: instance, suffix, warehouse key, validity |
| `erp/app/services/docs.py::record_doc` | Writes the blob, then the index row |
| `erp/tests/test_identifiers.py::test_boundary_no_forbidden_fields` | No telemetry or type-meaning field on an instance |

## ADR-0017 dec 3

An `E` or `SP` root is accepted as a type root. What an E-number designates is read from the `## E-numbers` table of the type registry; the ERP holds no designation.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::ROOT_RE` | An E or an SP root |
| `erp/app/services/registry.py::Module` | One registry row: E-number, designation, discipline, notes |
| `erp/app/api/routes.py::catalog` | Serves the registry's designations read-through |

## ADR-0017 dec 4

The assembly E-numbers over one bare design are entries of the type registry. The ERP treats each assembly E-number as its own root: it keeps one serial counter per E-number and version, so each populated configuration has its own serial sequence.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::counter_id` | The counter key `Exxxx-VVVVVV` |
| `erp/app/models/foundation.py::SerialCounter` | One gap-free sequence per E-number and version |
| `erp/tests/test_serials.py::test_allocate_per_module_version_independent` | Two roots count independently |

## ADR-0017 dec 5

Discipline is the third column of the registry's E-number table and is read into the parsed row. The E-number pattern carries no discipline field and the range is not partitioned.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::_MODULE_COLUMNS` | The column header with `Discipline` |
| `erp/app/services/registry.py::Module` | `discipline` as read from the row |

## ADR-0017 dec 6

The machine pattern is `<uppercase prefix>_NNNN`. A machine is enrolled under an operator-assigned identifier; the routes that create machine-scoped records refuse an identifier that is not of that form, and the gateway certificate's common name is checked against it.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::MACHINE` | `[A-Z]+_\d{4}` |
| `erp/app/models/identifiers.py::MACHINE_RE` | The anchored machine pattern |
| `erp/app/models/foundation.py::Machine` | One enrolled machine |
| `erp/app/api/routes.py::register_machine` | Enrols a machine; refuses a malformed identifier |
| `erp/app/services/mtls.py::gbox_from_dn` | Machine identifier from the client certificate |

## ADR-0017 dec 7

The depth code exists only on the integration record; the instance has no position field. A position is set by machine and depth path segments, and moving an instance closes one record and opens another.

The position route stores the machine and depth segments as given: it checks neither against the machine and depth patterns, nor that the machine is enrolled.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::IntegrationRecord` | Machine, depth code, instance, validity interval |
| `erp/app/models/foundation.py::ModuleInstance` | Instance row with no depth field |
| `erp/app/services/integration.py::install` | Opens a placement; one current instance per position |
| `erp/app/services/integration.py::replace` | Closes the current placement and opens the next |
| `erp/app/api/routes.py::set_position` | `PUT /machines/{gbox}/positions/{depth}` |
| `erp/tests/test_integration.py::test_mobility_serial_moves_and_history_follows` | One serial across positions with its history |

## ADR-0017 dec 8

Serials are issued only by the ERP's atomic counter. The provisioning binding stores the certificate serial, public-key fingerprint, validity and the `-PR` key against the instance identifier.

| Code | Role |
|---|---|
| `erp/app/services/serials.py::allocate` | Atomic, gap-free serial issue |
| `erp/app/models/foundation.py::InstanceIdentity` | Serial to secure-element binding, public material only |
| `erp/app/api/routes.py::bind_provisioning` | Writes the binding |
| `erp/tests/test_serials.py::test_allocate_concurrent_no_duplicates` | Concurrent issue never collides |

## ADR-0017 dec 9

The layer letters are a fixed set in the grammar; a store key's layer is read from the segment after the version. A label for each letter comes from a table in the registry module.

That table labels `P` "procedure" and `I` "instruction", where this decision names them Protocol and Interface; it labels `D` "design" and `S` "schematic".

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::DOCUMENT_LAYERS` | `S D L P M I F R` |
| `erp/app/models/identifiers.py::parse_store_key` | Reads the layer by position |
| `erp/app/services/registry.py::_DOC_LAYERS` | Layer letter to label |
| `erp/app/services/registry.py::layer_label` | The label of one letter |
| `erp/tests/test_identifiers.py::test_store_key_fields_are_read_by_position_not_by_search` | `D` after the version is a layer, later a slug word |

## ADR-0017 dec 10, 11, 12

The ERP accepts exactly the five lifecycle suffixes and refuses type-layer uploads. `-QP`, `-QR` and `-PR` take the bare key. `-CP` and `-CC` take `-YYYYMMDDThhmmss`, the day from `doc_date` when given and the time from the upload, then `-n` while the key is occupied. The grammar still reads a day-only stamp. `-CC` validity is indexed and queried for expiry.

The `-PR` binding route requires the `provisioning` role. No role is named for engineering or quality: a `-PR` blob is read with any token role, `readonly` included.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::LIFECYCLE_SUFFIXES` | `QP QR CP CC PR` |
| `erp/app/models/identifiers.py::LIFECYCLE_KEY_RE` | Instance, suffix, optional stamp, sequence and archive tail |
| `erp/app/models/foundation.py::DocType` | The five suffixes as index values |
| `erp/app/api/routes.py::ALLOWED_DOC_TYPES` | The upload allowlist |
| `erp/app/api/routes.py::upload_document` | Stamps `-CP` and `-CC`, sequences on collision |
| `erp/app/services/docs.py::expiring` | `-CC` rows by validity |
| `erp/app/api/routes.py::bind_provisioning` | `-PR` binding, `provisioning` role |
| `erp/app/api/routes.py::document_content` | Reads an indexed blob with any role |
| `erp/tests/test_api.py::test_document_allowlist_rejects_type_layer` | Type-layer upload refused |
| `erp/tests/test_api.py::test_two_calibrations_on_one_day_do_not_overwrite` | Two same-day calibrations keep two keys |

## ADR-0017 dec 13

Lifecycle keys are anchored on the instance identifier, and uploads are addressed by instance. An integration record carries no suffix; a reinstalled instance keeps its documents under its serial.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::LIFECYCLE_KEY_RE` | Requires an instance identifier before the suffix |
| `erp/app/api/routes.py::upload_document` | `POST /instances/{instance_id}/documents` |
| `erp/app/services/docs.py::docs_for` | An instance's documents by serial, whatever its position |

## ADR-0017 dec 14

Not yet realised.

## ADR-0017 dec 15

The warehouse is an S3-compatible bucket with a flat keyspace. The repository's `store/` and `spec/` files are uploaded under their file names as keys; lifecycle objects are written by the ERP. Listings are server-side prefix listings.

`store/` also holds EDA library files that carry no identifier; the key parser returns them with no root, and a footprint library is uploaded under a key that contains its directory name.

`store_sync --prune` deletes every warehouse object absent from `store/` and `spec/`, lifecycle objects and their archive keys included.

| Code | Role |
|---|---|
| `erp/app/services/warehouse.py::Warehouse` | The bucket client |
| `erp/app/services/warehouse.py::Warehouse.list_prefix` | Server-side prefix listing |
| `erp/app/store_sync.py::sync` | Uploads `store/` and `spec/` by file name; optional prune |
| `erp/app/models/identifiers.py::parse_store_key` | Reads a type-layer key into its fields; unrecognised keys keep no root |
| `erp/app/models/identifiers.py::StoreKey` | Parsed fields with `prefix` and `tail` |
| `erp/app/api/routes.py::list_store_documents` | Repository documents with parsed keys |
| `erp/tests/test_store_sync.py::test_sync_uploads_by_identifier_key` | Key equals file name |
| `erp/tests/test_identifiers.py::test_store_key_reports_what_it_cannot_read` | An unidentified key returns no root |

## ADR-0017 dec 16

The release script builds the node images and writes them to `store/` as `E0001-<firmware version>-F-<variant>`: bootloader, one image per slot with and without load address, and a source snapshot. The firmware version is a constant in the script, independent of the carrier board version. `REUSE.toml` licenses those keys AGPL-3.0-or-later over the store's hardware default. One personality table in the node firmware selects behaviour by class ID. The ERP names a release by its `Exxxx-VVVVVV-F` root.

| Code | Role |
|---|---|
| `firmware/tools/release.sh` | Builds and writes the `F` artifact set |
| `REUSE.toml` | AGPL-3.0-or-later for `F` keys |
| `firmware/nodes/registry.c` | Class ID to personality |
| `erp/app/services/firmware.py::RELEASE_RE` | The `Exxxx-VVVVVV-F` release root |
| `erp/app/services/firmware.py::available_releases` | Releases with both slot images in the warehouse |

## ADR-0017 dec 17

A status token sits where a layer letter would; the parser reads it into `status` and never as a layer. The repository procedure for withdrawing a set is in `CONTRIBUTING.md`.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::STATUS_TOKENS` | `BLOCKED SUPERSEDED` |
| `erp/app/models/identifiers.py::parse_store_key` | Status token before the layer slot |
| `erp/app/services/registry.py::store_doc_kind` | Labels a withdrawn archive |
| `erp/app/api/routes.py::_store_doc` | Marks a withdrawn archive as packaged |
| `erp/tests/test_identifiers.py::test_store_key_status_token_is_not_a_layer` | A token is never a layer |

## ADR-0017 dec 18

`-D-fab` is parsed as layer `D`, slug `fab`, and labelled a fabrication package. The member naming of the package is in `CONTRIBUTING.md`.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::store_doc_kind` | Labels `D` with slug `fab` |
| `erp/app/api/routes.py::_store_doc` | Marks the package as packaged |
| `erp/tests/test_store_sync.py::test_prune_deletes_stale_but_keeps_current` | A bundled set replaces its loose files in the bucket |

## ADR-0017 dec 19

`-S-src` and `-D-src` are parsed as layer `S` or `D` with slug `src`. The package members and the commands that build them are in `CONTRIBUTING.md`.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::parse_store_key` | Layer and slug of a source package |
| `erp/app/services/registry.py::store_doc_kind` | Labels a source package by its layer |

## ADR-0017 dec 20

Specifications are kept in `spec/` and uploaded into the same keyspace as `store/`; the directory is not part of the key. An E root without a version parses with layer `R`.

| Code | Role |
|---|---|
| `erp/app/store_sync.py::sync` | Uploads `spec/` beside `store/` |
| `erp/app/config.py::Settings.spec_dir` | The specification directory |
| `erp/app/models/identifiers.py::parse_store_key` | Version only when present; `R` layer |
| `erp/tests/test_api.py::test_a_specification_is_a_store_document` | A specification listed and read as a store document |

## ADR-0017 dec 21

The type registry and `store/` are tracked in the repository. Serials, bindings, lifecycle index rows and integration records live in the ERP's database, each row stamped with the operator, and lifecycle blobs in the warehouse; every read of them requires a token.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::ModuleInstance` | Instance row, operator-stamped |
| `erp/app/models/foundation.py::InstanceIdentity` | Secure-element binding |
| `erp/app/models/foundation.py::IntegrationRecord` | Integration record |
| `erp/app/api/deps.py::require_read` | Token required for every read |
| `erp/tests/test_api.py::test_a_repository_document_does_not_print_as_operator_private` | Repository documents are not marked operator-private |

## ADR-0017 dec 22

On a write to an occupied key, the previous blob is copied to `<key>-superseded-YYYYMMDDThhmmss`, `-n` appended while that key is taken, its index rows repointed there and marked superseded; then the new blob takes the stable key. Backup selects lifecycle objects by the same grammar, archive keys included.

| Code | Role |
|---|---|
| `erp/app/services/docs.py::record_doc` | Archives the previous issue, then writes the new one |
| `erp/app/models/identifiers.py::LIFECYCLE_KEY_RE` | Accepts the `-superseded-` tail |
| `erp/app/backup.py::is_backed_up_object` | Selects lifecycle and archive keys |
| `erp/tests/test_api.py::test_reissuing_a_qp_archives_the_first_issue` | First issue resolves under its archive key |
| `erp/tests/test_backup.py::test_sequenced_and_superseded_keys_are_backed_up` | Archive keys are backed up |

Links: [CONTRIBUTING.md](../CONTRIBUTING.md) (working with the document store), [erp/README.md](../erp/README.md), [ERP API](erp-api.md), [purchased-part identifier handling](purchased-part-identification.md), [ERP backup and restore](erp-backup-and-restore.md), [type registry](type-registry.md).
