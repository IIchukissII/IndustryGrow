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

# Where the DSDL namespaces are, in the two ways this script is run: from the
# bundle deploy.ps1 stages (which carries them next to provision.sh, because the
# firmware tree they live in is not part of gateway/), or straight from a repo
# checkout. The definitions themselves are never copied into gateway/ — one tree,
# staged at deploy time.
default_dsdl_roots() {
    if [ -d "${SCRIPT_DIR}/dsdl/uavcan" ] && [ -d "${SCRIPT_DIR}/dsdl/industryflow" ]; then
        printf '%s %s' "${SCRIPT_DIR}/dsdl/uavcan" "${SCRIPT_DIR}/dsdl/industryflow"
        return
    fi
    printf '%s %s'         "${SCRIPT_DIR}/../firmware/third_party/public_regulated_data_types/uavcan"         "${SCRIPT_DIR}/../firmware/dsdl/industryflow"
}

# Load defaults, then the installed env file (operator edits win).
load_env() {
    IGROW_SSH_IFACES="wlan0 eth0 end0"  # mgmt NICs SSH may arrive on (a set)
    IGROW_LAN_IFACE=""   # empty = auto-detect from the default route (below)
    IGROW_SSH_PORT="22"
    IGROW_F2B_IGNOREIP=""  # empty = derive the mgmt LAN subnets (below)
    IGROW_CAN_IFACE="vcan0"
    IGROW_CAN_BITRATE="500000"
    IGROW_CAN_HAT="off"
    IGROW_CAN_HAT_OSC="16000000"
    IGROW_CAN0_INT="23"   # Waveshare 2-CH Isolated HAT: INT0 -> GPIO23
    IGROW_CAN1_INT="25"   # Waveshare 2-CH Isolated HAT: INT1 -> GPIO25
    IGROW_CAN_HAT_SPI_MAXFREQ="1000000"
    IGROW_UNATTENDED_REBOOT_TIME=""
    IGROW_PERSISTENT_BUFFER="off"
    # DSDL root namespaces compiled at provisioning time (ADR-0005 d10: generated
    # code is not vendored). Empty = resolved by default_dsdl_roots below.
    IGROW_DSDL_ROOTS=""
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

    # An empty value in gateway.env means "keep the default", as for IGROW_LAN_IFACE
    # below: systemd's EnvironmentFile passes an empty assignment through, so the
    # template can name every variable without a blank one disabling a step.
    [ -n "${IGROW_DSDL_ROOTS}" ] || IGROW_DSDL_ROOTS="$(default_dsdl_roots)"

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
        warn "configured LAN iface '${IGROW_LAN_IFACE}' is not the current default-route iface '${detected}'; honoring the explicit setting — SSH will also be allowed on '${IGROW_LAN_IFACE}'"
    fi

    # SSH is allowed on the SET of LAN-facing management interfaces, not one baked
    # name (ADR-0004 d5, as amended). Default covers every way the Pi is reached —
    # Wi-Fi (wlan0), USB/legacy Ethernet (eth0), Pi 5 onboard (end0). The live
    # default-route iface and any explicit IGROW_LAN_IFACE are always unioned in,
    # so the current management path can never be excluded (lockout-safe).
    local ifaces="${IGROW_SSH_IFACES:-wlan0 eth0 end0} ${IGROW_LAN_IFACE} ${detected}"
    # Dedup, drop empties, and render as an nftables set: { "a", "b", "c" }.
    local set_body
    set_body="$(printf '%s\n' ${ifaces} | awk 'NF' | sort -u | sed 's/.*/"&"/' | paste -sd, - | sed 's/,/, /g')"
    IGROW_SSH_IFACES_NFT="{ ${set_body} }"

    # fail2ban must not be able to ban the operator off the management LAN
    # (ADR-0004 d3, as amended). Take the directly-attached subnet of each
    # management NIC from the kernel's own link routes, so this tracks whatever
    # the LAN actually is rather than a baked subnet. A NIC that is down
    # contributes nothing, which is correct — it is not a path to anywhere.
    local nets="" iface
    for iface in $(printf '%s\n' ${ifaces} | awk 'NF' | sort -u); do
        # `|| true` is load-bearing under `set -euo pipefail`: a name in the
        # default set that this host does not have (end0 on a board whose onboard
        # NIC is eth0) makes `ip` exit non-zero, pipefail propagates it, and the
        # assignment aborts provisioning before a single step has run.
        # Contributing nothing is the intended answer for such a NIC.
        nets="${nets} $(ip -4 route show dev "${iface}" proto kernel scope link \
            2>/dev/null | awk '{print $1}' || true)"
    done
    IGROW_F2B_IGNOREIP_RENDERED="$(printf '%s\n' 127.0.0.1/8 ::1 \
        ${IGROW_F2B_IGNOREIP} ${nets} | awk 'NF' | sort -u | paste -sd' ' -)"

    log "config: SSH ifaces=${IGROW_SSH_IFACES_NFT} CAN=${IGROW_CAN_IFACE} buffer=${IGROW_PERSISTENT_BUFFER}"
    log "config: fail2ban ignoreip=${IGROW_F2B_IGNOREIP_RENDERED}"
}

