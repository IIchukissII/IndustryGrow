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
check, the refusals and the restore steps. What they do NOT cover is
`mongodump`/`mongorestore`
themselves: those are external binaries, absent from this environment, and the
module's own contract is that it shells out to them. The tests exercise everything
around that boundary and skip the two subcommands that cross it.
"""

from __future__ import annotations

import json
import tarfile
from pathlib import Path

import pytest
from mongomock_motor import AsyncMongoMockClient

from app.backup import (
    BACKED_UP_COLLECTIONS,
    LIFECYCLE_SUFFIXES,
    BackupError,
    _counter_state,
    build_parser,
    check_live,
    counters_ahead,
    finish,
    is_backed_up_object,
    namespaces,
    read_manifest,
    restore,
    target_stores,
    verify,
)
from app.config import settings
from app.db import FOUNDATION

# ---------------------------------------------------------------------------
# The allowlist (ADR-0026 d1)
# ---------------------------------------------------------------------------


def test_lifecycle_documents_are_backed_up():
    # These exist in the warehouse and nowhere else — a calibration certificate
    # cannot be regenerated from the repository.
    for suffix in LIFECYCLE_SUFFIXES:
        assert is_backed_up_object(f"E0002-020100-000001{suffix}")


def test_dated_recalibration_keys_are_backed_up():
    # A recalibration must not overwrite its predecessor, so CP and CC keys carry
    # a stamp (ADR-0017 d11). Keys written before the time component carry the
    # day alone; both forms are live in a warehouse.
    assert is_backed_up_object("E0002-020100-000001-CC-20260722")
    assert is_backed_up_object("E0002-020100-000001-CC-20260722T120000")
    assert is_backed_up_object("E0002-020100-000001-CP-20260722T120000")


def test_sequenced_and_superseded_keys_are_backed_up():
    # `-n` separates two uploads in one second; a reissue archives the previous
    # issue under `-superseded-<stamp>`. Both are uploaded blobs that exist
    # nowhere else (ADR-0026 d1).
    assert is_backed_up_object("E0002-020100-000001-CP-20260722T120000-2")
    assert is_backed_up_object("E0002-020100-000001-QP-superseded-20260824T101500")
    assert is_backed_up_object("E0002-020100-000001-QP-superseded-20260824T101500-2")


def test_the_repository_mirror_is_not_backed_up():
    # Excluded deliberately: store_sync restores it from git (ADR-0026 dec 1 alternatives).
    # Including it would inflate every archive with the part of it least at risk.
    for key in (
        "E0001-000002-D-fab.zip",
        "E0001-000002.kicad_pcb",
        "SP0004-M-gateway-bringup.md",
        "E0001-000001-F-boot.hex",
        "E0007-000001-S.pdf",
    ):
        assert not is_backed_up_object(key)


def test_a_key_that_merely_contains_a_suffix_is_not_matched():
    # `-CP` is a lifecycle suffix; `-CPU` is not, and a substring match would take
    # both. The suffix is a terminal, so it is matched as one.
    assert not is_backed_up_object("E0002-020100-000001-CPU")
    assert not is_backed_up_object("E0002-020100-000001-PRELIM")
    assert not is_backed_up_object("E0002-020100-000001-CC-notes")
    assert not is_backed_up_object("E0002-020100-QP")


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


async def test_the_counter_state_is_read_from_real_allocations(db):
    # The literal-value tests above cannot catch a field-name mismatch between
    # the allocator and the backup; a zero here disarms the refusal.
    from app.services import serials

    await serials.allocate(db, "E0002", "020100", 3)
    await serials.allocate(db, "E0006", "010000", 1)
    assert await _counter_state(db) == {"E0002-020100": 3, "E0006-010000": 1}


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


# ---------------------------------------------------------------------------
# The cross-store check against a named database (ADR-0026 d9, d10)
# ---------------------------------------------------------------------------

REHEARSAL_DB = "industrygrow_rehearsal"
REHEARSAL_BUCKET = "industrygrow-rehearsal"


@pytest.fixture
def stores(monkeypatch, warehouse):
    """One in-memory Mongo server holding several databases, and one warehouse.

    `check_live` builds its own `Database` and `Warehouse`; both are replaced so
    the name it is given selects a database on a shared server, as on the real
    one `restore --into` writes to.
    """
    import app.backup as backup

    client = AsyncMongoMockClient()

    class _Database:
        def __init__(self, uri: str, db_name: str, *, mock: bool = False) -> None:
            self.db = client[db_name]

        def close(self) -> None:
            return None

    def _warehouse(bucket: str | None = None):
        # The bucket each step asked for, so a test can tell a rehearsal from the
        # live warehouse (ADR-0026 d10).
        warehouse.buckets.append(bucket)
        return warehouse

    warehouse.buckets = []
    monkeypatch.setattr(backup, "Database", _Database)
    monkeypatch.setattr(backup, "Warehouse", _warehouse)
    return client, warehouse


async def test_check_live_into_a_named_database_passes_when_every_key_resolves(stores, capsys):
    client, warehouse = stores
    key = "E0002-020100-000001-QP"
    await warehouse.put(key, b"blob")
    await client[REHEARSAL_DB][FOUNDATION["lifecycle_doc"]].insert_one({"object_key": key})

    assert await check_live(REHEARSAL_DB, bucket=REHEARSAL_BUCKET) == 0
    assert f"checked 1 indexed object key(s) in {REHEARSAL_DB}" in capsys.readouterr().out


async def test_check_live_into_a_named_database_fails_on_a_dangling_key(stores, capsys):
    client, warehouse = stores
    good, dangling = "E0002-020100-000001-QP", "E0002-020100-000002-CC-20260726"
    await warehouse.put(good, b"blob")
    lifecycle = FOUNDATION["lifecycle_doc"]
    await client[REHEARSAL_DB][lifecycle].insert_many(
        [{"object_key": good}, {"object_key": dangling}]
    )
    # The configured database is consistent and only the named one is broken, so
    # a pass on the named one would mean the check read the configured one.
    await client[settings.mongo_db][lifecycle].insert_one({"object_key": good})

    assert await check_live() == 0
    assert await check_live(REHEARSAL_DB, bucket=REHEARSAL_BUCKET) == 1
    assert dangling in capsys.readouterr().out


def test_check_live_takes_the_database_and_bucket_restore_into_wrote():
    args = build_parser().parse_args(
        ["check-live", "--into", REHEARSAL_DB, "--bucket", REHEARSAL_BUCKET]
    )
    assert (args.into, args.bucket) == (REHEARSAL_DB, REHEARSAL_BUCKET)
    assert build_parser().parse_args(["check-live"]).into is None


# ---------------------------------------------------------------------------
# The rehearsal bucket (ADR-0026 d10)
# ---------------------------------------------------------------------------


def test_a_rehearsal_without_a_bucket_is_refused():
    # The objects go into the bucket in place; into the live one, a QP re-issued
    # since the backup loses its current bytes.
    with pytest.raises(BackupError, match="needs --bucket"):
        target_stores(REHEARSAL_DB, None)


def test_a_rehearsal_into_the_live_bucket_is_refused():
    with pytest.raises(BackupError, match="configured warehouse bucket"):
        target_stores(REHEARSAL_DB, settings.warehouse_bucket)


def test_a_rehearsal_into_the_configured_database_is_refused():
    # --into skips the counter refusal, and the replay drops what it restores.
    with pytest.raises(BackupError, match="is the configured database"):
        target_stores(settings.mongo_db, REHEARSAL_BUCKET)


def test_a_live_restore_uses_the_configured_stores():
    assert target_stores(None, None) == (settings.mongo_db, None)
    with pytest.raises(BackupError, match="without --into"):
        target_stores(None, REHEARSAL_BUCKET)


def test_a_rehearsal_names_its_own_stores():
    assert target_stores(REHEARSAL_DB, REHEARSAL_BUCKET) == (REHEARSAL_DB, REHEARSAL_BUCKET)


async def test_check_live_into_without_a_bucket_is_refused(stores):
    with pytest.raises(BackupError, match="needs --bucket"):
        await check_live(REHEARSAL_DB)


# ---------------------------------------------------------------------------
# The restore steps: objects, index elsewhere, finish (ADR-0026 d4, d8, d9)
# ---------------------------------------------------------------------------

KEY = "E0002-020100-000001-QP"


async def test_restore_into_without_a_bucket_writes_nothing(stores, tmp_path):
    _, warehouse = stores
    archive = _archive(tmp_path, indexed=[KEY], objects=[KEY])
    with pytest.raises(BackupError, match="needs --bucket"):
        await restore(archive, database_name=REHEARSAL_DB, objects_only=True)
    assert warehouse.objects == {}


async def test_a_live_restore_over_an_advanced_counter_writes_nothing(stores, tmp_path):
    # d8: the archive's counter is 4; the live store has issued 7.
    client, warehouse = stores
    await client[settings.mongo_db][FOUNDATION["serial_counter"]].insert_one(
        {"_id": "E0002-020100", "last_serial": 7}
    )
    archive = _archive(tmp_path, indexed=[KEY], objects=[KEY])
    with pytest.raises(BackupError, match="allocated serials"):
        await restore(archive, objects_only=True)
    assert warehouse.objects == {}


async def test_an_archive_that_does_not_verify_writes_nothing(stores, tmp_path):
    _, warehouse = stores
    archive = _archive(tmp_path, indexed=[KEY], objects=[])
    with pytest.raises(BackupError, match="does not verify"):
        await restore(archive, database_name=REHEARSAL_DB, bucket=REHEARSAL_BUCKET)
    assert warehouse.objects == {}


async def test_objects_only_puts_the_objects_into_the_rehearsal_bucket(stores, tmp_path, capsys):
    client, warehouse = stores
    archive = _archive(tmp_path, indexed=[KEY], objects=[KEY])
    restored = await restore(
        archive, database_name=REHEARSAL_DB, bucket=REHEARSAL_BUCKET, objects_only=True
    )
    assert restored == 0
    assert warehouse.objects == {KEY: b"blob"}
    assert warehouse.buckets == [REHEARSAL_BUCKET]
    # The index is replayed by the next step, not this one.
    assert await client[REHEARSAL_DB].list_collection_names() == []
    out = capsys.readouterr().out
    assert f"finish --into {REHEARSAL_DB} --bucket {REHEARSAL_BUCKET}" in out


async def test_a_live_restore_puts_the_objects_into_the_configured_bucket(stores, tmp_path):
    _, warehouse = stores
    archive = _archive(tmp_path, indexed=[KEY], objects=[KEY])
    assert await restore(archive, objects_only=True) == 0
    assert warehouse.buckets == [None]
    assert KEY in warehouse.objects


async def test_restore_without_mongorestore_stops_after_the_objects(stores, tmp_path, monkeypatch):
    import app.backup as backup

    _, warehouse = stores
    monkeypatch.setattr(backup.shutil, "which", lambda name: None)
    archive = _archive(tmp_path, indexed=[KEY], objects=[KEY])
    with pytest.raises(BackupError, match=r"deploy/restore\.sh"):
        await restore(archive, database_name=REHEARSAL_DB, bucket=REHEARSAL_BUCKET)
    assert KEY in warehouse.objects


async def test_finish_sets_the_counter_tenant_then_checks(stores):
    client, warehouse = stores
    db = client[REHEARSAL_DB]
    await db[FOUNDATION["serial_counter"]].insert_one({"_id": "E0002-020100", "last_serial": 4})
    await db[FOUNDATION["lifecycle_doc"]].insert_one({"object_key": KEY})

    # The index names an object the bucket lacks: the restore is not done (d9).
    assert await finish(REHEARSAL_DB, bucket=REHEARSAL_BUCKET) == 1
    counter = await db[FOUNDATION["serial_counter"]].find_one({"_id": "E0002-020100"})
    assert counter["tenant_id"] == settings.operator_uuid
    assert counter["last_serial"] == 4

    await warehouse.put(KEY, b"blob")
    assert await finish(REHEARSAL_DB, bucket=REHEARSAL_BUCKET) == 0
    assert set(warehouse.buckets) == {REHEARSAL_BUCKET}


async def test_finish_into_without_a_bucket_is_refused(stores):
    with pytest.raises(BackupError, match="needs --bucket"):
        await finish(REHEARSAL_DB)


def test_namespaces_map_the_archive_onto_the_target(tmp_path, capsys):
    archive = _archive(tmp_path, indexed=[], objects=[])
    assert namespaces(archive, REHEARSAL_DB, REHEARSAL_BUCKET) == 0
    assert capsys.readouterr().out.split() == ["industrygrow_erp.*", f"{REHEARSAL_DB}.*"]
