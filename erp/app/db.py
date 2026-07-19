# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""MongoDB layer — the document metadata store (ADR-0021 rev 1).

Collection names encode the [F]/[D] split as a physical prefix boundary so the
stage-11 re-layering is mechanical: ``foundation.*`` migrates into IndustryFlow's
``production_unit`` core; ``domain.*`` stays the IndustryGrow layer. The only
cross-group reference direction is domain -> foundation.
"""

from __future__ import annotations

from motor.motor_asyncio import (
    AsyncIOMotorClient,
    AsyncIOMotorCollection,
    AsyncIOMotorDatabase,
)
from pymongo import ASCENDING, IndexModel

# [F] foundational collections — migrate to IndustryFlow production_unit (stage 11).
FOUNDATION = {
    "machine": "foundation.machine",
    "serial_counter": "foundation.serial_counter",
    "module_instance": "foundation.module_instance",
    "instance_identity": "foundation.instance_identity",
    "integration_record": "foundation.integration_record",
    "lifecycle_doc": "foundation.lifecycle_doc",
    "sp_stock": "foundation.sp_stock",
    "sp_placement": "foundation.sp_placement",
}

# [D] domain collections — stay the IndustryGrow layer.
DOMAIN = {
    "gbox": "domain.gbox",
    "profile_version": "domain.profile_version",
    "profile_deployment": "domain.gbox_profile_deployment",
}


class Database:
    """Thin async wrapper over a single MongoDB database."""

    def __init__(self, uri: str, db_name: str, *, mock: bool = False) -> None:
        if mock:
            # In-memory Mongo for local dev/demo — no server needed.
            from mongomock_motor import AsyncMongoMockClient

            self._client = AsyncMongoMockClient()
        else:
            self._client = AsyncIOMotorClient(uri)
        self.db: AsyncIOMotorDatabase = self._client[db_name]

    def coll(self, name: str) -> AsyncIOMotorCollection:
        return self.db[name]

    def close(self) -> None:
        close = getattr(self._client, "close", None)
        if callable(close):
            close()


async def ensure_indexes(database: Database) -> None:
    """Create the invariants the ADR requires, expressed as Mongo indexes."""
    db = database.db

    # Full identity is unique per tenant (ADR-0017 d1; serial unique per module+version).
    await db[FOUNDATION["module_instance"]].create_indexes(
        [
            IndexModel(
                [
                    ("tenant_id", ASCENDING),
                    ("e_number", ASCENDING),
                    ("version", ASCENDING),
                    ("serial", ASCENDING),
                ],
                unique=True,
                name="uq_instance_full_id",
            )
        ]
    )

    # Exactly one *current* instance per (machine, depth) — the mutable
    # cross-reference invariant (ADR-0017 d13), as a partial-unique index.
    await db[FOUNDATION["integration_record"]].create_indexes(
        [
            IndexModel(
                [("machine_id", ASCENDING), ("depth_code", ASCENDING)],
                unique=True,
                name="uq_current_position",
                partialFilterExpression={"removed_at": None},
            ),
            IndexModel([("instance_id", ASCENDING)], name="ix_instance_history"),
        ]
    )

    # Lifecycle-document index — query by instance and by calibration expiry (ADR-0021 d7).
    await db[FOUNDATION["lifecycle_doc"]].create_indexes(
        [
            IndexModel([("instance_full_id", ASCENDING)], name="ix_doc_instance"),
            IndexModel([("valid_until", ASCENDING)], name="ix_doc_expiry"),
        ]
    )

    # One profile version tag per machine; one active deployment per machine (ADR-0021 d8).
    await db[DOMAIN["profile_version"]].create_indexes(
        [
            IndexModel(
                [("machine_id", ASCENDING), ("version_tag", ASCENDING)],
                unique=True,
                name="uq_profile_version",
            )
        ]
    )
    await db[DOMAIN["profile_deployment"]].create_indexes(
        [
            IndexModel(
                [("machine_id", ASCENDING)],
                unique=True,
                name="uq_active_deployment",
                partialFilterExpression={"deactivated_at": None},
            )
        ]
    )
