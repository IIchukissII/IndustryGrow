# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""store/ -> warehouse sync: correct keys, VCS files skipped (no network)."""

from __future__ import annotations

import app.store_sync as store_sync


class _FakeWarehouse:
    def __init__(self) -> None:
        self.puts: list[str] = []

    async def ensure_bucket(self) -> None:
        pass

    async def put_file(self, key: str, path: str, content_type: str | None = None) -> str:
        self.puts.append(key)
        return key


async def test_sync_uploads_by_identifier_key(tmp_path, monkeypatch):
    (tmp_path / "E0001-000002-L.csv").write_text("bom")
    lib = tmp_path / "industrygrow.pretty"
    lib.mkdir()
    (lib / "weact.kicad_mod").write_text("fp")
    (tmp_path / ".gitattributes").write_text("* text")  # must be skipped

    fake = _FakeWarehouse()
    monkeypatch.setattr(store_sync, "Warehouse", lambda: fake)

    count = await store_sync.sync(str(tmp_path))

    assert count == 2
    assert "E0001-000002-L.csv" in fake.puts
    assert "industrygrow.pretty/weact.kicad_mod" in fake.puts
    assert ".gitattributes" not in fake.puts
