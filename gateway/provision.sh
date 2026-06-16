#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
#
# IndustryGrow gateway provisioning — idempotent, safe to re-run.
#
# Runs ON the Pi (gbox-dev), POST-first-boot, invoked over SSH from the Windows
# control node (see deploy.ps1 or the Manual). It authenticates by the SSH key the
# Imager already installed; it contains NO secrets. It does NOT image the card.
#
# Grounding (cite, do not restate — ADR-0000):
#   ADR-0002 rev 3 d6/d8  — Pycyphal/SocketCAN gateway; 500 kbit/s classic CAN.
#   ADR-0004 rev 1 d2-7,11 — host hardening, least-priv service user, journald.
#   ADR-0017 / ADR-0019    — role-scoped users; secrets off-repo; SP0004 gateway.
#   ADR-0020               — SD provisional; SSD/NVMe is the production medium.
#
# Usage:   sudo ./provision.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FILES_DIR="${SCRIPT_DIR}/files"
CONFIG_DIR="/etc/industrygrow"
APP_DIR="/opt/industrygrow"
VENV_DIR="${APP_DIR}/venv"
REBOOT_REQUIRED=0

log()  { printf '\033[1;32m[provision]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[provision][WARN]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[provision][ERROR]\033[0m %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------------------
require_root() {
    [ "$(id -u)" -eq 0 ] || die "run as root: sudo ./provision.sh"
}

preflight() {
    log "preflight checks"
    [ -d "${FILES_DIR}" ] || die "files/ bundle not found next to provision.sh"
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        case "${VERSION_CODENAME:-}" in
            trixie) : ;;
            *) warn "expected Debian 13 'trixie', found '${VERSION_CODENAME:-?}' — continuing" ;;
        esac
    fi
    log "architecture: $(uname -m)"
}

# Interface that currently carries the default route — the path you are almost
# certainly managing the Pi over. Used to keep the SSH allow-rule on the right
# interface and avoid a firewall lockout (eth0 is NOT a safe assumption: WiFi is
# wlan0, and recent kernels name onboard Ethernet end0 on Pi 5).
detect_lan_iface() { ip route show default 2>/dev/null | awk '/default/ {print $5; exit}'; }

# Load defaults, then the installed env file (operator edits win).
load_env() {
    IGROW_LAN_IFACE=""   # empty = auto-detect from the default route (below)
    IGROW_SSH_PORT="22"
    IGROW_CAN_IFACE="vcan0"
    IGROW_CAN_BITRATE="500000"
    IGROW_CAN_HAT="off"
    IGROW_CAN_HAT_OSC="16000000"
    IGROW_CAN0_INT="25"
    IGROW_CAN1_INT="24"
    IGROW_UNATTENDED_REBOOT_TIME=""
    IGROW_PERSISTENT_BUFFER="off"
    IGROW_INDUSTRYFLOW_ENDPOINT=""
    IGROW_REQUIRE_HASHES="0"

    install -d -m 0755 "${CONFIG_DIR}"
    install -m 0644 "${SCRIPT_DIR}/gateway.env.example" "${CONFIG_DIR}/gateway.env.example"
    if [ ! -f "${CONFIG_DIR}/gateway.env" ]; then
        install -m 0640 "${SCRIPT_DIR}/gateway.env.example" "${CONFIG_DIR}/gateway.env"
        log "seeded ${CONFIG_DIR}/gateway.env from template (edit + re-run to apply changes)"
    fi
    # shellcheck disable=SC1091
    . "${CONFIG_DIR}/gateway.env"

    # Lockout-safe LAN interface resolution. If unset, or if the configured iface
    # is not the one carrying the default route (the management path), prefer the
    # default-route iface so the SSH allow-rule lands where we can actually reach
    # the box. An explicit, correct value is kept as-is.
    local detected; detected="$(detect_lan_iface || true)"
    if [ -z "${IGROW_LAN_IFACE}" ]; then
        if [ -n "${detected}" ]; then
            log "auto-detected LAN iface '${detected}' (default route)"
            IGROW_LAN_IFACE="${detected}"
        else
            warn "no default route found; falling back to eth0"
            IGROW_LAN_IFACE="eth0"
        fi
    elif [ -n "${detected}" ] && [ "${IGROW_LAN_IFACE}" != "${detected}" ]; then
        # Honor an explicit setting (the operator knows the intended management
        # iface, e.g. wlan0 while recovering over a temporary eth0 cable). Only
        # warn — do not override, or we'd fight the operator's end-state choice.
        warn "configured LAN iface '${IGROW_LAN_IFACE}' is not the current default-route iface '${detected}'; honoring the explicit setting — SSH will be allowed on '${IGROW_LAN_IFACE}'"
    fi
    log "config: LAN=${IGROW_LAN_IFACE} CAN=${IGROW_CAN_IFACE} buffer=${IGROW_PERSISTENT_BUFFER}"
}

