#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Node-ID provisioning over the bus — the operator action ADR-0027 leaves to an
operational specification.

ADR-0027 decision 5 makes provisioning a write to the `uavcan.node.id` register,
effective at the next restart; decision 7 keeps allocation out of the gateway, so
which node gets which number stays something a human decided and can audit. This
tool is that human's hands, and nothing about it runs automatically.

    provision_node_id.py list                 # what is on the bus, and what is at 127
    provision_node_id.py show 127
    provision_node_id.py set 127 96 --restart
    provision_node_id.py clear 96 --restart   # back to unprovisioned

WHY A NODE-ID OF ITS OWN. Cyphal forbids anonymous service transfers, so a client
that calls `uavcan.register.Access` must be addressable. The gateway's own ID
belongs to the time master (ADR-0002 d11, the lowest ID on the bus); this takes
the next one up, still far below the node range, and only while it runs.

THE WRITE BLOCKS FOR ABOUT A SECOND. The node erases a flash sector inside the
service call with interrupts masked, so the response comes back late by the
standards of a register read. The client timeout below is sized for that, not for
a healthy round trip — a default 1 s timeout fails every write on a working node.

TWO UNPROVISIONED NODES COLLIDE AT 127, which ADR-0027 d6 states plainly and no
reserved value fixes. Provision them one at a time with only one powered. `list`
and `show` try to detect the condition rather than write into it, but detection
from outside is best-effort: the transport has no tie-break to report.
"""

from __future__ import annotations

import argparse
import asyncio
import itertools
import os
import sys
from typing import Any

UNPROVISIONED = 127
MAX_PROVISIONABLE = 126

# Sized for the sector erase inside the write, not for a round trip.
WRITE_TIMEOUT_S = float(os.environ.get("IGROW_PROVISION_TIMEOUT_S") or "15")
READ_TIMEOUT_S = 3.0

# A short-lived process starts its transfer-ID counter at zero, and a node
# remembers the last transfer-ID it saw from a given source for the transfer-ID
# timeout (2 s). Two invocations of these tools inside that window therefore
# present the same transfer-ID and the second is dropped as a duplicate --
# reproduced on gbox-dev: back-to-back calls fail, the same call five seconds
# later succeeds. A retry costs nothing and uses the next transfer-ID, which the
# node accepts. Every call these tools make is idempotent, so a retry after a
# lost response cannot do a second thing.
ATTEMPTS = 3
CAN_IFACE = os.environ.get("IGROW_CAN_IFACE") or "vcan0"
CLIENT_NODE_ID = int(os.environ.get("IGROW_PROVISION_NODE_ID") or "2")


def log(msg: str) -> None:
    print(msg, flush=True)


async def call_with_retry(client: Any, request: Any) -> Any:
    """Call a service, retrying a silent timeout. See ATTEMPTS."""
    for _ in range(ATTEMPTS):
        got = await client.call(request)
        if got is not None:
            return got
    return None


class Bus:
    """Presentation-layer session for one invocation. Closed on exit."""

    def __init__(self) -> None:
        import pycyphal.presentation
        from pycyphal.transport.can import CANTransport
        from pycyphal.transport.can.media.socketcan import SocketCANMedia

        media = SocketCANMedia(CAN_IFACE, mtu=8)  # classic CAN, ADR-0002 d8
        self._transport = CANTransport(media, local_node_id=CLIENT_NODE_ID)
        self.pres = pycyphal.presentation.Presentation(self._transport)

    def close(self) -> None:
        self.pres.close()

    async def survey(self, seconds: float) -> dict[int, list[int]]:
        """Node-IDs heard, each with the sequence of heartbeat uptimes seen.

        The uptime sequence is the duplicate-at-127 tell: one node counts up, two
        interleave and the value walks backwards.
        """
        import uavcan.node

        seen: dict[int, list[int]] = {}
        sub = self.pres.make_subscriber(uavcan.node.Heartbeat_1_0, 7509)
        try:
            deadline = asyncio.get_running_loop().time() + seconds
            while asyncio.get_running_loop().time() < deadline:
                remaining = deadline - asyncio.get_running_loop().time()
                got = await sub.receive_for(max(remaining, 0.0))
                if got is None:
                    continue
                msg, meta = got
                if meta.source_node_id is not None:
                    seen.setdefault(meta.source_node_id, []).append(int(msg.uptime))
        finally:
            sub.close()
        return seen

    async def get_info(self, node_id: int) -> Any:
        import uavcan.node

        client = self.pres.make_client(uavcan.node.GetInfo_1_0, 430, node_id)
        client.response_timeout = READ_TIMEOUT_S
        try:
            got = await call_with_retry(client, uavcan.node.GetInfo_1_0.Request())
        finally:
            client.close()
        return None if got is None else got[0]

    async def register(
        self,
        node_id: int,
        name: str,
        value: Any = None,
        response_timeout: float = READ_TIMEOUT_S,
    ) -> Any:
        """Read (value=None) or write one register. Returns the Response."""
        import uavcan.register

        req = uavcan.register.Access_1_0.Request(name=uavcan.register.Name_1_0(name=name))
        if value is not None:
            req.value = value
        client = self.pres.make_client(uavcan.register.Access_1_0, 384, node_id)
        client.response_timeout = response_timeout
        try:
            got = await call_with_retry(client, req)
        finally:
            client.close()
        return None if got is None else got[0]

    async def restart(self, node_id: int) -> bool:
        import uavcan.node

        client = self.pres.make_client(uavcan.node.ExecuteCommand_1_0, 435, node_id)
        client.response_timeout = READ_TIMEOUT_S
        try:
            got = await call_with_retry(
                client,
                uavcan.node.ExecuteCommand_1_0.Request(
                    command=uavcan.node.ExecuteCommand_1_0.Request.COMMAND_RESTART
                ),
            )
        finally:
            client.close()
        return got is not None and got[0].status == 0


def read_node_id(resp: Any) -> int | None:
    """The natural16 in a register response, or None if it is not one."""
    if resp is None:
        return None
    v = resp.value
    if v.natural16 is None or len(v.natural16.value) < 1:
        return None
    return int(v.natural16.value[0])


def name_of(info: Any) -> str:
    return "" if info is None else bytes(info.name).decode("utf-8", "replace")


def unique_id_of(info: Any) -> str:
    return "" if info is None else bytes(info.unique_id).hex()


def uptimes_look_like_two_nodes(uptimes: list[int]) -> bool:
    """One node's uptime never decreases between consecutive heartbeats."""
    return any(b < a for a, b in itertools.pairwise(uptimes))


