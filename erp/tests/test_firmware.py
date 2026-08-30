# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The firmware channel (ADR-0022 d14) — an operator's selection, a gateway's pull.

What is asserted here is as much what the API refuses as what it records: the
selection is a record and not an update, the gateway's routes write nothing, and
there is no way to ask this API what a node is running (ADR-0029 d15;
ADR-0022 d9, alternative Q).

The release used throughout is the one the repository actually publishes, read
from `store/`, because that is what makes a release selectable (ADR-0029 d13).
"""

from __future__ import annotations

import asyncio

import pytest
from fastapi.testclient import TestClient

from app.api.deps import get_warehouse
from app.config import settings
from app.main import create_app
from app.services import firmware

AUTH = {"Authorization": "Bearer dev-operator-token"}
PROXY = "10.9.0.2"
VERIFIED = {"X-Client-Verify": "SUCCESS", "X-Client-DN": "CN=GBOX_0001,OU=gw,O=OP"}

RELEASE = "E0001-000001-F"


@pytest.fixture
def client(monkeypatch, warehouse):
    # The release these tests select must be in the bucket, because that is what
    # makes it selectable (ADR-0022 d14).
    for slot in ("a", "b"):
        warehouse.objects[f"{RELEASE}-slot-{slot}.img"] = b"IGIM-header-and-body"
    monkeypatch.setattr(settings, "mongo_mock", True)
    monkeypatch.setattr(settings, "gateway_trusted_proxies", ["10.9.0.0/24"])
    app = create_app()
    app.dependency_overrides[get_warehouse] = lambda: warehouse
    with TestClient(app, client=(PROXY, 51000)) as c:
        yield c


# ---- what makes a release selectable ---------------------------------------


def test_a_release_is_what_the_warehouse_can_serve(warehouse):
    """The bucket decides, not a checkout — it is where the gateway's bytes come from."""
    assert asyncio.run(firmware.is_release(warehouse, RELEASE)) is False

    warehouse.objects[f"{RELEASE}-slot-a.img"] = b"header+body"
    # One slot image is not a release: a node running the other slot could not be
    # served, and that failure would surface at a gateway days later (d17).
    assert asyncio.run(firmware.is_release(warehouse, RELEASE)) is False
    assert asyncio.run(firmware.available_releases(warehouse)) == []

    warehouse.objects[f"{RELEASE}-slot-b.img"] = b"header+body"
    assert asyncio.run(firmware.is_release(warehouse, RELEASE)) is True
    assert asyncio.run(firmware.available_releases(warehouse)) == [RELEASE]


def test_a_key_that_is_not_a_release_root_is_not_a_release(warehouse):
    warehouse.objects["E0002-000001-D-slot-a.img"] = b"x"
    warehouse.objects["E0002-000001-D-slot-b.img"] = b"x"
    assert asyncio.run(firmware.available_releases(warehouse)) == []


def test_releases_are_listed_with_their_decoded_version(client):
    listed = client.get("/api/v1/firmware-releases", headers=AUTH).json()
    mine = next(r for r in listed if r["release_root"] == RELEASE)
    assert mine["version"] == "000001"
    assert mine["version_label"] == "v0.0.1"
    assert mine["artifact_keys"] == [f"{RELEASE}-slot-a.img", f"{RELEASE}-slot-b.img"]


# ---- the operator's selection ----------------------------------------------


def test_recording_an_intended_release(client):
    put = client.put(
        "/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH
    )
    assert put.status_code == 200
    body = put.json()
    assert body["machine_id"] == "GBOX_0001"
    assert body["release_root"] == RELEASE
    assert body["version_label"] == "v0.0.1"

    got = client.get("/api/v1/machines/GBOX_0001/firmware", headers=AUTH).json()
    assert got["release_root"] == RELEASE


def test_the_selection_is_latest_wins_and_keeps_no_history(client):
    """ADR-0022 d14: configuration kept current, not a history."""
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    got = client.get("/api/v1/machines/GBOX_0001/firmware", headers=AUTH).json()
    assert got["release_root"] == RELEASE
    # No route returns a previous selection, and no field carries one.
    assert "previous_release" not in got
    assert "history" not in got


