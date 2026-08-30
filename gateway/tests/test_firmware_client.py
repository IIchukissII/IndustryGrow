# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The firmware client's decision, without a bus (ADR-0029 d15, d17).

`plan_for_node` is the one place a wrong answer writes a wrong image to a node,
and it is pure — it takes what the node reported and what the gateway holds, and
returns what to do. So it is tested directly, and the CAN transport is not
involved in any of this.

The header reader is exercised against the release the repository actually
publishes, because a struct that parses a fixture proves nothing about the file
`mkimage.py` writes.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import pytest

GATEWAY = Path(__file__).resolve().parents[1]
REPO = GATEWAY.parent
sys.path.insert(0, str(GATEWAY))

from firmware_client import (  # noqa: E402
    IMAGE_HEADER_SIZE,
    IMAGE_MAGIC,
    Image,
    other_slot,
    plan_for_node,
    read_image,
)

RELEASE = "E0001-000001-F"
OLDER = "E0001-000000-F"


def image(release: str, slot: str, crc: int) -> Image:
    return Image(
        path=Path(f"/nonexistent/{release}-{slot}.img"),
        release_root=release,
        slot=slot,
        version=(0, 1),
        body_crc32=crc,
        hardware_class=1,
    )


# The intended release, both slots, plus an older release the gateway still holds.
INTENDED_A = image(RELEASE, "slot-a", 0xAAAA_0001)
INTENDED_B = image(RELEASE, "slot-b", 0xBBBB_0001)
OLDER_A = image(OLDER, "slot-a", 0xAAAA_0000)
OLDER_B = image(OLDER, "slot-b", 0xBBBB_0000)
HELD = [INTENDED_A, INTENDED_B, OLDER_A, OLDER_B]


def test_other_slot_is_an_involution():
    assert other_slot("slot-a") == "slot-b"
    assert other_slot("slot-b") == "slot-a"


@pytest.mark.parametrize("running", [INTENDED_A, INTENDED_B])
def test_a_node_already_on_the_release_is_left_alone(running):
    """Either slot counts: the release is what was asked for, not the slot."""
    plan = plan_for_node(97, running.body_crc32, HELD, RELEASE)
    assert plan.action == "up-to-date"
    assert plan.serve is None


def test_a_node_on_an_older_release_is_written_to_the_other_slot():
    # Running the old slot A, so slot B is the one not running — and slot B of
    # the *intended* release is what gets served (ADR-0029 d17).
    plan = plan_for_node(97, OLDER_A.body_crc32, HELD, RELEASE)
    assert plan.action == "update"
    assert plan.serve is INTENDED_B

    plan = plan_for_node(97, OLDER_B.body_crc32, HELD, RELEASE)
    assert plan.action == "update"
    assert plan.serve is INTENDED_A


def test_an_unrecognised_image_stops_the_update():
    """d17: no match means the running slot is unknown, so nothing is served."""
    plan = plan_for_node(97, 0xDEAD_BEEF, HELD, RELEASE)
    assert plan.action == "unidentified"
    assert plan.serve is None
    assert "0xDEADBEEF" in plan.reason


def test_a_node_reporting_no_image_crc_stops_the_update():
    # Firmware older than the image header reports an empty array. Guessing from
    # the version number instead is what d15 refuses.
    plan = plan_for_node(97, None, HELD, RELEASE)
    assert plan.action == "unidentified"
    assert plan.serve is None


def test_a_half_held_release_serves_nothing():
    """Both slot images or none: the node may target either slot."""
    plan = plan_for_node(97, OLDER_A.body_crc32, [INTENDED_A, OLDER_A], RELEASE)
    assert plan.action == "no-image"
    assert plan.serve is None


def test_the_slots_of_one_release_are_distinguishable():
    """The premise d17 rests on — if the two slot builds ever collided on CRC,
    identification would silently pick whichever came first."""
    assert INTENDED_A.body_crc32 != INTENDED_B.body_crc32


# ---- the header reader, against the published release ----------------------


def published_images() -> list[Path]:
    return sorted((REPO / "store").glob(f"{RELEASE}-slot-*.img"))


@pytest.mark.skipif(not published_images(), reason="no release artifacts in store/")
def test_reads_the_published_release_images():
    images = [read_image(p) for p in published_images()]
    assert all(i is not None for i in images)
    assert {i.slot for i in images} == {"slot-a", "slot-b"}
    assert {i.release_root for i in images} == {RELEASE}
    assert all(i.hardware_class == 1 for i in images)  # IGROW_HW_CLASS_E0001
    # The two builds of one release must not collide, or d17 cannot tell them
    # apart — asserted on the real artifacts, not only on the fixtures above.
    assert len({i.body_crc32 for i in images}) == 2


@pytest.mark.skipif(not published_images(), reason="no release artifacts in store/")
def test_the_header_length_field_matches_the_file():
    for path in published_images():
        image = read_image(path)
        length = struct.unpack_from("<I", path.read_bytes(), 0x08)[0]
        assert IMAGE_HEADER_SIZE + length == path.stat().st_size
        assert image is not None


def test_a_file_that_is_not_an_image_is_skipped(tmp_path):
    """The artifact directory is a cache; junk in it must not fail a whole pass."""
    assert read_image(tmp_path / "notes.txt") is None

    truncated = tmp_path / f"{RELEASE}-slot-a.img"
    truncated.write_bytes(b"\x00" * 16)
    assert read_image(truncated) is None

    wrong_magic = tmp_path / f"{RELEASE}-slot-b.img"
    wrong_magic.write_bytes(b"\x00" * IMAGE_HEADER_SIZE)
    assert read_image(wrong_magic) is None

    not_a_slot = tmp_path / f"{RELEASE}-boot.img"
    not_a_slot.write_bytes(struct.pack("<I", IMAGE_MAGIC) + b"\x00" * IMAGE_HEADER_SIZE)
    assert read_image(not_a_slot) is None