apt_base() {
    log "installing base packages (no Docker — single systemd service per ADR-0004)"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y --no-install-recommends \
        python3-venv python3-full \
        nftables fail2ban unattended-upgrades \
        can-utils iproute2 ca-certificates
}

create_service_user() {
    # ADR-0004 rev 1 d7: dedicated unprivileged SYSTEM user, no login shell, no sudo.
    # Role-scoped name `gateway` — identical on every box; position (GBOX) is the
    # hostname, never the username (ADR-0017).
    if id gateway >/dev/null 2>&1; then
        log "service user 'gateway' already exists"
    else
        log "creating system user 'gateway'"
        adduser --system --group --no-create-home \
                --shell /usr/sbin/nologin gateway
    fi
}

setup_dirs() {
    install -d -m 0755 "${APP_DIR}"
    install -d -m 0750 -o root -g gateway "${CONFIG_DIR}"
    # gateway.env may hold an endpoint but no secret; readable by the service group.
    [ -f "${CONFIG_DIR}/gateway.env" ] && chgrp gateway "${CONFIG_DIR}/gateway.env" && chmod 0640 "${CONFIG_DIR}/gateway.env"
    install -d -m 0750 -o root -g gateway "${APP_DIR}"
    install -m 0644 "${FILES_DIR}/app/gateway_selftest.py" "${APP_DIR}/gateway_selftest.py"
    install -m 0755 "${FILES_DIR}/app/can-up.sh" "${APP_DIR}/can-up.sh"
}

setup_venv() {
    # PEP 668: venv only, NEVER --break-system-packages.
    if [ ! -x "${VENV_DIR}/bin/python" ]; then
        log "creating venv at ${VENV_DIR}"
        python3 -m venv "${VENV_DIR}"
    fi
    "${VENV_DIR}/bin/pip" install --quiet --upgrade pip
    local pip_args=(install --quiet --upgrade -r "${SCRIPT_DIR}/requirements.txt")
    if [ "${IGROW_REQUIRE_HASHES}" = "1" ] && [ -f "${SCRIPT_DIR}/requirements.lock" ]; then
        log "installing pinned+hashed deps (requirements.lock)"
        pip_args=(install --quiet --require-hashes -r "${SCRIPT_DIR}/requirements.lock")
    else
        log "installing pinned deps (requirements.txt)"
    fi
    "${VENV_DIR}/bin/pip" "${pip_args[@]}"
    # venv is root-owned and world-readable: the service only reads/executes it.
    chmod -R a+rX "${VENV_DIR}"
}

