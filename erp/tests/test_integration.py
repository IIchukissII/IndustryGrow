# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Integration history — install, remove, replace, and mobility (ADR-0021 d6)."""

from __future__ import annotations

from app.services import integration, serials


async def _make(db, e="E0002", v="020100"):
    (inst,) = await serials.allocate(db, e, v, 1)
    return inst


async def test_install_then_current(db):
    inst = await _make(db)
    await integration.install(db, "GBOX_0001", "010100", inst)
    current = await integration.current_at(db, "GBOX_0001", "010100")
    assert current["instance_id"] == inst
    estate = await integration.current_estate(db, "GBOX_0001")
    assert len(estate) == 1


async def test_remove_clears_position_and_keeps_history(db):
    inst = await _make(db)
    await integration.install(db, "GBOX_0001", "010100", inst)
    assert await integration.remove(db, "GBOX_0001", "010100") is True
    assert await integration.current_at(db, "GBOX_0001", "010100") is None
    history = await integration.instance_history(db, inst)
    assert len(history) == 1
    assert history[0]["removed_at"] is not None


async def test_mobility_serial_moves_and_history_follows(db):
    inst = await _make(db)
    await integration.install(db, "GBOX_0001", "010100", inst)
    await integration.remove(db, "GBOX_0001", "010100", reason="replaced")
    await integration.install(db, "GBOX_0001", "020100", inst)
    history = await integration.instance_history(db, inst)
    assert len(history) == 2  # where the serial has been, over its life
    positions = {h["depth_code"] for h in history}
    assert positions == {"010100", "020100"}
