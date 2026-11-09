<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Gateway host hardening and firmware signing

The manual of [ADR-0004](../ADR/ADR-0004-gateway-host-hardening-firmware-signing-and-stateless-edge-operation.md): the provisioning script, host configuration, service units, telemetry stamps and release signing that realise the gateway's security posture.

`gateway/provision.sh::main` runs every step below in order; each installs one file from `gateway/files/` and enables its service.

## ADR-0004 dec 1

Not yet realised. No gateway code connects to IndustryFlow; `IGROW_INDUSTRYFLOW_ENDPOINT` in `gateway/gateway.env.example` is read by nothing. The gateway's mTLS client identity exists and is used toward the ERP ([gateway-identity-and-provisioning.md](gateway-identity-and-provisioning.md)).

## ADR-0004 dec 2

The sshd drop-in is read first, so its settings win over the image's own drop-ins: key-only, no password, no keyboard-interactive, no root login. `harden_ssh` tests the configuration before reloading sshd. sshd stays enabled (dec 19).

| Code | Role |
|---|---|
| `gateway/files/sshd/00-industrygrow-hardening.conf` | Key-only, root refused |
| `gateway/provision.sh::harden_ssh` | Installs it, tests, reloads |

## ADR-0004 dec 3

fail2ban reads the journal and bans with escalating duration. `ignoreip` is loopback plus the attached subnet of each management interface, from the kernel's link routes, plus `IGROW_F2B_IGNOREIP`.

| Code | Role |
|---|---|
| `gateway/files/fail2ban/jail.local` | Thresholds, escalation, SSH jail |
| `gateway/provision.sh::load_env` | Derives the exempt subnets |
| `gateway/provision.sh::setup_fail2ban` | Renders `ignoreip` and installs the jail |

## ADR-0004 dec 4

Unattended upgrades install security origins only. Automatic reboot is off; `setup_unattended` enables it at `IGROW_UNATTENDED_REBOOT_TIME` only when that is set (dec 19).

| Code | Role |
|---|---|
| `gateway/files/apt/52-industrygrow-unattended-upgrades` | Origins and the reboot switch |
| `gateway/provision.sh::setup_unattended` | Installs it; sets the reboot time from `gateway.env` |

## ADR-0004 dec 5

Inbound policy is drop, with SSH accepted on the set `SSH_IFACES`. `load_env` builds that set from `IGROW_SSH_IFACES`, `IGROW_LAN_IFACE` and the live default-route interface; CAN interfaces are not in it. Output policy is accept (dec 19); the restricted output chain is a comment in the ruleset.

| Code | Role |
|---|---|
| `gateway/files/nftables/industrygrow-gateway.nft` | The ruleset |
| `gateway/provision.sh::load_env` | Renders the SSH interface set |
| `gateway/provision.sh::setup_firewall` | Validates and loads the ruleset |

## ADR-0004 dec 6

No gateway unit opens a listening socket; the inbound chain admits SSH only. No local debug interface exists.

| Code | Role |
|---|---|
| `gateway/files/nftables/industrygrow-gateway.nft` | Inbound default drop |

## ADR-0004 dec 7, 20

`create_service_user` makes the system user `gateway` with no shell and no home. Every application unit runs as it under one sandbox: `NoNewPrivileges`, `ProtectSystem=strict`, the `Protect*` and `Restrict*` set, and `RestrictAddressFamilies` limited to what the unit uses. `/etc/industrygrow` is read-only to every unit except the profile pull, whose job is to write the active profile. CAN is reached through `AF_CAN`; no unit has a `DeviceAllow`.

| Code | Role |
|---|---|
| `gateway/provision.sh::create_service_user` | Creates the service user |
| `gateway/provision.sh::setup_dirs` | Configuration and application directories, owned root, group `gateway` |
| `gateway/files/systemd/gateway-pycyphal.service` | Telemetry edge; configuration read-only |
| `gateway/files/systemd/industrygrow-timesync.service` | Bus time master; configuration read-only |
| `gateway/files/systemd/industrygrow-firmware.service` | Firmware client; configuration read-only |
| `gateway/files/systemd/industrygrow-profile-pull.service` | Profile pull; the one unit with `/etc/industrygrow` writable |

## ADR-0004 dec 8, 9

Not yet realised. The gateway forwards nothing: no code uploads telemetry, so no in-memory upload buffer exists. What the gateway keeps locally is [gateway-persistence.md](gateway-persistence.md).

## ADR-0004 dec 10

Not yet realised. No gateway code computes a batch hash or keeps a batch sequence number; the comment in `gateway/files/systemd/gateway-pycyphal.service` naming a sequence number in the state directory has no code behind it.

## ADR-0004 dec 11

The journal is persistent and size-capped by a drop-in. Every unit logs to stdout, which the journal captures.

| Code | Role |
|---|---|
| `gateway/files/journald/99-industrygrow.conf` | Persistent storage and its cap |
| `gateway/provision.sh::setup_journald` | Installs it |

