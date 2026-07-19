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
    """Write the blob to the warehouse first, then index it (referential integrity)."""
    await warehouse.put(object_key, blob, content_type)
    doc = {
        "tenant_id": settings.operator_uuid,
        "instance_full_id": instance_full_id,
        "doc_type": doc_type,
        "doc_date": doc_date,
        "object_key": object_key,
        "valid_until": valid_until,
        "status": "valid",
    }
    result = await db[FOUNDATION["lifecycle_doc"]].insert_one(doc)
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
