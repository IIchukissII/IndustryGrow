# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The node command tool's allowlist, without a bus (ADR-0015 d1, d14, d15).

`authorise` is the decision whether a command reaches a node, and it is pure, so
it is tested directly. The table is also checked against the `CMD_*` IDs the
firmware's command handlers define, so a command added to a node is classified
here before this tool will send it.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import pytest

GATEWAY = Path(__file__).resolve().parents[1]
REPO = GATEWAY.parent
sys.path.insert(0, str(GATEWAY))

from node_command import ALLOWED, BENCH, COMMANDS, STANDARD, authorise  # noqa: E402

M01 = "org.industrygrow.node.m01"
M02 = "org.industrygrow.node.m02"
M04 = "org.industrygrow.node.m04"
M05 = "org.industrygrow.node.m05"
A01 = "org.industrygrow.node.a01"

# Node name (firmware/nodes/registry.c) -> the file holding its command handler.
HANDLERS = {
    M01: "firmware/nodes/m01_climate/sensors.c",
    M02: "firmware/nodes/m02_light/sensors.c",
    M04: "firmware/nodes/m04_plant/sensors.c",
    M05: "firmware/nodes/m05_safety/sensors.c",
    A01: "firmware/nodes/a01_climate/actuators.c",
}

CMD_DEFINE = re.compile(r"^#define\s+CMD_\w+\s+(\d+)u\b", re.MULTILINE)


@pytest.mark.parametrize(
    ("node", "command"),
    [
        (M01, 3),  # trim commit, ADR-0028 d4
        (M02, 1),
        (M02, 2),
        (M04, 1),
        (M04, 2),
        (M04, 3),
        (M05, 2),
        (M05, 3),
        (A01, 1),
        (A01, 2),
    ],
)
def test_allowed_without_bench(node: str, command: int) -> None:
    assert authorise(node, command, bench=False) is None


@pytest.mark.parametrize(
    ("node", "command"),
    [(M01, 1), (M01, 2), (M01, 4), (M01, 5), (M01, 6), (M01, 7), (M05, 1)],
)
def test_bench_operation_needs_the_flag(node: str, command: int) -> None:
    refusal = authorise(node, command, bench=False)
    assert refusal is not None and "--bench" in refusal
    assert authorise(node, command, bench=True) is None


@pytest.mark.parametrize("bench", [False, True])
@pytest.mark.parametrize(
    ("node", "command"),
    [
        (M01, 0),
        (M01, 8),
        (A01, 3),
        (M05, 4),
        (M02, 3),
        ("org.industrygrow.node.unidentified", 1),
        ("org.industrygrow.bootloader", 1),
    ],
)
def test_unlisted_command_refused(node: str, command: int, bench: bool) -> None:
    assert authorise(node, command, bench=bench) is not None


@pytest.mark.parametrize("bench", [False, True])
@pytest.mark.parametrize("command", sorted(STANDARD))
@pytest.mark.parametrize("node", sorted(HANDLERS))
def test_standard_command_refused(node: str, command: int, bench: bool) -> None:
    assert authorise(node, command, bench=bench) == STANDARD[command]


def test_trim_commit_is_the_only_allowed_m01_command() -> None:
    allowed = [cid for cid, entry in COMMANDS[M01].items() if entry.kind == ALLOWED]
    assert allowed == [3]


def test_every_entry_is_classified_and_sourced() -> None:
    for commands in COMMANDS.values():
        for entry in commands.values():
            assert entry.kind in (ALLOWED, BENCH)
            assert entry.source


@pytest.mark.parametrize("node", sorted(HANDLERS))
def test_table_matches_the_firmware_command_ids(node: str) -> None:
    source = (REPO / HANDLERS[node]).read_text()
    firmware = {int(n) for n in CMD_DEFINE.findall(source)}
    assert firmware, f"no CMD_ define found in {HANDLERS[node]}"
    assert set(COMMANDS[node]) == firmware


def test_every_node_type_with_commands_has_a_handler_mapping() -> None:
    assert set(COMMANDS) == set(HANDLERS)