## ADR-0004 dec 12

`release.sh` requires `--key` or `--unsigned`; with a key it derives the verification key from it and passes both to a dedicated build directory. `mkimage.py` signs the image header with the P-256 key. Key custody is [operator-ca-ceremony.md](operator-ca-ceremony.md).

| Code | Role |
|---|---|
| `firmware/tools/release.sh` | Builds and signs a release into `store/` |
| `firmware/tools/mkimage.py::sign` | Signs the image header |
| `firmware/tools/mkimage.py::public_key_hex` | Derives the key the bootloader embeds |
| `firmware/tools/mkimage.py::new_key` | Creates a signing key |

## ADR-0004 dec 13

The firmware client pulls the intended release from the ERP, holds its artifacts in its state directory and serves the one in flight over `uavcan.file.Read`. Which artifacts and which slot is [node-firmware-update.md](node-firmware-update.md).

| Code | Role |
|---|---|
| `gateway/firmware_client.py::fetch_artifacts` | Holds the release artifacts on disk |
| `gateway/firmware_client.py::Bus.serve_file` | Serves the image over the Cyphal file service |
| `gateway/files/systemd/industrygrow-firmware.service` | Runs one pass |
| `gateway/files/systemd/industrygrow-firmware.timer` | Pass cadence |

## ADR-0004 dec 14

The verification key is generated into the bootloader at build. After a bus download the bootloader checks header, digest and signature, and marks the slot bootable only on success; a build with no key refuses every update. At boot a slot is checked by header and CRC only, so an image written over SWD boots without a signature check.

| Code | Role |
|---|---|
| `firmware/boot/verify_key.h.in` | The embedded verification key |
| `firmware/boot/verify.c::image_verify_slot` | Digest and P-256 signature check |
| `firmware/boot/update.c::update_run_request` | Verifies before the slot is marked bootable |
| `firmware/common/platform/image.c::image_slot_bootable` | Boot-time header and CRC check |

## ADR-0004 dec 15

The bootloader has no update path for its own sectors; it writes only application slots. Replacing it is an SWD flash ([firmware/README.md](../firmware/README.md)).

| Code | Role |
|---|---|
| `firmware/boot/update.c::erase_slot` | Erases an application slot only |
| `firmware/ldscripts/STM32F405RGTx_BOOT.ld` | The bootloader's own sectors |

## ADR-0004 dec 16

Not yet realised. The firmware client reports update outcomes to the journal only; it sends nothing to the ERP or IndustryFlow (`gateway/firmware_client.py`).

## ADR-0004 dec 17

The node stack accepts every well-formed transfer; frames carry no authentication or encryption.

| Code | Role |
|---|---|
| `firmware/common/cyphal/cyphal.c` | Node Cyphal stack, no per-node authentication |

## ADR-0004 dec 18

The telemetry consumer writes `t_acq` from the message's synchronized timestamp, `0` kept as `0`; `t_rx` from the kernel reception timestamp in realtime and monotonic form; `t_store` at the commit that writes the row. With the store off no row is written and `t_store` does not exist. The platform tiers do not exist yet.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::Consumer.on_sample` | `t_acq` and `t_rx` per sample |
| `gateway/files/app/gateway_telemetry.py::Store.commit` | `t_store` at the write |
| `gateway/files/app/gateway_telemetry.py::SCHEMA` | The stamp columns |

## ADR-0004 dec 19

The provisioning material carries the bring-up posture of decisions 2, 4 and 5: sshd enabled, reboot off unless `IGROW_UNATTENDED_REBOOT_TIME` is set, output open. No code applies the production posture.

| Code | Role |
|---|---|
| `gateway/files/sshd/00-industrygrow-hardening.conf` | sshd left enabled |
| `gateway/files/apt/52-industrygrow-unattended-upgrades` | Reboot off by default |
| `gateway/files/nftables/industrygrow-gateway.nft` | Output open; the restricted chain as a comment |

## ADR-0004 dec 21

`ClockGuard` withholds the latency while the host clock is unsynchronized, read from `adjtimex` or, under `ProtectClock`, from the timesyncd flag, and for a blackout after a step. A step is a jump in realtime minus monotonic; the blackout is two node publication periods plus `IGROW_CLOCK_SETTLE_S`. A withheld latency is stored as NULL beside both stamps.

| Code | Role |
|---|---|
| `gateway/files/app/gateway_telemetry.py::ClockGuard` | Synchronization and step detection |
| `gateway/files/app/gateway_telemetry.py::ClockGuard.latency_valid` | Whether the latency is recorded |
| `gateway/files/app/gateway_telemetry.py::LATENCY_BLACKOUT_S` | The blackout after a step |

Links: [gateway/README.md](../gateway/README.md), [firmware/README.md](../firmware/README.md), [node-firmware-update.md](node-firmware-update.md), [gateway-identity-and-provisioning.md](gateway-identity-and-provisioning.md), [operator-ca-ceremony.md](operator-ca-ceremony.md).
