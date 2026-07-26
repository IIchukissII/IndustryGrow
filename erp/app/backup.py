# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Backup and restore for the pre-cloud system of record (ADR-0026).

    python -m app.backup save    --out ./backups
    python -m app.backup verify  --archive ./backups/erp-2026-07-26T14-05-00Z.tar.gz
    python -m app.backup restore --archive ./backups/erp-…tar.gz

The ERP's state lives in two stores with an ordering invariant between them, and
that is what shapes this module. ADR-0022 d7 writes the blob to the warehouse
*first* and records its key *second*, so at every instant the warehouse is a
superset of what the index references. Capture therefore goes **index first,
blobs second** (ADR-0026 d3) and restore goes **blobs first, index second**
(d4) — the inverse of the write order in each direction. Do it the other way and
you get a restored index naming objects the backup does not contain, which is the
one failure ADR-0022 d7 exists to prevent.

What is in scope is an allowlist, not everything (ADR-0026 d1): the Mongo store,
and the warehouse objects that are *not* reconstructible from the repository. The
`store/` mirror is excluded because `store_sync` restores it from git.

The reason any of this exists is the serial counter (ADR-0026 d2): the ERP is the
gap-free serial-allocation authority (ADR-0021 d4), so losing it means re-issuing
a number already stamped on a board. That is also why `restore` refuses to run
over a store whose counter is ahead of the archive's (d8) rather than completing
and reporting success.

Encryption is the caller's job and the runbook's: this module writes a plain
archive, and `erp/deploy/backup.sh` is what encrypts it under an operator
passphrase before it leaves the host (ADR-0026 d7). Keeping the two separate means
the Python here never handles a passphrase.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from app.config import settings
from app.db import DOMAIN, FOUNDATION, Database
from app.services.warehouse import Warehouse

# ADR-0026 d1's allowlist. Collections first — everything the ERP owns and nothing
# it merely references.
BACKED_UP_COLLECTIONS = sorted({*FOUNDATION.values(), *DOMAIN.values()})

# Object prefixes that are NOT reconstructible from the repository. Lifecycle
# documents are keyed `<instance>-<suffix>` (ADR-0017 d15; ADR-0022 d7's
# allowlist), so they are recognised by suffix rather than by a folder — the
# keyspace is flat.
LIFECYCLE_SUFFIXES = ("-QP", "-QR", "-CP", "-CC", "-PR")

MANIFEST_NAME = "manifest.json"
DUMP_NAME = "mongo.archive"
OBJECTS_DIR = "objects"


class BackupError(Exception):
    """A refusal or a failure the operator has to resolve."""


def log(message: str) -> None:
    print(f"[backup] {message}", flush=True)


# ---------------------------------------------------------------------------
# What counts as ours
# ---------------------------------------------------------------------------


def is_backed_up_object(key: str) -> bool:
    """True for warehouse objects that exist nowhere else (ADR-0026 d1).

    The `store/` mirror is excluded: it is repository content, and `store_sync`
    puts it back. Recognised by the ADR-0022 d7 lifecycle suffixes, because the
    warehouse keyspace is flat and a key *is* the identifier (ADR-0017 d15) —
    there is no directory to filter on.
    """
    stem = key.rsplit("/", 1)[-1]
    if any(stem.endswith(s) for s in LIFECYCLE_SUFFIXES):
        return True
    # Recalibration keys carry a date: `<instance>-CC-YYYYMMDD`.
    parts = stem.rsplit("-", 2)
    return len(parts) == 3 and f"-{parts[1]}" == "-CC" and parts[2].isdigit()


# ---------------------------------------------------------------------------
# Save
# ---------------------------------------------------------------------------


async def _counter_state(db) -> dict[str, int]:
    """The serial allocator's high-water mark per (module, version).

    Recorded in the manifest so `restore` can compare it against the live store
    without parsing the dump — this is the value ADR-0026 d8 refuses to roll back
    silently.
    """
    counters: dict[str, int] = {}
    async for doc in db[FOUNDATION["serial_counter"]].find({}):
        counters[str(doc.get("_id"))] = int(doc.get("last", doc.get("seq", 0)))
    return counters


