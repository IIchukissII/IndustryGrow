<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Commissioning steps and calibration-trim stores

The manual of [ADR-0028](../ADR/ADR-0028-commissioning-sequence-and-calibration-trim-custody.md): the code behind each commissioning step, the two trim stores and their write paths, and how a node reports a trim.

## ADR-0028 dec 1

Each step has its own tool. No code enforces the order across steps; within step 3 the service tool runs a protocol's steps in order.

| Code | Role |
|---|---|
| `firmware/tools/release.sh` | Step 1: the flashable artifact |
| `gateway/provision_node_id.py` | Step 2: `cmd_write` |
| `service-tool/app/protocol.c` | Step 3: `protocol_record` accepts an outcome only for the step due; `protocol_emit` composes the record |
| `gateway/node_command.py` | Step 4: sends the trim write command |
| `erp/app/api/routes.py` | Step 5: `upload_document` files `-QP`, `-CP` and `-CC` |
| `erp/app/services/docs.py` | Step 5: `record_doc` indexes the filed record |

## ADR-0028 dec 2, 10

The M01 U3 offset is held in the sensor's own EEPROM; the M04 flat-field correction in the module serial EEPROM, after its class-ID byte.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/drivers/scd4x.c` | `scd4x_set_temperature_offset_centi` writes the device, `scd4x_persist_settings` commits it to the device EEPROM |
| `firmware/nodes/m04_plant/flatfield.h` | `FLATFIELD_U2_OFFSET`: the record starts after the class-ID byte and the reserved bytes; record layout |
| `firmware/nodes/m04_plant/flatfield.c` | `flatfield_load` reads and validates; `flatfield_commit_step` writes page by page |
| `firmware/nodes/m04_plant/drivers/m24c64.c` | Module EEPROM transport |
| `firmware/nodes/m04_plant/drivers/m24c64.h` | `M24C64_CLASS_ID_ADDR`: byte 0 is the class ID |
| `firmware/common/carrier/e0001.c` | `e0001_read_class_id` reads byte 0 of the same EEPROM |

## ADR-0028 dec 3

The ERP indexes `-CC` records against the instance and lists those expiring. No code compares a device's value with its `-CC`.

| Code | Role |
|---|---|
| `erp/app/services/docs.py` | `record_doc`, `docs_for`, `expiring` |
| `erp/app/api/routes.py` | `calibration_expiring` |

## ADR-0028 dec 4

The release artifact carries no trim. Both trims arrive over the bus on a running node.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | `m01_command`: `CMD_U3_SET_OFFSET`, parameter in centi-degrees; `service_u3_job` stops the device, writes and persists |
| `firmware/nodes/m04_plant/sensors.c` | `serve_write` accepts `uavcan.file.Write` at `PATH_FLATFIELD` |
| `gateway/node_command.py` | `COMMANDS`: the M01 offset write is `allowed`, outside `--bench` |

## ADR-0028 dec 5

M01's boot log prints the offset and marks the factory default as such. M04 reports an absent, corrupt or foreign field and publishes uncorrected; the summary carries `flat_field`. Neither writes a default.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | `log_population`: factory default or trimmed, without naming the run |
| `firmware/nodes/m01_climate/drivers/scd4x.c` | `scd4x_configure` reads the offset and writes none |
| `firmware/nodes/m04_plant/flatfield.c` | `flatfield_state_str`, `flatfield_applied`, `flatfield_correction` returns zero with no field in force |
| `firmware/nodes/m04_plant/sensors.c` | `publish_summary` sets `flat_field` |

## ADR-0028 dec 6

Not yet realised. Both write paths write whenever commanded; no code compares the device's read-back with the intended value first.

## ADR-0028 dec 7

Not yet realised. No code checks a `-CC`'s instance or validity conditions before a restore.

## ADR-0028 dec 8

The M01 trim lives in the sensor, so a replacement sensor carries its factory default. An M04 field is bound to the imager's device ID and refused for another imager.

| Code | Role |
|---|---|
| `firmware/nodes/m04_plant/flatfield.c` | `validate` compares the record's device ID with the fitted imager's; `flatfield_commit_begin` refuses a foreign record |
| `firmware/nodes/m01_climate/sensors.c` | `log_population` prints the sensor serial beside its offset |

## ADR-0028 dec 9

Not yet realised. No tool sequences decisions 1–7, reads the ERP, or refuses a restore; the steps run through the separate tools of decision 1.

Links: [firmware/README.md](../firmware/README.md#default-subject-id-map), [service-tool/README.md](../service-tool/README.md#interface), [node-identity.md](node-identity.md), store document E0002-000001-M-calibration-protocol.
