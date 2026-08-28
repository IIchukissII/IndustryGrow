# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API v1 tests (ADR-0022) — auth, allocation, integration, and the deliberate
absences (no deploy path, doc allowlist). In-memory Mongo, no warehouse calls."""

from __future__ import annotations

import base64
import json

import pytest
from fastapi.testclient import TestClient

from app.api.deps import get_warehouse
from app.config import settings
from app.main import create_app

AUTH = {"Authorization": "Bearer dev-operator-token"}


@pytest.fixture
def client(monkeypatch, warehouse):
    monkeypatch.setattr(settings, "mongo_mock", True)
    monkeypatch.setattr(settings, "seed_on_start", False)
    app = create_app()
    app.dependency_overrides[get_warehouse] = lambda: warehouse
    with TestClient(app) as c:
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


def test_both_axes_arrive_read_into_fields(client):
    """ADR-0022 d13: the API speaks the grammar on the instance axis too.

    The depth and the version are both six digits and mean unrelated things
    (ADR-0017 d1). `020100` here is main 02 as a position and v2.1.0 as a
    version — a caller telling them apart by counting hyphens is exactly the
    re-implementation of the scheme this avoids.
    """
    client.post(
        "/api/v1/instances",
        json={"e_number": "E0002", "version": "020100", "quantity": 1},
        headers=AUTH,
    )
    inst = "E0002-020100-000001"
    client.put(
        "/api/v1/machines/GBOX_0001/positions/020100",
        json={"instance_id": inst},
        headers=AUTH,
    )

    listed = client.get("/api/v1/instances", headers=AUTH).json()[0]
    assert listed["version"] == "020100" and listed["version_label"] == "v2.1.0"

    rec = client.get("/api/v1/machines/GBOX_0001/integration", headers=AUTH).json()[0]
    # Position side: three two-digit levels, not a six-digit number (d7).
    assert rec["depth_levels"] == [2, 1, 0]
    assert rec["depth_label"] == "02.01.00"
    # Identity side, split out of the instance the position holds.
    assert (rec["e_number"], rec["version"], rec["serial"]) == ("E0002", "020100", "000001")
    # Same six digits, two different readings — which is the whole point.
    assert rec["version_label"] == "v2.1.0" and rec["depth_code"] == "020100"


def test_document_allowlist_rejects_type_layer(client):
    files = {"file": ("x.zip", b"data", "application/zip")}
    r = client.post(
        "/api/v1/instances/E0004-010100-000001/documents",
        data={"doc_type": "D-fab"},
        files=files,
        headers=AUTH,
    )
    assert r.status_code == 422  # type-layer docs never route through the ERP


def _one_instance(client) -> str:
    client.post(
        "/api/v1/instances",
        json={"e_number": "E0002", "version": "020100", "quantity": 1},
        headers=AUTH,
    )
    return "E0002-020100-000001"


def test_document_upload_writes_the_blob_then_indexes_its_key(client, warehouse):
    inst = _one_instance(client)
    r = client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "qp"},
        files={"file": ("qp.pdf", b"%PDF-1.7 quality plan", "application/pdf")},
        headers=AUTH,
    )
    assert r.status_code == 200

    # The identifier *is* the object key (ADR-0017 d15) — no separate blob id.
    key = f"{inst}-QP"
    assert r.json()["object_key"] == key
    assert warehouse.objects[key] == b"%PDF-1.7 quality plan"
    assert warehouse.content_types[key] == "application/pdf"

    listed = client.get(f"/api/v1/instances/{inst}/documents", headers=AUTH).json()
    assert [d["object_key"] for d in listed] == [key]


def test_an_indexed_document_can_be_retrieved(client, warehouse):
    # ADR-0022 d7 offers exactly two things for a blob: the key, or a time-limited
    # URL. This is the second — and the API still returns no blob content, which is
    # why the console fetches from the object store rather than through here.
    inst = _one_instance(client)
    client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "QP"},
        files={"file": ("qp.pdf", b"%PDF-1.7", "application/pdf")},
        headers=AUTH,
    )
    r = client.get(f"/api/v1/instances/{inst}/documents/{inst}-QP/url", headers=AUTH)
    assert r.status_code == 200
    body = r.json()
    assert body["object_key"] == f"{inst}-QP"
    assert body["expires_in"] > 0
    assert body["url"].startswith("http")
    # The bytes are not in the response. That is the boundary, not an omission.
    assert "%PDF" not in r.text


def test_only_indexed_keys_can_be_retrieved(client, warehouse):
    # The guard that keeps this route from being a general read primitive over the
    # bucket. Without the index lookup, a caller could name any key — including the
    # store/ mirror — and the ERP would presign it, which is a different resource
    # from the one ADR-0022 d1 exposes.
    inst = _one_instance(client)
    warehouse.objects["E0001-000002-D-fab.zip"] = b"repo content, not this instance's"

    r = client.get(f"/api/v1/instances/{inst}/documents/E0001-000002-D-fab.zip/url", headers=AUTH)
    assert r.status_code == 404
    assert "no indexed document" in r.json()["detail"]


def test_an_indexed_key_that_does_not_resolve_says_so(client, warehouse):
    # ADR-0021 d7: a recorded key always resolves. When it does not, the index and
    # the store have diverged — better to say that than hand back a URL that 404s
    # at the object store with no explanation.
    inst = _one_instance(client)
    posted = client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "CP", "doc_date": "2026-07-22"},
        files={"file": ("cp.pdf", b"%PDF-1.7", "application/pdf")},
        headers=AUTH,
    )
    key = posted.json()["object_key"]
    warehouse.objects.pop(key)  # the divergence

    r = client.get(f"/api/v1/instances/{inst}/documents/{key}/url", headers=AUTH)
    assert r.status_code == 502
    assert "diverged" in r.json()["detail"]


def test_the_repository_documents_are_listed_by_what_they_are(client):
    # ADR-0022 d1's 2026-07-26 clarification: read-only, storing nothing, the same
    # shape ADR-0023 gave REGISTRY.md. The listing comes from the repository's
    # store/ directory, so the repo decides what is servable — not the bucket.
    docs = client.get("/api/v1/store-documents", headers=AUTH).json()
    by_key = {d["object_key"]: d for d in docs}
    assert "SP0004-M-gateway-bringup.md" in by_key
    assert by_key["SP0004-M-gateway-bringup.md"]["kind"] == "manual"
    assert by_key["E0007-000001-S.pdf"]["kind"] == "schematic"
    assert all(d["size_bytes"] > 0 for d in docs)


def test_a_listed_document_carries_its_key_read_into_fields(client):
    """ADR-0022 d13: the API speaks the grammar, so a console never re-parses it.

    A client that derived root/version/layer from the string itself would be a
    second implementation of ADR-0017's scheme, free to drift from the one the
    store is actually filed by.
    """
    by_key = {
        d["object_key"]: d for d in client.get("/api/v1/store-documents", headers=AUTH).json()
    }

    fab = by_key["E0001-000003-D-fab.zip"]
    assert (fab["root"], fab["version"], fab["layer"], fab["slug"]) == (
        "E0001",
        "000003",
        "D",
        "fab",
    )
    assert fab["version_label"] == "v0.0.3"
    # One indivisible manufacturing deliverable, not a folder of gerbers (d18).
    assert fab["packaged"] and fab["kind"] == "fabrication package"

    # A withdrawal is a published fact about a version, and the status token is
    # what makes it machine-readable (d17) — so it arrives as a field, not as a
    # `.zip` the reader has to call an "archive".
    assert by_key["E0001-000001-BLOCKED.zip"]["status"] == "BLOCKED"
    assert by_key["E0001-000001-SUPERSEDED.zip"]["status"] == "SUPERSEDED"
    assert by_key["E0001-000002-SUPERSEDED.zip"]["status"] == "SUPERSEDED"

    # Same E prefix, different axis: the firmware is not swept into the board's
    # withdrawn v0.0.1 set (d16, d17) and its version is the codebase's.
    assert by_key["E0001-000001-F.hex"]["status"] is None
    assert by_key["E0001-000001-F.hex"]["layer_label"] == "firmware"

    # An SP root carries no version — the supplier owns it (ADR-0019 d2).
    manual = by_key["SP0004-M-gateway-bringup.md"]
    assert (manual["root_kind"], manual["version"]) == ("SP", None)
    assert manual["slug"] == "gateway-bringup"


def test_a_repository_document_can_be_read(client, warehouse):
    warehouse.objects["SP0004-M-gateway-bringup.md"] = b"# Gateway bring-up\n"
    r = client.get("/api/v1/store-documents/SP0004-M-gateway-bringup.md/url", headers=AUTH)
    assert r.status_code == 200
    assert r.json()["object_key"] == "SP0004-M-gateway-bringup.md"
    # Still a grant, never content — the ERP is not a proxy for the store (d7).
    assert "Gateway bring-up" not in r.text


def test_only_store_files_are_servable(client, warehouse):
    # The guard that keeps this a read of the *mirror* rather than of the bucket.
    # An operator-private instance document lives in the same flat keyspace and
    # must not be reachable here — it has its own route, with its own index check.
    warehouse.objects["E0002-020100-000001-CC-20260722"] = b"%PDF private"
    r = client.get("/api/v1/store-documents/E0002-020100-000001-CC-20260722/url", headers=AUTH)
    assert r.status_code == 404
    assert "not a document in the repository's store/" in r.json()["detail"]


def test_a_key_cannot_walk_out_of_the_store(client, warehouse):
    # Resolved against the directory, not joined onto it. Without that, a key of
    # `../app/config.py` would name a real file and the guard would pass it.
    for escape in ("../pyproject.toml", "..%2Fpyproject.toml", "../../README.md"):
        r = client.get(f"/api/v1/store-documents/{escape}/url", headers=AUTH)
        assert r.status_code in (404, 307), escape


def test_a_store_file_not_yet_mirrored_says_so(client, warehouse):
    # In store/ but never synced. The fix is store_sync, and saying which command
    # beats a bare 404 that reads as "this document does not exist".
    r = client.get("/api/v1/store-documents/SP0004-M-atecc-provisioning.md/url", headers=AUTH)
    assert r.status_code == 404
    assert "store_sync" in r.json()["detail"]


def test_a_document_can_be_read_through(client, warehouse):
    # ADR-0022 rev 2 d7's third form. The ERP holds no copy — the bytes stream from
    # the object store — so ADR-0021 d7's "does not duplicate" still holds. It
    # exists because a presigned URL is only spendable by a browser the store has
    # been configured to accept, and a grant that cannot be spent is nothing.
    warehouse.objects["SP0004-M-gateway-bringup.md"] = b"# Gateway bring-up\n"
    warehouse.content_types["SP0004-M-gateway-bringup.md"] = "text/markdown"
    r = client.get("/api/v1/store-documents/SP0004-M-gateway-bringup.md/content", headers=AUTH)
    assert r.status_code == 200
    assert r.text == "# Gateway bring-up\n"
    assert "markdown" in r.headers["content-type"]
    # inline, not attachment: the point is to read it here, not to download it.
    assert r.headers["content-disposition"].startswith("inline")


def test_read_through_honours_the_same_guards(client, warehouse):
    # The read-through must not be a softer door than the URL route beside it.
    warehouse.objects["E0002-020100-000001-CC-20260722"] = b"%PDF private"
    assert (
        client.get(
            "/api/v1/store-documents/E0002-020100-000001-CC-20260722/content", headers=AUTH
        ).status_code
        == 404
    )
    assert client.get(
        "/api/v1/store-documents/..%2Fpyproject.toml/content", headers=AUTH
    ).status_code in (404, 307)
    # An instance document is readable only for the instance that indexed it.
    inst = _one_instance(client)
    assert (
        client.get(
            f"/api/v1/instances/{inst}/documents/E0001-000002-D-pinmap.md/content", headers=AUTH
        ).status_code
        == 404
    )


def test_calibration_key_carries_its_date_and_is_queryable(client, warehouse):
    inst = _one_instance(client)
    r = client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "CC", "doc_date": "2026-07-22", "valid_until": "2026-08-01"},
        files={"file": ("cal.pdf", b"%PDF-1.7 cert", "application/pdf")},
        headers=AUTH,
    )
    # A recalibration must not overwrite its predecessor, so the CC key is dated.
    assert r.json()["object_key"] == f"{inst}-CC-20260722"
    assert f"{inst}-CC-20260722" in warehouse.objects

    # The -CP carries the same guarantee: it is the raw data behind the -CC, and
    # a second run must not overwrite the first run's points.
    r = client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "CP", "doc_date": "2026-07-22"},
        files={"file": ("points.pdf", b"%PDF-1.7 points", "application/pdf")},
        headers=AUTH,
    )
    assert r.json()["object_key"] == f"{inst}-CP-20260722"
    assert f"{inst}-CP-20260722" in warehouse.objects

    # A later run lands beside it, not on top of it.
    client.post(
        f"/api/v1/instances/{inst}/documents",
        data={"doc_type": "CP", "doc_date": "2026-09-01"},
        files={"file": ("points2.pdf", b"%PDF-1.7 later", "application/pdf")},
        headers=AUTH,
    )
    assert warehouse.objects[f"{inst}-CP-20260722"] == b"%PDF-1.7 points"
    assert warehouse.objects[f"{inst}-CP-20260901"] == b"%PDF-1.7 later"

    # ADR-0021 d7: the point of indexing the key is that expiry is a query here.
    expiring = client.get("/api/v1/calibration/expiring?days=3650", headers=AUTH).json()
    assert [c["instance_full_id"] for c in expiring] == [inst]


def test_failed_blob_write_leaves_no_index_row(client, warehouse):
    inst = _one_instance(client)
    warehouse.fail_on_put = True

    with pytest.raises(RuntimeError):  # blob first: the store's failure is the request's
        client.post(
            f"/api/v1/instances/{inst}/documents",
            data={"doc_type": "QR"},
            files={"file": ("qr.pdf", b"%PDF-1.7", "application/pdf")},
            headers=AUTH,
        )

    # A recorded key must always resolve — an unwritten blob is never indexed.
    assert client.get(f"/api/v1/instances/{inst}/documents", headers=AUTH).json() == []


def test_no_deploy_or_push_endpoint(client):
    # ADR-0022 d8: the ERP is not a deploy path — these must not exist.
    assert (
        client.post("/api/v1/machines/GBOX_0001/profiles/deploy", headers=AUTH).status_code == 404
    )
    assert client.post("/api/v1/gateways/GBOX_0001/activate", headers=AUTH).status_code == 404
    assert client.post("/api/v1/telemetry", headers=AUTH).status_code == 404


def _profile_body(tag: str, *, signed: bool = True, machine: str = "GBOX_0001") -> dict:
    """A stored profile version as ADR-0025 d6 shapes it — opaque signed bytes.

    The signature here is a placeholder: the ERP stores and serves it without
    verifying anything (it holds no key and is not an authority — ADR-0025 d3).
    What the ERP enforces is that one is *present* before a version is recorded
    active (d11); whether it verifies is the gateway's business.
    """
    document = json.dumps({"machine_id": machine, "version_tag": tag, "setpoints": {}}).encode()
    return {
        "version_tag": tag,
        "document_b64": base64.b64encode(document).decode(),
        "signature": "cGxhY2Vob2xkZXI=" if signed else None,
    }


def _machine_binding(**over) -> dict:
    body = {
        "vendor_serial": "SP0004-SN-000123",
        "atecc_serial": "0123AABBCCDDEEFF01",
        "public_key_fingerprint": "A" * 64,
        "cert_serial": "5141236E2770C7FC",
        "cert_not_before": "2026-07-01T00:00:00Z",
        "cert_not_after": "2026-10-01T00:00:00Z",
    }
    body.update(over)
    return body


def test_a_machine_carries_its_own_provisioning_binding(client):
    # ADR-0022 rev 1 d12: a gateway is SP0004 with no Exxxx-VVVVVV-NNNNNN serial,
    # so it cannot use the E-instance binding route (alternative N rejects minting
    # it a synthetic instance id to reach that row).
    r = client.post(
        "/api/v1/machines/GBOX_0001/provisioning", json=_machine_binding(), headers=AUTH
    )
    assert r.status_code == 200

    channel = client.get("/api/v1/machines/GBOX_0001/gateway-channel", headers=AUTH).json()
    assert channel["identity"]["vendor_serial"] == "SP0004-SN-000123"
    assert channel["identity"]["cert_serial"] == "5141236E2770C7FC"
    assert channel["identity"]["public_key_fingerprint"] == "A" * 64


def test_the_binding_is_upserted_so_renewal_replaces_it(client):
    # Certificates are short-lived and auto-renewed (ADR-0007 d7), so this route
    # is called again on re-certification. It must not accumulate: what a
    # certificate *used to be* is not a question this API answers (d12).
    client.post("/api/v1/machines/GBOX_0001/provisioning", json=_machine_binding(), headers=AUTH)
    client.post(
        "/api/v1/machines/GBOX_0001/provisioning",
        json=_machine_binding(cert_serial="NEWSERIAL01", cert_not_after="2027-01-01T00:00:00Z"),
        headers=AUTH,
    )
    identity = client.get("/api/v1/machines/GBOX_0001/gateway-channel", headers=AUTH).json()[
        "identity"
    ]
    assert identity["cert_serial"] == "NEWSERIAL01"
    # The anchors that survive renewal are unchanged (ADR-0007 rev 1 d10d).
    assert identity["public_key_fingerprint"] == "A" * 64
    assert identity["vendor_serial"] == "SP0004-SN-000123"


def test_the_channel_reports_days_until_the_recorded_certificate_expires(client):
    # The same shape as the calibration-expiring query (ADR-0021 d7): a query over
    # metadata the ERP owns, resolving to something an operator must act on.
    from datetime import UTC, datetime, timedelta

    soon = (datetime.now(UTC) + timedelta(days=5)).isoformat()
    client.post(
        "/api/v1/machines/GBOX_0001/provisioning",
        json=_machine_binding(cert_not_after=soon),
        headers=AUTH,
    )
    identity = client.get("/api/v1/machines/GBOX_0001/gateway-channel", headers=AUTH).json()[
        "identity"
    ]
    assert 4 <= identity["expires_in_days"] <= 5


def test_a_machine_binding_needs_a_machine_identifier(client):
    r = client.post(
        "/api/v1/machines/E0002-020100-000001/provisioning",
        json=_machine_binding(),
        headers=AUTH,
    )
    assert r.status_code == 422


def test_the_gateway_channel_never_reports_a_pull(client):
    # ADR-0022 d8 (rev 1) / d9: the pull is a pure read and the ERP records no
    # operational act. This asserts the *absence* deliberately — a future field
    # named anything like this should fail here and be argued through the ADR
    # rather than added quietly.
    client.post("/api/v1/machines/GBOX_0001/provisioning", json=_machine_binding(), headers=AUTH)
    channel = client.get("/api/v1/machines/GBOX_0001/gateway-channel", headers=AUTH).json()
    assert not [k for k in channel if "pull" in k.lower() or "last_seen" in k.lower()]


def test_the_channel_of_an_unprovisioned_machine_is_empty_not_absent(client):
    # A machine with no binding is a normal, expected state (it has not been
    # provisioned yet), not a 404 — the console needs to render that difference.
    channel = client.get("/api/v1/machines/GBOX_0009/gateway-channel", headers=AUTH).json()
    assert channel["machine_id"] == "GBOX_0009"
    assert channel["identity"] is None
    assert channel["active_version"] is None


def test_the_channel_counts_versions_a_gateway_could_not_apply(client):
    # Unsigned versions are stuck: ADR-0025 d11 refuses to activate them, so the
    # gateway can never receive them. Operator-fixable, so worth surfacing.
    client.post("/api/v1/machines/GBOX_0001/profiles", json=_profile_body("v1"), headers=AUTH)
    client.post(
        "/api/v1/machines/GBOX_0001/profiles", json=_profile_body("v2", signed=False), headers=AUTH
    )
    channel = client.get("/api/v1/machines/GBOX_0001/gateway-channel", headers=AUTH).json()
    assert channel["stored_versions"] == 2
    assert channel["unsigned_versions"] == 1


def test_profile_store_then_record_active(client):
    client.post("/api/v1/machines/GBOX_0001/profiles", json=_profile_body("v1"), headers=AUTH)
    r = client.put(
        "/api/v1/machines/GBOX_0001/active-profile",
        json={"version_tag": "v1"},
        headers=AUTH,
    )
    assert r.status_code == 200 and r.json()["ok"] is True


def test_an_unsigned_version_cannot_be_recorded_active(client):
    # ADR-0025 d11: a version may be parked unsigned, and recording it active would
    # describe a deployment that cannot happen — a gateway will not apply what it
    # cannot verify (ADR-0015 d7). Caught here, where an operator can fix it.
    client.post(
        "/api/v1/machines/GBOX_0001/profiles",
        json=_profile_body("v9", signed=False),
        headers=AUTH,
    )
    r = client.put(
        "/api/v1/machines/GBOX_0001/active-profile", json={"version_tag": "v9"}, headers=AUTH
    )
    assert r.status_code == 409
    assert "no signature" in r.json()["detail"]


def test_recording_an_unknown_version_active_is_a_404(client):
    r = client.put(
        "/api/v1/machines/GBOX_0001/active-profile", json={"version_tag": "nope"}, headers=AUTH
    )
    assert r.status_code == 404


def test_profile_list_marks_the_active_version(client):
    for tag in ("v1", "v2"):
        client.post("/api/v1/machines/GBOX_0001/profiles", json=_profile_body(tag), headers=AUTH)
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


def test_gateway_pull_is_closed_without_an_mtls_front_end(client):
    # Identity comes from a verified certificate, not a parameter (ADR-0022 d2).
    # With no trusted proxy configured — the default — there is nothing that could
    # have verified one, so the channel is shut rather than credulous. The seam
    # itself is tested in test_mtls.py; this holds the default in place.
    assert client.get("/api/v1/gateway/active-profile").status_code == 503
