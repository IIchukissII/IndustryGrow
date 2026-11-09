<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Type registry consumption

The manual of [ADR-0023](../ADR/ADR-0023-the-type-registry-as-a-machine-readable-interface.md): how the ERP and its console read the type registry, and how CI checks its canonical form.

## ADR-0023 dec 1

The ERP reads the type registry from a configured path, by default `REGISTRY.md` at the repository root. The compose file points it at the read-only repository mount, so a `git pull` changes what the ERP serves; the image carries the same file for running without the mount. No generated or structured copy of the registry exists in the repository.

| Code | Role |
|---|---|
| `erp/app/config.py::Settings.registry_path` | Path of the type registry |
| `erp/app/services/registry.py::registry_path` | Resolves a relative path against `erp/` |
| `erp/docker-compose.yml` | `ERP_REGISTRY_PATH` on the repository mount |
| `erp/Dockerfile` | Copies the type registry into the image |

## ADR-0023 dec 2

The parser takes the `##` sections whose headings begin `## E-numbers` and `## SP numbers`, reads every table row in them, and keeps a row only when its first cell is a backticked bare `E` or `SP` identifier. `Instance-tracked?` is true when the cell begins with `yes`, false otherwise. `Notes` is carried as text.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::_MODULES_HEADING` | `## E-numbers` |
| `erp/app/services/registry.py::_PARTS_HEADING` | `## SP numbers` |
| `erp/app/services/registry.py::_MODULE_COLUMNS` | The E-number column header |
| `erp/app/services/registry.py::_PART_COLUMNS` | The SP-number column header |
| `erp/app/services/registry.py::_E_CELL` | A bare backticked E-number cell |
| `erp/app/services/registry.py::_SP_CELL` | A bare backticked SP-number cell |
| `erp/app/services/registry.py::parse` | The two tables into `Module` and `Part` rows |
| `erp/app/services/registry.py::Catalog` | The parsed registry |
| `erp/tests/test_registry.py::test_sp_instance_tracking_is_read_from_the_column` | `Instance-tracked?` read from the column |

## ADR-0023 dec 3

Sections other than the two registry sections are skipped, so identifiers in the document-layer and withdrawn-artifact tables never enter the catalog.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::_sections` | Splits the document by `##` heading |
| `erp/tests/test_registry.py::test_versioned_identifiers_elsewhere_are_mentions_not_entries` | Mentions stay out of the catalog |

## ADR-0023 dec 4

The catalog is parsed on demand and memoised in process, keyed on the file's path and modification time, so an edited file is re-read without a restart. Nothing writes it to the database or the warehouse. The console fetches the catalog once at start.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::catalog` | The parsed registry, re-read on change |
| `erp/app/services/registry.py::_load` | In-process memo keyed on modification time |
| `erp/app/api/routes.py::catalog` | `GET /api/v1/catalog` |
| `erp/frontend/src/api.ts::setCatalog` | Holds the fetched catalog in the console |
| `erp/tests/test_registry.py::test_catalog_is_read_through_and_never_stored` | No database write |

## ADR-0023 dec 5

An E-number absent from the catalog is shown as itself; an absent SP number has no role. A root's type space (designed or purchased) is taken from its prefix.

| Code | Role |
|---|---|
| `erp/frontend/src/api.ts::moduleDesignation` | Designation, or the E-number itself |
| `erp/frontend/src/api.ts::partRole` | Role, or none |
| `erp/frontend/src/api.ts::rootLabel` | Name from the catalog, space from the prefix |

## ADR-0023 dec 6

The registry tables carry no presentation column. The console derives a module's hue from the E-number's digits, cycling through the stylesheet's leaf palette.

| Code | Role |
|---|---|
| `erp/frontend/src/api.ts::moduleHue` | Hue from the E-number |
| `erp/frontend/src/api.ts::LEAF_HUES` | Size of the palette |

## ADR-0023 dec 7

A workflow on every push to `main` and every pull request runs the registry module as a script, which exits non-zero on any violation. The check fails on a missing section, a section with no table, a changed column header, an empty catalog, a duplicate identifier and an empty designation or role.

A row in a registry section whose first cell is not a bare backticked identifier is skipped by the parser and does not fail the check.

| Code | Role |
|---|---|
| `.github/workflows/registry.yml` | Runs the check in CI |
| `erp/app/services/registry.py::check` | Returns the violations |
| `erp/app/services/registry.py::_check_header` | Section and column-header check |
| `erp/app/services/registry.py::main` | Exit status for CI |
| `erp/tests/test_registry.py::test_check_rejects_a_registry_consumers_could_not_read` | Renamed column and missing section fail |
| `erp/tests/test_registry.py::test_check_rejects_duplicate_identifiers` | Duplicate fails |
| `erp/tests/test_registry.py::test_the_real_registry_conforms` | The tracked registry passes |

Links: [erp/README.md](../erp/README.md) (the registry check and the catalog route), [component, document and instance identifier handling](component-and-instance-identification.md), [ERP API](erp-api.md).
