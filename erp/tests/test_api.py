# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API v1 tests (ADR-0022) — auth, allocation, integration, and the deliberate
absences (no deploy path, doc allowlist). In-memory Mongo, no warehouse calls."""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.config import settings
from app.main import create_app

AUTH = {"Authorization": "Bearer dev-operator-token"}


@pytest.fixture
def client(monkeypatch):
    monkeypatch.setattr(settings, "mongo_mock", True)
    monkeypatch.setattr(settings, "seed_on_start", False)
    with TestClient(create_app()) as c:
        yield c


def test_auth_required(client):
    assert client.get("/api/v1/instances").status_code == 401
    assert (
        client.get("/api/v1/instances", headers={"Authorization": "Bearer nope"}).status_code == 401
    )


def test_allocate_is_authoritative_and_gap_free(client):
    r = client.post(
        "/api/v1/instances",
        json={"e_number": "E0002", "version": "020100", "quantity": 3},
        headers=AUTH,
    )
    assert r.status_code == 200
    assert r.json()["serials"] == [
        "E0002-020100-000001",
        "E0002-020100-000002",
        "E0002-020100-000003",
    ]
    assert len(client.get("/api/v1/instances", headers=AUTH).json()) == 3


def test_install_move_remove(client):
    client.post(
        "/api/v1/instances",
        json={"e_number": "E0002", "version": "020100", "quantity": 1},
        headers=AUTH,
    )
    inst = "E0002-020100-000001"
    r = client.put(
        "/api/v1/machines/GBOX_0001/positions/010100",
        json={"instance_id": inst},
        headers=AUTH,
    )
    assert r.status_code == 200
    estate = client.get("/api/v1/machines/GBOX_0001/integration", headers=AUTH).json()
    assert len(estate) == 1 and estate[0]["instance_id"] == inst

    d = client.delete("/api/v1/machines/GBOX_0001/positions/010100", headers=AUTH)
    assert d.status_code == 200
    assert client.get("/api/v1/machines/GBOX_0001/integration", headers=AUTH).json() == []

    history = client.get(f"/api/v1/instances/{inst}/history", headers=AUTH).json()
    assert len(history) == 1 and history[0]["removed_at"] is not None


def test_document_allowlist_rejects_type_layer(client):
    files = {"file": ("x.zip", b"data", "application/zip")}
    r = client.post(
        "/api/v1/instances/E0004-010100-000001/documents",
        data={"doc_type": "D-fab"},
        files=files,
        headers=AUTH,
    )
    assert r.status_code == 422  # type-layer docs never route through the ERP


def test_no_deploy_or_push_endpoint(client):
    # ADR-0022 d8: the ERP is not a deploy path — these must not exist.
    assert (
        client.post("/api/v1/machines/GBOX_0001/profiles/deploy", headers=AUTH).status_code == 404
    )
    assert client.post("/api/v1/gateways/GBOX_0001/activate", headers=AUTH).status_code == 404
    assert client.post("/api/v1/telemetry", headers=AUTH).status_code == 404


def test_profile_store_then_record_active(client):
    client.post(
        "/api/v1/machines/GBOX_0001/profiles",
        json={"version_tag": "v1", "payload": {"setpoints": {}, "model": {}}},
        headers=AUTH,
    )
    r = client.put(
        "/api/v1/machines/GBOX_0001/active-profile",
        json={"version_tag": "v1"},
        headers=AUTH,
    )
    assert r.status_code == 200 and r.json()["ok"] is True


def test_profile_list_marks_the_active_version(client):
    for tag in ("v1", "v2"):
        client.post(
            "/api/v1/machines/GBOX_0001/profiles",
            json={"version_tag": tag, "payload": {"setpoints": {}, "model": {}}},
            headers=AUTH,
        )
    listed = client.get("/api/v1/machines/GBOX_0001/profiles", headers=AUTH).json()
    assert all(p["active"] is False for p in listed)  # storing never activates

    client.put(
        "/api/v1/machines/GBOX_0001/active-profile", json={"version_tag": "v1"}, headers=AUTH
    )
    listed = client.get("/api/v1/machines/GBOX_0001/profiles", headers=AUTH).json()
    assert {p["version_tag"]: p["active"] for p in listed} == {"v1": True, "v2": False}


def test_instance_documents_listing(client):
    client.post(
        "/api/v1/instances",
        json={"e_number": "E0002", "version": "020100", "quantity": 1},
        headers=AUTH,
    )
    r = client.get("/api/v1/instances/E0002-020100-000001/documents", headers=AUTH)
    assert r.status_code == 200 and r.json() == []


def test_gateway_pull_needs_mtls_identity(client):
    # No forwarded cert identity -> 401 (identity comes from the cert, not a param).
    assert client.get("/api/v1/gateway/active-profile").status_code == 401
