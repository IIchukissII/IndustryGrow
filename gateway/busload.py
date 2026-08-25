#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Exact Cyphal/CAN bus load, measured from the wire.

Counts real bits, not an estimate: for every frame it rebuilds the extended-frame
bit sequence, computes the CAN CRC-15 over it, applies the stuffing rule, and adds
the 13 fixed-form bits (CRC delimiter, ACK slot, ACK delimiter, EOF, IFS). Nothing
here is a worst-case bound, so the result can be compared against the headroom
ADR-0002 decision 8 claims for the 500 kbit/s rate.

Run on the gateway, by hand, against a live bus:

    python3 busload.py [seconds]        # default 60

Reports total load, load per source Node-ID, and load per subject. Interpreting it:
one 1 Hz two-frame telemetry subject costs about 0.054 % at 500 kbit/s.
"""

from __future__ import annotations

import socket
import struct
import sys
import time
from collections import defaultdict

# struct can_frame: can_id, can_dlc, 3 pad, 8 data.
CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)

CAN_EFF_MASK = 0x1FFFFFFF
FLAG_SERVICE_NOT_MESSAGE = 1 << 25
NODE_ID_MASK = 0x7F
SUBJECT_ID_MASK = 0x1FFF

BITRATE = 500_000  # ADR-0002 decision 8; a single fixed rate, no negotiation.
CRC15_POLY = 0x4599

# Bits that follow the CRC sequence and are never stuffed:
# CRC delimiter, ACK slot, ACK delimiter, EOF (7), IFS (3).
FIXED_FORM_BITS = 13

FIXED_PORT_NAMES = {
    7168: "time sync",
    7509: "heartbeat",
    7510: "port.List",
    8184: "diagnostic",
    -1: "service",
}


def crc15(bits: list[int]) -> int:
    """CAN CRC-15 over the unstuffed bit sequence from SOF through the data field."""
    crc = 0
    for bit in bits:
        inverted = bit ^ ((crc >> 14) & 1)
        crc = (crc << 1) & 0x7FFF
        if inverted:
            crc ^= CRC15_POLY
    return crc


def frame_bits(can_id: int, data: bytes) -> int:
    """Exact bit count for one extended data frame, stuffing included."""
    bits = [0]  # SOF
    bits += [(can_id >> i) & 1 for i in range(28, 17, -1)]  # ID[28:18]
    bits += [1, 1]  # SRR, IDE
    bits += [(can_id >> i) & 1 for i in range(17, -1, -1)]  # ID[17:0]
    bits += [0, 0, 0]  # RTR, r1, r0
    bits += [(len(data) >> i) & 1 for i in range(3, -1, -1)]  # DLC
    for byte in data:
        bits += [(byte >> i) & 1 for i in range(7, -1, -1)]

    crc = crc15(bits)
    bits += [(crc >> i) & 1 for i in range(14, -1, -1)]

    # Stuffing applies from SOF through the CRC sequence. An inserted bit is the
    # opposite of the run that triggered it, so it restarts the count.
    stuffed = 0
    run = 1
    for i in range(1, len(bits)):
        if bits[i] == bits[i - 1]:
            run += 1
            if run == 5:
                stuffed += 1
                run = 1
        else:
            run = 1

    return len(bits) + stuffed + FIXED_FORM_BITS


def capture(iface: str, window: float) -> tuple[float, dict, dict, dict, dict]:
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    sock.bind((iface,))
    sock.settimeout(2.0)

    bits_by_src: dict[int, int] = defaultdict(int)
    frames_by_src: dict[int, int] = defaultdict(int)
    bits_by_subj: dict[int, int] = defaultdict(int)
    frames_by_subj: dict[int, int] = defaultdict(int)

    start = time.monotonic()
    end = start + window
    while time.monotonic() < end:
        try:
            raw = sock.recv(CAN_FRAME_SIZE)
        except OSError:
            continue
        can_id, dlc, payload = struct.unpack(CAN_FRAME_FMT, raw)
        eff = can_id & CAN_EFF_MASK
        nbits = frame_bits(eff, payload[:dlc])
        src = eff & NODE_ID_MASK
        is_service = bool(eff & FLAG_SERVICE_NOT_MESSAGE)
        subj = -1 if is_service else (eff >> 8) & SUBJECT_ID_MASK
        bits_by_src[src] += nbits
        frames_by_src[src] += 1
        bits_by_subj[subj] += nbits
        frames_by_subj[subj] += 1
    sock.close()

    elapsed = time.monotonic() - start
    return elapsed, bits_by_src, frames_by_src, bits_by_subj, frames_by_subj


def main(argv: list[str]) -> int:
    iface = "can0"
    window = 60.0
    if len(argv) >= 1:
        window = float(argv[0])
    if len(argv) >= 2:
        iface = argv[1]

    elapsed, bits_src, frames_src, bits_subj, frames_subj = capture(iface, window)
    total_bits = sum(bits_src.values())
    total_frames = sum(frames_src.values())
    if total_frames == 0:
        print(f"no traffic on {iface} in {elapsed:.1f} s")
        return 1

    load = total_bits / elapsed / BITRATE * 100
    print(f"window {elapsed:.1f} s   frames {total_frames}   bits {total_bits}")
    print(
        f"BUS LOAD: {load:.3f} % of {BITRATE // 1000} kbit/s   "
        f"({total_bits / elapsed / 1000:.2f} kbit/s, {total_frames / elapsed:.1f} frames/s)"
    )

    print("\nby source node:")
    for src in sorted(bits_src):
        b = bits_src[src]
        print(
            f"  node {src:3d}: {frames_src[src] / elapsed:6.1f} fps  "
            f"{b / elapsed / 1000:7.2f} kbit/s  {b / elapsed / BITRATE * 100:6.3f} %"
        )

    print("\nby subject (top 12):")
    for subj in sorted(bits_subj, key=lambda k: -bits_subj[k])[:12]:
        b = bits_subj[subj]
        name = FIXED_PORT_NAMES.get(subj, "")
        print(
            f"  subj {subj:5d} {name:11s}: {frames_subj[subj] / elapsed:6.1f} fps  "
            f"{b / elapsed / BITRATE * 100:6.3f} %"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
