<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Node identity in the node firmware

The manual of [ADR-0027](../ADR/ADR-0027-node-identity-model.md): the carrier Node-ID store, the provisioning write and its tool, the unprovisioned and unidentified states, and the source of the Cyphal `unique_id`.

## ADR-0027 dec 1

The class ID and the Node-ID are read by separate code; the class ID selects only the personality.

| Code | Role |
|---|---|
| `firmware/common/node/main.c` | Reads the class ID, then the Node-ID, and passes each to its own consumer |
| `firmware/nodes/registry.c` | `node_for_module_id`: class ID to personality |
| `firmware/common/carrier/identity.c` | `identity_node_id`: the Node-ID, from the carrier store alone |

## ADR-0027 dec 2, 11

One record — magic, version, Node-ID, CRC-32 — in one of two stores. The build selects the store by carrier revision: `IGROW_CARRIER_REV` `000003` uses the last flash sector, `000100` the carrier EEPROM. An image has one store, never both.

| Code | Role |
|---|---|
| `firmware/common/carrier/identity.c` | `identity_record_t`; `validate`; `identity_init` reads the store once at boot |
| `firmware/common/carrier/identity.h` | `IGROW_IDENTITY_FLASH_ADDR`, `IGROW_IDENTITY_FLASH_SECTOR`, `IGROW_IDENTITY_EEPROM_ADDR`, `IGROW_IDENTITY_EEPROM_WORD` |
| `firmware/ldscripts/STM32F405RGTx_APP.ld.in` | Reserves the identity sector; `_identity_start`, checked against `identity.h` at init |
| `firmware/ldscripts/STM32F405RGTx_BOOT.ld` | Reserves the same sector for the bootloader |
| `firmware/common/platform/crc32.c` | `crc32_mpeg2_words` over the record body |
| `firmware/common/drivers/i2c.c` | Transport to the carrier EEPROM |
| `firmware/CMakeLists.txt` | `IGROW_CARRIER_REV` sets `IGROW_CARRIER_000100` |

## ADR-0027 dec 3

Both stores are on the carrier; nothing on the module holds the Node-ID.

| Code | Role |
|---|---|
| `firmware/common/carrier/identity.c` | Reads the MCU flash sector or the carrier EEPROM, never a module device |

## ADR-0027 dec 4

| Code | Role |
|---|---|
| `firmware/common/platform/partition.h` | The identity sector lies outside every slot and every update write region |
| `firmware/boot/update.c` | `erase_slot` erases slot sectors only |

## ADR-0027 dec 5

A `uavcan.node.id` write of 0–126 commits a record, 127 clears the store, a higher value is ignored. The register then reads what the store holds. The running Node-ID is latched at boot.

| Code | Role |
|---|---|
| `firmware/common/cyphal/registers.c` | `registers_access`: the special case for `REG_IDX_NODE_ID`; `uavcan.node.id` is mutable and persistent |
| `firmware/common/carrier/identity.c` | `identity_commit` writes and reads back; `identity_commit_pending` |
| `firmware/common/cyphal/cyphal.c` | `cyphal_spin` repeats a diagnostic naming the committed Node-ID while it differs from the running one |
| `gateway/provision_node_id.py` | `cmd_write` surveys the bus, refuses a Node-ID already in use, writes, and restarts on `--restart` |

## ADR-0027 dec 6, 10

Telemetry starts only with a provisioned Node-ID and a resolved personality. The skeleton runs in every case.

| Code | Role |
|---|---|
| `firmware/common/carrier/identity.h` | `IGROW_NODE_ID_UNPROVISIONED`, `IGROW_NODE_ID_MAX_PROVISIONABLE` |
| `firmware/common/node/main.c` | `publish` gates the personality's `init` and `spin` |
| `firmware/common/cyphal/cyphal.c` | `cyphal_report_identity` publishes the missing-provisioning diagnostic; `publish_heartbeat` reports ADVISORY |
| `firmware/common/node/node.h` | `IGROW_NODE_NAME_UNIDENTIFIED`: the GetInfo name of an unresolved personality |
| `gateway/provision_node_id.py` | `uptimes_look_like_two_nodes` flags two nodes at 127 in a survey |

## ADR-0027 dec 7

The gateway allocates no Node-ID; an operator chooses each value through the provisioning tool. No ERP record holds a cabinet's Node-ID map.

| Code | Role |
|---|---|
| `gateway/provision_node_id.py` | `cmd_write`: the operator's write; runs only when invoked |

## ADR-0027 dec 8

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | `read_unique_id`: secure-element serial left-justified and zero-padded, else the MCU factory UID; `publish_heartbeat` reports ADVISORY while unanchored |
| `firmware/common/carrier/atecc608.c` | `atecc608_present`, `atecc608_serial` |
| `erp/app/models/foundation.py` | `InstanceIdentity`: the `-PR` binding of a serial to its secure element |

## ADR-0027 dec 9

| Code | Role |
|---|---|
| `firmware/common/carrier/e0001.c` | Module class ID: `e0001_read_class_id` from the module EEPROM, else `e0001_read_module_id` from the strap |
| `firmware/common/carrier/identity.c` | Node-ID: the carrier store |
| `firmware/common/cyphal/cyphal.c` | `unique_id`: `read_unique_id` |
| `firmware/common/carrier/atecc608.c` | Reads only the secure-element serial; the keypair and certificate are not touched by node firmware |
| `erp/app/models/foundation.py` | Serial / E-number: `ModuleInstance`, and `InstanceIdentity` binding it to the secure element |

Links: [firmware/README.md](../firmware/README.md#identity-resolution), [firmware/README.md provisioning](../firmware/README.md#provisioning-a-node-id), [node-firmware-update.md](node-firmware-update.md), carrier specification E0001-R.
