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

from motor.motor_asyncio import AsyncIOMotorDatabase

from app.db import DOMAIN


class UnknownVersionError(Exception):
    """Asked to act on a version tag this machine does not have."""


class UnsignedVersionError(Exception):
    """Asked to record active a version no gateway could verify (ADR-0025 d11)."""


async def add_version(
    db: AsyncIOMotorDatabase,
    machine_id: str,
    version_tag: str,
    document_b64: str,
    signature: str | None = None,
    source_template_ref: str | None = None,
    created_by: str | None = None,
) -> dict:
    """Store a new deployment-specific profile version (whole artifact).

    ``document_b64`` is stored and returned untouched — the bytes it decodes to
    are what the operator signed and what the gateway will verify (ADR-0025 d6).
    """
    doc = {
        "machine_id": machine_id,
        "version_tag": version_tag,
        "document_b64": document_b64,
        "signature": signature,
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

    Refuses a version that carries no signature. ADR-0025 d11 puts the check here
    rather than at the gateway alone so the absence surfaces where an operator can
    fix it, instead of as a cabinet that has quietly stopped taking updates.
    """
    version = await db[DOMAIN["profile_version"]].find_one(
        {"machine_id": machine_id, "version_tag": version_tag}
    )
    if version is None:
        raise UnknownVersionError(f"{machine_id} has no version tagged '{version_tag}'")
    if not version.get("signature"):
        raise UnsignedVersionError(
            f"'{version_tag}' carries no signature. A gateway will not apply a profile it "
            f"cannot verify (ADR-0015 d7), so recording it active would describe a deployment "
            f"that cannot happen (ADR-0025 d11). Sign it with signing/sign_profile.py and "
            f"store the signature."
        )

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
