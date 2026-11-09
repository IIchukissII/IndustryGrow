<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ERP store and entities

The manual of [ADR-0021](../ADR/ADR-0021-instance-and-integration-erp-as-the-pre-cloud-system-of-record.md): how the ERP's container, collections and entities realise the pre-cloud system of record.

## ADR-0021 dec 1

One application container and one MongoDB container run under one compose project on the operator's host; the gateway runs neither. The app and Mongo ports bind to localhost; the mTLS overlay puts a terminating proxy in front of the app.

| Code | Role |
|---|---|
| `erp/Dockerfile` | Builds the API and the console into one image |
| `erp/docker-compose.yml` | The `erp` and `mongo` services; `minio` under the `standalone` profile only |
| `erp/docker-compose.mtls.yml` | Adds the mTLS-terminating proxy |

## ADR-0021 dec 2, 3

Collection names carry the `[F]`/`[D]` split as a prefix: `foundation.*` is the part that aligns to `production_unit`, `domain.*` stays the IndustryGrow layer. References run from `domain.*` to `foundation.*` only. Keys follow the ADR-0017 grammar. The stage-11 mapping and export are not implemented.

| Code | Role |
|---|---|
| `erp/app/db.py::FOUNDATION` | The `[F]` collections |
| `erp/app/db.py::DOMAIN` | The `[D]` collections |
| `erp/app/models/foundation.py` | `[F]` entity models |
| `erp/app/models/domain.py` | `[D]` entity models |
| `erp/app/models/identifiers.py` | Formats and parses the identifier keys |

## ADR-0021 dec 4

Machines are registered by their operator-assigned `GBOX_NNNN`. Serials are allocated by one atomic increment on a counter per module and version, the counter carrying the tenant, which also creates the instance rows. An instance row holds the identifier fields and status, and no type meaning.

| Code | Role |
|---|---|
| `erp/app/services/serials.py::allocate` | Gap-free block allocation and instance creation |
| `erp/app/models/identifiers.py::counter_id` | The counter key `Exxxx-VVVVVV` |
| `erp/app/models/foundation.py::ModuleInstance` | The E-instance record |
| `erp/app/api/routes.py::register_machine` | Upserts a machine by identifier |
| `erp/app/db.py::ensure_indexes` | `uq_instance_full_id`: tenant, module, version, serial unique; migrates the counter tenant first |
| `erp/tests/test_serials.py` | Gap-free, per-version and concurrent allocation |

## ADR-0021 dec 5

The binding is one document per instance: certificate serial, public-key fingerprint, validity and the warehouse key of the `-PR` blob. No field holds private material.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::InstanceIdentity` | The binding record |
| `erp/app/api/routes.py::bind_provisioning` | Upserts the binding for an existing instance |

## ADR-0021 dec 6

An integration record holds machine, depth code, instance, install and removal times and removal reason. A null `removed_at` marks the current placement; a partial-unique index allows one current record per machine and depth. Removal closes the record; history is every record for an instance. The depth code is stored only on integration records.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::IntegrationRecord` | The integration record |
| `erp/app/services/integration.py` | Install, remove, replace, current estate, instance history |
| `erp/app/db.py::ensure_indexes` | `uq_current_position` and `ix_instance_history` |
| `erp/tests/test_integration.py` | Install, removal and a serial moving between machines |

## ADR-0021 dec 7

The index row holds instance, document type, date, validity, status and the object key; the blob is written to the warehouse before the row. Calibration expiry is a query over `valid_until`.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::LifecycleDoc` | The index row |
| `erp/app/services/docs.py::record_doc` | Blob first, row second; archives a re-issued key |
| `erp/app/services/docs.py::expiring` | Calibration certificates expiring within a window |
| `erp/app/db.py::ensure_indexes` | `ix_doc_instance` and `ix_doc_expiry` |

## ADR-0021 dec 8

Profile versions are stored per machine under a unique tag. A deployment record marks which tag is active; a partial-unique index allows one active record per machine, and recording a new one closes the previous.

| Code | Role |
|---|---|
| `erp/app/models/domain.py::ProfileVersion` | One stored version |
| `erp/app/models/domain.py::GBoxProfileDeployment` | Which version is or was active |
| `erp/app/services/profiles.py` | Add a version, mark one active, read the active one |
| `erp/app/db.py::ensure_indexes` | `uq_profile_version` and `uq_active_deployment` |

## ADR-0021 dec 9

Stock is one row per SP number with quantity and location. The placement model holds machine, SP number and vendor serial; no route writes `foundation.sp_placement`. The gateway's vendor serial is held in its machine binding (dec 17).

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::SPStock` | Stock row |
| `erp/app/models/foundation.py::SPPlacement` | Placement row |
| `erp/app/api/routes.py::set_sp_stock` | Upserts quantity and location |
| `erp/app/api/routes.py::list_sp_stock` | Lists stock |

## ADR-0021 dec 10

No collection, model or route holds telemetry, flash events, control decisions or an audit trail. The firmware intent has no field for what a node runs.

