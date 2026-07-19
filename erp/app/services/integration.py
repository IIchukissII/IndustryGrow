# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Integration history — the mutable cross-reference (ADR-0021 d6).

Installs, removals, and replacements are validity-stamped records
(``installed_at`` / ``removed_at``). "What is at this position now" and "where
has this serial been" are queries, not archaeology (ADR-0016 mobility). The
partial-unique index on (machine_id, depth_code) where ``removed_at`` is null
enforces exactly one current instance per position.
"""

from __future__ import annotations

from datetime import UTC, datetime

from motor.motor_asyncio import AsyncIOMotorDatabase
from pymongo.errors import DuplicateKeyError

from app.config import settings
from app.db import FOUNDATION


class PositionOccupiedError(ValueError):
    """Raised when installing into a position that already holds a current instance."""


async def install(
    db: AsyncIOMotorDatabase, machine_id: str, depth_code: str, instance_id: str
) -> dict:
    """Install an instance at a machine position. Fails if the position is occupied."""
    record = {
        "tenant_id": settings.operator_uuid,
        "machine_id": machine_id,
        "depth_code": depth_code,
        "instance_id": instance_id,
        "installed_at": datetime.now(UTC),
        "removed_at": None,
        "removal_reason": None,
    }
    try:
        result = await db[FOUNDATION["integration_record"]].insert_one(record)
    except DuplicateKeyError as exc:
        raise PositionOccupiedError(
            f"{machine_id}-{depth_code} already has a current instance"
        ) from exc

    await db[FOUNDATION["module_instance"]].update_one(
        {"_id": instance_id}, {"$set": {"status": "installed"}}
    )
    record["_id"] = result.inserted_id
    return record


async def remove(
    db: AsyncIOMotorDatabase, machine_id: str, depth_code: str, reason: str = "removed"
) -> bool:
    """Remove the current instance at a position. Returns False if none was there."""
    now = datetime.now(UTC)
    current = await db[FOUNDATION["integration_record"]].find_one_and_update(
        {"machine_id": machine_id, "depth_code": depth_code, "removed_at": None},
        {"$set": {"removed_at": now, "removal_reason": reason}},
    )
    if current is None:
        return False
    new_status = "retired" if reason == "retired" else "in-inventory"
    await db[FOUNDATION["module_instance"]].update_one(
        {"_id": current["instance_id"]}, {"$set": {"status": new_status}}
    )
    return True


async def replace(
    db: AsyncIOMotorDatabase, machine_id: str, depth_code: str, new_instance_id: str
) -> dict:
    """Remove whatever is at the position and install a new instance there."""
    await remove(db, machine_id, depth_code, reason="replaced")
    return await install(db, machine_id, depth_code, new_instance_id)


async def current_at(db: AsyncIOMotorDatabase, machine_id: str, depth_code: str) -> dict | None:
    return await db[FOUNDATION["integration_record"]].find_one(
        {"machine_id": machine_id, "depth_code": depth_code, "removed_at": None}
    )


async def current_estate(db: AsyncIOMotorDatabase, machine_id: str) -> list[dict]:
    cursor = (
        db[FOUNDATION["integration_record"]]
        .find({"machine_id": machine_id, "removed_at": None})
        .sort("depth_code", 1)
    )
    return await cursor.to_list(length=None)


async def instance_history(db: AsyncIOMotorDatabase, instance_id: str) -> list[dict]:
    """Where has this serial been, over its life."""
    cursor = (
        db[FOUNDATION["integration_record"]]
        .find({"instance_id": instance_id})
        .sort("installed_at", -1)
    )
    return await cursor.to_list(length=None)
