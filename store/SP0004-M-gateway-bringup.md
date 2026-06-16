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
| Base packages | `python3-venv`, `nftables`, `fail2ban`, `unattended-upgrades`, `can-utils`. **No Docker** — one systemd service, not a container. | ADR-0002; ADR-0004 |
| Service user | System user `gateway`: `adduser --system`, no login shell, no sudo; scoped to CAN + its config dir. | ADR-0004 d7 |
| Python venv | `/opt/industrygrow/venv`, pinned deps. **Never** `pip --break-system-packages` (PEP 668). | ADR-0002 d6 |
| vcan0 | Bring up **virtual CAN first** for validation before any HAT. | ADR-0002 rev 3 d6/d8 |
| Gateway service | `gateway-pycyphal.service` as `gateway`, hardened sandbox (`NoNewPrivileges`, `ProtectSystem=strict`, `RestrictAddressFamilies=AF_CAN`, `Restart=`) **+ resource limits** (`MemoryMax`/`MemoryHigh`, `TasksMax`) sized for the Pi 3B+/1 GB floor, headroom above the 100 MB ring buffer. Runs a bring-up self-test placeholder until the DSDL app exists (ADR-0005). | ADR-0004 d7; ADR-0002 d6 |
| SSH | Drop-in `00-industrygrow-hardening.conf` (read before cloud-init's `50-`; sshd is first-value-wins): key-only, no root, no passwords. sshd stays enabled. | ADR-0004 d2 |
| fail2ban | Strict SSH thresholds, journald backend. | ADR-0004 d3 |
| unattended-upgrades | Security patches on; reboot disabled unless `IGROW_UNATTENDED_REBOOT_TIME` set. | ADR-0004 d4 |
| Firewall | nftables default-deny inbound except SSH on the LAN iface (auto-detected); egress open at bring-up. | ADR-0004 d5/d6 |
| journald | Persistent, `SystemMaxUse=100M`. | ADR-0004 d11 |

---

## 5. Bring-up vs production posture

The automation applies the **bring-up** posture. Production differences are
documented, not silently applied (applying them now would lock you out or break
provisioning):

| Concern | Bring-up (now) | Production target | Source |
|---------|----------------|-------------------|--------|
| SSH daemon | Enabled (key-only, no root). | Disabled by default, re-enabled per-op. | ADR-0004 d2 |
| Inbound | default-deny except SSH on the LAN iface. | unchanged. | ADR-0004 d6 |
| Outbound | Open (apt/pip/DNS need it; IndustryFlow doesn't exist yet). | Locked to IndustryFlow only. | ADR-0004 d5 |
| Reboot window | Disabled (`IGROW_UNATTENDED_REBOOT_TIME` empty; photoperiod undefined). | Set once photoperiod is known. | ADR-0004 d4 |
| Local store | RAM-only on SD (`IGROW_PERSISTENT_BUFFER=off`). | Bounded buffer on SSD/NVMe. | ADR-0020 |

The production egress lock-down lives commented in
`files/nftables/industrygrow-gateway.nft`, gated on `IGROW_INDUSTRYFLOW_ENDPOINT`.
**Do not enable it before IndustryFlow exists** — it would sever apt/pip.

**LAN interface.** `provision.sh` allows SSH on the interface carrying the default
route (`eth0` is not assumed — WiFi is `wlan0`, Pi 5 onboard Ethernet is `end0`).
Leave `IGROW_LAN_IFACE` empty to auto-detect, or set it explicitly to pin a
specific interface. **If you ever lock yourself out**, recover from the local
console: `sudo systemctl stop nftables && sudo nft flush ruleset`, fix
`IGROW_LAN_IFACE`, and re-run.

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
> the placeholder self-test and any `cansend` will not pass until real nodes are on
> the bus. To bench-test the controller alone, use internal loopback:
> `sudo ip link set can0 type can bitrate 500000 loopback on` (turn it off for the
> real bus).

> **Pi 5:** the GPIO/SPI lines sit behind the **RP1 southbridge**. In validation on
> a Pi 5 the standard `mcp2515-can0/1` overlay above worked unchanged (SPI0,
> `spi0.0`/`spi0.1`); if a future kernel/HAT needs a different `spi`/interrupt
> mapping, adjust the overlay. Not applicable to vcan0.

---

## 7. Storage medium

- **Bring-up = SD card (provisional).** Acceptable only RAM-only, no local store
  (`IGROW_PERSISTENT_BUFFER=off`; ADR-0020 d10; ADR-0004 d8-9).
- **Production = SSD/NVMe** (USB-SSD on Pi 4, NVMe-via-M.2-HAT on Pi 5): the
  boot-and-data medium for any gateway that buffers telemetry or runs survey
  capture, and the precondition for `IGROW_PERSISTENT_BUFFER=on` (ADR-0020
  d2/d3/d10). **Do not assume SD in production.**

---

## 8. SocketCAN and `DeviceAllow`

The service grants CAN access via `RestrictAddressFamilies=AF_CAN`, **not**
`DeviceAllow=`: classic SocketCAN (`vcan0` / a future `can0`) is a network
interface reached through `AF_CAN` sockets and has no `/dev` node, so `DeviceAllow=`
would be a no-op. `PrivateDevices=yes` is still set and does not affect `AF_CAN`.
Leave this as-is when editing the unit.

---

## 9. Verification

Run on the Pi (or via `ssh igrow@gbox-dev "<cmd>"`):

```bash
ip -details link show vcan0                       # state UP
systemctl --no-pager status gateway-pycyphal.service   # active; "self-test PASSED" in log
journalctl -u gateway-pycyphal.service -n 20 --no-pager
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

## 10. References (why — owned by the ADRs)

- **ADR-0000** — decision records / single-source-of-truth.
- **ADR-0002 (rev 3)** — Pycyphal/SocketCAN gateway; classic CAN, 500 kbit/s; Pi tiers.
- **ADR-0004 (rev 1)** — host hardening (d2 SSH, d3 fail2ban, d4 upgrades, d5/d6 firewall, d7 service user, d11 journald).
- **ADR-0017** — identification (d6 GBOX, d9 document layers).
- **ADR-0019** — purchased-part SP scheme (d2 no version, d7 Pi=SP).
- **ADR-0020** — gateway persistence (SD provisional, SSD/NVMe production).
- **REGISTRY.md** — SP0004 = gateway SBC; SP document-layer naming convention.