| Code | Role |
|---|---|
| `erp/app/db.py::FOUNDATION` | The complete `[F]` collection set |
| `erp/app/db.py::DOMAIN` | The complete `[D]` collection set |
| `erp/tests/test_api.py::test_no_deploy_or_push_endpoint` | A telemetry path answers 404 |
| `erp/tests/test_firmware.py::test_there_is_no_route_that_records_what_a_node_runs` | No running-firmware write |

## ADR-0021 dec 11

Type meaning is parsed from `REGISTRY.md` on request and never written to Mongo. Blobs stay in the warehouse; the ERP stores object keys. No model has a SKU, price or supplier field.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::catalog` | Parses the registry, cached by file modification time |
| `erp/app/api/routes.py::catalog` | Serves the parsed registry read-through |
| `erp/tests/test_registry.py::test_catalog_is_read_through_and_never_stored` | Nothing from the registry reaches the store |
| `erp/tests/test_identifiers.py::test_boundary_no_forbidden_fields` | No SKU, price, supplier, telemetry or description field |

## ADR-0021 dec 12

The ERP stores versions and records the active one; the gateway reads the active version over mTLS. No route deploys, pushes or activates on a gateway.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::record_active_profile` | Records the active version; refuses an unsigned one |
| `erp/app/api/routes.py::gateway_pull_active_profile` | The gateway's read of its active version |
| `erp/tests/test_api.py::test_no_deploy_or_push_endpoint` | Deploy and activate paths answer 404 |

## ADR-0021 dec 13

A version is one opaque base64 document holding setpoints and model parameters together, with its signature. It is stored and served byte-for-byte; no route writes part of it.

| Code | Role |
|---|---|
| `erp/app/models/domain.py::ProfileVersion` | `document_b64` and `signature` |
| `erp/app/services/profiles.py::add_version` | Stores the document untouched |

## ADR-0021 dec 14

The application is licensed AGPL-3.0-or-later. The database starts empty; no fixture or seed data ships with it.

| Code | Role |
|---|---|
| `erp/pyproject.toml` | The application licence |

## ADR-0021 dec 15

One application container over MongoDB. Integration history is validity-stamped records with a partial-unique index for the current occupant. The PDF reports render records the API already serves and add no entity; their text-shaping and font libraries are installed in the same image.

| Code | Role |
|---|---|
| `erp/Dockerfile` | The single image, font stack included |
| `erp/app/db.py::Database` | The MongoDB client |
| `erp/app/db.py::ensure_indexes` | The store's invariants as indexes |
| `erp/app/services/reports.py` | Printed representations of existing records |

## ADR-0021 dec 16

Every `[F]` model defaults `tenant_id` to the configured operator UUID, and every `[F]` write sets it. The instance uniqueness index includes the tenant; the allocator matches a counter by its id and writes the tenant when it creates one. Nothing routes, partitions or isolates by tenant.

A counter written without `tenant_id` gets the operator UUID at every ERP start and after every `restore`. The sequence stays one per module and version: the counter id is `_id`. The migration sets only `tenant_id` on counters where it is missing or null; `last_serial` is never written, and a second run changes nothing. The allocator matches on the counter id alone, so a counter without a tenant still continues its sequence.

| Code | Role |
|---|---|
| `erp/app/config.py::Settings.operator_uuid` | The single tenant's identifier |
| `erp/app/models/foundation.py` | `tenant_id` on the `[F]` models |
| `erp/app/db.py::ensure_indexes` | `uq_instance_full_id` keyed on the tenant; the counter migration |
| `erp/app/db.py::migrate_counter_tenant` | Sets the operator UUID on counters without a tenant |
| `erp/app/main.py::lifespan` | Runs `ensure_indexes` at start against a real Mongo |
| `erp/tests/test_serials.py` | Counter tenant, legacy migration, idempotence |

## ADR-0021 dec 17

The machine binding is one document per `GBOX_NNNN`: vendor serial, secure-element serial, public-key fingerprint, certificate serial and validity. The gateway-channel view reports days to expiry.

| Code | Role |
|---|---|
| `erp/app/db.py::FOUNDATION` | `foundation.machine_identity`, keyed by machine |
| `erp/app/api/routes.py::bind_machine_provisioning` | Upserts the binding |
| `erp/app/api/routes.py::gateway_channel` | Binding with `expires_in_days` |

## ADR-0021 dec 18

The intent is one document per machine naming a release root `Exxxx-VVVVVV-F`, who selected it and when. A selection is accepted only when the warehouse holds both slot images; the images are read through from the warehouse.

| Code | Role |
|---|---|
| `erp/app/models/domain.py::FirmwareIntent` | The intent record |
| `erp/app/services/firmware.py::set_intent` | Validates against the warehouse and upserts |
| `erp/app/services/firmware.py::is_release` | Both slot images present |

Links: [erp/README.md](../erp/README.md) (collections and boundaries), [erp/deploy/README.md](../erp/deploy/README.md) (deployment), [erp-api.md](erp-api.md), [erp-backup-and-restore.md](erp-backup-and-restore.md).
