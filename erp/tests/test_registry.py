# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The type registry is read, never restated (ADR-0023).

These tests run against the repository's real ``REGISTRY.md`` on purpose. A
fixture copy would be the very duplication the ADR removes, and would keep
passing while the actual registry rotted.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.config import settings
from app.main import create_app
from app.services import registry

AUTH = {"Authorization": "Bearer dev-operator-token"}


@pytest.fixture
def client(monkeypatch):
    monkeypatch.setattr(settings, "mongo_mock", True)
    with TestClient(create_app()) as c:
        yield c


def test_reads_the_real_registry():
    cat = registry.catalog()
    designations = {m.e_number: m.designation for m in cat.modules}
    assert designations["E0002"].startswith("M01-CLIMATE")
    # E0007 is the entry the two hardcoded tables had drifted past (ADR-0023
    # context) — parsing the registry is what makes it reachable at all.
    assert "E0007" in designations


def test_sp_instance_tracking_is_read_from_the_column():
    parts = {p.sp_number: p for p in registry.catalog().parts}
    # The gateway SBC is the one purchased part with per-instance identity
    # (ADR-0019 d2); the rest are type-level only.
    assert parts["SP0004"].instance_tracked is True
    assert parts["SP0001"].instance_tracked is False


def test_versioned_identifiers_elsewhere_are_mentions_not_entries():
    """Only the two registry sections are the registry (ADR-0023 d3)."""
    cat = registry.catalog()
    assert all("-" not in m.e_number for m in cat.modules)
    # The document-layer tables mention `E0001-000002`; it is an artifact, not a
    # type, and must never enter the catalog.
    assert len(cat.modules) == len({m.e_number for m in cat.modules})


def test_the_real_registry_conforms():
    assert registry.check() == []


_RENAMED_COLUMN = "## E-numbers\n\n| Code | Designation | Discipline | Notes |\n|-|-|-|-|\n"


@pytest.mark.parametrize(
    ("broken", "expected"),
    [
        # A renamed column: still fine to a human, a different document to a
        # consumer.
        (_RENAMED_COLUMN, "column header"),
        # The section gone entirely.
        ("", "section missing"),
    ],
)
def test_check_rejects_a_registry_consumers_could_not_read(tmp_path, broken, expected):
    path = tmp_path / "REGISTRY.md"
    path.write_text(broken)
    problems = registry.check(path)
    assert any(expected in p for p in problems), problems


def test_check_rejects_duplicate_identifiers(tmp_path):
    path = tmp_path / "REGISTRY.md"
    path.write_text(
        "## E-numbers\n\n"
        "| E-number | Designation | Discipline | Notes |\n|-|-|-|-|\n"
        "| `E0001` | Carrier | electrical | — |\n"
        "| `E0001` | Carrier again | electrical | — |\n\n"
        "## SP numbers\n\n"
        "| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |\n|-|-|-|-|\n"
        "| `SP0001` | Meter | no | — |\n"
    )
    assert any("duplicate E-number" in p for p in registry.check(path))


def test_catalog_endpoint_serves_the_registry(client):
    assert client.get("/api/v1/catalog").status_code == 401

    body = client.get("/api/v1/catalog", headers=AUTH).json()
    assert {m["e_number"] for m in body["modules"]} >= {"E0001", "E0002", "E0007"}
    assert {p["sp_number"] for p in body["parts"]} >= {"SP0004"}


async def test_catalog_is_read_through_and_never_stored(client):
    """ADR-0021 d11 / ADR-0023 d4: type meaning must not land in the store."""
    assert client.get("/api/v1/catalog", headers=AUTH).status_code == 200
    names = await client.app.state.db.db.list_collection_names()
    assert not any("catalog" in n or "registry" in n for n in names)


def _registry(e_rows: str, sp_rows: str) -> str:
    return (
        "## E-numbers\n\n"
        "| E-number | Designation | Discipline | Notes |\n|-|-|-|-|\n"
        f"{e_rows}\n"
        "## SP numbers\n\n"
        "| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |\n|-|-|-|-|\n"
        f"{sp_rows}\n"
    )


_GOOD_E = "| `E0001` | Carrier | electrical | — |\n"
_GOOD_SP = "| `SP0001` | Meter | no | — |\n"


@pytest.mark.parametrize(
    ("e_rows", "sp_rows", "expected"),
    [
        # ADR-0023 d2: column 1 is the bare identifier, no version or prose.
        (_GOOD_E + "| `E0002-000001` | Sensor | electrical | — |\n", _GOOD_SP, "`E0002-000001`"),
        (_GOOD_E + "| E0002 | Sensor | electrical | — |\n", _GOOD_SP, "'E0002'"),
        (_GOOD_E, _GOOD_SP + "| see `SP0002` | Breaker | no | — |\n", "see `SP0002`"),
        # ADR-0023 d2: `Instance-tracked?` begins with yes or no.
        (_GOOD_E, _GOOD_SP + "| `SP0002` | Breaker | maybe | — |\n", "begin yes or no"),
        (_GOOD_E, _GOOD_SP + "| `SP0002` | Breaker | none | — |\n", "begin yes or no"),
        # Fixed column count.
        (_GOOD_E + "| `E0002` | Sensor | electrical |\n", _GOOD_SP, "3 cells"),
    ],
)
def test_check_rejects_a_row_that_is_not_an_entry(tmp_path, e_rows, sp_rows, expected):
    path = tmp_path / "REGISTRY.md"
    path.write_text(_registry(e_rows, sp_rows))
    problems = registry.check(path)
    assert any(expected in p for p in problems), problems


def test_check_accepts_entries_and_ignores_subsection_tables(tmp_path):
    """A `###` subsection's tables are mentions (ADR-0023 d3), not malformed rows."""
    path = tmp_path / "REGISTRY.md"
    path.write_text(
        _registry(_GOOD_E, _GOOD_SP + "| `SP0004` | Gateway | yes (vendor serial) | — |\n")
        + "\n### Vendor documents\n\n| Object | Serves |\n|-|-|\n"
        + "| `SP0004-M-guide.md` | `SP0004` |\n"
    )
    assert registry.check(path) == []
    parts = {p.sp_number: p.instance_tracked for p in registry.parse(path.read_text()).parts}
    assert parts == {"SP0001": False, "SP0004": True}


def test_check_rejects_a_second_table_in_a_registry_section(tmp_path):
    path = tmp_path / "REGISTRY.md"
    rows = _GOOD_E + "\nprose\n\n| `E0002` | Sensor | electrical | — |\n"
    path.write_text(_registry(rows, _GOOD_SP))
    assert any("2 tables" in p for p in registry.check(path))
