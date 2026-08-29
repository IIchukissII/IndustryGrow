<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Gateway bring-up Manual — `SP0004-M-gateway-bringup`

- **Type:** HOW document (Manual, document layer **M** — ADR-0017 d9). It owns the
  *how*; the *why* is delegated to the ADRs by number (ADR-0000 d2/d3).
- **Subject:** the gateway Raspberry Pi = **SP0004** (REGISTRY.md; ADR-0019 d7).
- **Identifier:** the filename is the object key; form `SPxxxx-<layer>-<slug>` per
  the SP document-layer convention in `REGISTRY.md`.
- **Companion automation:** in the repo's `gateway/` directory — `provision.sh`,
  `deploy.ps1`, `files/`, `requirements.txt`, `gateway.env.example`.
- **Stage:** roadmap stage 1 (CAN bring-up), bench host `gbox-dev`.
- **Validated configuration:** Raspberry Pi 5 (8 GB) on Debian 13 trixie / aarch64
  / Python 3.13, incl. a real Waveshare 2-CH MCP2515 HAT at 500 kbit/s. The same
  unit targets the Pi 3B+ / 1 GB floor (ADR-0002 rev 3 d6).

---

## 1. Conventions

| Thing | Rule | Source |
|-------|------|--------|
| **Hostname = position** | `gbox-NNNN` per cabinet; `gbox-dev` for the bench. Set by the Imager. | ADR-0017 d6/d7 |
| **Users = role** | `igrow` (admin) and `gateway` (service), identical on every box. `GBOX` is never in a username — only the hostname is position-scoped. | ADR-0004 d7; ADR-0017 |
| **Password / keys = per-instance, off-repo** | No password or private key is in the repo. Per-instance secrets (login/sudo password, SSH keys, future ATECC608 binding + cert) live in a secret manager / IndustryFlow. | ADR-0017 (two-homes); ADR-0019 d2 |
| **Part = SP0004** | Purchased gateway SBC: no version field; instance-tracked by vendor serial; model in the BOM. | ADR-0019 d2/d7 |

