# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API v1 routes (ADR-0022).

The deliberate absences are as load-bearing as the routes: there is no
profile deploy/push, no telemetry intake, no type-meaning or SKU write, and
document ingestion is allowlisted to the instance-lifecycle suffixes.
"""

from __future__ import annotations

import asyncio
from datetime import UTC, date, datetime
from pathlib import Path

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile, status
from fastapi.concurrency import iterate_in_threadpool
from fastapi.responses import StreamingResponse
from motor.motor_asyncio import AsyncIOMotorDatabase
from pymongo.errors import DuplicateKeyError

from app.api import schemas
from app.api.deps import (
    gateway_identity,
    get_db,
    get_warehouse,
    require_provisioning,
    require_read,
    require_write,
)
from app.config import settings
from app.db import DOMAIN, FOUNDATION
from app.models import identifiers
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
    warehouse: Warehouse = Depends(get_warehouse),
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
        warehouse,
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
            body.document_b64,
            signature=body.signature,
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
    try:
        await profiles.mark_active(db, gbox, body.version_tag)
    except profiles.UnknownVersionError as exc:
        raise HTTPException(status.HTTP_404_NOT_FOUND, str(exc)) from exc
    except profiles.UnsignedVersionError as exc:
        # 409, not 422: the request is well-formed and the version exists — the
        # conflict is with the state of that version (ADR-0025 d11).
        raise HTTPException(status.HTTP_409_CONFLICT, str(exc)) from exc
    return schemas.Ack(detail=f"recorded {body.version_tag} active on {gbox}")


# ============================ machine provisioning ==========================


@router.post("/machines/{gbox}/provisioning", response_model=schemas.Ack, tags=["machines"])
async def bind_machine_provisioning(
    gbox: str,
    body: schemas.MachineProvisionRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_provisioning),
):
    """Bind a machine to its ATECC608 certificate (ADR-0022 rev 1 d12).

    Separate from the E-instance route because a gateway has no
    `Exxxx-VVVVVV-NNNNNN` serial — it is SP0004 identified as a machine, and
    minting it a synthetic instance id to reuse that route would corrupt the
    ADR-0017 grammar to reach a database row (alternative N).

    Upsert, not insert: certificates are short-lived and auto-renewed
    (ADR-0007 d7), so re-certification calls this again with a new serial and
    validity. The stable anchors — the machine identifier and the public-key
    fingerprint — do not change across renewal (ADR-0007 rev 1 d10d).
    """
    if not identifiers.MACHINE_RE.match(gbox):
        raise HTTPException(
            status.HTTP_422_UNPROCESSABLE_ENTITY,
            f"{gbox} is not an ADR-0017 machine identifier (GBOX_NNNN)",
        )
    await db[FOUNDATION["machine_identity"]].update_one(
        {"_id": gbox},
        {
            "$set": {
                "tenant_id": settings.operator_uuid,
                "machine_id": gbox,
                "vendor_serial": body.vendor_serial,
                "atecc_serial": body.atecc_serial,
                "public_key_fingerprint": body.public_key_fingerprint,
                "cert_serial": body.cert_serial,
                "cert_not_before": body.cert_not_before,
                "cert_not_after": body.cert_not_after,
                "provisioned_at": datetime.now(UTC),
            }
        },
        upsert=True,
    )
    return schemas.Ack(detail=f"bound {gbox} to certificate {body.cert_serial}")


@router.get(
    "/machines/{gbox}/gateway-channel", response_model=schemas.GatewayChannelOut, tags=["machines"]
)
async def gateway_channel(
    gbox: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    """Everything the ERP owns about one machine's gateway channel (card 15).

    Note what is absent: whether the gateway has actually pulled. That is
    operational and stays out of the ERP (ADR-0022 d9; d8 rev 1) — the console
    shows it as a gap rather than this route inventing an answer.
    """
    identity = await db[FOUNDATION["machine_identity"]].find_one({"_id": gbox})
    identity_out = None
    if identity is not None:
        not_after = identity["cert_not_after"]
        if not_after.tzinfo is None:  # Mongo round-trips naive UTC
            not_after = not_after.replace(tzinfo=UTC)
        identity_out = schemas.MachineIdentityOut(
            machine_id=gbox,
            vendor_serial=identity["vendor_serial"],
            atecc_serial=identity.get("atecc_serial"),
            public_key_fingerprint=identity["public_key_fingerprint"],
            cert_serial=identity["cert_serial"],
            cert_not_before=identity["cert_not_before"],
            cert_not_after=identity["cert_not_after"],
            expires_in_days=(not_after - datetime.now(UTC)).days,
            provisioned_at=identity.get("provisioned_at"),
        )

    active = await profiles.active_version(db, gbox)
    stored = await profiles.versions(db, gbox)
    return schemas.GatewayChannelOut(
        machine_id=gbox,
        identity=identity_out,
        active_version=active["version_tag"] if active else None,
        active_since=active.get("activated_at") if active else None,
        stored_versions=len(stored),
        unsigned_versions=sum(1 for v in stored if not v.get("signature")),
    )


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
    # The envelope is UNSIGNED and the gateway treats it as such: it carries the
    # signed bytes and their signature, and the machine and version the client
    # acts on come from inside those bytes after verification (ADR-0025 d6-d7).
    # machine_id and version_tag are echoed here for operators reading the
    # channel by hand, not as anything a client should trust.
    return {
        "machine_id": gbox,
        "version_tag": version["version_tag"],
        "document_b64": version["document_b64"],
        "signature": version.get("signature"),
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


def _is_store_file(object_key: str) -> bool:
    """True when the key names a file directly in the repository's `store/`.

    Resolved against the directory rather than joined onto it, so a key holding
    `..` or a separator cannot walk out of the mirror. Directly in it, not below:
    the warehouse keyspace is flat (ADR-0017 d15), and the nested KiCad footprint
    directories are not documents anyone reads through the console.
    """
    root = Path(settings.store_dir).resolve()
    candidate = (root / object_key).resolve()
    return candidate.parent == root and candidate.is_file()


async def _read_through(warehouse: Warehouse, object_key: str) -> StreamingResponse:
    """Stream one object from the store to the caller, holding none of it.

    ADR-0022 rev 2 d7's third form. The ERP keeps no copy — chunks pass through —
    so ADR-0021 d7's "does not duplicate blob content" still holds. This exists
    because a presigned URL is only spendable by a browser the object store has
    been configured to accept, and an operator handed a grant they cannot spend
    has been given nothing.

    `inline` rather than `attachment`: the point is to read the document where you
    are, and a Content-Disposition of attachment would send every manual to the
    downloads folder instead.
    """
    try:
        body, content_type, length = await warehouse.open_stream(object_key)
    except Exception as exc:
        raise HTTPException(
            status.HTTP_502_BAD_GATEWAY,
            f"{object_key} could not be read from the warehouse: {exc}",
        ) from exc

    def _chunks():
        try:
            while chunk := body.read(64 * 1024):
                yield chunk
        finally:
            body.close()

    headers = {"Content-Disposition": f'inline; filename="{object_key}"'}
    if length is not None:
        headers["Content-Length"] = str(length)
    return StreamingResponse(
        iterate_in_threadpool(_chunks()), media_type=content_type, headers=headers
    )


@router.get("/store-documents", response_model=list[schemas.StoreDocOut], tags=["documents"])
async def list_store_documents(_role: str = Depends(require_read)):
    """The type-layer documents the repository owns and `store_sync` mirrors.

    Read-only, storing nothing — the same shape ADR-0023 established for
    `REGISTRY.md` and the reasoning decision 1's 2026-07-26 clarification extends
    to these. The ERP owns none of this: the listing is taken from the mounted
    `store/` directory, which is the repository's, so what is servable is defined
    by the repo rather than by whatever happens to be in the bucket.
    """

    def _scan() -> list[schemas.StoreDocOut]:
        return sorted(
            (
                schemas.StoreDocOut(
                    object_key=p.name,
                    kind=registry.store_doc_kind(p.name),
                    size_bytes=p.stat().st_size,
                )
                for p in Path(settings.store_dir).iterdir()
                if p.is_file() and not p.name.startswith(".")
            ),
            key=lambda d: d.object_key,
        )

    # Off-thread: this handler serves requests, so a directory walk on a slow or
    # network-mounted store/ would stall the loop for everyone else.
    return await asyncio.to_thread(_scan)


@router.get(
    "/store-documents/{object_key}/url", response_model=schemas.DocumentUrlOut, tags=["documents"]
)
async def store_document_url(
    object_key: str,
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """A read grant for one type-layer document (decision 1, 2026-07-26 clarification).

    Guarded twice, and both guards matter. The key must name a file in the
    repository's `store/` directory — so this reads the *mirror*, not the bucket,
    and an operator-private instance document can never be reached through here
    (that is the indexed route above, with its own index check). And it is
    resolved against the directory rather than joined onto it, so a key
    containing `..` or a slash cannot walk out of the mirror.
    """

    if not await asyncio.to_thread(_is_store_file, object_key):
        raise HTTPException(
            status.HTTP_404_NOT_FOUND,
            f"{object_key} is not a document in the repository's store/",
        )
    if not await warehouse.exists(object_key):
        raise HTTPException(
            status.HTTP_404_NOT_FOUND,
            f"{object_key} is in store/ but not in the warehouse — run "
            "`python -m app.store_sync` to mirror it",
        )

    expires = settings.document_url_ttl
    return schemas.DocumentUrlOut(
        object_key=object_key,
        url=await warehouse.presigned_get(object_key, expires=expires),
        expires_in=expires,
    )


@router.get("/store-documents/{object_key}/content", tags=["documents"])
async def store_document_content(
    object_key: str,
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """Read one repository document through (ADR-0022 rev 2 d7).

    Guarded exactly as the URL route above is: the key must name a file in the
    repository's `store/`, resolved against it rather than joined onto it.
    """
    if not await asyncio.to_thread(_is_store_file, object_key):
        raise HTTPException(
            status.HTTP_404_NOT_FOUND,
            f"{object_key} is not a document in the repository's store/",
        )
    return await _read_through(warehouse, object_key)


@router.get(
    "/instances/{instance_id}/documents/{object_key}/url",
    response_model=schemas.DocumentUrlOut,
    tags=["documents"],
)
async def document_url(
    instance_id: str,
    object_key: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """A time-limited retrieval URL for one indexed document (ADR-0022 d7).

    d7 gives the ERP exactly two things it may hand back for a blob: the object
    key, or a time-limited retrieval URL. This is the second — the API still never
    returns blob content, so a reader fetches from the object store directly and
    the ERP stays the index rather than becoming a proxy for it.

    **The key must be one this instance has indexed.** Presigning whatever key a
    caller names would turn the index into a general read primitive over the whole
    bucket — including the `store/` mirror and anything else living there — which
    is a different resource from the one ADR-0022 d1 exposes. The lookup below is
    what keeps this "serve my own record" rather than "proxy the object store".
    """
    doc = await db[FOUNDATION["lifecycle_doc"]].find_one(
        {"instance_full_id": instance_id, "object_key": object_key}
    )
    if doc is None:
        raise HTTPException(
            status.HTTP_404_NOT_FOUND,
            f"{instance_id} has no indexed document {object_key}",
        )

    # A recorded key always resolves (ADR-0021 d7). If it does not, the index and
    # the store have diverged, and saying so beats handing back a URL that 404s
    # at the object store with no explanation.
    if not await warehouse.exists(object_key):
        raise HTTPException(
            status.HTTP_502_BAD_GATEWAY,
            f"{object_key} is indexed but absent from the warehouse — the index and the "
            "object store have diverged (ADR-0021 d7). Run `python -m app.backup check-live`.",
        )

    expires = settings.document_url_ttl
    return schemas.DocumentUrlOut(
        object_key=object_key,
        url=await warehouse.presigned_get(object_key, expires=expires),
        expires_in=expires,
    )


@router.get("/instances/{instance_id}/documents/{object_key}/content", tags=["documents"])
async def document_content(
    instance_id: str,
    object_key: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """Read one indexed document through (ADR-0022 rev 2 d7).

    Same index guard as the URL route: the key must be one this instance has
    indexed, so this serves the ERP's own record rather than the bucket.
    """
    doc = await db[FOUNDATION["lifecycle_doc"]].find_one(
        {"instance_full_id": instance_id, "object_key": object_key}
    )
    if doc is None:
        raise HTTPException(
            status.HTTP_404_NOT_FOUND, f"{instance_id} has no indexed document {object_key}"
        )
    return await _read_through(warehouse, object_key)


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