async def cmd_list(bus: Bus, args: argparse.Namespace) -> int:
    seen = await bus.survey(args.seconds)
    if not seen:
        log(f"no heartbeats on {CAN_IFACE} in {args.seconds:g} s")
        return 1
    log(f"{len(seen)} Node-ID(s) heard on {CAN_IFACE}:")
    rc = 0
    for node_id in sorted(seen):
        info = await bus.get_info(node_id)
        stored = read_node_id(await bus.register(node_id, "uavcan.node.id"))
        tag = "  <-- UNPROVISIONED" if node_id == UNPROVISIONED else ""
        pending = (
            "" if stored in (None, node_id) else f", register reads {stored} (restart pending)"
        )
        log(
            f"  {node_id:>3}  {name_of(info) or '?':<38} "
            f"uid={unique_id_of(info)[:18]}{pending}{tag}"
        )
        if uptimes_look_like_two_nodes(seen[node_id]):
            log(
                f"       WARNING: uptime from {node_id} walks backwards — more than one node "
                f"is claiming it. Power down all but one before provisioning."
            )
            rc = 2
    return rc


async def cmd_show(bus: Bus, args: argparse.Namespace) -> int:
    info = await bus.get_info(args.node_id)
    if info is None:
        log(f"node {args.node_id} did not answer GetInfo")
        return 1
    resp = await bus.register(args.node_id, "uavcan.node.id")
    stored = read_node_id(resp)
    log(f"node {args.node_id}: {name_of(info)}")
    log(f"  unique_id     {unique_id_of(info)}")
    log(f"  uavcan.node.id {stored}  mutable={resp.mutable} persistent={resp.persistent}")
    if stored is not None and stored != args.node_id:
        log(f"  a restart adopts {stored}")
    return 0