def test_a_release_the_store_does_not_publish_is_refused(client):
    refused = client.put(
        "/api/v1/machines/GBOX_0001/firmware",
        json={"release_root": "E0001-999999-F"},
        headers=AUTH,
    )
    assert refused.status_code == 404
    assert "warehouse" in refused.json()["detail"]
    # Nothing was recorded by the attempt.
    assert client.get("/api/v1/machines/GBOX_0001/firmware", headers=AUTH).status_code == 404


def test_a_slot_image_is_not_a_release(client):
    """An operator selects a release; the node chooses the slot (ADR-0029 d3)."""
    refused = client.put(
        "/api/v1/machines/GBOX_0001/firmware",
        json={"release_root": f"{RELEASE}-slot-a.img"},
        headers=AUTH,
    )
    assert refused.status_code == 422


def test_the_machine_must_be_a_machine_identifier(client):
    refused = client.put(
        "/api/v1/machines/not-a-gbox/firmware", json={"release_root": RELEASE}, headers=AUTH
    )
    assert refused.status_code == 422


def test_a_readonly_token_cannot_select(client, monkeypatch):
    monkeypatch.setitem(settings.api_tokens, "ro-token", "readonly")
    refused = client.put(
        "/api/v1/machines/GBOX_0001/firmware",
        json={"release_root": RELEASE},
        headers={"Authorization": "Bearer ro-token"},
    )
    assert refused.status_code == 403


# ---- the gateway's pull ----------------------------------------------------


def test_the_gateway_pulls_the_release_its_machine_intends(client):
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    pulled = client.get("/api/v1/gateway/firmware", headers=VERIFIED)
    assert pulled.status_code == 200
    assert pulled.json()["release_root"] == RELEASE
    assert pulled.json()["artifact_keys"] == [
        f"{RELEASE}-slot-a.img",
        f"{RELEASE}-slot-b.img",
    ]


def test_the_gateway_identity_comes_from_the_certificate(client):
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    # A different verified gateway gets its own (absent) intent, and a query
    # parameter does not override the certificate.
    other = client.get(
        "/api/v1/gateway/firmware",
        headers={"X-Client-Verify": "SUCCESS", "X-Client-DN": "CN=GBOX_0002,OU=gw,O=OP"},
    )
    assert other.status_code == 404
    assert (
        client.get(
            "/api/v1/gateway/firmware", headers=VERIFIED, params={"gbox": "GBOX_0002"}
        ).json()["release_root"]
        == RELEASE
    )


def test_an_unverified_caller_gets_nothing(client):
    assert client.get("/api/v1/gateway/firmware").status_code == 401
    assert (
        client.get("/api/v1/gateway/firmware", headers={"X-Client-Verify": "NONE"}).status_code
        == 401
    )


def test_the_gateway_reads_the_artifact_through(client, warehouse):
    warehouse.objects[f"{RELEASE}-slot-a.img"] = b"IGIM-header-and-body"
    warehouse.content_types[f"{RELEASE}-slot-a.img"] = "application/octet-stream"
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    got = client.get(f"/api/v1/gateway/firmware/{RELEASE}-slot-a.img/content", headers=VERIFIED)
    assert got.status_code == 200
    assert got.content == b"IGIM-header-and-body"


def test_the_gateway_cannot_fetch_a_key_outside_its_intended_release(client, warehouse):
    """The pull channel is not a general read primitive over the bucket."""
    warehouse.objects["E0002-000001-L.csv"] = b"a bill of materials"
    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    refused = client.get("/api/v1/gateway/firmware/E0002-000001-L.csv/content", headers=VERIFIED)
    assert refused.status_code == 404
    # Nor the bootloader image, which is flashable-only and never served on the
    # bus (ADR-0029 d10, d13).
    assert (
        client.get(
            f"/api/v1/gateway/firmware/{RELEASE}-boot.hex/content", headers=VERIFIED
        ).status_code
        == 404
    )


