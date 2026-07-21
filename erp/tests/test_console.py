# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The app serves its own console (ADR-0021 d15): one container, one origin.

What matters here is the ordering — a static mount at `/` must not swallow the
API — and that a source checkout with no built bundle still starts.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.config import settings
from app.main import create_app


@pytest.fixture
def built(tmp_path, monkeypatch):
    """A checkout whose console has been built."""
    (tmp_path / "assets").mkdir()
    (tmp_path / "index.html").write_text("<!doctype html><div id=app></div>")
    (tmp_path / "assets" / "index.css").write_text(".oper{}")
    monkeypatch.setattr(settings, "console_dir", str(tmp_path))
    monkeypatch.setattr(settings, "mongo_mock", True)
    with TestClient(create_app()) as c:
        yield c


@pytest.fixture
def unbuilt(tmp_path, monkeypatch):
    """A checkout that has never run `npm run build`."""
    monkeypatch.setattr(settings, "console_dir", str(tmp_path / "absent"))
    monkeypatch.setattr(settings, "mongo_mock", True)
    with TestClient(create_app()) as c:
        yield c


def test_root_serves_the_built_console(built):
    r = built.get("/")
    assert r.status_code == 200
    assert "id=app" in r.text
    assert built.get("/assets/index.css").status_code == 200


def test_console_does_not_swallow_unmatched_api_paths(built):
    assert built.get("/api/v1/instances").status_code == 401  # exists, needs auth
    assert built.get("/healthz").json() == {"status": "ok"}
    # A root static mount would answer these instead — 405 for the POST, the
    # SPA's own HTML for the GET. Both must stay honest 404s.
    assert built.post("/api/v1/machines/GBOX_0001/profiles/deploy").status_code == 404
    assert built.get("/api/v1/nonexistent").status_code == 404


def test_unbuilt_checkout_explains_itself_instead_of_404(unbuilt):
    r = unbuilt.get("/")
    assert r.status_code == 200
    assert "npm run build" in r.text
    # The API is unaffected by the missing bundle.
    assert unbuilt.get("/api/v1/instances").status_code == 401
    assert unbuilt.get("/healthz").status_code == 200
