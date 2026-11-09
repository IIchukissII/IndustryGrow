<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Gateway profile pull and control loops

The manual of [ADR-0015](../ADR/ADR-0015-gateway-profile-caching-and-local-control-loops.md): the profile pull, its ERP counterpart, the control model and the node-side conditioning.

## ADR-0015 dec 1, 13-15

The profile pull is the gateway's only inbound path for behaviour. The ERP stores profile versions and records which is active; it has no deploy or push function and no route that commands an actuator. The gateway has no listening service and no remote-execution path. `node_command.py` is an operator tool run on the gateway itself; it sends `ExecuteCommand` to a node over the bus, only for the IDs in its allowlist, per node type as GetInfo names it. Any other ID is refused before anything is sent. No vendor command on any node drives an actuator output or demand.

| Class | Commands | Sent |
|---|---|---|
| Calibration trim commit | M01 3, U3 temperature offset (ADR-0028 dec 4) | Always |
| Report-only diagnostics | M02 1, 2; M04 1, 2, 3; M05 2, 3; A01 1, 2 | Always |
| Bench operations that change what a node measures or publishes | M01 1, 2, 4, 5, 6, 7; M05 1 | Only with `--bench` |
| Begin-software-update, 65533 | Sent by `firmware_client.py`, which serves the file (ADR-0029 dec 5) | Never |
| Restart, 65535 | Sent by `provision_node_id.py --restart` | Never |
| Any other ID, or a node type not in the table | none | Never |

| Code | Role |
|---|---|
| `gateway/profile_client.py` | The one path by which gateway behaviour changes |
| `erp/app/services/profiles.py` | Versioned profile store and active record, no push |
| `erp/app/api/routes.py::gateway_pull_active_profile` | The gateway route; serves, commands nothing |
| `gateway/node_command.py` | Local bus command tool, not a remote channel |
| `gateway/node_command.py::COMMANDS` | The allowlist, with each command's source |
| `gateway/node_command.py::authorise` | Refuses a command outside the allowlist |
| `gateway/tests/test_node_command.py` | Allowed and refused commands; the table against the firmware's `CMD_*` IDs |

## ADR-0015 dec 2

A profile is a JSON object; the client reads `machine_id` and `version_tag` from inside the signed bytes. The reference content is the profile under `profiles/`.

| Code | Role |
|---|---|
| `gateway/profile_client.py::version_of` | Parses the verified document |
| `profiles/strawberry-day-neutral-v1.json` | Reference profile document |

## ADR-0015 dec 3

The ERP stores each version as an immutable document per machine and rejects a reused tag. The gateway refuses a version whose leading integer is not above the running one.

| Code | Role |
|---|---|
| `erp/app/services/profiles.py::add_version` | Stores a version |
| `erp/app/services/profiles.py::versions` | Lists a machine's retained versions |
| `erp/app/api/routes.py::store_profile_version` | Rejects a reused tag |
| `gateway/profile_client.py::version_ordinal` | Orders versions |
| `gateway/profile_client.py::pull_once` | Refuses a version not newer than the running one |

## ADR-0015 dec 4

The active profile and its last-known-good copy are files in `/etc/industrygrow`.

| Code | Role |
|---|---|
| `gateway/profile_client.py::ACTIVE_PROFILE` | The active profile file |
| `gateway/profile_client.py::LAST_KNOWN_GOOD` | The previous profile file |

## ADR-0015 dec 5

A systemd timer runs one pull at a fixed interval with jitter. The pull is an outbound mTLS request to the ERP's gateway route; the machine is read from the client certificate.

| Code | Role |
|---|---|
| `gateway/files/systemd/industrygrow-profile-pull.timer` | Poll cadence |
| `gateway/files/systemd/industrygrow-profile-pull.service` | Runs one pull |
| `gateway/profile_client.py::pull` | mTLS request to the ERP |
| `gateway/profile_client.py::machine_identity` | Machine from the certificate CN |
| `gateway/provision.sh::install_profile_pull` | Installs the unit and timer |

## ADR-0015 dec 6

The new document is written to a temporary file in the same directory, fsynced and renamed over the active file; the previous file is copied to last-known-good first.

| Code | Role |
|---|---|
| `gateway/profile_client.py::apply_profile` | Atomic switch |

## ADR-0015 dec 7

The signature is checked against the provisioned verification key before the document is parsed; a missing key refuses every pull. A refusal is logged and leaves the running profile in place. The scheme is [profile-signing.md](profile-signing.md).

| Code | Role |
|---|---|
| `gateway/profile_client.py::verify_signature` | Signature check |
| `gateway/profile_client.py::document_and_signature` | Extracts the signed bytes and signature |
| `gateway/profile_client.py::cmd_once` | Logs a refusal and keeps the profile |

## ADR-0015 dec 8

Not yet realised. `gateway/control_model.py` holds the supervisory loop structure with every coefficient unset; no unit runs it, no gateway code reads the active profile for control, and no gateway code publishes a demand.

| Code | Role |
|---|---|
| `gateway/control_model.py::OuterLoop` | Supervisory loop structure, coefficients unset |
| `gateway/control_model.py::PiArw` | PI with anti-windup used by the loop |

## ADR-0015 dec 9, 12

The ERP is profile source only: it stores versions and serves the active one on pull. IndustryFlow and advanced control modules do not exist yet.

| Code | Role |
|---|---|
| `erp/app/services/profiles.py::active_version` | The version served on pull |
| `erp/app/api/routes.py::gateway_pull_active_profile` | Serves it |

## ADR-0015 dec 10

Not yet realised. No built-in fallback profile exists in the gateway code; `gateway/profile_client.py` keeps the previous profile on failure and has no fallback to load.

## ADR-0015 dec 11

The hardware over-temperature trip is on the heating actuator module, not in software. The A01 firmware reads the interlock lines and publishes their state; it does not drive them.

| Code | Role |
|---|---|
| `firmware/nodes/a01_climate/actuators.c::read_lines` | Reads the hardware interlock lines |
| `firmware/nodes/a01_climate/actuators.c::pub_interlock` | Publishes interlock state |
| `firmware/nodes/m05_safety/sensors.c` | Leak and door, report-only |

## ADR-0015 dec 16, 17

Each gateway pulls only its own machine's profile, named by its certificate. No gateway code talks to another gateway or loads an ML model; control code is rule-based and standard library only.

| Code | Role |
|---|---|
| `gateway/profile_client.py::machine_identity` | Binds the pull to one machine |
| `gateway/control_model.py` | Rule-based structure, no ML |

## ADR-0015 dec 18

The node receives a demand with a horizon and sets a deadline from it; past the deadline the element is stale and drives its safe output. Dead band, slew, reversal dwell and derating are node-side pure functions. The gateway half is the outer loop of dec 8 and the conditioner model in `control_model.py`.

| Code | Role |
|---|---|
| `firmware/nodes/a01_climate/actuators.c::on_demand` | Takes a demand and its deadline |
| `firmware/nodes/a01_climate/actuators.c::condition` | Staleness and safe output per element |
| `firmware/nodes/a01_climate/condition.c` | Dead band, slew, reversal dwell, derating |
| `gateway/control_model.py::Conditioner` | Model of the node-side conditioning |

Links: [gateway/README.md](../gateway/README.md), [profile-signing.md](profile-signing.md), [gateway-persistence.md](gateway-persistence.md).
