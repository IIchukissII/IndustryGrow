# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The ADR-0017 identifier grammar, and a boundary guard on the models."""

from __future__ import annotations

import pytest

from app.models.foundation import ModuleInstance
from app.models.identifiers import (
    counter_id,
    decode_version,
    encode_version,
    instance_id,
    integration_id,
    parse_instance,
)


def test_version_roundtrip():
    assert encode_version(2, 1, 3) == "020103"
    assert tuple(decode_version("020103")) == (2, 1, 3)


def test_instance_id_ok_and_rejects_bad_grammar():
    assert instance_id("E0002", "020100", "000188") == "E0002-020100-000188"
    with pytest.raises(ValueError):
        instance_id("E2", "020100", "000188")  # module not 4 digits


def test_integration_id_and_counter():
    key = integration_id("GBOX_0001", "010100", "E0002", "020100", "000188")
    assert key == "GBOX_0001-010100-E0002-020100-000188"
    assert counter_id("E0002", "020100") == "E0002-020100"


def test_parse_instance():
    assert parse_instance("E0004-010100-000103") == {
        "e_number": "E0004",
        "version": "010100",
        "serial": "000103",
    }


def test_boundary_no_forbidden_fields():
    """ADR-0021 d10-11: the ERP never holds SKU/price/telemetry/type-meaning."""
    fields = set(ModuleInstance.model_fields)
    for forbidden in {"sku", "price", "supplier", "telemetry", "description"}:
        assert forbidden not in fields
