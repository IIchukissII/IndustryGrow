# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Live-warehouse tests — the one part of the ERP a fake cannot vouch for.

Everything else in this suite runs against in-memory doubles, which is right:
they are fast and hermetic. But "write the blob to the object store, then index
its key" (ADR-0021) is a claim about a *real* S3-compatible service, and a fake
that returns success proves nothing about R2's signing, its region handling, or
whether a presigned URL actually resolves.

Skipped unless ``ERP_LIVE_WAREHOUSE=1``, because they need credentials and write
to the real bucket. Run them against the configured warehouse with:

    ERP_LIVE_WAREHOUSE=1 op run --env-file=.env.op.tpl -- \\
        python -m pytest tests/test_warehouse_live.py -v

Every object is written under the ``_selftest/`` prefix with a random name and
deleted afterwards, so a run leaves the warehouse as it found it. The prefix is
outside the identifier keyspace, so a stray object can never be mistaken for a
real artifact — and ``store_sync --prune`` would remove one anyway.
"""

from __future__ import annotations

import asyncio
import os
import uuid

import httpx
import pytest
from fastapi.testclient import TestClient

from app.config import settings
from app.db import FOUNDATION
from app.main import create_app
from app.services.warehouse import Warehouse

pytestmark = [
    pytest.mark.live,
    pytest.mark.skipif(
        os.getenv("ERP_LIVE_WAREHOUSE") != "1",
        reason="live warehouse test — set ERP_LIVE_WAREHOUSE=1 and supply credentials",
    ),
]

AUTH = {"Authorization": "Bearer dev-operator-token"}
PREFIX = "_selftest/"


@pytest.fixture
def live_key():
    """A throwaway key, removed from the real bucket afterwards either way."""
    key = f"{PREFIX}{uuid.uuid4().hex}"
    yield key
    asyncio.run(Warehouse().delete(key))


async def test_blob_round_trips_through_the_real_warehouse(live_key):
    warehouse = Warehouse()
    blob = b"%PDF-1.7 live warehouse round-trip\n"

    await warehouse.put(live_key, blob, "application/pdf")
    assert await warehouse.exists(live_key)
    assert live_key in await warehouse.list_prefix(PREFIX)

    # The console never proxies bytes — it redirects to a presigned URL, so the
    # signature has to be one the warehouse itself accepts.
    url = await warehouse.presigned_get(live_key, expires=300)
    async with httpx.AsyncClient(timeout=30) as http:
        resp = await http.get(url)
    assert resp.status_code == 200
    assert resp.content == blob
    assert resp.headers["content-type"] == "application/pdf"

    await warehouse.delete(live_key)
    assert not await warehouse.exists(live_key)


def test_document_upload_reaches_the_real_warehouse(monkeypatch):
    """The upload route end to end: real bucket, in-memory index.

    Mongo stays mocked — this is about the object-store seam, and the index
    behaviour is already covered offline in test_api.py.

    The route derives the object key from the instance identifier, so the write
    necessarily lands in the real keyspace. The serial counter is parked at
    999000 first: the resulting key is syntactically valid but far outside any
    plausible production run, and the test refuses to touch a key that already
    holds something.
    """
    monkeypatch.setattr(settings, "mongo_mock", True)
    monkeypatch.setattr(settings, "seed_on_start", False)

    blob = b"%PDF-1.7 quality plan (live)\n"

    with TestClient(create_app()) as client:
        asyncio.run(
            client.app.state.db.db[FOUNDATION["serial_counter"]].update_one(
                {"_id": "E0002-020100"}, {"$set": {"last_serial": 999_000}}, upsert=True
            )
        )
        allocated = client.post(
            "/api/v1/instances",
            json={"e_number": "E0002", "version": "020100", "quantity": 1},
            headers=AUTH,
        )
        # The allocator is authoritative, so use the serial it actually issued.
        instance = allocated.json()["serials"][0]
        key = f"{instance}-QP"

        warehouse = client.app.state.warehouse
        if asyncio.run(warehouse.exists(key)):
            pytest.skip(f"{key} already exists in the warehouse — refusing to overwrite")

        try:
            r = client.post(
                f"/api/v1/instances/{instance}/documents",
                data={"doc_type": "QP"},
                files={"file": ("qp.pdf", blob, "application/pdf")},
                headers=AUTH,
            )
            assert r.status_code == 200, r.text
            assert r.json()["object_key"] == key

            # The recorded key resolves in the warehouse — the whole point of
            # indexing keys rather than storing blobs (ADR-0021 d7).
            url = client.get(f"/warehouse/{key}", follow_redirects=False).headers["location"]
            assert httpx.get(url, timeout=30).content == blob
        finally:
            asyncio.run(warehouse.delete(key))
