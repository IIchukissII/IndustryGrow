#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Send a node a vendor `uavcan.node.ExecuteCommand` — the bench operations the
module specifications require to be commanded rather than automatic.

The cultivation profile is the single mutation interface (ADR-0015 d1), so this
tool sends only the commands in `COMMANDS`, per node type as
GetInfo names it, and refuses every other ID before the bus sees it:

- `allowed`: the calibration trim commit (ADR-0028 d4), and diagnostics that
  report a value and change nothing the node measures or publishes;
- `bench`: the specification's bench operations that alter what the node
  measures or publishes, sent only with `--bench`, an operator's acknowledgement
  and not a check of the node;
- the standard commands are other tools': begin-software-update is
  `firmware_client.py`'s, which serves the file the node then reads (ADR-0029
  d5), and restart is `provision_node_id.py --restart`'s.

No vendor command on any node drives an actuator output or demand; a demand
reaches A01 only on its subscribed subjects. The table guards against mistakes on
a trusted bus (ADR-0004 d17); it is not access control, since GetInfo names are
the node's own. The trim commit is ADR-0028 d4's commissioning command and stays
outside `--bench`; when to write it (ADR-0028 d6, d7) is the calibration protocol's.

    node_command.py 97 --list
    node_command.py 97 3 --parameter 425      # write U3's offset, 4.25 C (V7)
    node_command.py 97 6 --bench              # stop U3 (V1 state a/b)
    node_command.py 97 4 --bench              # park U2's gas scan

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
from typing import NamedTuple

from provision_node_id import Bus, call_with_retry, log

ALLOWED = "allowed"
BENCH = "bench"


class Command(NamedTuple):
    text: str
    kind: str  # ALLOWED or BENCH
    source: str


# What this tool sends. The firmware's command handlers decide what a node
# answers; an ID absent here is refused, whatever the node would do with it.
COMMANDS: dict[str, dict[int, Command]] = {
    "org.industrygrow.node.m01": {
        1: Command("U3 self-test, U3 stopped 10 s", BENCH, "E0002 spec 10"),
        2: Command("U1 condensate-recovery heater pulse", BENCH, "E0002 spec 10"),
        3: Command(
            "write U3 temperature offset (parameter: centi-degrees C)",
            ALLOWED,
            "ADR-0028 d4; E0002 spec 10; E0002-000001-M calibration protocol 7",
        ),
        4: Command("park U2 gas scan", BENCH, "E0002 spec 10"),
        5: Command("arm U2 gas scan", BENCH, "E0002 spec 10"),
        6: Command("stop U3 measurement", BENCH, "E0002 spec 10"),
        7: Command("start U3 measurement", BENCH, "E0002 spec 10"),
    },
    "org.industrygrow.node.m02": {
        1: Command("report spectral autorange state", ALLOWED, "E0003-000001-M 6.1"),
        2: Command("report U3 raw UV counts", ALLOWED, "E0003-000001-M 6.1"),
    },
    "org.industrygrow.node.m04": {
        1: Command("report U1 device-ID words", ALLOWED, "E0005 spec 11 V12"),
        2: Command("report frame counters", ALLOWED, "E0005 spec 11 V3"),
        3: Command("report flat-field store state", ALLOWED, "E0005 spec 11 V16"),
    },
    "org.industrygrow.node.m05": {
        1: Command("zero the energy accumulator", BENCH, "E0006 spec 9"),
        2: Command("report the raw leak ADC sample", ALLOWED, "E0006 spec 9"),
        3: Command(
            "report the bus-current window min/max, and restart the window",
            ALLOWED,
            "E0006 spec 9",
        ),
    },
    "org.industrygrow.node.a01": {
        1: Command("report the 1-Wire ROM codes found at boot", ALLOWED, "firmware/README.md A01"),
        2: Command("report E1 and E3 string currents", ALLOWED, "E0011 spec V2"),
    },
}

# uavcan.node.ExecuteCommand.1.0 standard commands, and the tool that owns each.
STANDARD = {
    65533: "begin-software-update is firmware_client.py's; it serves the file (ADR-0029 d5)",
    65535: "restart is provision_node_id.py --restart's",
}


def authorise(node_name: str, command: int, bench: bool) -> str | None:
    """None when the command may be sent, otherwise why it is refused."""
    if command in STANDARD:
        return STANDARD[command]
    entry = COMMANDS.get(node_name, {}).get(command)
    if entry is None:
        return f"command {command} is not allowed to {node_name} (ADR-0015 d1)"
    if entry.kind == BENCH and not bench:
        return f"command {command} ({entry.text}) is a bench operation; give --bench"
    return None


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
        commands = COMMANDS.get(name, {})
        if args.list:
            log(f"node {args.node_id}: {name}")
            if not commands:
                log("  no command is allowed to this node type")
            for cid, entry in sorted(commands.items()):
                log(f"  {cid}  {entry.kind:7}  {entry.text}  [{entry.source}]")
            return 0
        if args.command is None:
            log("give a command ID, or --list")
            return 1
        refusal = authorise(name, args.command, args.bench)
        if refusal is not None:
            log(f"refused, nothing sent: {refusal}")
            return 2
        log(f"node {args.node_id} ({name}): {commands[args.command].text}")
        return await execute(bus, args.node_id, args.command, args.parameter)
    finally:
        bus.close()


def main() -> int:
    ap = argparse.ArgumentParser(description="Send a vendor ExecuteCommand to a node")
    ap.add_argument("node_id", type=int)
    ap.add_argument("command", type=int, nargs="?", help="vendor command ID")
    ap.add_argument("--parameter", default="", help="value for commands that take one")
    ap.add_argument("--list", action="store_true", help="show the IDs this tool sends")
    ap.add_argument(
        "--bench",
        action="store_true",
        help="also send the bench operations that alter what a node measures or publishes",
    )
    return asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    sys.exit(main())
