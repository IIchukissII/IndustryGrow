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
    parse_store_key,
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


def test_store_key_fields_are_read_by_position_not_by_search():
    """The layer letter is a *slot*, not a token that may appear anywhere.

    Scanning a key for "a segment that happens to be S/D/L/P/M/I/F" reads a slug
    word as a layer the moment one is a single capital letter. The slot after the
    version is the layer; everything after it is the slug (ADR-0017 d1).
    """
    fab = parse_store_key("E0001-000002-D-fab.zip")
    assert (fab.root, fab.version, fab.layer, fab.slug, fab.extension) == (
        "E0001",
        "000002",
        "D",
        "fab",
        "zip",
    )
    # An SP root has no version slot at all (ADR-0019 d2), so the layer sits one
    # segment earlier — the same letter in a different place.
    sp = parse_store_key("SP0004-D-rp5-case-src.zip")
    assert (sp.root_kind, sp.version, sp.layer, sp.slug) == ("SP", None, "D", "rp5-case-src")


def test_store_key_status_token_is_not_a_layer():
    """ADR-0017 d17: full uppercase words, never mistakable for a layer letter."""
    blocked = parse_store_key("E0001-000001-BLOCKED.zip")
    assert (blocked.status, blocked.layer, blocked.version) == ("BLOCKED", None, "000001")
    # Same prefix, different axis — the firmware is not part of that withdrawal.
    firmware = parse_store_key("E0001-000001-F-src.zip")
    assert (firmware.status, firmware.layer, firmware.slug) == (None, "F", "src")


def test_store_key_reports_what_it_cannot_read():
    """A key that does not fit the grammar is shown as unfiled, never guessed at."""
    loose = parse_store_key("fp-lib-table")
    assert loose.root is None and loose.extension == ""
    # No layer letter is a real answer too: the raw EDA sources carry none.
    source = parse_store_key("E0001-000002.kicad_sch")
    assert (source.root, source.version, source.layer) == ("E0001", "000002", None)


def test_store_key_prefix_and_tail_rebuild_the_key():
    """What the console's plate relies on: the row plus the cell IS the key."""
    for name in (
        "E0001-000002-D-fab.zip",
        "E0001-000002.kicad_sch",
        "E0001-000001-SUPERSEDED.zip",
        "SP0004-M-gateway-bringup.md",
        "fp-lib-table",
    ):
        key = parse_store_key(name)
        assert key.prefix + key.tail == name


def test_boundary_no_forbidden_fields():
    """ADR-0021 d10-11: the ERP never holds SKU/price/telemetry/type-meaning."""
    fields = set(ModuleInstance.model_fields)
    for forbidden in {"sku", "price", "supplier", "telemetry", "description"}:
        assert forbidden not in fields
