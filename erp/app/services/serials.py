# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Serial-allocation authority (ADR-0021 d4).

Gap-free serial issuance per module + version. A single atomic ``$inc`` on the
counter document reserves a contiguous block, so concurrent callers never
collide and never leave gaps — the ADR-0017 d8 requirement, without a separate
serial authority.

There is deliberately no overflow-into-version: at 999999 the serial field is
widened, never carried into the version (ADR-0017 d1, alt D).
"""

from __future__ import annotations

from datetime import UTC, datetime

from motor.motor_asyncio import AsyncIOMotorDatabase
from pymongo import ReturnDocument

from app.config import settings
from app.db import FOUNDATION
from app.models.identifiers import counter_id, instance_id

_SERIAL_MAX = 999_999


async def allocate(
    db: AsyncIOMotorDatabase, e_number: str, version: str, quantity: int = 1
) -> list[str]:
    """Issue ``quantity`` gap-free serials for (e_number, version).

    Creates the ``module_instance`` rows and returns their full identifiers
    ``Exxxx-VVVVVV-NNNNNN``.
    """
    if quantity < 1:
        raise ValueError("quantity must be >= 1")

    cid = counter_id(e_number, version)  # validates the module/version grammar
    counter = await db[FOUNDATION["serial_counter"]].find_one_and_update(
        {"_id": cid},
        {"$inc": {"last_serial": quantity}},
        upsert=True,
        return_document=ReturnDocument.AFTER,
    )
    last = counter["last_serial"]
    if last > _SERIAL_MAX:
        raise ValueError(f"serial range exhausted for {cid} — widen the field (ADR-0017 d1)")

    first = last - quantity + 1
    now = datetime.now(UTC)
    instances = [
        {
            "_id": instance_id(e_number, version, f"{n:06d}"),
            "tenant_id": settings.operator_uuid,
            "e_number": e_number,
            "version": version,
            "serial": f"{n:06d}",
            "status": "in-inventory",
            "produced_at": now,
        }
        for n in range(first, last + 1)
    ]
    await db[FOUNDATION["module_instance"]].insert_many(instances)
    return [doc["_id"] for doc in instances]


async def peek(db: AsyncIOMotorDatabase, e_number: str, version: str) -> int:
    """The last serial issued for (e_number, version); 0 if none yet."""
    doc = await db[FOUNDATION["serial_counter"]].find_one({"_id": counter_id(e_number, version)})
    return doc["last_serial"] if doc else 0
