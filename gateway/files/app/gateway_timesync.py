#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: CC-BY-SA-4.0
"""IndustryGrow gateway time-synchronization master (ADR-0002 rev 3 d11).

Publishes `uavcan.time.Synchronization` (fixed subject 7168) at 1 Hz so every
node on the bus shares one time base. Without it each node stamps its telemetry
`UNKNOWN` (0), because a node's own uptime is not a network time base and two
nodes' uptimes share no origin -- which makes cross-node sample alignment, what
ADR-0016's state estimator does, silently wrong.

The algorithm is the one specified in `7168.Synchronization.1.0`: each message
carries the transmit timestamp of the PREVIOUS message, so a slave learns the
offset from a consecutive pair. That is why this publishes 0 in its first
message and again after any gap longer than one publication period -- the field
is defined to be zero when no valid previous transmission exists.

WHY A RAW SOCKET AND NOT PYCYPHAL. The payload is the transmit timestamp of the
previous frame, so the master needs the moment its own frame reached the wire.
SocketCAN surfaces that as the kernel timestamp on the loopback echo of a frame
the socket itself sent (CAN_RAW_RECV_OWN_MSGS + SO_TIMESTAMPNS). Reaching that
echo means owning the socket. The message is a single 8-byte frame with a fixed
port-ID, so the framing this hand-builds is a CAN ID and a tail byte, not a
transport.

ACCURACY. The echo is timestamped when the driver completes transmission, not at
start-of-frame, and on the reference gateway the controller is an MCP2515 across
SPI (ADR-0002 d6). The result is milliseconds, and ADR-0002 d11 claims no more
than that. It is enough to give samples from different nodes a shared origin;
it is not enough for any finer claim.

NODE-ID. The dominant master is the master with the LOWEST Node-ID. The gateway
holds an ID below the node range (ADR-0002 d11), so election is decided without
a second master ever existing.

Run modes:
  (default)  publish until stopped.
  --once     publish two messages and exit -- the pair a slave needs to
             synchronize. For a manual check.
"""

from __future__ import annotations

import argparse
import os
import socket
import struct
import sys
import time

# --- Cyphal/CAN framing ------------------------------------------------------
# Message frame CAN ID (Cyphal/CAN v1.0):
#   bits 28..26 priority | bit 25 service-not-message = 0 | bit 24 anonymous = 0
#   bits 20..8  subject-ID (13 bits)                     | bits 6..0 source Node-ID
# Bits 23 and 7 are reserved and MUST be zero -- a receiver rejects the frame
# otherwise, so they are not merely unused here.
SUBJECT_ID = 7168  # uavcan.time.Synchronization, fixed port-ID
OFFSET_PRIORITY = 26
OFFSET_SUBJECT_ID = 8

# Immediate, one step below Exceptional. Arbitration delay is the error term this
# can actually control, so the message is near the top -- but not at the top,
# which stays free for the safety traffic ADR-0002's determinism driver protects.
PRIORITY_IMMEDIATE = 1

CAN_EFF_FLAG = 0x80000000
CAN_EFF_MASK = 0x1FFFFFFF

# The setsockopt LEVEL for CAN_RAW options is SOL_CAN_RAW (SOL_CAN_BASE + CAN_RAW
# = 101), not the CAN_RAW protocol number 1. Passing 1 silently addresses
# SOL_SOCKET instead, where option 1 is SO_DEBUG -- which needs CAP_NET_ADMIN and
# fails with EACCES rather than with anything that names the real mistake.
SOL_CAN_RAW = getattr(socket, "SOL_CAN_RAW", 101)
CAN_RAW_FILTER = getattr(socket, "CAN_RAW_FILTER", 1)
# 4, not 5. 5 is CAN_RAW_FD_FRAMES, which is accepted without complaint and
# switches the socket to canfd_frame -- so the mistake shows up as "no echo ever
# arrives", never as an error on the call that caused it.
CAN_RAW_RECV_OWN_MSGS = getattr(socket, "CAN_RAW_RECV_OWN_MSGS", 4)

