#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
#
# Bring the configured SocketCAN interface up or down. Driven by IGROW_CAN_IFACE
# and IGROW_CAN_BITRATE from /etc/industrygrow/gateway.env (passed in by the
# industrygrow-can.service EnvironmentFile). Idempotent.
#
#   vcan*  — virtual CAN: software loopback, carries NO bit rate. Used at bring-up
#            to validate the stack before any physical hardware (ADR-0002 rev 3).
#   canN   — physical CAN: brought up at the FIXED 500 kbit/s classic-CAN rate
#            (ADR-0002 rev 3 d8 — a single fixed rate, no negotiation). The HAT and
#            its device-tree overlay must already be present so the device exists.
set -euo pipefail

IFACE="${IGROW_CAN_IFACE:-vcan0}"
BITRATE="${IGROW_CAN_BITRATE:-500000}"
action="${1:-up}"

is_virtual() { [ "${IFACE#vcan}" != "${IFACE}" ]; }

case "${action}" in
  up)
    if is_virtual; then
      modprobe vcan 2>/dev/null || true
      ip link show "${IFACE}" >/dev/null 2>&1 || ip link add dev "${IFACE}" type vcan
      ip link set up dev "${IFACE}"
    else
      if ip link show "${IFACE}" >/dev/null 2>&1; then
        ip link set down dev "${IFACE}" 2>/dev/null || true
        ip link set up dev "${IFACE}" type can bitrate "${BITRATE}"
      else
        echo "can-up: physical CAN '${IFACE}' not present (HAT / device-tree overlay missing?) — skipping" >&2
      fi
    fi
    ;;
  down)
    ip link set down dev "${IFACE}" 2>/dev/null || true
    is_virtual && ip link delete dev "${IFACE}" 2>/dev/null || true
    ;;
  *)
    echo "usage: can-up.sh {up|down}" >&2
    exit 2
    ;;
esac