setup_can_hat() {
    # Optional, flag-gated management of the physical MCP2515 HAT device-tree
    # overlay in config.txt. OFF by default: it is HAT-specific (the oscillator
    # must match the board crystal; INT GPIOs are board-specific) and needs a
    # REBOOT before can0/can1 appear. Declarative + idempotent: the marked block
    # is rewritten each run and removed when IGROW_CAN_HAT=off.
    local cfg=/boot/firmware/config.txt
    [ -f "${cfg}" ] || cfg=/boot/config.txt
    [ -f "${cfg}" ] || { [ "${IGROW_CAN_HAT}" = "off" ] || warn "no config.txt found; cannot manage CAN HAT overlay"; return 0; }

    local begin="# BEGIN IndustryGrow-CAN-HAT (managed by provision.sh)"
    local end="# END IndustryGrow-CAN-HAT"
    local before after
    before="$(sha256sum "${cfg}" | awk '{print $1}')"

    # Remove any previously-managed block (declarative rewrite / cleanup).
    sed -i "/^# BEGIN IndustryGrow-CAN-HAT/,/^# END IndustryGrow-CAN-HAT/d" "${cfg}"

    if [ "${IGROW_CAN_HAT}" = "mcp2515" ]; then
        log "managing MCP2515 HAT overlay in ${cfg} (osc=${IGROW_CAN_HAT_OSC}, can0 int=${IGROW_CAN0_INT}, can1 int=${IGROW_CAN1_INT:-none})"
        {
            echo "${begin}"
            echo "dtparam=spi=on"
            echo "dtoverlay=mcp2515-can0,oscillator=${IGROW_CAN_HAT_OSC},interrupt=${IGROW_CAN0_INT}"
            [ -n "${IGROW_CAN1_INT}" ] && \
              echo "dtoverlay=mcp2515-can1,oscillator=${IGROW_CAN_HAT_OSC},interrupt=${IGROW_CAN1_INT}"
            echo "${end}"
        } >> "${cfg}"
    elif [ "${IGROW_CAN_HAT}" != "off" ]; then
        warn "unknown IGROW_CAN_HAT='${IGROW_CAN_HAT}' (expected 'off' or 'mcp2515'); leaving overlay unmanaged"
    fi

    after="$(sha256sum "${cfg}" | awk '{print $1}')"
    if [ "${before}" != "${after}" ]; then
        REBOOT_REQUIRED=1
        warn "${cfg} changed — REBOOT required for the CAN HAT overlay to take effect (can0/can1 appear after reboot)"
    fi
}

setup_can() {
    # ADR-0002: bring up the configured CAN interface. At bring-up that is vcan0
    # (validation, no bit rate); a physical canN is set to IGROW_CAN_BITRATE
    # (fixed 500 kbit/s, ADR-0002 rev 3 d8) by can-up.sh. The bit rate has ONE home
    # (gateway.env), not a literal per interface.
    log "installing CAN bring-up unit (iface=${IGROW_CAN_IFACE}, physical bitrate=${IGROW_CAN_BITRATE})"
    # Retire the old single-purpose vcan0.service if a previous deploy left it.
    if [ -e /etc/systemd/system/vcan0.service ]; then
        systemctl disable --now vcan0.service 2>/dev/null || true
        rm -f /etc/systemd/system/vcan0.service
    fi
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-can.service" \
        /etc/systemd/system/industrygrow-can.service
    systemctl daemon-reload
    systemctl enable industrygrow-can.service
    systemctl restart industrygrow-can.service   # re-apply iface/bitrate on re-run
    if ip link show "${IGROW_CAN_IFACE}" >/dev/null 2>&1; then
        log "${IGROW_CAN_IFACE} is up"
    else
        warn "${IGROW_CAN_IFACE} not up — check 'journalctl -u industrygrow-can' (physical HAT/overlay present?)"
    fi
}

install_gateway_service() {
    log "installing gateway-pycyphal.service (runs as 'gateway', hardened)"
    install -m 0644 "${FILES_DIR}/systemd/gateway-pycyphal.service" \
        /etc/systemd/system/gateway-pycyphal.service
    systemctl daemon-reload
    systemctl enable gateway-pycyphal.service
    # restart (not just enable --now) so a re-run picks up updated unit/app code.
    systemctl restart gateway-pycyphal.service
}

harden_ssh() {
    # ADR-0004 d2 — key-only, no root. BRING-UP: sshd stays ENABLED (do not disable).
    # The drop-in is installed as 00- so it is read BEFORE cloud-init's
    # 50-cloud-init.conf (which sets PasswordAuthentication yes); sshd is
    # first-value-wins, so the lower number wins.
    log "applying sshd hardening drop-in (00-, key-only, no root login)"
    install -d -m 0755 /etc/ssh/sshd_config.d
    rm -f /etc/ssh/sshd_config.d/99-industrygrow-hardening.conf   # retire stale name
    install -m 0644 "${FILES_DIR}/sshd/00-industrygrow-hardening.conf" \
        /etc/ssh/sshd_config.d/00-industrygrow-hardening.conf
    if sshd -t; then
        systemctl reload ssh 2>/dev/null || systemctl reload sshd 2>/dev/null || true
        log "sshd config valid and reloaded"
    else
        die "sshd config test FAILED — not reloading (you would risk lockout)"
    fi
}