# Single-frame transfer: start-of-transfer, end-of-transfer, and the toggle bit
# in its initial state -- all three set. The low 5 bits are the transfer-ID.
TAIL_SINGLE_FRAME = 0xE0
TRANSFER_ID_MASK = 0x1F

MAX_PUBLICATION_PERIOD = 1.0  # seconds; the type's own LIMIT, not a target

# The actual period. Deliberately under the limit rather than equal to it: a slave
# discards the pair whenever two messages arrive more than MAX_PUBLICATION_PERIOD
# apart, and ordinary scheduling jitter on a non-realtime host pushes a nominal
# 1.000 s period over that line about half the time. Publishing faster than 1 Hz
# is explicitly allowed; publishing at exactly 1 Hz is what breaks.
PUBLICATION_PERIOD = 0.9

# struct can_frame: can_id, can_dlc, 3 pad, 8 data.
_CAN_FRAME_FMT = "=IB3x8s"
_CAN_FRAME_SIZE = struct.calcsize(_CAN_FRAME_FMT)

# SO_TIMESTAMPNS is not exposed by the socket module on every Python build; the
# value is the asm-generic one, shared by aarch64 and armhf.
SO_TIMESTAMPNS = getattr(socket, "SO_TIMESTAMPNS", 35)
SCM_TIMESTAMPNS = SO_TIMESTAMPNS

CAN_IFACE = os.environ.get("IGROW_CAN_IFACE", "vcan0")
NODE_ID = int(os.environ.get("IGROW_CYPHAL_NODE_ID", "1"))


def log(msg: str) -> None:
    """One line to stdout, unbuffered -- systemd captures it into the journal."""
    print(msg, flush=True)


def make_frame(can_id: int, transfer_id: int, previous_tx_usec: int) -> bytes:
    """Build the complete 8-byte single-frame transfer."""
    # truncated uint56, little-endian: the low 7 bytes of the 64-bit value.
    payload = struct.pack("<Q", previous_tx_usec)[:7]
    tail = TAIL_SINGLE_FRAME | (transfer_id & TRANSFER_ID_MASK)
    data = payload + bytes([tail])
    return struct.pack(_CAN_FRAME_FMT, can_id, len(data), data)


def make_can_id(node_id: int) -> int:
    """The CAN ID of every message this publishes -- fixed for the whole run, since
    the transfer-ID lives in the tail byte rather than in the identifier."""
    return (
        (PRIORITY_IMMEDIATE << OFFSET_PRIORITY)
        | (SUBJECT_ID << OFFSET_SUBJECT_ID)
        | node_id
        | CAN_EFF_FLAG
    )


def open_socket(iface: str, can_id: int) -> socket.socket:
    """A raw CAN socket that receives the echo of its own transmissions, stamped.

    The filter admits exactly one identifier: our own. Everything else on the bus
    is node telemetry this process has no use for, and leaving it to accumulate in
    the receive buffer during the one-second sleep risks the buffer overrunning
    and dropping the one frame that matters -- the echo.
    """
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.setsockopt(
        SOL_CAN_RAW, CAN_RAW_FILTER, struct.pack("=II", can_id, CAN_EFF_FLAG | CAN_EFF_MASK)
    )
    sock.setsockopt(SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, 1)
    sock.setsockopt(socket.SOL_SOCKET, SO_TIMESTAMPNS, 1)
    sock.bind((iface,))
    return sock


def drain(sock: socket.socket) -> None:
    """Discard any echo left over from an earlier cycle, so the timestamp read
    after the next send belongs to that send."""
    sock.setblocking(False)
    try:
        while True:
            try:
                sock.recv(_CAN_FRAME_SIZE)
            except (BlockingIOError, InterruptedError):
                return
    finally:
        sock.setblocking(True)


