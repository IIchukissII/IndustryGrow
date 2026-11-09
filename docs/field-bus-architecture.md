<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Field bus, carrier and gateway stack

The manual of [ADR-0002](../ADR/ADR-0002-field-bus-architecture.md): the firmware, gateway code and store files that realise the Cyphal/CAN bus, the carrier and core board, and the gateway stack.

## ADR-0002 dec 1

Nodes run libcanard over DSDL compiled by Nunavut at build time; the gateway runs Pycyphal over DSDL compiled at provisioning. The project namespace is `firmware/dsdl/industryflow`; the standard namespace is the `public_regulated_data_types` submodule.

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | Node Cyphal stack on libcanard: publish, subscribe, services, heartbeat |
| `firmware/cmake/dsdl.cmake` | Node-side Nunavut generation of the DSDL headers |
| `gateway/provision.sh::setup_dsdl` | Gateway-side compilation of the DSDL namespaces into Python packages |
| `gateway/requirements.txt` | Pins the Pycyphal version of the gateway venv |
| `gateway/files/app/igrow_subjects.py` | The subjects and DSDL types the gateway subscribes to |

## ADR-0002 dec 2

The core board is the purchased part `SP0005`; its entry and its snapshot are in [REGISTRY.md](../REGISTRY.md). The carrier layout carries the core-board sockets.

| Code | Role |
|---|---|
| `store/E0001-000003-D-src.zip` | Carrier layout in service, with the core-board sockets |
| `store/E0001-000100-D-src.zip` | Next carrier layout, with the core-board sockets |

## ADR-0002 dec 3

The carrier parts and their values are the carrier specification's ([spec/E0001-R-specification.md](../spec/E0001-R-specification.md)); the design files below hold them.

| Code | Role |
|---|---|
| `store/E0001-000003-S-src.zip` | Carrier schematic, revision in service |
| `store/E0001-000003-L.csv` | Carrier BOM, revision in service |
| `store/E0001-000100-S-src.zip` | Carrier schematic, next revision |
| `store/E0001-000100-L.csv` | Carrier BOM, next revision |
| `firmware/common/carrier/atecc608.c` | Driver for the secure element every carrier carries |
| `firmware/common/carrier/e0001.c` | Carrier board support: module header, indicators |

## ADR-0002 dec 4, 10

The carrier design files are under the store's CERN-OHL-S default in `REUSE.toml`. The core-board snapshot is one archive under the `SP0005` identity; `REUSE.toml` annotates it with upstream copyright and `LicenseRef-WeAct-unstated`, overriding the store default. The archive's contents and provenance are listed in [REGISTRY.md](../REGISTRY.md).

| Code | Role |
|---|---|
| `store/SP0005-D-coreboard-snapshot.zip` | The upstream schematic, outline and 3D model |
| `store/E0001-000003-D-fab.zip` | Carrier fabrication outputs, revision in service |
| `store/E0001-000100-D-fab.zip` | Carrier fabrication outputs, next revision |

## ADR-0002 dec 5

Boot is the same on every node. The carrier reads the module class ID (module EEPROM, else the strap, by carrier revision), and the registry maps it to a personality. A class with no personality boots unidentified and publishes no telemetry. The release script builds one image set for every node type, named under the carrier `E0001`.

| Code | Role |
|---|---|
| `firmware/common/node/main.c::main` | Common boot; reads the class ID and starts the personality |
| `firmware/common/carrier/e0001.c::e0001_read_class_id` | Reads the class ID from the module EEPROM |
| `firmware/common/carrier/e0001.c::e0001_read_module_id` | Reads the module-ID strap |
| `firmware/nodes/registry.c::node_for_module_id` | Maps a class ID to its personality |
| `firmware/common/node/node.h` | The interface every personality implements |
| `firmware/tools/release.sh` | Builds and files the one image set under `E0001-VVVVVV-F` |

## ADR-0002 dec 6