def test_a_gateway_with_no_intent_recorded_is_told_so(client):
    assert client.get("/api/v1/gateway/firmware", headers=VERIFIED).status_code == 404
    assert (
        client.get(
            f"/api/v1/gateway/firmware/{RELEASE}-slot-a.img/content", headers=VERIFIED
        ).status_code
        == 404
    )


# ---- the boundary ----------------------------------------------------------


def _endpoints(app):
    """Every mounted endpoint, flattened.

    ``app.routes`` holds mounts and included-router wrappers beside real routes,
    and the wrapper carries the included router rather than a path — so a test
    that iterated the top level alone would silently inspect nothing, which for
    an assertion about an *absence* would pass for the wrong reason.
    """
    found = []
    pending = list(app.routes)
    while pending:
        route = pending.pop()
        inner = getattr(route, "original_router", None)
        if inner is not None:
            pending.extend(inner.routes)
        elif hasattr(route, "path") and hasattr(route, "methods"):
            found.append(route)
    return found


def test_there_is_no_route_that_records_what_a_node_runs(client):
    """ADR-0022 d9 and alternative Q, as an absence the test suite defends.

    The firmware channel is the one place a machine-written 'we updated' would be
    most tempting. If a future route adds it, this fails.
    """
    endpoints = _endpoints(client.app)
    for route in endpoints:
        assert "flash" not in route.path
        assert "running" not in route.path

    # And every gateway route is read-only: no method but GET is mounted.
    gateway_routes = [r for r in endpoints if r.path.startswith("/api/v1/gateway/")]
    assert gateway_routes, "the gateway routes should be mounted"
    for route in gateway_routes:
        assert set(route.methods) <= {"GET", "HEAD"}


# ---- enrolling the cabinet the rest of this hangs off -----------------------


def test_a_machine_can_be_registered_and_then_carries_an_intent(client):
    """Without this route the record cannot acquire its first cabinet.

    The seed fixture used to be the only writer of `foundation.machine`, so its
    removal would otherwise have left every machine-scoped view unreachable.
    """
    assert client.get("/api/v1/machines", headers=AUTH).json() == []

    put = client.put(
        "/api/v1/machines/GBOX_0001", json={"notes": "reference cabinet"}, headers=AUTH
    )
    assert put.status_code == 200
    listed = client.get("/api/v1/machines", headers=AUTH).json()
    assert listed == [{"machine_id": "GBOX_0001", "notes": "reference cabinet"}]

    client.put("/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH)
    assert (
        client.get("/api/v1/machines/GBOX_0001/firmware", headers=AUTH).json()["release_root"]
        == RELEASE
    )


def test_registering_a_machine_twice_edits_it(client):
    client.put("/api/v1/machines/GBOX_0001", json={"notes": "first"}, headers=AUTH)
    client.put("/api/v1/machines/GBOX_0001", json={"notes": "second"}, headers=AUTH)
    listed = client.get("/api/v1/machines", headers=AUTH).json()
    assert listed == [{"machine_id": "GBOX_0001", "notes": "second"}]


def test_a_machine_identifier_is_required(client):
    assert client.put("/api/v1/machines/cabinet-1", json={}, headers=AUTH).status_code == 422


def test_the_erp_needs_no_repository_checkout_for_firmware(client, monkeypatch):
    """The firmware channel does not read store/ at all.

    Pointing store_dir at nothing must not affect it: the ERP serves firmware from
    the warehouse, so a deployment that has not bind-mounted the repository still
    has a working firmware channel.
    """
    monkeypatch.setattr(settings, "store_dir", "/nonexistent/store")
    listed = client.get("/api/v1/firmware-releases", headers=AUTH)
    assert listed.status_code == 200
    assert [r["release_root"] for r in listed.json()] == [RELEASE]
    assert (
        client.put(
            "/api/v1/machines/GBOX_0001/firmware", json={"release_root": RELEASE}, headers=AUTH
        ).status_code
        == 200
    )
