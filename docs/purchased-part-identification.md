<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Purchased-part identifier handling

The manual of [ADR-0019](../ADR/ADR-0019-purchased-part-sp-identification.md): how the ERP parses, validates and reads SP numbers and their document keys.

## ADR-0019 dec 1

The SP pattern sits beside the E pattern in the identifier grammar; both are accepted as a type root. The meaning of each SP number is read from the `## SP numbers` table of the type registry and never stored.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::SP_PART` | The `SP` plus four digits pattern |
| `erp/app/models/identifiers.py::ROOT_RE` | Accepts an E or an SP root |
| `erp/app/services/registry.py::Part` | One registry row: SP number, spec, instance tracking, notes |
| `erp/app/services/registry.py::parse` | Reads the `## SP numbers` table into `Part` rows |
| `erp/app/services/registry.py::check` | Fails on a malformed table, a duplicate SP number or an empty spec |
| `erp/app/api/schemas.py::SPStockRequest` | Rejects a stock write whose `sp_number` is not `SPxxxx` |
| `erp/tests/test_registry.py` | Asserts the registry parse, its canonical form and the catalog read-through |

## ADR-0019 dec 2

An SP key has no version field. Instance tracking is a registry column, read into `Part.instance_tracked`. A tracked purchased unit is keyed by its vendor serial: the gateway binding stores it as `vendor_serial`, and an SP placement row carries one.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::parse_store_key` | Reads a version only after an E root |
| `erp/app/services/registry.py::parse` | Reads the instance-tracked column |
| `erp/app/models/foundation.py::SPPlacement` | Placement row with `vendor_serial` |
| `erp/app/api/routes.py::bind_machine_provisioning` | Stores the gateway's vendor serial in its machine binding |

## ADR-0019 dec 3

The registry check requires the SP table's spec column to be the vendor-free spec. No ERP entity or request carries a manufacturer, SKU or price for an SP part.

| Code | Role |
|---|---|
| `erp/app/services/registry.py::check` | Enforces the SP table header, spec column included |
| `erp/app/models/foundation.py::SPStock` | Stock row: SP number, quantity, location |
| `erp/app/api/routes.py::set_sp_stock` | Writes quantity and location only |

## ADR-0019 dec 4

Not yet realised.

## ADR-0019 dec 5

SP rows carry no depth code: stock is keyed by SP number and placement by machine and vendor serial. The depth axis exists only for E-instances.

| Code | Role |
|---|---|
| `erp/app/models/foundation.py::SPPlacement` | Machine, SP number and vendor serial; no depth field |
| `erp/app/models/foundation.py::IntegrationRecord` | Depth code on E-instance placements only |

## ADR-0019 dec 6, 7

Not yet realised.

## ADR-0019 dec 8

An SP document key is parsed as root, layer letter, slug and extension, with no version. The layer letter is read from the segment after the root, and its label from the registry module.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::parse_store_key` | Root, optional status token, layer, slug |
| `erp/app/models/identifiers.py::StoreKey` | The parsed fields; `prefix` is the root alone for an SP key |
| `erp/app/services/registry.py::store_doc_kind` | Labels a parsed key by its layer |
| `erp/tests/test_identifiers.py` | Asserts the SP key parse and the prefix-plus-tail rebuild |

## ADR-0019 dec 9

An accessory key `<parent-root>-D-<slug>` parses with the parent's root and version, layer `D` and the slug, so the parent's prefix lists it with the parent's other documents.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py::parse_store_key` | Reads the `D` layer and the slug under the parent root |
| `erp/app/api/routes.py::list_store_documents` | Lists repository documents with their parsed fields |
| `erp/tests/test_identifiers.py` | Asserts `SP0004-D-rp5-case-src.zip` parses to root, layer and slug |

Links: [erp/README.md](../erp/README.md) (the type registry check and the catalog route).
