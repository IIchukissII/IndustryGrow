# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API v1 routes (ADR-0022).

The deliberate absences are as load-bearing as the routes: there is no
profile deploy/push, no telemetry intake, no type-meaning or SKU write, and
document ingestion is allowlisted to the instance-lifecycle suffixes.
"""

from __future__ import annotations

from datetime import UTC, date, datetime

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile, status
from motor.motor_asyncio import AsyncIOMotorDatabase
from pymongo.errors import DuplicateKeyError

from app.api import schemas
from app.api.deps import (
    gateway_identity,
    get_db,
    require_provisioning,
    require_read,
    require_write,
)
from app.config import settings
from app.db import DOMAIN, FOUNDATION
from app.services import docs, integration, profiles, registry
from app.services import serials as serials_svc
from app.services.integration import PositionOccupiedError
from app.services.warehouse import Warehouse

# The exhaustive instance-lifecycle allowlist (ADR-0022 d7; ADR-0017 d10-12).
ALLOWED_DOC_TYPES = frozenset({"QP", "QR", "CP", "CC", "PR"})

router = APIRouter(prefix="/api/v1")


def _instance_out(doc: dict) -> schemas.InstanceOut:
    return schemas.InstanceOut(
        instance_id=doc["_id"],
        e_number=doc["e_number"],
        version=doc["version"],
        serial=doc["serial"],
        status=doc.get("status", "in-inventory"),
    )


def _integration_out(rec: dict) -> schemas.IntegrationOut:
    return schemas.IntegrationOut(
        machine_id=rec["machine_id"],
        depth_code=rec["depth_code"],
        instance_id=rec["instance_id"],
        installed_at=rec.get("installed_at"),
        removed_at=rec.get("removed_at"),
        removal_reason=rec.get("removal_reason"),
    )


async def _require_instance(db: AsyncIOMotorDatabase, instance_id: str) -> dict:
    doc = await db[FOUNDATION["module_instance"]].find_one({"_id": instance_id})
    if doc is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"no instance {instance_id}")
    return doc


@router.get("/meta", tags=["meta"])
async def console_meta(role: str = Depends(require_read)):
    """Who this instance belongs to, and what the caller's token may do. Single
    tenant in operation, multitenant-shaped in the schema (ADR-0021 d15)."""
    return {
        "operator_name": settings.operator_name,
        "operator_uuid": settings.operator_uuid,
        "role": role,
    }


@router.get("/catalog", response_model=schemas.CatalogOut, tags=["meta"])
async def catalog(_role: str = Depends(require_read)):
    """The type registry — designations for `Exxxx` / `SPxxxx`, read from
    REGISTRY.md (ADR-0017 d3, ADR-0019).

    Read-through, not stored: the ERP owns instances, never type meaning
    (ADR-0021 d11). Callers that need a human label for an identifier take it
    from here instead of carrying a table, so a new type in the registry needs
    no change on either side.
    """
    cat = registry.catalog()
    return schemas.CatalogOut(
        modules=[schemas.ModuleOut(**vars(m)) for m in cat.modules],
        parts=[schemas.PartOut(**vars(p)) for p in cat.parts],
    )


# ============================ instances / serials ============================


@router.post("/instances", response_model=schemas.AllocateResponse, tags=["instances"])
async def allocate_serials(
    body: schemas.AllocateRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_provisioning),
):
    """Allocate gap-free serials (ADR-0022 d4). Serials are server-issued."""
    issued = await serials_svc.allocate(db, body.e_number, body.version, body.quantity)
    last = await serials_svc.peek(db, body.e_number, body.version)
    return schemas.AllocateResponse(serials=issued, next_serial=last + 1)


@router.get("/instances", response_model=list[schemas.InstanceOut], tags=["instances"])
async def list_instances(
    e_number: str | None = None,
    status_filter: str | None = None,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    query: dict = {}
    if e_number:
        query["e_number"] = e_number
    if status_filter:
        query["status"] = status_filter
    cursor = db[FOUNDATION["module_instance"]].find(query).sort("_id", 1)
    return [_instance_out(d) async for d in cursor]


@router.get("/instances/{instance_id}", response_model=schemas.InstanceOut, tags=["instances"])
async def get_instance(
    instance_id: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    return _instance_out(await _require_instance(db, instance_id))


@router.post(
    "/instances/{instance_id}/provisioning", response_model=schemas.Ack, tags=["instances"]
)
async def bind_provisioning(
    instance_id: str,
    body: schemas.ProvisionRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_provisioning),
):
    """Bind serial<->ATECC608 (the -PR record). Public material only (ADR-0022 d5)."""
    await _require_instance(db, instance_id)
    await db[FOUNDATION["instance_identity"]].update_one(
        {"_id": instance_id},
        {
            "$set": {
                "tenant_id": settings.operator_uuid,
                "cert_serial": body.cert_serial,
                "public_key_fingerprint": body.public_key_fingerprint,
                "cert_not_before": body.cert_not_before,
                "cert_not_after": body.cert_not_after,
                "pr_object_key": body.pr_object_key,
                "provisioned_at": datetime.now(UTC),
            }
        },
        upsert=True,
    )
    return schemas.Ack(detail=f"bound {instance_id}")


@router.get(
    "/instances/{instance_id}/history",
    response_model=list[schemas.IntegrationOut],
    tags=["instances"],
)
async def instance_history(
    instance_id: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    return [_integration_out(r) for r in await integration.instance_history(db, instance_id)]


@router.post(
    "/instances/{instance_id}/documents",
    response_model=schemas.LifecycleDocOut,
    tags=["documents"],
)
async def upload_document(
    instance_id: str,
    doc_type: str = Form(...),
    valid_until: date | None = Form(default=None),
    doc_date: date | None = Form(default=None),
    file: UploadFile = File(...),
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Index a lifecycle document; blob -> warehouse, key -> ERP (ADR-0022 d7).

    Allowlisted to {QP,QR,CP,CC,PR}; type-layer documents are rejected.
    """
    doc_type = doc_type.upper()
    if doc_type not in ALLOWED_DOC_TYPES:
        raise HTTPException(
            status.HTTP_422_UNPROCESSABLE_ENTITY,
            f"doc_type must be one of {sorted(ALLOWED_DOC_TYPES)} — "
            "type-layer documents go through store_sync, not the ERP",
        )
    await _require_instance(db, instance_id)

    suffix = doc_type
    if doc_type == "CC":
        stamp = (doc_date or datetime.now(UTC).date()).strftime("%Y%m%d")
        suffix = f"CC-{stamp}"
    object_key = f"{instance_id}-{suffix}"

    valid_until_dt = (
        datetime.combine(valid_until, datetime.min.time(), tzinfo=UTC) if valid_until else None
    )
    doc_date_dt = datetime.combine(doc_date, datetime.min.time(), tzinfo=UTC) if doc_date else None

    rec = await docs.record_doc(
        db,
        Warehouse(),
        instance_full_id=instance_id,
        doc_type=doc_type,
        object_key=object_key,
        blob=await file.read(),
        content_type=file.content_type or "application/octet-stream",
        valid_until=valid_until_dt,
        doc_date=doc_date_dt,
    )
    return schemas.LifecycleDocOut(
        instance_full_id=instance_id,
        doc_type=doc_type,
        object_key=rec["object_key"],
        valid_until=rec.get("valid_until"),
        status=rec.get("status", "valid"),
    )


