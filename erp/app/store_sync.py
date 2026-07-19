# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sync the repo ``store/`` into the warehouse (ADR-0017 d15).

``store/`` is the public, type-level slice of the object store rendered as a flat
git keyspace — identifiers *are* the object keys. This uploads each file to the
S3-compatible warehouse under the same key, so a git checkout and the bucket hold
the same objects. Per-instance private blobs (-QP/-QR/-CP/-CC/-PR) are written
separately at provisioning/calibration time, not here.

With ``--prune`` it becomes a true mirror: objects present in the warehouse but no
longer in ``store/`` are deleted. This matters because ADR-0017 keeps one object
per identifier — when a version's loose gerbers are bundled into a single
``-D-fab.zip`` (ADR-0017 d18) or an artifact is withdrawn (d17), the superseded
loose objects must not linger in the bucket.

Run: ``python -m app.store_sync [--prune]``  (endpoint/bucket from ERP_WAREHOUSE_*,
so it targets MinIO, AWS S3, or Cloudflare R2 identically).
"""

from __future__ import annotations

import argparse
import asyncio
import mimetypes
from collections.abc import Iterator
from pathlib import Path

from app.config import settings
from app.services.warehouse import Warehouse

_SKIP = {".gitattributes", ".gitignore", ".DS_Store"}


def _iter_files(root: Path) -> Iterator[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name not in _SKIP:
            yield path


def _collect(store_dir: str) -> tuple[Path, list[Path]]:
    """Resolve the store dir and list its files (blocking IO kept out of async)."""
    root = Path(store_dir).resolve()
    if not root.is_dir():
        raise SystemExit(f"store dir not found: {root}")
    return root, list(_iter_files(root))


async def sync(store_dir: str | None = None, prune: bool = False) -> int:
    root, files = _collect(store_dir or settings.store_dir)

    warehouse = Warehouse()
    await warehouse.ensure_bucket()

    local_keys = {path.relative_to(root).as_posix(): path for path in files}
    for key, path in local_keys.items():
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        await warehouse.put_file(key, str(path), content_type)
        print(f"  ↑ {key}")

    pruned = 0
    if prune:
        for key in await warehouse.list_prefix(""):
            if key not in local_keys:
                await warehouse.delete(key)
                print(f"  ✗ {key}  (pruned — no longer in store/)")
                pruned += 1

    tail = f", pruned {pruned} stale" if prune else ""
    print(
        f"Synced {len(local_keys)} objects from {root} → bucket {settings.warehouse_bucket}{tail}"
    )
    return len(local_keys)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Mirror the repo store/ into the warehouse.")
    parser.add_argument(
        "--prune", action="store_true", help="delete warehouse objects not in store/"
    )
    parser.add_argument("--store-dir", default=None, help="override store/ path")
    args = parser.parse_args()
    asyncio.run(sync(args.store_dir, prune=args.prune))
