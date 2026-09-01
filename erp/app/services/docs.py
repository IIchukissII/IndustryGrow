# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Lifecycle-document index over the warehouse (ADR-0021 d5, d7).

Records -QP/-QR/-CP/-CC/-PR metadata plus the warehouse object key; the blob
stays in the object store. "Show every instance whose calibration -CC expires
this month" is a query here that resolves to object keys (ADR-0021 d7).
"""

from __future__ import annotations

from datetime import UTC, datetime, timedelta

from motor.motor_asyncio import AsyncIOMotorDatabase

from app.config import settings
from app.db import FOUNDATION
from app.services.warehouse import Warehouse


async def record_doc(
    db: AsyncIOMotorDatabase,
    warehouse: Warehouse,
    *,
    instance_full_id: str,
    doc_type: str,
    object_key: str,
    blob: bytes,
    content_type: str = "application/pdf",
    valid_until: datetime | None = None,
    doc_date: datetime | None = None,
) -> dict:
    """Write the blob to the warehouse first, then index it (referential integrity).

    A re-issue lands on a key that is already occupied. ADR-0017 d11 gives QP, QR
    and PR one key per instance — they are issued once where calibration recurs —
    so a second issue of one of them is the same key by design, and the warehouse
    put replaces in place. Left alone that destroys the first issue's content and
    leaves its index row resolving to the replacement, which is what happened to
    `E0002-000001-000001-QP` on 2026-08-24.

    So the previous content is archived under a stamped key and its index row is
    repointed at the archive and marked superseded, before the new blob takes the
    stable key. The stable key always resolves to the current issue, every row
    still resolves to the bytes it indexed, and nothing is destroyed.

    CP and CC never reach this path: routes stamps their keys per upload, so no
    previous object stands at the key.
    """
    collection = db[FOUNDATION["lifecycle_doc"]]

    previous = await warehouse.get_bytes(object_key)
    if previous is not None:
        prior = await collection.find_one(
            {"tenant_id": settings.operator_uuid, "object_key": object_key}
        )
        stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S")
        archive_key = f"{object_key}-superseded-{stamp}"
        n = 1
        while await warehouse.exists(archive_key):
            n += 1
            archive_key = f"{object_key}-superseded-{stamp}-{n}"

        await warehouse.put(
            archive_key,
            previous,
            (prior or {}).get("content_type", content_type),
        )
        await collection.update_many(
            {"tenant_id": settings.operator_uuid, "object_key": object_key},
            {"$set": {"object_key": archive_key, "status": "superseded"}},
        )

    await warehouse.put(object_key, blob, content_type)
    doc = {
        "tenant_id": settings.operator_uuid,
        "instance_full_id": instance_full_id,
        "doc_type": doc_type,
        "doc_date": doc_date,
        "object_key": object_key,
        "content_type": content_type,
        "valid_until": valid_until,
        "status": "valid",
    }
    result = await collection.insert_one(doc)
    doc["_id"] = result.inserted_id
    return doc


async def docs_for(db: AsyncIOMotorDatabase, instance_full_id: str) -> list[dict]:
    cursor = db[FOUNDATION["lifecycle_doc"]].find({"instance_full_id": instance_full_id})
    return await cursor.to_list(length=None)


async def expiring(db: AsyncIOMotorDatabase, within_days: int = 30) -> list[dict]:
    """Calibration certificates expiring within ``within_days`` (or already expired)."""
    horizon = datetime.now(UTC) + timedelta(days=within_days)
    cursor = (
        db[FOUNDATION["lifecycle_doc"]]
        .find({"doc_type": "CC", "valid_until": {"$ne": None, "$lte": horizon}})
        .sort("valid_until", 1)
    )
    return await cursor.to_list(length=None)
