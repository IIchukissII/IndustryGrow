<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Gateway local store and persistent state

The manual of [ADR-0020](../ADR/ADR-0020-gateway-persistence-model.md): the telemetry store, its switch and bound, and the files that hold the gateway's configuration and identity state.

The store is a SQLite file in the telemetry unit's state directory, written by the telemetry consumer. It is off unless `IGROW_PERSISTENT_BUFFER` is set; its settings are in `gateway/gateway.env.example`.

## ADR-0020 dec 1

With no IndustryFlow, the store is the record: the consumer writes it and nothing reads it out. The buffer role has no form yet; no code uploads from it.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::Store` | The local store |
| `gateway/files/app/gateway_telemetry.py::STORE_ENABLED` | The store switch |

## ADR-0020 dec 2

Rows are evicted oldest-first by `t_store` past `IGROW_RETENTION_DAYS`, swept every `IGROW_RETENTION_SWEEP_S`. Flushing to IndustryFlow is not realised.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::Store.evict` | Time-bound eviction |
| `gateway/files/app/gateway_telemetry.py::RETENTION_DAYS` | The bound, from configuration |
| `gateway/gateway.env.example` | Store switch and retention settings |

## ADR-0020 dec 3

The store runs in WAL mode with `synchronous=NORMAL` and commits in batches by row count or interval, so a power cut loses at most the uncommitted batch. A failed write drops the batch and logs it.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::Store.__init__` | WAL and synchronous pragmas |
| `gateway/files/app/gateway_telemetry.py::Store.due` | Batch commit trigger |
| `gateway/files/app/gateway_telemetry.py::Store.commit` | Batch write, rollback on error |

## ADR-0020 dec 4

Not yet realised. The consumer has one recording rate; no survey mode, campaign bound or export exists in `gateway/`.

## ADR-0020 dec 5

Five of the six entries have a writer or reader; the batch sequence number has none, because no code keeps one.

| Code | Role |
|---|---|
| `gateway/profile_client.py::apply_profile` | Writes the active profile and its last-known-good copy |
| `gateway/firmware_client.py::fetch_artifacts` | Holds the firmware artifacts |
| `gateway/provision_identity.py::cmd_install` | Installs the secure-element-bound identity chain and the operator CA trust anchor under `/etc/industrygrow/pki` |
| `gateway/profile_client.py::Config.from_env` | Reads the profile-verification public key from `/etc/industrygrow/pki` |
| `gateway/provision.sh::setup_dirs` | Creates `/etc/industrygrow` and its `pki` directory |

## ADR-0020 dec 6

The latest value per node and subject, heartbeat health and counters stay in memory.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::NodeState` | Per-node live state |
| `gateway/files/app/gateway_telemetry.py::Consumer` | Holds the working set |

## ADR-0020 dec 7

Not yet realised. No upload exists, so no in-flight upload batch exists.

## ADR-0020 dec 9

The store schema carries no hash or chain column; rows are plain samples and node events.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::SCHEMA` | Store tables |

## ADR-0020 dec 10

The store switch defaults to off, so a bare SD card runs with no store; an operator turns it on for an SSD/NVMe medium. The medium itself is a BOM line, not code.

| Code | Role |
|---|---|
| `gateway/gateway.env.example` | `IGROW_PERSISTENT_BUFFER`, off by default |

## ADR-0020 dec 11

The last-known-good profile is on disk and needs no network. Resuming control and flushing on reconnect are not realised: no control loop runs and no upload exists ([gateway-profile-and-control.md](gateway-profile-and-control.md)).

| Code | Role |
|---|---|
| `gateway/profile_client.py::LAST_KNOWN_GOOD` | The profile a cold boot resumes from |

## ADR-0020 dec 12

Not yet realised. No code varies capture richness by lifecycle phase.

Links: [gateway/README.md](../gateway/README.md), [gateway-host-hardening.md](gateway-host-hardening.md), [gateway-profile-and-control.md](gateway-profile-and-control.md).