@router.get(
    "/instances/{instance_id}/documents",
    response_model=list[schemas.LifecycleDocOut],
    tags=["documents"],
)
async def list_instance_documents(
    instance_id: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    """The indexed lifecycle documents for one instance — metadata plus the
    warehouse key. The blobs stay in the object store (ADR-0022 d7)."""
    return [
        schemas.LifecycleDocOut(
            instance_full_id=d["instance_full_id"],
            doc_type=d["doc_type"],
            object_key=d["object_key"],
            valid_until=d.get("valid_until"),
            status=d.get("status", "valid"),
        )
        for d in await docs.docs_for(db, instance_id)
    ]


# ============================ machines / integration ========================


@router.get("/machines", tags=["machines"])
async def list_machines(
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    cursor = db[FOUNDATION["machine"]].find({}).sort("_id", 1)
    return [{"machine_id": m["_id"], "notes": m.get("notes")} async for m in cursor]


@router.get(
    "/machines/{gbox}/integration",
    response_model=list[schemas.IntegrationOut],
    tags=["machines"],
)
async def machine_integration(
    gbox: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    return [_integration_out(r) for r in await integration.current_estate(db, gbox)]


@router.put(
    "/machines/{gbox}/positions/{depth}",
    response_model=schemas.IntegrationOut,
    tags=["machines"],
)
async def set_position(
    gbox: str,
    depth: str,
    body: schemas.InstallRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Install (or replace) the instance at a position (ADR-0022 d6). Depth is
    assigned here at integration; it is never written onto the instance."""
    await _require_instance(db, body.instance_id)
    current = await integration.current_at(db, gbox, depth)
    if current and current["instance_id"] == body.instance_id:
        return _integration_out(current)
    try:
        if current:
            rec = await integration.replace(db, gbox, depth, body.instance_id)
        else:
            rec = await integration.install(db, gbox, depth, body.instance_id)
    except PositionOccupiedError as exc:
        raise HTTPException(status.HTTP_409_CONFLICT, str(exc)) from exc
    return _integration_out(rec)


@router.delete("/machines/{gbox}/positions/{depth}", response_model=schemas.Ack, tags=["machines"])
async def clear_position(
    gbox: str,
    depth: str,
    reason: str = "removed",
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    removed = await integration.remove(db, gbox, depth, reason=reason)
    if not removed:
        raise HTTPException(status.HTTP_404_NOT_FOUND, "no current instance at that position")
    return schemas.Ack(detail=f"removed from {gbox}-{depth}")


# ============================ profiles (store + record) =====================


@router.post("/machines/{gbox}/profiles", response_model=schemas.ProfileOut, tags=["profiles"])
async def store_profile_version(
    gbox: str,
    body: schemas.ProfileVersionRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Store a profile version (whole artifact). This does NOT deploy it — the
    gateway pulls the active version (ADR-0022 d8; ADR-0015 single channel)."""
    try:
        doc = await profiles.add_version(
            db,
            gbox,
            body.version_tag,
            body.payload,
            signed_hash=body.signed_hash,
            source_template_ref=body.source_template_ref,
        )
    except DuplicateKeyError as exc:
        # uq_profile_version: a stored version is immutable, so re-using a tag
        # would silently fork what the gateway believes it pulled.
        raise HTTPException(
            status.HTTP_409_CONFLICT,
            f"{gbox} already has a version tagged '{body.version_tag}' — tags are immutable",
        ) from exc
    return schemas.ProfileOut(
        machine_id=gbox, version_tag=doc["version_tag"], created_at=doc.get("created_at")
    )


@router.get("/machines/{gbox}/profiles", response_model=list[schemas.ProfileOut], tags=["profiles"])
async def list_profile_versions(
    gbox: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    active = await profiles.active_version(db, gbox)
    active_tag = active["version_tag"] if active else None
    return [
        schemas.ProfileOut(
            machine_id=gbox,
            version_tag=v["version_tag"],
            created_at=v.get("created_at"),
            active=v["version_tag"] == active_tag,
        )
        for v in await profiles.versions(db, gbox)
    ]


@router.put("/machines/{gbox}/active-profile", response_model=schemas.Ack, tags=["profiles"])
async def record_active_profile(
    gbox: str,
    body: schemas.ActiveProfileRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Record which version is active on the gateway. This is a RECORD write, not
    a push — there is no deploy endpoint (ADR-0022 d8)."""
    await profiles.mark_active(db, gbox, body.version_tag)
    return schemas.Ack(detail=f"recorded {body.version_tag} active on {gbox}")


# ---- gateway pull channel (mTLS) ----


@router.get("/gateway/active-profile", tags=["gateway"])
async def gateway_pull_active_profile(
    gbox: str = Depends(gateway_identity),
    db: AsyncIOMotorDatabase = Depends(get_db),
):
    """The gateway pulls its active profile version (ADR-0022 d8). The machine
    identity comes from the mTLS certificate, never a query parameter."""
    active = await profiles.active_version(db, gbox)
    if active is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, "no active profile recorded")
    version = await db[DOMAIN["profile_version"]].find_one(
        {"machine_id": gbox, "version_tag": active["version_tag"]}
    )
    if version is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, "active version not found in store")
    return {
        "machine_id": gbox,
        "version_tag": version["version_tag"],
        "signed_hash": version.get("signed_hash"),
        "payload": version["payload"],
    }


# ============================ SP stock =====================================


@router.get("/sp-stock", tags=["stock"])
async def list_sp_stock(
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    cursor = db[FOUNDATION["sp_stock"]].find({}).sort("sp_number", 1)
    return [
        {
            "sp_number": p["sp_number"],
            "quantity": p.get("quantity", 0),
            "location": p.get("location"),
        }
        async for p in cursor
    ]


@router.post("/sp-stock", response_model=schemas.Ack, tags=["stock"])
async def set_sp_stock(
    body: schemas.SPStockRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Set stock and location for an SP part. No SKU/price — those are the BOM's
    (ADR-0022 d9)."""
    await db[FOUNDATION["sp_stock"]].update_one(
        {"sp_number": body.sp_number},
        {
            "$set": {
                "tenant_id": settings.operator_uuid,
                "quantity": body.quantity,
                "location": body.location,
            }
        },
        upsert=True,
    )
    return schemas.Ack(detail=f"stock set for {body.sp_number}")


# ============================ calibration ==================================


@router.get(
    "/calibration/expiring", response_model=list[schemas.LifecycleDocOut], tags=["documents"]
)
async def calibration_expiring(
    days: int = 30,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    return [
        schemas.LifecycleDocOut(
            instance_full_id=c["instance_full_id"],
            doc_type=c["doc_type"],
            object_key=c["object_key"],
            valid_until=c.get("valid_until"),
            status=c.get("status", "valid"),
        )
        for c in await docs.expiring(db, within_days=days)
    ]
