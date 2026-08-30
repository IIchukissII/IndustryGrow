# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The intended firmware release for a machine (ADR-0021 d18, ADR-0022 d14).

This is the operator's *selection* and nothing else. It does not update a node,
it does not know whether one was updated, and it cannot be made to: what a node
is actually running is observed at the gateway, on the bus, and stays there
(ADR-0029 d15; ADR-0022 d9). The record answers "what should this machine's
nodes run" — an intent, exactly as ``profiles.mark_active`` records which profile
version is meant to be live without claiming the gateway has collected it.

A release is named by its object-key root, ``Exxxx-VVVVVV-F`` (ADR-0029 d13;
ADR-0017 d16 roots firmware on the carrier), which stands for the artifact set
filed under it. One release covers every node type, because every node runs the
one carrier codebase.
"""

from __future__ import annotations

import re
from datetime import UTC, datetime
from pathlib import Path

from motor.motor_asyncio import AsyncIOMotorDatabase

from app.config import settings
from app.db import DOMAIN
from app.models.identifiers import E_MODULE, VERSION

# `Exxxx-VVVVVV-F` — the F-layer prefix an artifact set hangs off. Anchored, so
# a value that reaches the filesystem below cannot carry a separator or `..`:
# this regex is the path guard, which is why the scan can join rather than
# resolve-and-compare as the store-document routes must.
RELEASE_RE = re.compile(rf"^{E_MODULE}-{VERSION}-F$")

# The two artifacts a release must publish to be servable over the bus: header
# plus body, one per slot (ADR-0029 d13). The `.hex` files carry a load address
# and are for SWD; the bootloader's image is never served over the bus (d10), so
# neither is required here and neither is offered.
SLOT_IMAGES = ("slot-a", "slot-b")


class UnknownReleaseError(Exception):
    """Asked to intend a release the repository does not publish."""


def slot_image_key(release_root: str, slot: str) -> str:
    return f"{release_root}-{slot}.img"


def artifact_keys(release_root: str) -> list[str]:
    """The object keys a gateway may fetch for this release, and only these."""
    return [slot_image_key(release_root, slot) for slot in SLOT_IMAGES]


def _store_dir() -> Path:
    return Path(settings.store_dir)


def is_release(release_root: str) -> bool:
    """True when both slot images are present in the repository's ``store/``.

    Both, not either: a release missing one slot image can be selected and then
    fails at the gateway for whichever node happens to be running the other slot
    (ADR-0029 d17) — a fault that surfaces one node at a time, days later, far
    from the selection that caused it.
    """
    if not RELEASE_RE.match(release_root):
        return False
    store = _store_dir()
    return all((store / key).is_file() for key in artifact_keys(release_root))


def available_releases() -> list[str]:
    """Every release the repository publishes, newest-looking last.

    Read from ``store/`` rather than from the warehouse: the repository is what
    defines a release exists, and the bucket is a mirror of it (ADR-0022 d1's
    read-through shape, as for the store-document listing).
    """
    roots = {
        name[: -len("-slot-a.img")]
        for name in (p.name for p in _store_dir().iterdir() if p.is_file())
        if name.endswith("-slot-a.img")
    }
    return sorted(root for root in roots if is_release(root))


async def set_intent(
    db: AsyncIOMotorDatabase,
    machine_id: str,
    release_root: str,
    selected_by: str | None = None,
) -> dict:
    """Record the release an operator intends for this machine.

    Upserted, latest-wins (ADR-0022 d14). A machine has one intended release, and
    what it used to be is not a question this record answers — the same shape the
    machine identity binding takes, and for the same reason: it is configuration
    kept current, not a history.
    """
    if not is_release(release_root):
        raise UnknownReleaseError(
            f"{release_root} is not a firmware release in the repository's store/ — "
            f"expected {', '.join(artifact_keys(release_root))}"
        )
    now = datetime.now(UTC)
    await db[DOMAIN["firmware_intent"]].update_one(
        {"_id": machine_id},
        {
            "$set": {
                "machine_id": machine_id,
                "release_root": release_root,
                "selected_at": now,
                "selected_by": selected_by,
            }
        },
        upsert=True,
    )
    return {
        "machine_id": machine_id,
        "release_root": release_root,
        "selected_at": now,
        "selected_by": selected_by,
    }


async def intent(db: AsyncIOMotorDatabase, machine_id: str) -> dict | None:
    return await db[DOMAIN["firmware_intent"]].find_one({"_id": machine_id})