async def cmd_write(bus: Bus, target: int, new_id: int, restart: bool, skip_scan: bool) -> int:
    import uavcan.primitive.array
    import uavcan.register

    if not (0 <= new_id <= UNPROVISIONED):
        log(f"{new_id} is outside the Cyphal/CAN Node-ID range")
        return 1

    if not skip_scan:
        seen = await bus.survey(3.0)
        if target not in seen:
            log(f"node {target} is not on {CAN_IFACE}; nothing was written")
            return 1
        if uptimes_look_like_two_nodes(seen[target]):
            log(
                f"more than one node is claiming {target} — power down all but one. "
                f"Nothing was written."
            )
            return 1
        if new_id != UNPROVISIONED and new_id in seen and new_id != target:
            log(f"{new_id} is already claimed on this bus; nothing was written")
            return 1

    value = uavcan.register.Value_1_0(natural16=uavcan.primitive.array.Natural16_1_0([new_id]))
    log(
        f"writing uavcan.node.id = {new_id} to node {target} "
        f"(the node erases a flash sector; up to {WRITE_TIMEOUT_S:g} s)"
    )
    resp = await bus.register(target, "uavcan.node.id", value, response_timeout=WRITE_TIMEOUT_S)
    if resp is None:
        log("no response — the write may or may not have committed; run `show` to see")
        return 1
    stored = read_node_id(resp)
    if stored != new_id:
        log(
            f"REJECTED: the register reads back {stored}. The store holds what it held; "
            f"an out-of-range value or a flash failure looks exactly like this"
        )
        return 1
    if not resp.persistent:
        log(
            "WARNING: the node reports uavcan.node.id as NOT persistent — it is running "
            "firmware older than the ADR-0027 store, and this write will not survive"
        )
        return 1

    if new_id == UNPROVISIONED:
        log(
            f"node {target} de-provisioned; it comes up at {UNPROVISIONED} after a restart "
            f"and publishes no telemetry subjects"
        )
    else:
        log(f"committed. Node {target} adopts {new_id} at its next restart")

    if not restart:
        log("not restarted: pass --restart, or power-cycle the node")
        return 0

    log(f"restarting node {target}")
    if not await bus.restart(target):
        log("the restart command was not accepted; power-cycle the node instead")
        return 1
    await asyncio.sleep(6.0)
    seen = await bus.survey(4.0)
    if new_id in seen:
        info = await bus.get_info(new_id)
        log(f"node is back at {new_id}: {name_of(info)}")
        return 0
    log(f"node {new_id} has not been heard yet; give it a moment and run `list`")
    return 1


async def run(args: argparse.Namespace) -> int:
    try:
        import pycyphal.presentation  # noqa: F401
        import uavcan.node
        import uavcan.register  # noqa: F401
        from pycyphal.transport.can.media.socketcan import SocketCANMedia  # noqa: F401
    except ImportError as exc:
        log(f"cannot start: {exc!r}")
        log(
            "PYTHONPATH must point at the namespaces compiled by provision.sh "
            "(/opt/industrygrow/dsdl), and SocketCAN exists only on Linux"
        )
        return 1

    try:
        bus = Bus()
    except OSError as exc:
        log(f"cannot open {CAN_IFACE}: {exc!r}; is industrygrow-can.service up?")
        return 1
    try:
        if args.command == "list":
            return await cmd_list(bus, args)
        if args.command == "show":
            return await cmd_show(bus, args)
        if args.command == "set":
            return await cmd_write(bus, args.node_id, args.new_id, args.restart, args.no_scan)
        if args.command == "clear":
            return await cmd_write(bus, args.node_id, UNPROVISIONED, args.restart, args.no_scan)
    finally:
        bus.close()
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Provision a node's Cyphal Node-ID (ADR-0027)")
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("list", help="what is on the bus")
    p.add_argument("--seconds", type=float, default=3.0)

    p = sub.add_parser("show", help="one node's identity and stored Node-ID")
    p.add_argument("node_id", type=int)

    p = sub.add_parser("set", help="write a Node-ID, effective at the next restart")
    p.add_argument("node_id", type=int, help="the node's CURRENT Node-ID")
    p.add_argument(
        "new_id",
        type=int,
        choices=range(0, MAX_PROVISIONABLE + 1),
        metavar=f"NEW_ID(0-{MAX_PROVISIONABLE})",
    )
    p.add_argument("--restart", action="store_true", help="restart the node so it adopts it")
    p.add_argument(
        "--no-scan",
        action="store_true",
        help="skip the pre-write bus scan; only when the bus is known-quiet",
    )

    p = sub.add_parser("clear", help="de-provision: the node returns to 127")
    p.add_argument("node_id", type=int)
    p.add_argument("--restart", action="store_true")
    p.add_argument("--no-scan", action="store_true")

    return asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