def _mongodump(uri: str, database: str, target: Path) -> None:
    if not shutil.which("mongodump"):
        raise BackupError(
            "mongodump is not on PATH. It ships in the MongoDB Database Tools; inside the\n"
            "       compose stack, run this through the mongo service instead:\n"
            "         docker compose exec -T mongo mongodump …"
        )
    done = subprocess.run(
        ["mongodump", f"--uri={uri}", f"--db={database}", f"--archive={target}"],
        capture_output=True,
        check=False,
    )
    if done.returncode != 0:
        raise BackupError(f"mongodump failed: {done.stderr.decode(errors='replace').strip()}")


async def save(out_dir: Path, *, stamp: str | None = None) -> Path:
    """Capture both stores into one archive. Index first, blobs second (d3)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = stamp or datetime.now(UTC).strftime("%Y-%m-%dT%H-%M-%SZ")

    database = Database(settings.mongo_uri, settings.mongo_db, mock=settings.mongo_mock)
    warehouse = Warehouse()

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)

        # --- the index, FIRST (ADR-0026 d3) -------------------------------
        # Everything it references was written to the warehouse before now, so
        # the blob pass below cannot miss any of it.
        log("capturing the index")
        counters = await _counter_state(database.db)
        indexed_keys = sorted(
            {
                doc["object_key"]
                async for doc in database.db[FOUNDATION["lifecycle_doc"]].find(
                    {}, {"object_key": 1}
                )
                if doc.get("object_key")
            }
        )
        _mongodump(settings.mongo_uri, settings.mongo_db, work / DUMP_NAME)
        index_taken = datetime.now(UTC)

        # --- the blobs, SECOND --------------------------------------------
        log(f"copying warehouse objects ({len(indexed_keys)} referenced by the index)")
        objects = work / OBJECTS_DIR
        objects.mkdir()
        stored, missing = [], []
        for key in await warehouse.list_prefix(""):
            if not is_backed_up_object(key):
                continue
            body = await warehouse.get_bytes(key)
            if body is None:
                missing.append(key)
                continue
            # The key IS the identifier (ADR-0017 d15) and the keyspace is flat,
            # so a key can hold a `/` only if an operator put one there. Encode
            # rather than nest, to keep the archive layout flat too.
            (objects / key.replace("/", "%2F")).write_bytes(body)
            stored.append(key)

        # d9's invariant, checked at capture as well as at restore: a recorded
        # key must resolve. Catching it here means the operator learns their live
        # store is inconsistent now, not during a recovery.
        dangling = [k for k in indexed_keys if k not in set(stored)]
        if dangling:
            raise BackupError(
                f"{len(dangling)} indexed object(s) are not in the warehouse, so this backup "
                f"would restore a broken index (ADR-0021 d7).\n"
                f"       First few: {dangling[:5]}\n"
                f"       This is a fault in the LIVE store, not in the backup. Investigate "
                f"before relying on either."
            )

        manifest = {
            "format": 1,
            "taken_at": index_taken.isoformat(),
            "database": settings.mongo_db,
            "operator_uuid": settings.operator_uuid,
            "collections": BACKED_UP_COLLECTIONS,
            "serial_counters": counters,
            "indexed_object_keys": indexed_keys,
            "objects": stored,
            "excluded": "the store/ warehouse mirror (reconstructible via store_sync), the "
            "operator CA, the profile-signing key, the gateway (ADR-0026 d1)",
        }
        (work / MANIFEST_NAME).write_text(json.dumps(manifest, indent=2) + "\n")

        archive = out_dir / f"erp-{stamp}.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            for item in (MANIFEST_NAME, DUMP_NAME, OBJECTS_DIR):
                tar.add(work / item, arcname=item)

    if missing:
        log(f"warning: {len(missing)} object(s) vanished mid-copy and were skipped")
    log(f"wrote {archive} ({archive.stat().st_size / 1e6:.1f} MB, {len(stored)} objects)")
    log(
        "NOT encrypted. It carries operator-private production data (ADR-0021 d14) and must "
        "be encrypted before it leaves this host — deploy/backup.sh does that (ADR-0026 d7)."
    )
    return archive


# ---------------------------------------------------------------------------
# Verify and restore
# ---------------------------------------------------------------------------


def read_manifest(archive: Path) -> dict[str, Any]:
    try:
        with tarfile.open(archive, "r:gz") as tar:
            # KeyError, not None, is what a missing member raises — returning None
            # is only for a member that exists but is not a regular file.
            member = tar.extractfile(MANIFEST_NAME)
            if member is None:
                raise BackupError(f"{archive}'s {MANIFEST_NAME} is not a file")
            return json.loads(member.read())
    except KeyError as exc:
        raise BackupError(f"{archive} has no {MANIFEST_NAME} — not an ERP backup") from exc
    except tarfile.TarError as exc:
        raise BackupError(f"{archive} is not a readable archive: {exc}") from exc


def verify(archive: Path) -> int:
    """Check the archive is internally consistent, without touching the live store.

    ADR-0026 d9's invariant applied to the archive itself: every object key the
    captured index names must be present among the captured objects. This is the
    check that makes the difference between having a file and having a backup.
    """
    manifest = read_manifest(archive)
    with tarfile.open(archive, "r:gz") as tar:
        names = set(tar.getnames())

    problems = []
    if DUMP_NAME not in names:
        problems.append(f"no {DUMP_NAME} in the archive")

    prefix = OBJECTS_DIR + "/"
    present = {n.split("/", 1)[1].replace("%2F", "/") for n in names if n.startswith(prefix)}
    dangling = [k for k in manifest.get("indexed_object_keys", []) if k not in present]
    if dangling:
        problems.append(
            f"{len(dangling)} indexed key(s) have no object in the archive "
            f"(first: {dangling[:3]}) — restoring it would produce a broken index"
        )

    log(f"archive taken {manifest.get('taken_at')} for database {manifest.get('database')}")
    log(
        f"  {len(manifest.get('objects', []))} objects, "
        f"{len(manifest.get('collections', []))} collections"
    )
    counters = manifest.get("serial_counters", {})
    log(f"  serial counters: {counters or 'none allocated yet'}")

    for p in problems:
        log(f"  FAIL  {p}")
    if problems:
        return 1
    log("archive is internally consistent")
    return 0


async def _live_counters() -> dict[str, int]:
    database = Database(settings.mongo_uri, settings.mongo_db, mock=settings.mongo_mock)
    return await _counter_state(database.db)


def counters_ahead(live: dict[str, int], archived: dict[str, int]) -> dict[str, tuple[int, int]]:
    """Counters where the live store has issued serials the archive does not know.

    This is ADR-0026 d8's test. Restoring over these re-arms numbers that may
    already be stamped on hardware, and nothing afterwards can detect it.
    """
    return {
        key: (value, archived.get(key, 0))
        for key, value in live.items()
        if value > archived.get(key, 0)
    }


async def restore(archive: Path, *, force: bool = False, database_name: str | None = None) -> int:
    """Replay the archive: blobs first, then the index (ADR-0026 d4)."""
    manifest = read_manifest(archive)
    if verify(archive) != 0:
        raise BackupError("refusing to restore an archive that does not verify")

    target_db = database_name or settings.mongo_db

    # --- d8: the refusal ---------------------------------------------------
    if database_name is None:
        ahead = counters_ahead(await _live_counters(), manifest.get("serial_counters", {}))
        if ahead and not force:
            lines = "\n".join(
                f"         {k}: live {live}, archive {arch}" for k, (live, arch) in ahead.items()
            )
            raise BackupError(
                "the live store has allocated serials this archive does not know about:\n"
                f"{lines}\n"
                "       Restoring would re-arm those numbers for re-issue, and if parts were\n"
                "       built with them two units will claim one identity (ADR-0021 d4;\n"
                "       ADR-0026 d8). Nothing detects that afterwards.\n"
                "       Reconcile first — establish which serials reached hardware — then pass\n"
                "       --force to say you have."
            )
        if ahead and force:
            log(f"WARNING: --force given; rolling {len(ahead)} serial counter(s) backwards")

    warehouse = Warehouse()

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        with tarfile.open(archive, "r:gz") as tar:
            tar.extractall(work, filter="data")

        # --- the blobs, FIRST (ADR-0026 d4) -------------------------------
        # So that no moment exists where the index references an absent object.
        objects = sorted((work / OBJECTS_DIR).iterdir()) if (work / OBJECTS_DIR).is_dir() else []
        log(f"restoring {len(objects)} warehouse objects")
        await warehouse.ensure_bucket()
        for path in objects:
            await warehouse.put(path.name.replace("%2F", "/"), path.read_bytes())

        # --- the index, SECOND --------------------------------------------
        log(f"restoring the index into {target_db}")
        if not shutil.which("mongorestore"):
            raise BackupError(
                "mongorestore is not on PATH. The objects are restored; the index is not.\n"
                "       Re-run once the MongoDB Database Tools are available — object\n"
                "       restoration is idempotent."
            )
        args = [
            "mongorestore",
            f"--uri={settings.mongo_uri}",
            f"--archive={work / DUMP_NAME}",
            f"--nsFrom={manifest['database']}.*",
            f"--nsTo={target_db}.*",
            "--drop",
        ]
        done = subprocess.run(args, capture_output=True, check=False)
        if done.returncode != 0:
            raise BackupError(
                f"mongorestore failed: {done.stderr.decode(errors='replace').strip()}\n"
                "       The objects are restored and the index is not — re-run to finish."
            )

    log("restored. Verify the live store before trusting it:")
    log("  python -m app.backup check-live")
    return 0


async def check_live() -> int:
    """ADR-0026 d9 against the running store: does every recorded key resolve?"""
    database = Database(settings.mongo_uri, settings.mongo_db, mock=settings.mongo_mock)
    warehouse = Warehouse()
    dangling = []
    checked = 0
    async for doc in database.db[FOUNDATION["lifecycle_doc"]].find({}, {"object_key": 1}):
        key = doc.get("object_key")
        if not key:
            continue
        checked += 1
        if not await warehouse.exists(key):
            dangling.append(key)

    log(f"checked {checked} indexed object key(s)")
    if dangling:
        log(f"  FAIL  {len(dangling)} do not resolve in the warehouse: {dangling[:5]}")
        log("        ADR-0021 d7 requires that a recorded key always resolves.")
        return 1
    log("  every recorded key resolves (ADR-0021 d7 holds)")
    return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python -m app.backup",
        description="Backup and restore the pre-cloud system of record (ADR-0026).",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("save", help="capture both stores into one archive")
    p.add_argument("--out", default="./backups", help="directory to write the archive into")

    p = sub.add_parser("verify", help="check an archive is internally consistent")
    p.add_argument("--archive", required=True)

    p = sub.add_parser("restore", help="replay an archive: blobs first, then the index")
    p.add_argument("--archive", required=True)
    p.add_argument(
        "--force",
        action="store_true",
        help="proceed even though the live store has allocated serials the archive does not "
        "know about — only after reconciling which serials reached hardware",
    )
    p.add_argument(
        "--into",
        metavar="DB",
        help="restore into this database instead of the configured one. Use for the "
        "restore rehearsal ADR-0026 d10 requires; skips the serial-counter check, since "
        "a throwaway database has no hardware behind it",
    )

    sub.add_parser("check-live", help="verify every recorded key resolves in the warehouse")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "save":
            asyncio.run(save(Path(args.out)))
        elif args.command == "verify":
            return verify(Path(args.archive))
        elif args.command == "restore":
            return asyncio.run(
                restore(Path(args.archive), force=args.force, database_name=args.into)
            )
        elif args.command == "check-live":
            return asyncio.run(check_live())
    except BackupError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