**Supported models (SP0004 variants).** SP0004 is a spec, not one SKU (ADR-0019 d3);
the chosen model is a BOM line. **Pi 3B+** (apartment minimum), **Pi 4**, **Pi 5**
(ADR-0002 rev 3 d6). The only model-specific bring-up step is the Pi 5 CAN overlay
([§6](#6-physical-mcp2515-hat-optional-later)).

---

## 2. Imager step

Done once with **Raspberry Pi Imager**, before any automation (the automation runs
post-first-boot and does **not** image the card).

- **Image:** Raspberry Pi OS **Lite, 64-bit, Trixie (Debian 13)** — headless
  (ADR-0002 rev 3 d6).
- **Advanced options (Ctrl-Shift-X):**
  - **Hostname:** `gbox-dev` (bench) or `gbox-NNNN` (field).
  - **Username:** `igrow`; set a strong per-instance password (off-repo, §1).
  - **SSH:** enable, **public-key only** — paste the control node's public key.
  - Wired `eth0` is convenient but not required (the automation auto-detects the
    LAN interface, §5).
- Boot medium at bring-up is an **SD card (provisional)**; production is SSD/NVMe
  ([§7](#7-storage-medium)).

---

## 3. Run the provisioning automation (from Windows)

Prereqs: OpenSSH client (built into Windows 10/11), the SSH private key matching the
public key from the Imager, LAN reachability to the Pi.

```powershell
cd <repo>\gateway
.\deploy.ps1 -HostName gbox-dev
# or: .\deploy.ps1 -HostName 192.168.1.50 -User igrow -SshKey $HOME\.ssh\id_ed25519
```

`deploy.ps1` copies the `gateway/` bundle to the Pi and runs `sudo bash
provision.sh`, using only `ssh`/`scp` and the existing key. Manual equivalent:

```powershell
scp -r .\* igrow@gbox-dev:/tmp/industrygrow-provision/
ssh igrow@gbox-dev "sudo bash /tmp/industrygrow-provision/provision.sh"
```

The script is **idempotent** — safe to re-run after editing
`/etc/industrygrow/gateway.env` on the Pi.

> **Fleet-scale upgrade path:** Ansible is the natural next step but needs WSL on a
> Windows control node; for bring-up the portable `ssh`/`scp` approach is enough.

---

## 4. What the automation does

| Step | Action | ADR |
|------|--------|-----|
| Base packages | `python3-venv`, `nftables`, `fail2ban`, `unattended-upgrades`, `can-utils`, `sqlite3`. **No Docker** — one systemd service, not a container. | ADR-0002; ADR-0004 |
| Service user | System user `gateway`: `adduser --system`, no login shell, no sudo; scoped to CAN + its config dir. | ADR-0004 d7 |
| Python venv | `/opt/industrygrow/venv`, pinned deps. **Never** `pip --break-system-packages` (PEP 668). | ADR-0002 d6 |
| vcan0 | Bring up **virtual CAN first** for validation before any HAT. | ADR-0002 rev 3 d6/d8 |
| Gateway service | `gateway-pycyphal.service` as `gateway`, hardened sandbox (`NoNewPrivileges`, `ProtectSystem=strict`, `RestrictAddressFamilies=AF_CAN`, `Restart=`) **+ resource limits** (`MemoryMax`/`MemoryHigh`, `TasksMax`) sized for the Pi 3B+/1 GB floor, headroom above the 100 MB ring buffer. Runs `gateway_telemetry.py`: subscribes to the node subjects, decodes them through the DSDL vocabulary, stamps `t_acq`/`t_rx`/`t_store` and writes the local store. Subscribe-only, so it takes no Node-ID. | ADR-0004 d7, d18, d21; ADR-0002 d6; ADR-0020 d2 |
| DSDL packages | Compiled on the Pi into `/opt/industrygrow/dsdl` from `firmware/dsdl/industryflow` and the pinned regulated set, staged beside `provision.sh` by `deploy.ps1`. Generated code is never vendored. | ADR-0005 d10 |
| Time master | `industrygrow-timesync.service` as `gateway`, same sandbox. Publishes `uavcan.time.Synchronization` (subject 7168) so nodes stamp telemetry against one time base; without it every node reports `UNKNOWN` (0). Separate unit from the Cyphal edge — the time base must not go stale while that one restarts. | ADR-0002 d11; ADR-0004 d7, d20 |
| SSH | Drop-in `00-industrygrow-hardening.conf` (read before cloud-init's `50-`; sshd is first-value-wins): key-only, no root, no passwords. sshd stays enabled. | ADR-0004 d2 |
| fail2ban | Strict SSH thresholds, journald backend. | ADR-0004 d3 |
| unattended-upgrades | Security patches on; reboot disabled unless `IGROW_UNATTENDED_REBOOT_TIME` set. | ADR-0004 d4 |
| Firewall | nftables default-deny inbound except SSH on the LAN-facing mgmt interfaces (`wlan0`/`eth0`/`end0` + default route); egress open at bring-up. | ADR-0004 d5/d6 |
| journald | Persistent, `SystemMaxUse=100M`. | ADR-0004 d11 |

---

## 5. Bring-up vs production posture

The automation applies the **bring-up** posture. Production differences are
documented, not silently applied (applying them now would lock you out or break
provisioning):

| Concern | Bring-up (now) | Production target | Source |
|---------|----------------|-------------------|--------|
| SSH daemon | Enabled (key-only, no root). | Disabled by default, re-enabled per-op. | ADR-0004 d2, d19 |
| Inbound | default-deny except SSH on the LAN-facing mgmt interfaces. | unchanged. | ADR-0004 d6 |
| Outbound | Open (apt/pip/DNS need it; IndustryFlow doesn't exist yet). | Locked to the IndustryFlow and operator-ERP endpoints. | ADR-0004 d5, d19 |
| Reboot window | Disabled (`IGROW_UNATTENDED_REBOOT_TIME` empty; photoperiod undefined). | Set once photoperiod is known. | ADR-0004 d4, d19 |
| Local store | RAM-only on SD (`IGROW_PERSISTENT_BUFFER=off`). | Bounded buffer on SSD/NVMe. | ADR-0020 |

The production egress lock-down lives commented in
`files/nftables/industrygrow-gateway.nft`, gated on `IGROW_INDUSTRYFLOW_ENDPOINT`.
**Do not enable it before IndustryFlow exists** — it would sever apt/pip.

**SSH interfaces.** `provision.sh` allows SSH on the *set* of LAN-facing
management NICs — `IGROW_SSH_IFACES` (default `wlan0 eth0 end0`), always unioned
with the live default-route interface and any `IGROW_LAN_IFACE`. Moving the cable
between ports or falling back to Wi-Fi therefore does not lock you out; the CAN
interfaces are never in the set. Set `IGROW_SSH_IFACES` only to add an interface
the default list misses. **If you ever do lock yourself out** (e.g. a box
provisioned before this change, pinned to one interface), the boot-medium
recovery needs no console: mount the FAT boot partition on another machine and
append `systemd.mask=nftables.service` to `cmdline.txt` (single line) — SSH is
reachable on the next boot with the firewall skipped, key-only; then re-run
`provision.sh` and remove the token. From a local console instead:
`sudo systemctl stop nftables && sudo nft flush ruleset`, then re-run.

---

## 6. Physical MCP2515 HAT (optional, later)

vcan0 needs none of this — do not configure it for validation. For a physical bus:

1. Fit the isolated 2-channel CAN HAT (MCP2515 + SN65HVD230, ADR-0002 rev 3 d6).
2. Enable SPI + the MCP2515 overlay in `/boot/firmware/config.txt` and reboot (so
   the `can0` device exists). The **oscillator must match the crystal printed on
   the board**. Validated for the Waveshare 2-CH Isolated CAN HAT (16 MHz crystals):

   ```
   dtparam=spi=on
   dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25
   dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=24
   ```

   Confirm with `dmesg | grep mcp251x` (expect "MCP2515 successfully initialized")
   and `ip link show can0`.

   *Or let the automation manage it:* set `IGROW_CAN_HAT=mcp2515` (with
   `IGROW_CAN_HAT_OSC` / `IGROW_CAN0_INT` / `IGROW_CAN1_INT`) in `gateway.env` and
   run `provision.sh`. It writes a declarative, idempotent managed block to
   `config.txt` and flags that a reboot is required. `IGROW_CAN_HAT=off` (default)
   leaves `config.txt` untouched.
3. Set `IGROW_CAN_IFACE=can0` in `/etc/industrygrow/gateway.env`, then re-run
   `provision.sh` (or `sudo systemctl restart industrygrow-can`). The automation
   brings `can0` up at the **fixed 500 kbit/s classic-CAN** rate from
   `IGROW_CAN_BITRATE` (ADR-0002 rev 3 d8) — no manual `ip link` needed. Verify:
   `ip -details link show can0` → `bitrate 500000`.

> **A physical bus needs another node.** Classic CAN requires at least one other
> node to ACK a frame; on a lone interface (no peer) transmits never complete, so
> any `cansend` will not pass until real nodes are on the bus. To bench-test the controller alone, use internal loopback:
> `sudo ip link set can0 type can bitrate 500000 loopback on` (turn it off for the
> real bus).

> **Pi 5:** the GPIO/SPI lines sit behind the **RP1 southbridge**. In validation on
> a Pi 5 the standard `mcp2515-can0/1` overlay above worked unchanged (SPI0,
> `spi0.0`/`spi0.1`); if a future kernel/HAT needs a different `spi`/interrupt
> mapping, adjust the overlay. Not applicable to vcan0.

---

## 7. Storage medium

- **Bring-up = SD card (provisional).** Acceptable only RAM-only, no local store
  (`IGROW_PERSISTENT_BUFFER=off`; ADR-0020 d10; ADR-0004 d8-9). The consumer still
  decodes and keeps the live working set; nothing is written and `t_store` is
  absent rather than zero.
- **Production = SSD/NVMe** (USB-SSD on Pi 4, NVMe-via-M.2-HAT on Pi 5): the
  boot-and-data medium for any gateway that buffers telemetry or runs survey
  capture, and the precondition for `IGROW_PERSISTENT_BUFFER=on` (ADR-0020
  d2/d3/d10). **Do not assume SD in production.**

### What the store holds

`/var/lib/industrygrow/telemetry.sqlite3`, WAL, `synchronous=NORMAL` — best-effort
by decision (ADR-0020 d3), not a durability guarantee against device failure.
Eviction is oldest-first past `IGROW_RETENTION_DAYS`, a time bound and not a
capacity bound.

| Table | Rows |
|---|---|
| `sample` | one per decoded telemetry message: `node_id`, `subject_id`, `t_acq_us`, `t_rx_ns`, `t_rx_mono_ns`, `t_store_ns`, `latency_ns`, and either `value` (scalar subjects, SI units) or `payload` (JSON, for the door/leak status and the gas sweep) |
| `node_event` | heartbeat state changes plus a keepalive, and every `uavcan.diagnostic.Record` |

`t_acq_us = 0` means the node was unsynchronized; it is stored as `0` and never
replaced by `t_rx`. `latency_ns` is `NULL` whenever `t_rx - t_acq` is not
admissible (ADR-0004 d21) — both stamps are still there, only the difference is
withheld.

```bash
sudo -u gateway sqlite3 /var/lib/industrygrow/telemetry.sqlite3   "SELECT node_id, subject_id, count(*), round(avg(latency_ns)/1e6,2) AS ms
     FROM sample GROUP BY 1,2 ORDER BY 1,2;"
```

---

## 8. Node-ID provisioning

A node holds its Cyphal Node-ID in carrier flash and is **not usable until it is
written once** (ADR-0027 d5/d6). A node that has never been provisioned, or that
has just taken firmware for the first time, comes up at `127`, publishes no
telemetry subjects and reports ADVISORY health. Allocation is an operator
decision, not the gateway's (d7), so this runs by hand.

```bash
cd /opt/industrygrow
sudo -u gateway PYTHONPATH=/opt/industrygrow/dsdl venv/bin/python      provision_node_id.py list
sudo -u gateway PYTHONPATH=/opt/industrygrow/dsdl venv/bin/python      provision_node_id.py set 127 96 --restart
```

| Rule | Why |
|---|---|
| Provisionable range is 0–126; writing `127` clears the store | `127` means *no Node-ID provisioned* and nothing else (d10) |
| The value is adopted at the next restart, not immediately | Changing it under a live transport invalidates in-flight transfer state (d5) |
| Provision one unprovisioned node at a time, with the others powered down | Two unprovisioned nodes both answer at `127`, which the transport cannot tie-break (d6) |
| The write takes seconds, not milliseconds | The node erases a flash sector inside the service call |
| A read-back that differs, or `persistent = false`, means the write did not take | An out-of-range value, a flash failure, and firmware older than the store all present this way |

The tool takes Node-ID `2` for itself, because Cyphal forbids anonymous service
transfers; `1` belongs to the time master. Override with
`IGROW_PROVISION_NODE_ID`.

**A flashing tool must not mass-erase.** Writing an image reaches only the
sectors it covers, and the store is not one of them; a mass erase
de-provisions the node (ADR-0027 d4, ADR-0029 d1).

---

## 9. Bench commands

The module specifications require some operations to be commanded rather than
automatic — a sensor self-test, a heater pulse, writing a calibration. Each
specification's section 10 lists its own vendor `uavcan.node.ExecuteCommand` IDs;
this is how they are sent.

```bash
sudo -u gateway PYTHONPATH=/opt/industrygrow/dsdl      /opt/industrygrow/venv/bin/python /opt/industrygrow/node_command.py 97 --list
sudo -u gateway PYTHONPATH=/opt/industrygrow/dsdl      /opt/industrygrow/venv/bin/python /opt/industrygrow/node_command.py 97 3 --parameter 425
```

The response is a status, not a result: a command that stops a sensor finishes
after it. What it did arrives on `uavcan.diagnostic.Record` — read it in
`journalctl -u gateway-pycyphal.service`, or from the store's `node_event` table.

---

## 10. SocketCAN and `DeviceAllow`

The service grants CAN access via `RestrictAddressFamilies=AF_CAN`, **not**
`DeviceAllow=`: classic SocketCAN (`vcan0` / a future `can0`) is a network
interface reached through `AF_CAN` sockets and has no `/dev` node, so `DeviceAllow=`
would be a no-op. `PrivateDevices=yes` is still set and does not affect `AF_CAN`.
Leave this as-is when editing the unit.

---

## 11. Verification

Run on the Pi (or via `ssh igrow@gbox-dev "<cmd>"`):

```bash
ip -details link show vcan0                       # state UP
systemctl --no-pager status gateway-pycyphal.service   # active
journalctl -u gateway-pycyphal.service -n 20 --no-pager    # one summary line per node
sudo -u gateway /opt/industrygrow/venv/bin/python      /opt/industrygrow/gateway_telemetry.py --once 20      # a 20 s sample, by hand
systemctl --no-pager status industrygrow-timesync.service   # active
candump <iface> 041C0000:1FFFFF00                          # the time-sync frame
sudo sshd -T | grep -Ei 'passwordauthentication|permitrootlogin|pubkeyauthentication'
# expect: passwordauthentication no / permitrootlogin no / pubkeyauthentication yes
sudo nft list ruleset                             # policy drop; SSH accept on the LAN iface
sudo fail2ban-client status sshd
journalctl --disk-usage                           # bounded by SystemMaxUse
```

Optional CAN smoke test (start the listener first):

```bash
candump -n 1 vcan0 &        # listen
cansend vcan0 123#494752    # should be printed by candump
```

---

## 12. References (why — owned by the ADRs)

- **ADR-0000** — decision records / single-source-of-truth.
- **ADR-0002 (rev 3)** — Pycyphal/SocketCAN gateway; classic CAN, 500 kbit/s; Pi tiers.
- **ADR-0004 (rev 1)** — host hardening (d2 SSH, d3 fail2ban, d4 upgrades, d5/d6 firewall, d7 service user, d11 journald).
- **ADR-0017** — identification (d6 GBOX, d9 document layers).
- **ADR-0019** — purchased-part SP scheme (d2 no version, d7 Pi=SP).
- **ADR-0020** — gateway persistence (SD provisional, SSD/NVMe production).
- **REGISTRY.md** — SP0004 = gateway SBC; SP document-layer naming convention.