The gateway service is an asyncio Pycyphal consumer on SocketCAN, run by systemd. The CAN HAT is an MCP2515 two-channel board whose device-tree overlay `provision.sh` writes when `IGROW_CAN_HAT` selects it; the second channel is configured only when `IGROW_CAN1_INT` is set, and no service uses it. The local store is ADR-0020's ([gateway-persistence.md](gateway-persistence.md)).

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::run` | The asyncio Pycyphal subscriber on SocketCAN |
| `gateway/files/systemd/gateway-pycyphal.service` | Runs the consumer |
| `gateway/provision.sh::setup_can_hat` | Writes the HAT overlay for one or two channels |
| `gateway/provision.sh::setup_venv` | Builds the gateway Python environment |
| `gateway/gateway.env.example` | HAT, interface and channel settings |

## ADR-0002 dec 7

The bus carries no authentication (ADR-0004 dec 17). The boundary is the gateway host, whose hardening is [gateway-host-hardening.md](gateway-host-hardening.md).

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | Accepts every well-formed transfer; no per-node authentication |
| `gateway/files/nftables/industrygrow-gateway.nft` | The network side of the boundary |

## ADR-0002 dec 8

The bit rate has one home on the gateway, `IGROW_CAN_BITRATE` in `gateway.env`; `can-up.sh` applies it to a physical interface and sets none on a virtual one. The node bit timing is fixed in the CAN driver. `busload.py` measures the bus load by exact bit accounting against the same rate. Termination, shield and the separate field-rail conductors are carrier hardware (dec 3 files).

| Code | Role |
|---|---|
| `gateway/files/app/can-up.sh` | Brings the interface up at `IGROW_CAN_BITRATE` |
| `gateway/files/systemd/industrygrow-can.service` | Runs `can-up.sh` from `gateway.env` |
| `firmware/common/drivers/can.c` | Node bit timing |
| `gateway/busload.py` | Measures bus load in real bits |

## ADR-0002 dec 9

Not yet realised. The gateway consumer keys every sample and event by Node-ID only (`gateway/files/app/gateway_telemetry.py`); no gateway file holds a Node-ID-to-`module` or node-to-slot mapping.

## ADR-0002 dec 11

The time master is its own unit on a raw AF_CAN socket. It waits for the host clock to be synchronized, then publishes `uavcan.time.Synchronization` at a period inside the type's limit, carrying the transmit timestamp of its previous frame. Its Node-ID is `IGROW_CYPHAL_NODE_ID`, below the node range. The node keeps the offset beside its monotonic clock, applies it when a timestamp is written, and drops it on master loss so stamps read `UNKNOWN`.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_timesync.py::run` | Publishes the synchronization message |
| `gateway/files/app/gateway_timesync.py::wait_for_clock` | Holds the first message until the host clock is synchronized |
| `gateway/files/app/gateway_timesync.py::read_tx_timestamp` | Reads the kernel transmit timestamp of the master's own frame |
| `gateway/files/systemd/industrygrow-timesync.service` | Runs the master after `time-sync.target` |
| `firmware/common/cyphal/cyphal.c::handle_timesync` | Node-side offset from a consecutive pair |
| `firmware/common/cyphal/cyphal.c::cyphal_timestamp_usec` | Applies the offset at the timestamp; `UNKNOWN` without one |
| `firmware/common/cyphal/cyphal.c::cyphal_spin` | Drops the offset when the master falls silent |

## ADR-0002 dec 12

Each board's specification under `spec/` states its copper-layer count; the layout carries it.

| Code | Role |
|---|---|
| `store/E0001-000003-D-src.zip` | Carrier layout, revision in service |
| `store/E0001-000100-D-src.zip` | Carrier layout, next revision |

Links: [gateway/README.md](../gateway/README.md), [firmware/README.md](../firmware/README.md), [gateway-host-hardening.md](gateway-host-hardening.md), [gateway-persistence.md](gateway-persistence.md).
