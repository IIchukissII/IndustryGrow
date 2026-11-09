<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ERP API

The manual of [ADR-0022](../ADR/ADR-0022-machine-and-operator-facing-api-of-the-instance-and-integration-erp.md): how the ERP's `/api/v1` routes, authentication and response shapes realise the machine- and operator-facing API.

## ADR-0022 dec 1

One router under `/api/v1`, served by the ERP container; FastAPI generates the OpenAPI document at `/docs`. Its resources are instances, machines and positions, lifecycle documents, profiles, firmware intent, machine bindings and SP stock. Two reads are of repository-owned content and store nothing: the parsed type registry, and the repository's `store/` and `spec/` documents, as listing, time-limited URL, read-through or PDF.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::router` | Every API route |
| `erp/app/main.py::create_app` | Mounts the router and the console in one app |
| `erp/app/api/routes.py::catalog` | Type registry read-through |
| `erp/app/api/routes.py::list_store_documents` | Repository documents with parsed keys |
| `erp/app/api/routes.py::store_document_content` | A repository document read through from the warehouse |
| `erp/app/api/routes.py::_is_store_file` | Admits only keys naming a file under `store/` or `spec/` |

## ADR-0022 dec 2

A reverse proxy terminates TLS, verifies the client certificate against the operator root, strips inbound copies of its headers and forwards the verification result and the subject DN. The app believes those headers only from a configured trusted-proxy address, requires a successful verification, and parses `GBOX_NNNN` from the DN. With no trusted proxy configured the gateway routes answer 503. Uvicorn runs without proxy headers, so the peer address is the transport peer.

| Code | Role |
|---|---|
| `erp/app/services/mtls.py::gateway_identity` | Peer check, verification check, identity from the DN |
| `erp/app/services/mtls.py::gbox_from_dn` | Extracts and validates the machine identifier |
| `erp/app/api/deps.py::gateway_identity` | Maps the result to 401 or 503 |
| `erp/deploy/mtls/nginx.conf` | The terminating proxy and its header contract |
| `erp/docker-compose.mtls.yml` | The proxy overlay |
| `erp/tests/test_mtls.py` | DN parsing, forged headers, untrusted peers, unconfigured channel |

## ADR-0022 dec 3

`Authorization: Bearer <token>` or `X-Operator-Token` maps a token to a role from configuration. Roles are `provisioning`, `operator` and `readonly`; write routes require `operator` or `provisioning`, reads accept all three. With no tokens configured the app accepts one development token.

| Code | Role |
|---|---|
| `erp/app/config.py::Settings.api_tokens` | Token-to-role map |
| `erp/app/api/deps.py::require_role` | Token lookup and role check |
| `erp/app/api/deps.py::require_read` | Any role |
| `erp/app/api/deps.py::require_write` | `operator` or `provisioning` |
| `erp/app/api/deps.py::require_provisioning` | Serial allocation and bindings |

## ADR-0022 dec 4

The allocation request names module, version and quantity; the response returns the issued identifiers and the next serial. The client never supplies a serial.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::allocate_serials` | `POST /instances` |
| `erp/app/services/serials.py::allocate` | Atomic increment and instance creation |
| `erp/app/api/schemas.py::AllocateRequest` | Module, version, quantity |
| `erp/tests/test_api.py::test_allocate_is_authoritative_and_gap_free` | Server-issued, gap-free serials |

## ADR-0022 dec 5

The request carries certificate serial, fingerprint, validity and the `-PR` object key; it has no private-key field. The `-PR` blob is uploaded through the document route (dec 7).

| Code | Role |
|---|---|
| `erp/app/api/routes.py::bind_provisioning` | `POST /instances/{id}/provisioning` |
| `erp/app/api/schemas.py::ProvisionRequest` | The public fields |

## ADR-0022 dec 6

`PUT /machines/{gbox}/positions/{depth}` installs, or replaces the current occupant; `DELETE` removes with a reason. A move is a removal at one position and an install at another. The depth code is stored only on the integration record; responses return machine, depth and instance as fields, never a composed integration identifier.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::set_position` | Install or replace |
| `erp/app/api/routes.py::clear_position` | Remove |
| `erp/app/services/integration.py` | Record writes and history reads |
| `erp/tests/test_api.py::test_install_move_remove` | Install, remove, history kept |

## ADR-0022 dec 7

