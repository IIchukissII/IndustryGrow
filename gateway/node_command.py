#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Send a node a vendor `uavcan.node.ExecuteCommand` — the bench operations the
module specifications require to be commanded rather than automatic.

Which IDs a node answers is the module specification's, not this tool's: M01
section 10 and M05 section 10 each list their own, and nothing here restates
them. `--list` prints the names this tool knows for readability only; an ID it
does not recognise is still sent.

    node_command.py 97 --list
    node_command.py 97 3 --parameter 425      # write U3's offset, 4.25 C (V7)
    node_command.py 97 6                      # stop U3 (V1 state a/b)
    node_command.py 97 4                      # park U2's gas scan

A response is a status, not a result. Commands that stop U3 take seconds and
finish after the response; what they did arrives on `uavcan.diagnostic.Record`,
so watch the telemetry service's journal, or the `node_event` table.

The transport session is `provision_node_id.Bus` — one implementation of "an
operator addressing a node by hand", installed beside this file.
"""

from __future__ import annotations

import argparse
import asyncio
import sys

from provision_node_id import Bus, call_with_retry, log

# Readability only. The specifications own these; a mismatch here is a display
# defect, and an unknown ID is sent unchanged.
KNOWN = {
    "org.industrygrow.node.m01": {
        1: "U3 self-test",
        2: "U1 condensate-recovery pulse",
        3: "write U3 temperature offset (parameter: centi-degrees C)",
        4: "park U2 gas scan",
        5: "arm U2 gas scan",
        6: "stop U3 measurement",
        7: "start U3 measurement",
    },
    "org.industrygrow.node.m05": {
        1: "zero the energy accumulator",
        2: "report the raw leak ADC sample",
    },
}

STATUS = {
    0: "SUCCESS",
    1: "FAILURE",
    2: "NOT_AUTHORIZED",
    3: "BAD_COMMAND",
    4: "BAD_PARAMETER",
    5: "BAD_STATE",
    6: "INTERNAL_ERROR",
}


async def execute(bus: Bus, node_id: int, command: int, parameter: str) -> int:
    import uavcan.node

    client = bus.pres.make_client(uavcan.node.ExecuteCommand_1_0, 435, node_id)
    client.response_timeout = 5.0
    try:
        got = await call_with_retry(
            client,
            uavcan.node.ExecuteCommand_1_0.Request(
                command=command, parameter=parameter.encode("ascii")
            ),
        )
    finally:
        client.close()
    if got is None:
        log(f"node {node_id} did not answer command {command}")
        return 1
    status = int(got[0].status)
    log(f"node {node_id} command {command}: {STATUS.get(status, status)}")
    return 0 if status == 0 else 1


async def run(args: argparse.Namespace) -> int:
    try:
        import uavcan.node  # noqa: F401
    except ImportError as exc:
        log(f"cannot start: {exc!r}")
        log("PYTHONPATH must point at the namespaces compiled by provision.sh")
        return 1

    try:
        bus = Bus()
    except OSError as exc:
        log(f"cannot open the CAN interface: {exc!r}; is industrygrow-can.service up?")
        return 1
    try:
        info = await bus.get_info(args.node_id)
        if info is None:
            log(f"node {args.node_id} did not answer GetInfo; nothing was sent")
            return 1
        name = bytes(info.name).decode("utf-8", "replace")
        commands = KNOWN.get(name, {})
        if args.list:
            log(f"node {args.node_id}: {name}")
            if not commands:
                log("  no command names known for this node type; see its specification")
            for cid, text in sorted(commands.items()):
                log(f"  {cid}  {text}")
            return 0
        if args.command is None:
            log("give a command ID, or --list")
            return 1
        log(f"node {args.node_id} ({name}): {commands.get(args.command, 'unknown to this tool')}")
        return await execute(bus, args.node_id, args.command, args.parameter)
    finally:
        bus.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="Send a vendor ExecuteCommand to a node")
    ap.add_argument("node_id", type=int)
    ap.add_argument("command", type=int, nargs="?", help="vendor command ID")
    ap.add_argument("--parameter", default="", help="value for commands that take one")
    ap.add_argument("--list", action="store_true", help="show the IDs this tool has names for")
    return asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