setup_fail2ban() {
    # ADR-0004 d3
    log "configuring fail2ban (strict SSH thresholds)"
    install -m 0644 "${FILES_DIR}/fail2ban/jail.local" /etc/fail2ban/jail.local
    systemctl enable fail2ban
    systemctl restart fail2ban
}

setup_unattended() {
    # ADR-0004 d4 — security patches; reboot time configurable, NOT hardcoded.
    log "configuring unattended-upgrades"
    install -m 0644 "${FILES_DIR}/apt/52-industrygrow-unattended-upgrades" \
        /etc/apt/apt.conf.d/52-industrygrow-unattended-upgrades
    local f=/etc/apt/apt.conf.d/52-industrygrow-unattended-upgrades
    if [ -n "${IGROW_UNATTENDED_REBOOT_TIME}" ]; then
        warn "enabling automatic reboot at ${IGROW_UNATTENDED_REBOOT_TIME} (photoperiod-off window must be confirmed — ADR-0004 d4)"
        sed -i 's/^Unattended-Upgrade::Automatic-Reboot .*/Unattended-Upgrade::Automatic-Reboot "true";/' "$f"
        sed -i "s#^// Unattended-Upgrade::Automatic-Reboot-Time.*#Unattended-Upgrade::Automatic-Reboot-Time \"${IGROW_UNATTENDED_REBOOT_TIME}\";#" "$f"
    else
        log "automatic reboot left DISABLED (IGROW_UNATTENDED_REBOOT_TIME unset — photoperiod undefined)"
    fi
    systemctl enable unattended-upgrades
    systemctl restart unattended-upgrades || true
}

setup_firewall() {
    # ADR-0004 d5/d6 — default-deny inbound except SSH on LAN; egress OPEN at bring-up.
    log "rendering + installing nftables ruleset (egress open at bring-up; prod=egress-locked)"
    local rendered=/etc/nftables.conf
    sed -e "s/^define LAN_IFACE = .*/define LAN_IFACE = \"${IGROW_LAN_IFACE}\"/" \
        -e "s/^define SSH_PORT = .*/define SSH_PORT = ${IGROW_SSH_PORT}/" \
        "${FILES_DIR}/nftables/industrygrow-gateway.nft" > "${rendered}"
    chmod 0644 "${rendered}"
    nft -c -f "${rendered}" || die "nftables ruleset failed validation — not loading"
    systemctl enable nftables
    systemctl restart nftables
}

setup_journald() {
    # ADR-0004 d11
    log "configuring persistent, size-limited journald (SystemMaxUse=100M)"
    install -d -m 0755 /etc/systemd/journald.conf.d
    install -m 0644 "${FILES_DIR}/journald/99-industrygrow.conf" \
        /etc/systemd/journald.conf.d/99-industrygrow.conf
    systemctl restart systemd-journald
}

summary() {
    log "provisioning complete. Verify (see Manual 'Verification'):"
    cat <<'EOF'
    ip -details link show vcan0
    systemctl --no-pager status gateway-pycyphal.service
    journalctl -u gateway-pycyphal.service -n 20 --no-pager
    sshd -T | grep -Ei 'passwordauthentication|permitrootlogin|pubkeyauthentication'
    nft list ruleset
    fail2ban-client status sshd
    journalctl --disk-usage
EOF
    if [ "${REBOOT_REQUIRED}" = "1" ]; then
        warn "REBOOT REQUIRED: the CAN HAT overlay changed. Run 'sudo reboot', then"
        warn "  set IGROW_CAN_IFACE=can0 in ${CONFIG_DIR}/gateway.env and re-run to use the physical bus."
    fi
}

main() {
    require_root
    preflight
    load_env
    apt_base
    create_service_user
    setup_dirs
    setup_venv
    setup_can_hat
    setup_can
    install_gateway_service
    harden_ssh
    setup_fail2ban
    setup_unattended
    setup_firewall
    setup_journald
    summary
}

main "$@"
