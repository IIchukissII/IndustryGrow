<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# DSDL vocabulary of the node bus

The manual of [ADR-0005](../ADR/ADR-0005-dsdl-foundation.md): the project DSDL tree, its build-time generation for node, service tool and gateway, the node skeleton, and the subject publication rules in the node firmware.

## ADR-0005 dec 1, 2

The pinned regulated set supplies the SI samples, the node skeleton and the file services. The minted types sit under `industryflow.greenhouse`, one sub-namespace per subsystem.

| Code | Role |
|---|---|
| `firmware/tools/bootstrap.sh` | Pins `public_regulated_data_types` to one commit as a submodule |
| `firmware/dsdl/industryflow/greenhouse/climate/RelativeHumidity.1.0.dsdl` | Humidity as a ratio; no standard type carries it |
| `firmware/dsdl/industryflow/greenhouse/climate/Co2Concentration.1.0.dsdl` | CO₂ as a mole fraction |
| `firmware/dsdl/industryflow/greenhouse/climate/GasResistance.2.0.dsdl` | Gas-plate resistance sweep with its heater setpoints |
| `firmware/dsdl/industryflow/greenhouse/light/SpectralSample.1.0.dsdl` | Spectral band counts with gain and integration time |
| `firmware/dsdl/industryflow/greenhouse/light/PhotonFluxDensity.1.0.dsdl` | PPFD with a validity flag |
| `firmware/dsdl/industryflow/greenhouse/light/Irradiance.1.0.dsdl` | UV-A irradiance with a validity flag |
| `firmware/dsdl/industryflow/greenhouse/light/FlickerStatus.1.0.dsdl` | Flicker detection flags |
| `firmware/dsdl/industryflow/greenhouse/plant/CanopyThermalSummary.1.0.dsdl` | M04 frame statistics (dec 11) |
| `firmware/dsdl/industryflow/greenhouse/safety/DoorStatus.1.0.dsdl` | Door signal (dec 4) |
| `firmware/dsdl/industryflow/greenhouse/safety/LeakStatus.1.0.dsdl` | Leak signal (dec 4) |
| `firmware/dsdl/industryflow/greenhouse/actuator/Demand.1.0.dsdl` | Actuator demand, ADR-0031's |
| `firmware/dsdl/industryflow/greenhouse/actuator/ElementState.1.0.dsdl` | Actuator element state, ADR-0031's |
| `firmware/dsdl/industryflow/greenhouse/actuator/InterlockState.1.0.dsdl` | Interlock lines, ADR-0031's |

## ADR-0005 dec 3

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | `pub_temperature` in kelvin, `pub_pressure` in pascal, `vpd_pascal` |
| `firmware/nodes/m05_safety/sensors.c` | `pub_voltage`, `pub_current`, `pub_power`, `pub_temperature`, `pub_energy` on the standard SI samples |
| `firmware/nodes/m05_safety/drivers/s0.c` | `s0_energy_joule` scales the pulse count by the meter constant to joule |
| `gateway/files/app/igrow_subjects.py` | `Subject` carries the SI unit on the wire; display units are not converted on the node |

## ADR-0005 dec 4

`DoorStatus` and `LeakStatus` carry a timestamp, the state and `valid`, and no command field. M05 drives no output but the leak electrode's excitation.

| Code | Role |
|---|---|
| `firmware/nodes/m05_safety/sensors.c` | `pub_door` and `pub_leak` |
| `firmware/nodes/m05_safety/drivers/leak.c` | Excites the electrodes and reports whether this cycle's reading is valid |
| `gateway/files/app/igrow_subjects.py` | `_door` and `_leak` decode both into the store |

## ADR-0005 dec 5

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | `publish_heartbeat` at 1 Hz; `publish_port_list`; `handle_getinfo`, `handle_access`, `handle_list`; `handle_execcmd` answers restart and the update command |
| `firmware/common/cyphal/registers.c` | `registers_access`, `registers_name_at`: the register store behind Access and List |
| `firmware/boot/update.c` | `bus_up` runs the same skeleton in the bootloader |

The update commands are realised as in [node-firmware-update.md](node-firmware-update.md).

## ADR-0005 dec 6

The Node-ID is the `uavcan.node.id` register, persisted in the carrier store; realised as in [node-identity.md](node-identity.md). No plug-and-play allocator exists in node or gateway.

| Code | Role |
|---|---|
| `firmware/common/cyphal/registers.c` | `registers_access` commits a `uavcan.node.id` write through `identity_commit` |
| `firmware/common/carrier/identity.c` | The persistent store |
| `gateway/provision_node_id.py` | `cmd_write`: the operator's provisioning write |

