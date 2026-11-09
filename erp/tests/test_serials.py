# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Serial-allocation authority — gap-free and collision-free (ADR-0021 d4)."""

from __future__ import annotations

import asyncio

from app.config import settings
from app.db import FOUNDATION, Database, ensure_indexes, migrate_counter_tenant
from app.services import serials


async def test_allocate_is_gap_free(db):
    a = await serials.allocate(db, "E0002", "020100", 3)
    b = await serials.allocate(db, "E0002", "020100", 2)
    assert a == ["E0002-020100-000001", "E0002-020100-000002", "E0002-020100-000003"]
    assert b == ["E0002-020100-000004", "E0002-020100-000005"]
    assert await serials.peek(db, "E0002", "020100") == 5


async def test_allocate_per_module_version_independent(db):
    await serials.allocate(db, "E0002", "020100", 2)
    other = await serials.allocate(db, "E0003", "010000", 1)
    assert other == ["E0003-010000-000001"]


async def test_allocate_concurrent_no_duplicates(db):
    results = await asyncio.gather(*[serials.allocate(db, "E0005", "010000", 1) for _ in range(20)])
    issued = [s for batch in results for s in batch]
    assert len(issued) == 20
    assert len(set(issued)) == 20  # every serial unique — no gaps, no collisions


async def test_counter_carries_the_tenant(db):
    await serials.allocate(db, "E0002", "020100", 1)
    doc = await db[FOUNDATION["serial_counter"]].find_one({"_id": "E0002-020100"})
    assert doc["tenant_id"] == settings.operator_uuid
    assert doc["last_serial"] == 1


async def test_migration_sets_tenant_on_a_legacy_counter_and_keeps_its_value(db):
    coll = db[FOUNDATION["serial_counter"]]
    await coll.insert_one({"_id": "E0002-020100", "last_serial": 41})
    await coll.insert_one({"_id": "E0003-010000", "tenant_id": "other", "last_serial": 7})

    assert await migrate_counter_tenant(db) == 1
    assert await coll.find_one({"_id": "E0002-020100"}) == {
        "_id": "E0002-020100",
        "last_serial": 41,
        "tenant_id": settings.operator_uuid,
    }
    assert (await coll.find_one({"_id": "E0003-010000"}))["tenant_id"] == "other"

    assert await migrate_counter_tenant(db) == 0  # idempotent
    assert (await coll.find_one({"_id": "E0002-020100"}))["last_serial"] == 41

    # The allocator continues the migrated sequence, gap-free.
    assert await serials.allocate(db, "E0002", "020100", 1) == ["E0002-020100-000042"]


async def test_startup_migrates_legacy_counters():
    database = Database("mongodb://unused", "test_erp", mock=True)
    coll = database.db[FOUNDATION["serial_counter"]]
    await coll.insert_one({"_id": "E0002-020100", "last_serial": 3})

    await ensure_indexes(database)
    await ensure_indexes(database)  # a restart changes nothing

    doc = await coll.find_one({"_id": "E0002-020100"})
    assert doc == {"_id": "E0002-020100", "last_serial": 3, "tenant_id": settings.operator_uuid}
