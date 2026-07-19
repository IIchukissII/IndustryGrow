# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Deployment-profile store + deployment record (ADR-0021 d8, d12-13).

This is a versioned *store* and a record of *which version is active where* — it
is NOT a deploy path. There is deliberately no "push to gateway" function: the
cabinet runs what the gateway pulls into ``active-profile.json``, the single
mutation channel (ADR-0015 d4). A profile version is one whole document
(setpoints + model), never split (ADR-0016 alt D).
"""

from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

from motor.motor_asyncio import AsyncIOMotorDatabase

from app.db import DOMAIN


async def add_version(
    db: AsyncIOMotorDatabase,
    machine_id: str,
    version_tag: str,
    payload: dict[str, Any],
    signed_hash: str | None = None,
    source_template_ref: str | None = None,
    created_by: str | None = None,
) -> dict:
    """Store a new deployment-specific profile version (whole artifact)."""
    doc = {
        "machine_id": machine_id,
        "version_tag": version_tag,
        "payload": payload,
        "signed_hash": signed_hash,
        "source_template_ref": source_template_ref,
        "created_at": datetime.now(UTC),
        "created_by": created_by,
    }
    result = await db[DOMAIN["profile_version"]].insert_one(doc)
    doc["_id"] = result.inserted_id
    return doc


async def mark_active(
    db: AsyncIOMotorDatabase, machine_id: str, version_tag: str, activated_by: str | None = None
) -> dict:
    """Record that ``version_tag`` is the version active on the machine.

    This reflects a deployment that happened through the gateway's pull channel;
    it does not itself deploy anything.
    """
    now = datetime.now(UTC)
    await db[DOMAIN["profile_deployment"]].update_many(
        {"machine_id": machine_id, "deactivated_at": None},
        {"$set": {"deactivated_at": now}},
    )
    doc = {
        "machine_id": machine_id,
        "version_tag": version_tag,
        "activated_at": now,
        "deactivated_at": None,
        "activated_by": activated_by,
    }
    result = await db[DOMAIN["profile_deployment"]].insert_one(doc)
    doc["_id"] = result.inserted_id
    return doc


async def active_version(db: AsyncIOMotorDatabase, machine_id: str) -> dict | None:
    return await db[DOMAIN["profile_deployment"]].find_one(
        {"machine_id": machine_id, "deactivated_at": None}
    )


async def versions(db: AsyncIOMotorDatabase, machine_id: str) -> list[dict]:
    cursor = db[DOMAIN["profile_version"]].find({"machine_id": machine_id}).sort("created_at", -1)
    return await cursor.to_list(length=None)