apt_base() {
    log "installing base packages (no Docker — single systemd service per ADR-0004)"
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq
    apt-get install -y --no-install-recommends \
        python3-venv python3-full \
        nftables fail2ban unattended-upgrades \
        can-utils iproute2 ca-certificates         sqlite3
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
    install -m 0644 "${FILES_DIR}/app/gateway_timesync.py" "${APP_DIR}/gateway_timesync.py"
    install -m 0644 "${FILES_DIR}/app/gateway_telemetry.py" "${APP_DIR}/gateway_telemetry.py"
    install -m 0644 "${FILES_DIR}/app/igrow_subjects.py" "${APP_DIR}/igrow_subjects.py"
    install -m 0755 "${FILES_DIR}/app/can-up.sh" "${APP_DIR}/can-up.sh"
    # The profile client lives beside provision.sh rather than under files/app,
    # because it is also run by hand from a checkout (`show`, `once`). It needs no
    # dependency beyond the stdlib and the openssl already installed.
    install -m 0644 "${SCRIPT_DIR}/profile_client.py" "${APP_DIR}/profile_client.py"
    # Node-ID provisioning (ADR-0027 d5/d7) is an operator action, never automatic,
    # so it is installed beside the app and run by hand -- no unit, no timer.
    install -m 0644 "${SCRIPT_DIR}/provision_node_id.py" "${APP_DIR}/provision_node_id.py"
    # Bench commands (each module specification's section 10). Also by hand, and
    # it borrows the transport session above, so the two install together.
    install -m 0644 "${SCRIPT_DIR}/node_command.py" "${APP_DIR}/node_command.py"
    # The firmware client (ADR-0029 d14-d17). Installed beside the two above
    # because it imports both: provision_node_id for the transfer-ID retry and
    # profile_client for the machine identity and the mTLS paths.
    install -m 0644 "${SCRIPT_DIR}/firmware_client.py" "${APP_DIR}/firmware_client.py"
    # Where the identity and the profile-verification key live (ADR-0025 d10).
    # provision_identity.py writes the first two; the third is the operator's
    # public key, copied here during commissioning.
    install -d -m 0755 -o root -g gateway "${CONFIG_DIR}/pki"
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

setup_dsdl() {
    # Nunavut runs through pycyphal, so this needs the venv from setup_venv.
    local out="${APP_DIR}/dsdl"
    local roots=() r
    for r in ${IGROW_DSDL_ROOTS}; do
        if [ -d "${r}" ]; then roots+=("${r}"); else warn "DSDL namespace not found: ${r}"; fi
    done
    if [ "${#roots[@]}" -eq 0 ]; then
        warn "no DSDL namespaces found — gateway-pycyphal.service will not start."
        warn "copy firmware/dsdl and firmware/third_party/public_regulated_data_types"
        warn "alongside gateway/, or set IGROW_DSDL_ROOTS in ${CONFIG_DIR}/gateway.env"
        return 0
    fi
    log "compiling DSDL namespaces into ${out}"
    # Regenerated whole rather than merged: a stale package for a type that has
    # since changed is the one failure this step exists to prevent.
    rm -rf "${out}"
    install -d -m 0755 "${out}"
    "${VENV_DIR}/bin/python" - "${out}" "${roots[@]}" <<'PYDSDL'
import sys
import pycyphal.dsdl

out, *roots = sys.argv[1:]
pycyphal.dsdl.compile_all(roots, output_directory=out)
PYDSDL
    chmod -R a+rX "${out}"
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
        # spimaxfrequency caps the SPI clock to the MCP2515. REQUIRED on the Pi 5
        # (RP1): the default clock is too fast, so the reads that clear the INT flag
        # get corrupted and the interrupt storms — the driver then never surfaces
        # received frames (RX dead, though init/TX look fine). 1 MHz is reliable.
        local spi_suffix=""
        [ -n "${IGROW_CAN_HAT_SPI_MAXFREQ}" ] && \
          spi_suffix=",spimaxfrequency=${IGROW_CAN_HAT_SPI_MAXFREQ}"
        log "managing MCP2515 HAT overlay in ${cfg} (osc=${IGROW_CAN_HAT_OSC}, can0 int=${IGROW_CAN0_INT}, can1 int=${IGROW_CAN1_INT:-none}, spimaxfreq=${IGROW_CAN_HAT_SPI_MAXFREQ:-default})"
        {
            echo "${begin}"
            echo "dtparam=spi=on"
            echo "dtoverlay=mcp2515-can0,oscillator=${IGROW_CAN_HAT_OSC},interrupt=${IGROW_CAN0_INT}${spi_suffix}"
            [ -n "${IGROW_CAN1_INT}" ] && \
              echo "dtoverlay=mcp2515-can1,oscillator=${IGROW_CAN_HAT_OSC},interrupt=${IGROW_CAN1_INT}${spi_suffix}"
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
    log "installing gateway-pycyphal.service (telemetry consumer, runs as 'gateway', hardened)"
    install -m 0644 "${FILES_DIR}/systemd/gateway-pycyphal.service" \
        /etc/systemd/system/gateway-pycyphal.service
    systemctl daemon-reload
    systemctl enable gateway-pycyphal.service
    # restart (not just enable --now) so a re-run picks up updated unit/app code.
    systemctl restart gateway-pycyphal.service
}

install_timesync_service() {
    # The bus time master (ADR-0002 rev 3 d11). Separate unit from
    # gateway-pycyphal.service on purpose — see the unit file's header: the nodes'
    # time base must not go stale because the telemetry consumer is restarting.
    log "installing industrygrow-timesync.service (Cyphal time master, subject 7168)"
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-timesync.service"         /etc/systemd/system/industrygrow-timesync.service
    systemctl daemon-reload
    systemctl enable industrygrow-timesync.service
    # restart (not just enable --now) so a re-run picks up updated unit/app code.
    systemctl restart industrygrow-timesync.service
}

install_profile_pull() {
    # The ADR-0015 d5 profile poll. Separate from gateway-pycyphal.service because
    # that unit keeps /etc/industrygrow read-only and this one has to write the
    # active profile into it (see the unit's own comment).
    log "installing industrygrow-profile-pull.service + .timer (ADR-0015 d5, ADR-0025)"
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-profile-pull.service" \
        /etc/systemd/system/industrygrow-profile-pull.service
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-profile-pull.timer" \
        /etc/systemd/system/industrygrow-profile-pull.timer
    systemctl daemon-reload

    # Enable the TIMER, not the service: the service is oneshot and the timer is
    # what gives it a cadence. Enabling the service would run one pull at boot and
    # never again.
    #
    # Left STOPPED unless the gateway has what a pull needs. Starting the timer on
    # a unit with no ERP URL, no certificate, or no verification key would produce
    # a failed pull every minute in the journal and teach the operator to ignore it.
    if [ -n "${IGROW_ERP_URL:-}" ]; then
        systemctl enable --now industrygrow-profile-pull.timer
        log "profile pull enabled against ${IGROW_ERP_URL}"
    else
        systemctl enable industrygrow-profile-pull.timer
        systemctl stop industrygrow-profile-pull.timer 2>/dev/null || true
        warn "IGROW_ERP_URL is empty — profile pull installed but NOT started. Set it in
       ${CONFIG_DIR}/gateway.env, provision an identity (provision_identity.py) and the
       operator's profile-verification key, then: systemctl start industrygrow-profile-pull.timer"
    fi
}

install_firmware_update() {
    # The ADR-0029 d16 loop: the ERP holds the intended release, the gateway
    # compares it against what each node reports and performs the transfer.
    # Separate from gateway-pycyphal.service because that unit takes no Node-ID
    # and this one must be addressable for a whole transfer (see the unit header).
    log "installing industrygrow-firmware.service + .timer (ADR-0029 d14-d17)"
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-firmware.service" \
        /etc/systemd/system/industrygrow-firmware.service
    install -m 0644 "${FILES_DIR}/systemd/industrygrow-firmware.timer" \
        /etc/systemd/system/industrygrow-firmware.timer
    systemctl daemon-reload

    # The TIMER, not the service — same reason as the profile pull: the service
    # is oneshot and would otherwise run once at boot and never again.
    #
    # Two gates stand between this and a node being written, and both are outside
    # this script. Without IGROW_ERP_URL the timer stays stopped here; with it,
    # nothing is transferred until an operator selects a release for this machine
    # in the console, which is the operator action ADR-0029 d16 requires.
    if [ -n "${IGROW_ERP_URL:-}" ]; then
        systemctl enable --now industrygrow-firmware.timer
        log "firmware update enabled against ${IGROW_ERP_URL}"
    else
        systemctl enable industrygrow-firmware.timer
        systemctl stop industrygrow-firmware.timer 2>/dev/null || true
        warn "IGROW_ERP_URL is empty — firmware update installed but NOT started. Set it in
       ${CONFIG_DIR}/gateway.env, provision an identity (provision_identity.py), then:
       systemctl start industrygrow-firmware.timer"
    fi
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
    # ADR-0004 d3 — strict thresholds everywhere except the management LAN,
    # which is rendered in the same way the nftables SSH set is (setup_firewall).
    log "configuring fail2ban (strict SSH thresholds; ignoreip=${IGROW_F2B_IGNOREIP_RENDERED})"
    sed -e "s|^ignoreip .*|ignoreip = ${IGROW_F2B_IGNOREIP_RENDERED}|" \
        "${FILES_DIR}/fail2ban/jail.local" > /etc/fail2ban/jail.local
    chmod 0644 /etc/fail2ban/jail.local
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
    sed -e "s|^define SSH_IFACES = .*|define SSH_IFACES = ${IGROW_SSH_IFACES_NFT}|" \
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
    systemctl --no-pager status industrygrow-timesync.service
    systemctl list-timers industrygrow-firmware.timer --no-pager
    sudo -u gateway /opt/industrygrow/venv/bin/python /opt/industrygrow/firmware_client.py once --dry-run
    candump <iface> 041C0000:1FFFFF00   # the time-sync frame (subject 7168)
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
    setup_dsdl
    setup_can_hat
    setup_can
    install_gateway_service
    install_timesync_service
    install_profile_pull
    install_firmware_update
    harden_ssh
    setup_fail2ban
    setup_unattended
    setup_firewall
    setup_journald
    summary
}

main "$@"
