# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""store/ -> warehouse sync: correct keys, VCS files skipped (no network)."""

from __future__ import annotations

import app.store_sync as store_sync


class _FakeWarehouse:
    def __init__(self, remote: list[str] | None = None) -> None:
        self.puts: list[str] = []
        self.deletes: list[str] = []
        self._remote = list(remote or [])

    async def ensure_bucket(self) -> None:
        pass

    async def put_file(self, key: str, path: str, content_type: str | None = None) -> str:
        self.puts.append(key)
        return key

    async def list_prefix(self, prefix: str) -> list[str]:
        return [k for k in self._remote if k.startswith(prefix)]

    async def delete(self, key: str) -> None:
        self.deletes.append(key)


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


async def test_prune_deletes_stale_but_keeps_current(tmp_path, monkeypatch):
    (tmp_path / "E0001-000002-D-fab.zip").write_text("bundled gerbers")  # current

    # warehouse still holds a loose gerber that was bundled away (ADR-0017 d18)
    fake = _FakeWarehouse(remote=["E0001-000002-D-fab.zip", "E0001-000002-D-Top_Layer.gtl"])
    monkeypatch.setattr(store_sync, "Warehouse", lambda: fake)

    await store_sync.sync(str(tmp_path), prune=True)

    assert "E0001-000002-D-fab.zip" in fake.puts
    assert fake.deletes == ["E0001-000002-D-Top_Layer.gtl"]  # only the stale loose object


async def test_no_prune_leaves_remote_untouched(tmp_path, monkeypatch):
    (tmp_path / "E0001-000002-D-fab.zip").write_text("x")
    fake = _FakeWarehouse(remote=["E0001-000002-D-Top_Layer.gtl"])
    monkeypatch.setattr(store_sync, "Warehouse", lambda: fake)

    await store_sync.sync(str(tmp_path))  # prune defaults False

    assert fake.deletes == []