## ADR-0005 dec 7

The default subject-IDs are compiled into each personality in the unregulated range; the gateway restates the map. The register interface is not yet realised: no `uavcan.pub.<name>.id` register exists, so a subject-ID cannot be reassigned without reflashing.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | `SUBJ_AIR_TEMPERATURE` and the other M01 defaults |
| `firmware/nodes/m02_light/sensors.c` | `SUBJ_SPECTRUM` and the other M02 defaults |
| `firmware/nodes/m04_plant/sensors.c` | `SUBJ_CANOPY` |
| `firmware/nodes/m05_safety/sensors.c` | `SUBJ_BUS_VOLTAGE` and the other M05 defaults |
| `firmware/nodes/a01_climate/actuators.c` | `SUBJ_TEMPERATURE`, `SUBJ_STATE`, `SUBJ_INTERLOCK`, `SUBJ_DEMAND` |
| `firmware/common/cyphal/cyphal.c` | `cyphal_declare_publishers` lists a personality's subjects in the port list |
| `gateway/files/app/igrow_subjects.py` | `SUBJECTS`: the gateway's copy of the defaults |

## ADR-0005 dec 8

Each personality probes at boot and on a fixed period, and gates each publication on its device's presence flag.

| Code | Role |
|---|---|
| `firmware/nodes/m01_climate/sensors.c` | `probe`, `publish_primary` |
| `firmware/nodes/m02_light/sensors.c` | `probe`, `publish_all` |
| `firmware/nodes/m04_plant/sensors.c` | `probe` |
| `firmware/nodes/m05_safety/sensors.c` | `probe`, `publish_all` |
| `firmware/common/drivers/i2c.c` | `i2c_probe` |

## ADR-0005 dec 9

`GasResistance.1.0` and `GasResistance.2.0` coexist; M01 publishes the major-2 sweep.

| Code | Role |
|---|---|
| `firmware/dsdl/industryflow/greenhouse/climate/GasResistance.1.0.dsdl` | The single-setpoint type |
| `firmware/dsdl/industryflow/greenhouse/climate/GasResistance.2.0.dsdl` | The breaking successor |
| `firmware/nodes/m01_climate/sensors.c` | `pub_gas_sweep` |
| `firmware/tools/bootstrap.sh` | The pin of the regulated set |

## ADR-0005 dec 10

Generated sources land in the build tree and the gateway's install directory; `.gitignore` excludes the build trees. The Nunavut pin is not yet realised: the node build finds any `nnvg` on the path, and the gateway's Nunavut arrives as a dependency of the exactly pinned `pycyphal`.

| Code | Role |
|---|---|
| `firmware/cmake/dsdl.cmake` | Two `nnvg` passes, regulated set then project tree, into the build directory |
| `service-tool/cmake/dsdl.cmake` | The same generation for the service tool |
| `gateway/provision.sh` | `setup_dsdl` regenerates the Python packages through `pycyphal.dsdl.compile_all`; `default_dsdl_roots` names the two trees |
| `gateway/requirements.txt` | Pins `pycyphal` |
| `firmware/tools/bootstrap.sh` | Pins the regulated set by commit |
| `.gitignore` | Excludes `firmware/build*/` |

## ADR-0005 dec 11

M04 publishes one summary record and serves the interval frame as a file. The gateway side is not yet realised: no gateway code subscribes to the summary or reads the frame.

| Code | Role |
|---|---|
| `firmware/nodes/m04_plant/sensors.c` | `publish_summary` fills `frame_seq` and `frame_available`; `serve_read` answers `uavcan.file.Read` at `PATH_FRAME` |
| `firmware/nodes/m04_plant/frame.c` | `frame_read`, `frame_available`, `frame_seq` |

## ADR-0005 dec 12

The served product is the per-pixel interval mean and deviation, built on the node; the raw device image is not served.

| Code | Role |
|---|---|
| `firmware/nodes/m04_plant/sensors.c` | `INTERVAL_PERIOD_US`; `interval_tick` closes an interval |
| `firmware/nodes/m04_plant/frame.c` | `frame_build_interval`, `frame_build_event`: the file layout |
| `firmware/nodes/m04_plant/frame.h` | `FRAME_FILE_BYTES` and the layout constants |

Links: [firmware/README.md](../firmware/README.md#default-subject-id-map), [firmware/README.md build](../firmware/README.md#build), module specification E0005-R.
