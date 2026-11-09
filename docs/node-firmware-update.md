<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Node firmware update and bootloader

The manual of [ADR-0029](../ADR/ADR-0029-node-firmware-update-and-bootloader.md): the code that partitions node flash, runs the bootloader, signs and verifies images, and drives an update from the ERP intent through the gateway.

## ADR-0029 dec 1

| Code | Role |
|---|---|
| `firmware/common/platform/partition.h` | Region addresses, sector ranges and slot size; `partition_slot_of` names the slot an address lies in |
| `firmware/ldscripts/STM32F405RGTx_BOOT.ld` | Links the bootloader to its own sectors |
| `firmware/ldscripts/STM32F405RGTx_APP.ld.in` | Template the build instantiates once per slot base |
| `firmware/CMakeLists.txt` | Builds one application per slot and passes the slot map to the sources |
| `firmware/common/node/main.c` | Sets `SCB->VTOR` to the image's own vector table first |

## ADR-0029 dec 2, 8

| Code | Role |
|---|---|
| `firmware/common/platform/update_state.h` | `update_state_t`: boot slot, previous slot, trial state, attempt counter, pending request |
| `firmware/common/platform/update_state.c` | Loads and appends update-state records in their sector |
| `firmware/boot/main.c` | Spends one trial attempt before the jump, reverts an exhausted trial, falls back to the other slot when the selected one fails its check |
| `firmware/common/platform/image.c` | `image_slot_bootable`: target class and CRC-32 over the slot body on each boot |
| `firmware/common/platform/crc32.c` | CRC-32/MPEG-2 the boot check and the header use |

## ADR-0029 dec 3

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | `handle_execcmd` records the begin-software-update request through `update_state_request` and restarts; it writes no slot |
| `firmware/boot/update.c` | `update_run_request`: the only path that erases and programs a slot |
| `firmware/common/platform/flash.c` | Sector erase and word program, run from RAM |

## ADR-0029 dec 4

| Code | Role |
|---|---|
| `firmware/boot/update.c` | `bus_up` starts the Cyphal skeleton under `identity_node_id`; `read_block` is the file-read client |
| `firmware/common/cyphal/cyphal.c` | Heartbeat, node info and transport shared by bootloader and application |
| `firmware/common/carrier/identity.c` | Node-ID store, or the unprovisioned Node-ID |

## ADR-0029 dec 5

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | Serves `uavcan.node.ExecuteCommand` with `COMMAND_BEGIN_SOFTWARE_UPDATE`; the parameter is the artifact path |
| `firmware/boot/update.c` | Issues `uavcan.file.Read` requests to the node that sent the command |
| `gateway/firmware_client.py` | `Bus.begin_update` sends the command; `Bus.serve_file` serves the file |

## ADR-0029 dec 6

| Code | Role |
|---|---|
| `firmware/boot/verify.c` | `image_verify_slot`: header check, SHA-256 of the body against the header digest, ECDSA P-256 over the SHA-256 of the signed header span |
| `firmware/boot/verify_key.h.in` | Template that places the public verification key in the bootloader |
| `firmware/common/platform/sha256.c` | SHA-256 and its self-test |
| `firmware/tools/mkimage.py` | Signs the header with the private key it is given |
| `firmware/boot/update.c` | Marks the slot bootable only after `IMAGE_VERIFY_OK`; otherwise abandons and leaves the previous slot |

## ADR-0029 dec 7

| Code | Role |
|---|---|
| `firmware/common/platform/image.h` | `igrow_image_header_t`: byte layout of the header |
| `firmware/tools/mkimage.py` | The header's only writer |
| `firmware/boot/verify.c` | Refuses a header whose hardware class is not this node's |

## ADR-0029 dec 9

| Code | Role |
|---|---|
| `firmware/common/platform/partition.h` | No write region includes the identity sector |
| `firmware/common/carrier/identity.h` | The identity sector address, outside both slots |

## ADR-0029 dec 10

| Code | Role |
|---|---|
| `firmware/boot/update.c` | `erase_slot` erases slot sectors only |
| `firmware/tools/release.sh` | Releases the bootloader as a flashable artifact only |

## ADR-0029 dec 11

| Code | Role |
|---|---|
| `firmware/boot/update.c` | `abandon` publishes the failure reason; a verified image publishes its version and slot as a notice |
| `firmware/common/cyphal/cyphal.c` | `cyphal_diagnostic` publishes `uavcan.diagnostic.Record`; `handle_getinfo` reports the running image's version and CRC |

## ADR-0029 dec 12

| Code | Role |
|---|---|
| `firmware/common/node/main.c` | Calls `update_state_confirm` once Node-ID, personality and Cyphal node are up |
| `firmware/common/platform/update_state.c` | `update_state_confirm` closes an open trial |

## ADR-0029 dec 13

| Code | Role |
|---|---|
| `firmware/tools/release.sh` | Writes the flashable and bus artifacts per slot into `store/` |
| `firmware/tools/mkimage.py` | Produces header plus body |

## ADR-0029 dec 14

| Code | Role |
|---|---|
| `gateway/firmware_client.py` | `fetch_artifacts` downloads the intended release's artifacts into `ARTIFACT_DIR` |
| `erp/app/api/routes.py` | `gateway_pull_firmware_artifact` serves an artifact's bytes to the gateway |
| `erp/app/services/firmware.py` | `artifact_keys` names a release's artifacts |

## ADR-0029 dec 15

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | `handle_getinfo` reports `software_image_crc` from the running header |
| `gateway/firmware_client.py` | `image_crc_of` reads it; `plan_for_node` compares it with held images; nothing reports it to the ERP |

## ADR-0029 dec 16

| Code | Role |
|---|---|
| `erp/app/api/routes.py` | `set_firmware_intent`, `get_firmware_intent`, `list_firmware_releases`, `gateway_pull_firmware_intent` |
| `erp/app/services/firmware.py` | `set_intent` and `intent` keep the intended release per machine |
| `erp/app/models/domain.py` | `FirmwareIntent` |
| `gateway/firmware_client.py` | `pull_intent` and `pass_once` perform the transfer node by node |
| `gateway/files/systemd/industrygrow-firmware.timer` | Runs the gateway pass periodically |

## ADR-0029 dec 17

| Code | Role |
|---|---|
| `gateway/firmware_client.py` | `plan_for_node` serves the intended artifact for the other slot, or returns `unidentified` and names node and CRC |
| `gateway/tests/test_firmware_client.py` | Tests the plan without a bus |

Links: [firmware/README.md](../firmware/README.md#flashing-and-updating), [firmware/README.md release artifacts](../firmware/README.md#release-artifacts-store), [gateway/README.md](../gateway/README.md#7-verify-in-this-order).