The upload route accepts only `QP`, `QR`, `CP`, `CC` and `PR` against an existing E-instance, and rejects every other type with 422. `CP` and `CC` keys are stamped per upload; a re-issued `QP`, `QR` or `PR` archives the previous blob first. The blob is written, then the row is indexed. A recorded document is returned as its key, a presigned URL, the bytes read through, or a PDF rendering; only indexed keys are served.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::ALLOWED_DOC_TYPES` | The suffix allowlist |
| `erp/app/api/routes.py::upload_document` | Allowlist, key stamping, upload |
| `erp/app/services/docs.py::record_doc` | Blob first, row second |
| `erp/app/api/routes.py::document_url` | Presigned URL for an indexed key |
| `erp/app/api/routes.py::document_content` | Read-through for an indexed key |
| `erp/app/api/routes.py::_read_through` | Streams an object without keeping it |
| `erp/tests/test_api.py::test_failed_blob_write_leaves_no_index_row` | No row without its blob |
| `erp/tests/test_api.py::test_document_allowlist_rejects_type_layer` | Type-layer documents rejected |

## ADR-0022 dec 8

Versions are stored whole and immutable by tag; recording one active is a record write that refuses an unsigned version. The gateway's pull returns the active version's bytes and signature and writes nothing.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::store_profile_version` | `POST /machines/{gbox}/profiles`; 409 on a reused tag |
| `erp/app/api/routes.py::record_active_profile` | `PUT /machines/{gbox}/active-profile` |
| `erp/app/api/routes.py::gateway_pull_active_profile` | `GET /gateway/active-profile`, read only |
| `erp/tests/test_api.py::test_the_gateway_channel_never_reports_a_pull` | No pull record |
| `erp/tests/test_api.py::test_no_deploy_or_push_endpoint` | No deploy, push or activate route |

## ADR-0022 dec 9

No route accepts telemetry, operational events, flash events or audit data; such paths answer 404. Module and part numbers are stored as identifiers only. Stock writes take quantity and location; the machine binding keys a purchased gateway by its vendor serial.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::router` | The complete route set |
| `erp/app/api/schemas.py::SPStockRequest` | SP number, quantity, location |
| `erp/tests/test_api.py::test_no_deploy_or_push_endpoint` | Telemetry path answers 404 |
| `erp/tests/test_firmware.py::test_there_is_no_route_that_records_what_a_node_runs` | No firmware report route |

## ADR-0022 dec 10

Requests carry no tenant field. The tenant is the configured operator UUID, written on every `[F]` record.

| Code | Role |
|---|---|
| `erp/app/config.py::Settings.operator_uuid` | The implicit tenant |

## ADR-0022 dec 11

Not yet realised.

## ADR-0022 dec 12

`POST /machines/{gbox}/provisioning` upserts one binding per machine identifier: vendor serial, optional secure-element serial, fingerprint, certificate serial and validity. `GET /machines/{gbox}/gateway-channel` returns it with days to expiry.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::bind_machine_provisioning` | Upsert, latest wins |
| `erp/app/api/schemas.py::MachineProvisionRequest` | The binding fields |
| `erp/app/api/routes.py::gateway_channel` | The binding and profile state for one machine |
| `erp/tests/test_api.py::test_the_binding_is_upserted_so_renewal_replaces_it` | Renewal replaces the binding |

## ADR-0022 dec 13

Responses carry the parsed fields beside the identifier: module, version, decoded version label and serial for an instance; depth levels and label for a position; root, version, layer and slug for a repository document; version for a firmware release. `LifecycleDocOut` and `AllocateResponse` return identifiers without their parts.

| Code | Role |
|---|---|
| `erp/app/models/identifiers.py` | The one grammar parser |
| `erp/app/api/routes.py::_instance_out` | Instance fields |
| `erp/app/api/routes.py::_integration_out` | Both axes as fields |
| `erp/app/api/routes.py::_store_doc` | Document key fields |
| `erp/app/api/routes.py::_firmware_intent_out` | Release version fields |
| `erp/tests/test_api.py::test_both_axes_arrive_read_into_fields` | Depth and version decoded |

## ADR-0022 dec 14

The operator writes a release root per machine; it is accepted when the warehouse holds both slot images and is upserted. Releases are listed from the warehouse. The gateway reads its machine's intent and fetches only that release's slot images, read through. No route updates, flashes or accepts a gateway report.

| Code | Role |
|---|---|
| `erp/app/api/routes.py::set_firmware_intent` | `PUT /machines/{gbox}/firmware` |
| `erp/app/api/routes.py::list_firmware_releases` | Releases the warehouse can serve |
| `erp/app/api/routes.py::gateway_pull_firmware_intent` | `GET /gateway/firmware` |
| `erp/app/api/routes.py::gateway_pull_firmware_artifact` | Slot image of the intended release only |
| `erp/app/services/firmware.py` | Release validation, artifact keys, intent storage |
| `erp/tests/test_firmware.py` | Selection, scoping, gateway reads, absent report route |

Links: [erp/README.md](../erp/README.md) (callers and absences), [erp/deploy/README.md](../erp/deploy/README.md) (mTLS overlay and operator tokens), [erp/deploy/mtls/README.md](../erp/deploy/mtls/README.md) (proxy header contract), [erp-system-of-record.md](erp-system-of-record.md).
