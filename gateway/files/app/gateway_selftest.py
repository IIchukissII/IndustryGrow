#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
"""Bring-up self-test PLACEHOLDER for the IndustryGrow gateway service.

This is NOT the final Pycyphal gateway application. The real service depends on
the DSDL vocabulary (ADR-0005, planned; roadmap stages 2+) and the control/upload
logic (ADR-0015, ADR-0004 rev 1 d10). This placeholder exists so the systemd unit
`gateway-pycyphal.service` has something genuine to run during bring-up, proving:

  1. the venv and `pycyphal` import work under the unprivileged `gateway` user;
  2. the least-privilege sandbox (ADR-0004 rev 1 d7) does not block what the
     service legitimately needs;
  3. SocketCAN is reachable via an AF_CAN socket on the configured interface —
     the access path the sandbox grants through RestrictAddressFamilies=AF_CAN
     (NOT a /dev node / DeviceAllow=). On vcan0 this is a pure software loopback
     (ADR-0002 rev 3: validate before physical hardware).

Run modes:
  (default)  run the checks once and exit 0/1 (handy for a manual check).
  --serve    run the checks, then idle with a periodic journal heartbeat so the
             service stays "active (running)" and Restart= is meaningful.
"""
from __future__ import annotations

import argparse
import os
import socket
import struct
import sys
import time

CAN_IFACE = os.environ.get("IGROW_CAN_IFACE", "vcan0")

# struct layout of a classic CAN frame as used by SocketCAN's CAN_RAW.
_CAN_FRAME_FMT = "=IB3x8s"  # can_id, can_dlc, pad(3), data(8)


def _log(msg: str) -> None:
    # stdout/stderr are captured by journald for this unit.
    print(f"[gateway-selftest] {msg}", flush=True)


def check_pycyphal() -> bool:
    try:
        import pycyphal  # noqa: F401

        _log(f"pycyphal import OK (version {getattr(pycyphal, '__version__', '?')})")
        return True
    except Exception as exc:  # pragma: no cover - environment dependent
        _log(f"pycyphal import FAILED: {exc!r}")
        return False


def check_socketcan_loopback(iface: str) -> bool:
    """Open an AF_CAN raw socket on `iface` and loopback one frame.

    Version-independent of the Pycyphal presentation layer on purpose: this
    isolates the security-relevant fact (AF_CAN works under the sandbox) from the
    DSDL stack that does not exist yet.
    """
    try:
        sock = socket.socket(socket.PF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    except (AttributeError, OSError) as exc:
        _log(f"AF_CAN socket unavailable: {exc!r}")
        return False
    try:
        sock.bind((iface,))
    except OSError as exc:
        _log(f"bind to {iface!r} FAILED ({exc!r}); is {iface} up? (industrygrow-can.service)")
        sock.close()
        return False

    try:
        # SocketCAN does NOT deliver a socket its OWN sent frames unless this is
        # set. Without it a single-socket loopback recv() blocks forever (it only
        # ever sees OTHER sockets' frames). Required for a deterministic one-socket
        # self-test on vcan0.
        sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_RECV_OWN_MSGS, 1)
        sock.settimeout(2.0)
        payload = b"IGROW\x00\x00\x00"
        frame = struct.pack(_CAN_FRAME_FMT, 0x123, len(b"IGROW"), payload)
        sock.send(frame)
        # vcan loops sent frames back to bound readers on the same iface.
        data = sock.recv(16)
        can_id, dlc, recv = struct.unpack(_CAN_FRAME_FMT, data)
        _log(f"SocketCAN loopback OK on {iface}: id=0x{can_id:03X} dlc={dlc} "
             f"data={recv[:dlc]!r}")
        return True
    except socket.timeout:
        _log(f"SocketCAN loopback TIMEOUT on {iface} (fault on vcan0; on a real "
             f"can0 a missing ACK with no other node can also time out)")
        return False
    except OSError as exc:
        _log(f"SocketCAN loopback FAILED on {iface}: {exc!r}")
        return False
    finally:
        sock.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="IndustryGrow gateway bring-up self-test")
    parser.add_argument("--serve", action="store_true",
                        help="idle with a heartbeat after the checks pass")
    args = parser.parse_args()

    _log(f"starting; CAN interface = {CAN_IFACE}")
    ok = check_pycyphal()
    ok = check_socketcan_loopback(CAN_IFACE) and ok

    if not ok:
        _log("self-test FAILED")
        return 1
    _log("self-test PASSED")

    if args.serve:
        _log("entering idle heartbeat loop (placeholder for the real service)")
        while True:
            time.sleep(60)
            _log("heartbeat: gateway placeholder alive")
    return 0


if __name__ == "__main__":
    sys.exit(main())
