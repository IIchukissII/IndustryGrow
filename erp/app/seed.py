# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Seed the reference estate — the strawberry GBOX_0001 (ADR-0003).

Run: ``python -m app.seed``  (drops and reloads the ERP collections).
This mirrors the console mockup so the app has something to show on first run.
Lifecycle-doc rows are inserted as index entries only (object keys); no blob is
written to the warehouse here.
"""

from __future__ import annotations

import asyncio
from datetime import UTC, datetime

from app.config import settings
from app.db import DOMAIN, FOUNDATION, Database, ensure_indexes

TENANT = settings.operator_uuid
MACHINE = "GBOX_0001"


def _dt(iso: str) -> datetime:
    return datetime.fromisoformat(iso).replace(tzinfo=UTC)


# (depth, e_number, version, serial, status)
INTEGRATED = [
    ("010100", "E0002", "020100", "000188"),
    ("020100", "E0003", "010200", "000042"),
    ("030100", "E0004", "010100", "000103"),
    ("040100", "E0005", "010000", "000017"),
    ("050100", "E0006", "010100", "000009"),
]

COUNTERS = {
    "E0002-020100": 201,
    "E0003-010200": 42,
    "E0004-010100": 103,
    "E0005-010000": 17,
    "E0006-010100": 9,
}

CALIBRATIONS = [
    ("E0004-010100-000103", "CC", "E0004-010100-000201-CC-20260529", "2026-07-31"),
    ("E0004-010100-000103", "CC", "E0004-010100-000212-CC-20260602", "2026-08-09"),
    ("E0004-010100-000097", "CC", "E0004-010100-000188-CC-20260415", "2026-07-15"),
]

SP_STOCK = [
    ("SP0004", 1, "gateway at GBOX_0001"),
    ("SP0003", 2, "cabinet E0007"),
    ("SP0005", 4, "core boards"),
]


async def seed(db=None) -> None:
    if db is None:
        db = Database(settings.mongo_uri, settings.mongo_db).db
    now = datetime.now(UTC)

    for name in [*FOUNDATION.values(), *DOMAIN.values()]:
        await db[name].delete_many({})

    await db[FOUNDATION["machine"]].insert_one(
        {"_id": MACHINE, "tenant_id": TENANT, "created_at": now, "retired_at": None, "notes": None}
    )
    await db[DOMAIN["gbox"]].insert_one(
        {
            "_id": MACHINE,
            "slot_count": 9,
            "cultivar": "strawberry day-neutral",
            "location_label": "reference cabinet",
            "stagger_cadence_days": 14,
        }
    )

    for _depth, e, v, n in INTEGRATED:
        await db[FOUNDATION["module_instance"]].insert_one(
            {
                "_id": f"{e}-{v}-{n}",
                "tenant_id": TENANT,
                "e_number": e,
                "version": v,
                "serial": n,
                "status": "installed",
                "produced_at": now,
            }
        )
    for depth, e, v, n in INTEGRATED:
        await db[FOUNDATION["integration_record"]].insert_one(
            {
                "tenant_id": TENANT,
                "machine_id": MACHINE,
                "depth_code": depth,
                "instance_id": f"{e}-{v}-{n}",
                "installed_at": now,
                "removed_at": None,
                "removal_reason": None,
            }
        )

    for cid, last in COUNTERS.items():
        await db[FOUNDATION["serial_counter"]].insert_one({"_id": cid, "last_serial": last})

    await db[FOUNDATION["instance_identity"]].insert_one(
        {
            "_id": "E0002-020100-000188",
            "tenant_id": TENANT,
            "cert_serial": "3F:A2:19:0C",
            "public_key_fingerprint": "b7:2e:…:c4 (P-256)",
            "cert_not_before": _dt("2026-06-14"),
            "cert_not_after": _dt("2036-06-14"),
            "pr_object_key": "E0002-020100-000188-PR",
            "provisioned_at": _dt("2026-06-14"),
        }
    )

    for inst, doc_type, key, until in CALIBRATIONS:
        await db[FOUNDATION["lifecycle_doc"]].insert_one(
            {
                "tenant_id": TENANT,
                "instance_full_id": inst,
                "doc_type": doc_type,
                "doc_date": None,
                "object_key": key,
                "valid_until": _dt(until),
                "status": "valid",
            }
        )

    for tag in ["v5", "v6", "v7", "v8"]:
        await db[DOMAIN["profile_version"]].insert_one(
            {
                "machine_id": MACHINE,
                "version_tag": tag,
                "payload": {"setpoints": {}, "model": {}},
                "signed_hash": None,
                "source_template_ref": "v1",
                "created_at": now,
                "created_by": "seed",
            }
        )
    await db[DOMAIN["profile_deployment"]].insert_one(
        {
            "machine_id": MACHINE,
            "version_tag": "v7",
            "activated_at": now,
            "deactivated_at": None,
            "activated_by": "seed",
        }
    )

    for sp, qty, loc in SP_STOCK:
        await db[FOUNDATION["sp_stock"]].insert_one(
            {"tenant_id": TENANT, "sp_number": sp, "quantity": qty, "location": loc}
        )

    print(
        f"Seeded {MACHINE}: {len(INTEGRATED)} instances installed, "
        f"{len(CALIBRATIONS)} certs, 4 profile versions, {len(SP_STOCK)} SP rows."
    )


async def _main() -> None:
    db = Database(settings.mongo_uri, settings.mongo_db)
    await ensure_indexes(db)
    db.close()
    await seed()


if __name__ == "__main__":
    asyncio.run(_main())