def read_tx_timestamp(sock: socket.socket, can_id: int, deadline: float) -> int | None:
    """Wait for the loopback echo of our own frame and return its kernel timestamp
    in microseconds since the epoch.

    Returns None if the echo does not arrive before `deadline`, or arrives with no
    timestamp attached. A missing timestamp is reported as absent rather than
    substituted with the current time: the next message would then claim a
    transmit moment that never happened, and every slave would fold that error
    into its offset without being able to see it.
    """
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        sock.settimeout(remaining)
        try:
            data, ancdata, _flags, _addr = sock.recvmsg(_CAN_FRAME_SIZE, 1024)
        except TimeoutError:
            return None
        if len(data) < _CAN_FRAME_SIZE:
            continue
        echoed_id, _dlc, _payload = struct.unpack(_CAN_FRAME_FMT, data)
        if echoed_id != can_id:
            continue  # someone else's traffic, or an earlier frame of ours
        for level, ctype, cdata in ancdata:
            if level == socket.SOL_SOCKET and ctype == SCM_TIMESTAMPNS:
                # struct timespec: two longs, 32- or 64-bit per userland.
                if len(cdata) >= 16:
                    sec, nsec = struct.unpack("=qq", cdata[:16])
                elif len(cdata) >= 8:
                    sec, nsec = struct.unpack("=ii", cdata[:8])
                else:
                    continue
                return (sec * 1_000_000) + (nsec // 1000)
        return None


def run(iface: str, node_id: int, limit: int | None) -> int:
    can_id = make_can_id(node_id)
    sock = open_socket(iface, can_id)
    log(
        f"time master on {iface}, Node-ID {node_id}, subject {SUBJECT_ID}, "
        f"every {PUBLICATION_PERIOD:.1f} s"
    )

    transfer_id = 0
    previous_tx_usec = 0
    sent = 0
    next_tick = time.monotonic()

    while limit is None or sent < limit:
        frame = make_frame(can_id, transfer_id, previous_tx_usec)
        drain(sock)
        try:
            sock.send(frame)
        except OSError as exc:
            log(f"send failed on {iface}: {exc}")
            return 1

        # The echo must be read before the next publication, or the timestamp it
        # carries would be stale by a whole period.
        tx_usec = read_tx_timestamp(sock, can_id, time.monotonic() + 0.5)
        if tx_usec is None:
            # The field is defined as zero when there is no valid previous
            # transmission. A slave holding a pair sees the zero, discards the
            # pair and starts a new one -- which is the correct outcome.
            log("no transmit timestamp for the last frame; next message carries 0")
            previous_tx_usec = 0
        else:
            previous_tx_usec = tx_usec

        transfer_id = (transfer_id + 1) & TRANSFER_ID_MASK
        sent += 1

        next_tick += PUBLICATION_PERIOD
        sleep_for = next_tick - time.monotonic()
        if sleep_for < 0:
            # Publishing late is worse than publishing early: past the period the
            # slaves' own timing check rejects the pair. Re-anchor rather than
            # accumulate the lag.
            next_tick = time.monotonic() + PUBLICATION_PERIOD
        elif limit is None or sent < limit:
            time.sleep(sleep_for)

    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iface", default=CAN_IFACE, help="CAN interface (IGROW_CAN_IFACE)")
    ap.add_argument(
        "--node-id",
        type=int,
        default=NODE_ID,
        help="gateway Cyphal Node-ID (IGROW_CYPHAL_NODE_ID); must be the lowest on the bus",
    )
    ap.add_argument(
        "--once",
        action="store_true",
        help="publish two messages -- the pair a slave needs -- and exit",
    )
    args = ap.parse_args(argv)

    if not 0 <= args.node_id <= 127:
        log(f"Node-ID {args.node_id} is outside the Cyphal/CAN range 0..127")
        return 2

    try:
        return run(args.iface, args.node_id, 2 if args.once else None)
    except KeyboardInterrupt:
        return 0
    except OSError as exc:
        log(f"{args.iface}: {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
