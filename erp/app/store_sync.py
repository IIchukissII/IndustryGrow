# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sync the repo ``store/`` into the warehouse (ADR-0017 d15).

``store/`` is the public, type-level slice of the object store rendered as a flat
git keyspace — identifiers *are* the object keys. This uploads each file to the
S3-compatible warehouse under the same key, so a git checkout and the bucket hold
the same objects. Per-instance private blobs (-QP/-QR/-CP/-CC/-PR) are written
separately at provisioning/calibration time, not here.

With ``--prune`` it deletes the type-layer objects no longer in ``store/`` or
``spec/``: when a version's loose gerbers are bundled into a single
``-D-fab.zip`` (ADR-0017 d18) or an artifact is withdrawn (d17), the superseded
loose objects must not linger in the bucket. Only a key that reads as type-layer
content is a candidate (``_prunable``); every other object is kept, lifecycle
objects and their archives first among them, since they exist nowhere else
(ADR-0017 d15, d22; ADR-0021 d7).

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
from app.models.identifiers import is_lifecycle_key, parse_store_key
from app.services.warehouse import Warehouse

_SKIP = {".gitattributes", ".gitignore", ".DS_Store"}


def _iter_files(root: Path) -> Iterator[Path]:
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.name not in _SKIP:
            yield path


def _prunable(key: str) -> bool:
    """True only for a key that reads as type-layer content: an E or SP root
    followed by a layer letter or a status token. Lifecycle keys, keys without
    an identifier and anything else unread are kept."""
    if is_lifecycle_key(key):
        return False
    parsed = parse_store_key(key)
    return parsed.root is not None and (parsed.layer is not None or parsed.status is not None)


def _collect(store_dir: str) -> tuple[Path, list[Path]]:
    """Resolve one document dir and list its files (blocking IO kept out of async)."""
    root = Path(store_dir).resolve()
    if not root.is_dir():
        raise SystemExit(f"document dir not found: {root}")
    return root, list(_iter_files(root))


async def sync(store_dir: str | None = None, prune: bool = False) -> int:
    """Mirror the repository's document directories into the warehouse.

    Two roots, `store/` and `spec/`, into one flat keyspace — a key is a file's
    path relative to its own root, so which directory holds it is not part of its
    identity (ADR-0017 d15, d20). Both are collected before anything is pruned:
    pruning against one root's listing would delete the other's objects.
    """
    dirs = [store_dir] if store_dir else [settings.store_dir, settings.spec_dir]

    local_keys: dict[str, Path] = {}
    roots: list[Path] = []
    for d in dirs:
        root, files = _collect(d)
        roots.append(root)
        for path in files:
            key = path.relative_to(root).as_posix()
            # First root wins, and store/ is first: a name in both directories is
            # one key, and silently uploading whichever came last would make the
            # served document depend on iteration order.
            local_keys.setdefault(key, path)

    warehouse = Warehouse()
    await warehouse.ensure_bucket()

    for key, path in local_keys.items():
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        await warehouse.put_file(key, str(path), content_type)
        print(f"  ↑ {key}")

    pruned = 0
    if prune:
        for key in await warehouse.list_prefix(""):
            if key not in local_keys and _prunable(key):
                await warehouse.delete(key)
                print(f"  ✗ {key}  (pruned — in no document directory)")
                pruned += 1

    tail = f", pruned {pruned} stale" if prune else ""
    where = " + ".join(str(r) for r in roots)
    print(
        f"Synced {len(local_keys)} objects from {where} → bucket {settings.warehouse_bucket}{tail}"
    )
    return len(local_keys)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Mirror the repo's document directories (store/, spec/) into the warehouse."
    )
    parser.add_argument(
        "--prune",
        action="store_true",
        help="delete type-layer warehouse objects in no document directory",
    )
    parser.add_argument(
        "--store-dir", default=None, help="mirror only this directory instead of both"
    )
    args = parser.parse_args()
    asyncio.run(sync(args.store_dir, prune=args.prune))
