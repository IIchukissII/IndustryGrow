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
from fastapi.responses import Response, StreamingResponse
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
from app.services import docs, firmware, integration, profiles, registry, reports
from app.services import serials as serials_svc
from app.services.integration import PositionOccupiedError
from app.services.warehouse import Warehouse

# The exhaustive instance-lifecycle allowlist (ADR-0022 d7; ADR-0017 d10-12).
ALLOWED_DOC_TYPES = frozenset({"QP", "QR", "CP", "CC", "PR"})

router = APIRouter(prefix="/api/v1")


def _version_label(code: str | None) -> str | None:
    """`020100` -> `v2.1.0`, or None for anything that is not a version code.

    Decoding is the API's job, not a caller's (ADR-0022 d13). Returning None
    rather than the raw code keeps the two distinguishable: a client can then
    show the code alone instead of presenting a failed decode as a version.
    """
    if not code:
        return None
    try:
        return str(identifiers.decode_version(code))
    except ValueError:
        return None


def _instance_out(doc: dict) -> schemas.InstanceOut:
    return schemas.InstanceOut(
        instance_id=doc["_id"],
        e_number=doc["e_number"],
        version=doc["version"],
        serial=doc["serial"],
        status=doc.get("status", "in-inventory"),
        version_label=_version_label(doc["version"]),
    )


