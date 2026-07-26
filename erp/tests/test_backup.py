# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Backup and restore of the two-store system of record (ADR-0026).

The interesting failures here are not "the dump was empty". They are:

  * a restored index naming an object the backup does not contain — which is
    ADR-0022 d7's invariant inverted, and unrepairable, because the object is not
    in the archive to reconcile against; and
  * a restore silently rolling the serial counter backwards, re-arming numbers
    that may already be stamped on boards (ADR-0021 d4; ADR-0026 d8). Nothing
    detects that afterwards, which is why the tooling refuses rather than warns.

So these tests are about the allowlist, the ordering, the archive-consistency
check, and the refusal. What they do NOT cover is `mongodump`/`mongorestore`
themselves: those are external binaries, absent from this environment, and the
module's own contract is that it shells out to them. The tests exercise everything
around that boundary and skip the two subcommands that cross it.
"""

from __future__ import annotations

import json
import tarfile
from pathlib import Path

import pytest

from app.backup import (
    BACKED_UP_COLLECTIONS,
    LIFECYCLE_SUFFIXES,
    counters_ahead,
    is_backed_up_object,
    read_manifest,
    verify,
)

# ---------------------------------------------------------------------------
# The allowlist (ADR-0026 d1)
# ---------------------------------------------------------------------------


def test_lifecycle_documents_are_backed_up():
    # These exist in the warehouse and nowhere else — a calibration certificate
    # cannot be regenerated from the repository.
    for suffix in LIFECYCLE_SUFFIXES:
        assert is_backed_up_object(f"E0002-020100-000001{suffix}")


def test_dated_recalibration_keys_are_backed_up():
    # A recalibration must not overwrite its predecessor, so the CC key carries a
    # date (ADR-0022 d7). The suffix match has to see through that.
    assert is_backed_up_object("E0002-020100-000001-CC-20260722")


def test_the_repository_mirror_is_not_backed_up():
    # Excluded deliberately: store_sync restores it from git (ADR-0026 d1, alt B).
    # Including it would inflate every archive with the part of it least at risk.
    for key in (
        "E0001-000002-D-fab.zip",
        "E0001-000002.kicad_pcb",
        "SP0004-M-gateway-bringup.md",
        "E0001-000001-F.hex",
        "E0007-000001-S.pdf",
    ):
        assert not is_backed_up_object(key)


def test_a_key_that_merely_contains_a_suffix_is_not_matched():
    # `-CP` is a lifecycle suffix; `-CPU` is not, and a substring match would take
    # both. The suffix is a terminal, so it is matched as one.
    assert not is_backed_up_object("E0002-020100-000001-CPU")
    assert not is_backed_up_object("E0002-020100-000001-PRELIM")


def test_every_owned_collection_is_in_the_allowlist():
    # If a future feature adds a collection and not this list, the backup silently
    # does not cover it (ADR-0026's negative consequences say so). This test is
    # what turns that into a failure.
    from app.db import DOMAIN, FOUNDATION

    assert set(BACKED_UP_COLLECTIONS) == {*FOUNDATION.values(), *DOMAIN.values()}
    assert "foundation.serial_counter" in BACKED_UP_COLLECTIONS  # the reason d2 exists
    assert "foundation.machine_identity" in BACKED_UP_COLLECTIONS  # added by ADR-0022 d12


# ---------------------------------------------------------------------------
# The serial-counter refusal (ADR-0026 d8)
# ---------------------------------------------------------------------------


def test_a_live_counter_ahead_of_the_archive_is_detected():
    # Serials were issued after the backup. Restoring re-arms them, and if parts
    # were built they now share an identity with whatever gets issued next.
    ahead = counters_ahead({"E0002-020100": 7}, {"E0002-020100": 4})
    assert ahead == {"E0002-020100": (7, 4)}


def test_a_counter_the_archive_has_never_seen_counts_as_ahead():
    # A module first allocated after the backup point. The archive has no entry, so
    # a naive comparison would miss it entirely — treated as 0, which it is.
    assert counters_ahead({"E0006-010000": 3}, {}) == {"E0006-010000": (3, 0)}


def test_matching_or_behind_counters_do_not_block():
    assert counters_ahead({"E0002-020100": 4}, {"E0002-020100": 4}) == {}
    # Archive ahead of live is the normal case after a fresh restore; not a hazard,
    # because rolling *forwards* never re-issues a number.
    assert counters_ahead({"E0002-020100": 2}, {"E0002-020100": 9}) == {}


# ---------------------------------------------------------------------------
# Archive consistency (ADR-0026 d9)
# ---------------------------------------------------------------------------


def _archive(tmp_path: Path, *, indexed: list[str], objects: list[str], dump: bool = True) -> Path:
    """A hand-built archive, so the verifier can be tested against a broken one.

    Built rather than captured because the interesting input is the archive that
    should never exist — one whose index names an object it does not carry.
    """
    work = tmp_path / "work"
    (work / "objects").mkdir(parents=True)
    for key in objects:
        (work / "objects" / key.replace("/", "%2F")).write_bytes(b"blob")
    if dump:
        (work / "mongo.archive").write_bytes(b"not a real dump")
    (work / "manifest.json").write_text(
        json.dumps(
            {
                "format": 1,
                "taken_at": "2026-07-26T14:00:00+00:00",
                "database": "industrygrow_erp",
                "collections": BACKED_UP_COLLECTIONS,
                "serial_counters": {"E0002-020100": 4},
                "indexed_object_keys": indexed,
                "objects": objects,
            }
        )
    )
    path = tmp_path / "erp-test.tar.gz"
    with tarfile.open(path, "w:gz") as tar:
        for item in ("manifest.json", "mongo.archive", "objects"):
            if (work / item).exists():
                tar.add(work / item, arcname=item)
    return path


def test_a_consistent_archive_verifies(tmp_path, capsys):
    key = "E0002-020100-000001-QP"
    assert verify(_archive(tmp_path, indexed=[key], objects=[key])) == 0
    assert "internally consistent" in capsys.readouterr().out


def test_an_archive_whose_index_outruns_its_objects_fails(tmp_path, capsys):
    # This is the shape a wrongly-ordered capture produces: blobs snapshotted
    # before the index, so the index knows about an object the archive missed
    # (ADR-0026 d3). Restoring it yields a broken index, and the object is not in
    # the archive to reconcile against — so the verifier has to catch it here.
    archive = _archive(
        tmp_path,
        indexed=["E0002-020100-000001-QP", "E0002-020100-000002-CC-20260726"],
        objects=["E0002-020100-000001-QP"],
    )
    assert verify(archive) == 1
    out = capsys.readouterr().out
    assert "1 indexed key(s) have no object" in out
    assert "broken index" in out


def test_an_archive_with_no_dump_fails(tmp_path, capsys):
    key = "E0002-020100-000001-QP"
    assert verify(_archive(tmp_path, indexed=[key], objects=[key], dump=False)) == 1
    assert "no mongo.archive" in capsys.readouterr().out


def test_something_that_is_not_an_erp_backup_is_rejected(tmp_path):
    from app.backup import BackupError

    stray = tmp_path / "holiday-photos.tar.gz"
    (tmp_path / "photo.jpg").write_bytes(b"jpeg")
    with tarfile.open(stray, "w:gz") as tar:
        tar.add(tmp_path / "photo.jpg", arcname="photo.jpg")
    with pytest.raises(BackupError, match="not an ERP backup"):
        read_manifest(stray)


def test_the_manifest_records_what_was_excluded(tmp_path):
    # The exclusions are a decision (ADR-0026 d1), so the archive states them:
    # someone holding only the file can tell what it does not cover.
    manifest = read_manifest(_archive(tmp_path, indexed=[], objects=[]))
    assert manifest["format"] == 1
    assert "foundation.serial_counter" in manifest["collections"]
