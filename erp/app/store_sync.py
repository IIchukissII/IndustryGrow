# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sync the repo ``store/`` into the warehouse (ADR-0017 d15).

``store/`` is the public, type-level slice of the object store rendered as a flat
git keyspace — identifiers *are* the object keys. This uploads each file to the
S3-compatible warehouse under the same key, so a git checkout and the bucket hold
the same objects. Per-instance private blobs (-QP/-QR/-CP/-CC/-PR) are written
separately at provisioning/calibration time, not here.

Run: ``python -m app.store_sync``  (endpoint/bucket from ERP_WAREHOUSE_* config,
so it targets MinIO, AWS S3, or Cloudflare R2 identically).
"""

from __future__ import annotations

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


async def sync(store_dir: str | None = None) -> int:
    root, files = _collect(store_dir or settings.store_dir)

    warehouse = Warehouse()
    await warehouse.ensure_bucket()

    count = 0
    for path in files:
        key = path.relative_to(root).as_posix()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        await warehouse.put_file(key, str(path), content_type)
        print(f"  ↑ {key}")
        count += 1

    print(f"Synced {count} objects from {root} → bucket {settings.warehouse_bucket}")
    return count


if __name__ == "__main__":
    asyncio.run(sync())