def _integration_out(rec: dict) -> schemas.IntegrationOut:
    """Both axes, each read into its own fields.

    The depth and the version are both six digits and mean entirely different
    things — one is a position in the machine, the other a semantic version of
    the design (ADR-0017 d1). Handing back two identical-looking codes and
    leaving the reader to tell them apart by their place in a string is what
    this decoding exists to stop.
    """
    depth_code = rec["depth_code"]
    try:
        levels = list(identifiers.decode_depth(depth_code))
    except ValueError:
        levels = None

    try:
        parts = identifiers.parse_instance(rec["instance_id"])
    except ValueError:
        parts = {}

    return schemas.IntegrationOut(
        machine_id=rec["machine_id"],
        depth_code=depth_code,
        instance_id=rec["instance_id"],
        installed_at=rec.get("installed_at"),
        removed_at=rec.get("removed_at"),
        removal_reason=rec.get("removal_reason"),
        depth_levels=levels,
        depth_label=".".join(f"{n:02d}" for n in levels) if levels else None,
        e_number=parts.get("e_number"),
        version=parts.get("version"),
        version_label=_version_label(parts.get("version")),
        serial=parts.get("serial"),
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


@router.get("/instances/{instance_id}/report.pdf", tags=["documents"])
async def instance_report(
    instance_id: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    """One instance as a printable PDF.

    A representation, not a resource: every fact in it is served by the JSON
    routes above, so this introduces no entity and ADR-0022 d1's enumeration is
    unchanged. Rendered for monochrome — these get printed and filed.

    `attachment`, unlike the document read-through: a dossier is produced to be
    kept, and a browser that renders it inline gives an operator a tab instead of
    a file.
    """
    instance = await _require_instance(db, instance_id)
    identity = await db[FOUNDATION["instance_identity"]].find_one({"_id": instance_id})
    history = await integration.instance_history(db, instance_id)
    documents = await docs.docs_for(db, instance_id)

    # Off-thread: rendering is CPU-bound and this handler shares the event loop
    # with every other request.
    blob = await asyncio.to_thread(
        reports.instance_dossier,
        instance=instance,
        identity=identity,
        history=history,
        documents=documents,
    )
    return Response(
        content=blob,
        media_type="application/pdf",
        headers={"Content-Disposition": f'attachment; filename="{instance_id}.pdf"'},
    )


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

    # ADR-0017 d11: calibration recurs where quality control does not, so BOTH
    # calibration records are stamped and a later run cannot overwrite an earlier
    # one. QP/QR/PR are issued once per instance and take no stamp.
    #
    # To the SECOND, not to the day. A date alone satisfies d11 only for runs on
    # different days: two calibrations of the same probe on one day produced the
    # same key, the warehouse put overwrote in place, and the first run's raw
    # points were gone while its index row remained. Recalibrating twice in a
    # session is ordinary during bring-up, so the day was the wrong resolution.
    #
    # `doc_date` still names the day when the caller supplies one — it is the day
    # the calibration happened, which may not be the day it is filed — and the
    # time component comes from the upload either way. It only has to disambiguate
    # within that day.
    suffix = doc_type
    if doc_type in {"CP", "CC"}:
        now = datetime.now(UTC)
        day = (doc_date or now.date()).strftime("%Y%m%d")
        suffix = f"{doc_type}-{day}T{now.strftime('%H%M%S')}"
    object_key = f"{instance_id}-{suffix}"

    # d11 admits both encodings — dated or sequenced — and this uses each where it
    # works. The stamp separates runs; a sequence separates what the stamp cannot,
    # which is two uploads inside one second or a clock that stepped backwards.
    # Refusing instead would be correct and useless: the operator has the points
    # in front of them and no way to file them but to wait.
    if doc_type in {"CP", "CC"}:
        base, n = object_key, 1
        while await warehouse.exists(object_key):
            n += 1
            object_key = f"{base}-{n}"
            if n > 99:  # not reachable by clock skew; a bound, not a policy
                raise HTTPException(
                    status.HTTP_409_CONFLICT,
                    f"{base} and 99 sequenced variants all exist",
                )

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


@router.put("/machines/{gbox}", response_model=schemas.MachineOut, tags=["machines"])
async def register_machine(
    gbox: str,
    body: schemas.MachineRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_write),
):
    """Enrol a machine (ADR-0021 d4; ADR-0017 d6).

    Every other machine-scoped route — integration, profiles, firmware, the
    gateway channel — hangs off a `GBOX_NNNN` existing, so without this the
    record has no way to acquire its first cabinet.

    Upsert on the identifier: machines are *enumerated*, so the identifier is
    assigned by the operator rather than issued here (unlike a serial, ADR-0022
    d4), and re-registering one edits its notes instead of failing. Retiring a
    machine is not this route — `retired_at` is set where a removal is recorded.
    """
    if not identifiers.MACHINE_RE.match(gbox):
        raise HTTPException(
            status.HTTP_422_UNPROCESSABLE_ENTITY,
            f"{gbox} is not an ADR-0017 machine identifier (GBOX_NNNN)",
        )
    await db[FOUNDATION["machine"]].update_one(
        {"_id": gbox},
        {
            "$set": {"tenant_id": settings.operator_uuid, "notes": body.notes},
            "$setOnInsert": {"created_at": datetime.now(UTC), "retired_at": None},
        },
        upsert=True,
    )
    return schemas.MachineOut(machine_id=gbox, notes=body.notes)


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


# ============================ firmware (intent + artifacts) =================


def _firmware_intent_out(doc: dict) -> schemas.FirmwareIntentOut:
    release_root = doc["release_root"]
    version = identifiers.parse_store_key(release_root).version
    return schemas.FirmwareIntentOut(
        machine_id=doc["machine_id"],
        release_root=release_root,
        selected_at=doc.get("selected_at"),
        selected_by=doc.get("selected_by"),
        artifact_keys=firmware.artifact_keys(release_root),
        version=version,
        version_label=_version_label(version),
    )


@router.get(
    "/firmware-releases", response_model=list[schemas.FirmwareReleaseOut], tags=["firmware"]
)
async def list_firmware_releases(
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """The firmware releases the warehouse can serve (ADR-0029 d13).

    Listed from the bucket, not from a checkout: the bucket is where a gateway's
    bytes come from, so it is what decides a release is selectable. A release
    appears only if both of its slot images are there — one is not servable to
    whichever node is running the other slot (ADR-0029 d17), and offering it would
    move that failure to a gateway days later instead of refusing it here.
    """
    out = []
    for root in await firmware.available_releases(warehouse):
        version = identifiers.parse_store_key(root).version
        out.append(
            schemas.FirmwareReleaseOut(
                release_root=root,
                version=version,
                version_label=_version_label(version),
                artifact_keys=firmware.artifact_keys(root),
            )
        )
    return out


@router.put(
    "/machines/{gbox}/firmware", response_model=schemas.FirmwareIntentOut, tags=["firmware"]
)
async def set_firmware_intent(
    gbox: str,
    body: schemas.FirmwareIntentRequest,
    db: AsyncIOMotorDatabase = Depends(get_db),
    warehouse: Warehouse = Depends(get_warehouse),
    role: str = Depends(require_write),
):
    """Record which firmware release this machine's nodes should run (ADR-0022 d14).

    A RECORD write, not an update: nothing here reaches a node. The gateway reads
    this, compares it against what it observes on the bus, and performs the
    transfer (ADR-0029 d15-16). There is no flash endpoint and no way to ask this
    API whether the update happened.
    """
    if not identifiers.MACHINE_RE.match(gbox):
        raise HTTPException(
            status.HTTP_422_UNPROCESSABLE_ENTITY,
            f"{gbox} is not an ADR-0017 machine identifier (GBOX_NNNN)",
        )
    try:
        doc = await firmware.set_intent(db, warehouse, gbox, body.release_root, selected_by=role)
    except firmware.UnknownReleaseError as exc:
        # 404 on the release, not 422 on the request: the body is well-formed and
        # the identifier is grammatical — what is missing is the artifact set it
        # names, which is a fact about the warehouse rather than about the request.
        raise HTTPException(status.HTTP_404_NOT_FOUND, str(exc)) from exc
    return _firmware_intent_out(doc)


@router.get(
    "/machines/{gbox}/firmware", response_model=schemas.FirmwareIntentOut, tags=["firmware"]
)
async def get_firmware_intent(
    gbox: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    _role: str = Depends(require_read),
):
    doc = await firmware.intent(db, gbox)
    if doc is None:
        raise HTTPException(status.HTTP_404_NOT_FOUND, f"no firmware release intended for {gbox}")
    return _firmware_intent_out(doc)


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


@router.get("/gateway/firmware", response_model=schemas.FirmwareIntentOut, tags=["gateway"])
async def gateway_pull_firmware_intent(
    gbox: str = Depends(gateway_identity),
    db: AsyncIOMotorDatabase = Depends(get_db),
):
    """The gateway pulls the release its machine is meant to run (ADR-0022 d14).

    A pure read, like the profile pull beside it: nothing is written, so the ERP
    cannot say whether this was ever collected or acted on. The machine identity
    comes from the mTLS certificate, never a query parameter.
    """
    doc = await firmware.intent(db, gbox)
    if doc is None:
        raise HTTPException(
            status.HTTP_404_NOT_FOUND, "no firmware release intended for this machine"
        )
    return _firmware_intent_out(doc)


@router.get("/gateway/firmware/{object_key}/content", tags=["gateway"])
async def gateway_pull_firmware_artifact(
    object_key: str,
    gbox: str = Depends(gateway_identity),
    db: AsyncIOMotorDatabase = Depends(get_db),
    warehouse: Warehouse = Depends(get_warehouse),
):
    """One slot image of this machine's intended release, read through (ADR-0022 d14).

    **The key must be an artifact of the release this machine intends**, which is
    the same guard the indexed-document route applies for the same reason:
    presigning or streaming whatever key a caller names would turn the gateway
    channel into a general read primitive over the bucket, and the gateway is the
    one caller class that has been given no other read of the store at all.

    Comparing against the intended release rather than against the filesystem also
    scopes the read per machine: a gateway cannot reach an artifact of a release
    its own machine does not intend.
    """
    doc = await firmware.intent(db, gbox)
    if doc is None:
        raise HTTPException(
            status.HTTP_404_NOT_FOUND, "no firmware release intended for this machine"
        )
    if object_key not in firmware.artifact_keys(doc["release_root"]):
        raise HTTPException(
            status.HTTP_404_NOT_FOUND,
            f"{object_key} is not an artifact of {doc['release_root']}, "
            "the release this machine intends",
        )
    return await _read_through(warehouse, object_key)


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


def _is_listable(path: Path) -> bool:
    return path.is_file() and not path.name.startswith(".")


def _store_doc(path: Path) -> schemas.StoreDocOut:
    """One store object, with its key read into the ADR-0017 / ADR-0019 fields.

    Parsed here rather than by the caller: the API speaks the identifier grammar
    (ADR-0022 d13), so a console can lay a document out by root, version and layer
    without carrying a second copy of the scheme. Meaning — what a layer letter
    *is* — still comes from the registry module, never from this one.
    """
    key = identifiers.parse_store_key(path.name)
    version_label = None
    if key.version:
        try:
            version_label = str(identifiers.decode_version(key.version))
        except ValueError:  # a six-digit slot that is not a version code
            version_label = None
    return schemas.StoreDocOut(
        object_key=path.name,
        kind=registry.store_doc_kind(key),
        size_bytes=path.stat().st_size,
        root=key.root,
        root_kind=key.root_kind,
        version=key.version,
        version_label=version_label,
        layer=key.layer,
        layer_label=registry.layer_label(key.layer),
        slug=key.slug,
        status=key.status,
        # The two places the scheme lets one object stand for many files: the live
        # fabrication package (d18) and a withdrawn artifact set (d17). Everything
        # else in the store is one object per file.
        packaged=bool(key.status) or (key.layer == "D" and key.slug == "fab"),
    )


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
            (_store_doc(p) for p in Path(settings.store_dir).iterdir() if _is_listable(p)),
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


# Which object keys can be rendered as a PDF. Markdown only: the renderer turns
# markdown into the HTML subset fpdf2 draws, and handing it a CSV or a zip would
# produce a page of mojibake rather than an error a caller can act on.
def _is_markdown(object_key: str) -> bool:
    return object_key.lower().endswith((".md", ".markdown"))


async def _markdown_pdf(warehouse: Warehouse, object_key: str) -> Response:
    """Fetch one markdown object and render it. Holds no copy of either."""
    if not _is_markdown(object_key):
        raise HTTPException(
            status.HTTP_422_UNPROCESSABLE_ENTITY,
            f"{object_key} is not a markdown document; only .md renders to PDF",
        )
    body = await warehouse.get_bytes(object_key)
    if body is None:
        raise HTTPException(
            status.HTTP_502_BAD_GATEWAY, f"{object_key} could not be read from the warehouse"
        )
    blob = await asyncio.to_thread(
        reports.markdown_document,
        object_key=object_key,
        text=body.decode("utf-8", "replace"),
    )
    stem = object_key.rsplit(".", 1)[0]
    return Response(
        content=blob,
        media_type="application/pdf",
        headers={"Content-Disposition": f'attachment; filename="{stem}.pdf"'},
    )


@router.get("/store-documents/{object_key}/pdf", tags=["documents"])
async def store_document_pdf(
    object_key: str,
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """A repository document as a PDF.

    Guarded exactly as the read-through route is: the key must name a file in the
    repository's `store/`, resolved against it rather than joined onto it, so a
    key holding `..` cannot walk out of the mirror.
    """
    if not await asyncio.to_thread(_is_store_file, object_key):
        raise HTTPException(
            status.HTTP_404_NOT_FOUND, f"{object_key} is not a document in the repository's store/"
        )
    return await _markdown_pdf(warehouse, object_key)


@router.get("/instances/{instance_id}/documents/{object_key}/pdf", tags=["documents"])
async def instance_document_pdf(
    instance_id: str,
    object_key: str,
    db: AsyncIOMotorDatabase = Depends(get_db),
    warehouse: Warehouse = Depends(get_warehouse),
    _role: str = Depends(require_read),
):
    """One indexed document as a PDF. Same index guard as the read-through."""
    doc = await db[FOUNDATION["lifecycle_doc"]].find_one(
        {"instance_full_id": instance_id, "object_key": object_key}
    )
    if doc is None:
        raise HTTPException(
            status.HTTP_404_NOT_FOUND, f"{instance_id} has no indexed document {object_key}"
        )
    return await _markdown_pdf(warehouse, object_key)


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
