# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Test fixtures. An in-memory async Mongo (mongomock-motor) — no live server."""

from __future__ import annotations

import pytest
from mongomock_motor import AsyncMongoMockClient


@pytest.fixture
def db():
    return AsyncMongoMockClient()["test_erp"]


class FakeWarehouse:
    """An in-memory stand-in for the S3-compatible warehouse.

    Same async surface as ``app.services.warehouse.Warehouse``, so a test can
    assert *what* reached the object store and in what order relative to the
    Mongo index. Set ``fail_on_put`` to simulate a store that rejects the blob —
    the blob-first rule (ADR-0021) says nothing may be indexed in that case.
    """

    def __init__(self) -> None:
        self.objects: dict[str, bytes] = {}
        self.content_types: dict[str, str] = {}
        self.fail_on_put = False

    async def put(
        self, key: str, body: bytes, content_type: str = "application/octet-stream"
    ) -> str:
        if self.fail_on_put:
            raise RuntimeError("warehouse unavailable")
        self.objects[key] = body
        self.content_types[key] = content_type
        return key

    async def exists(self, key: str) -> bool:
        return key in self.objects

    async def list_prefix(self, prefix: str) -> list[str]:
        return sorted(k for k in self.objects if k.startswith(prefix))

    async def delete(self, key: str) -> None:
        self.objects.pop(key, None)
        self.content_types.pop(key, None)

    async def presigned_get(self, key: str, expires: int = 3600) -> str:
        return f"https://warehouse.test/{key}?expires={expires}"


@pytest.fixture
def warehouse() -> FakeWarehouse:
    return FakeWarehouse()
