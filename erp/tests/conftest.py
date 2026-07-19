# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Test fixtures. An in-memory async Mongo (mongomock-motor) — no live server."""

from __future__ import annotations

import pytest
from mongomock_motor import AsyncMongoMockClient


@pytest.fixture
def db():
    return AsyncMongoMockClient()["test_erp"]
