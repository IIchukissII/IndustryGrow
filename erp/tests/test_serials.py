# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Serial-allocation authority — gap-free and collision-free (ADR-0021 d4)."""

from __future__ import annotations

import asyncio

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
