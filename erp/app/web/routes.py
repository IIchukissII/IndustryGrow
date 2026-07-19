# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HTTP routes — the operator console (ADR-0021 ownership map).

Five areas: Overview (the estate + integration map), Instances (with serial
allocation), Calibration & Docs, SP Stock. Deliberately absent, by design:
no "deploy profile" action (ADR-0015 d4), no telemetry, no type-meaning editing,
no SKU/price (ADR-0021 d10-11).
"""

from __future__ import annotations

from datetime import UTC, datetime
from pathlib import Path

from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.templating import Jinja2Templates
from motor.motor_asyncio import AsyncIOMotorDatabase

from app.config import settings
from app.db import DOMAIN, FOUNDATION
from app.models.identifiers import decode_version, parse_instance
from app.services import docs, integration, profiles
from app.services import serials as serials_svc
from app.services.warehouse import Warehouse
from app.web.catalog import module_display

router = APIRouter()
templates = Jinja2Templates(directory=str(Path(__file__).parent / "templates"))


def _db(request: Request) -> AsyncIOMotorDatabase:
    return request.app.state.db.db


def _cal_status(valid_until: datetime | None) -> tuple[str, str, str]:
    if valid_until is None:
        return "ok", "valid", "—"
    vu = valid_until.replace(tzinfo=None) if valid_until.tzinfo else valid_until
    days = (vu - datetime.now(UTC).replace(tzinfo=None)).days
    when = vu.date().isoformat()
    if days < 0:
        return "crit", "expired", when
    if days <= 30:
        return "warn", f"{days} days", when
    return "ok", f"{days} days", when


async def _overview_context(request: Request) -> dict:
    db = _db(request)

    machine = await db[FOUNDATION["machine"]].find_one({}, sort=[("_id", 1)])
    machine_id = machine["_id"] if machine else None
    if machine:
        gbox = await db[DOMAIN["gbox"]].find_one({"_id": machine_id})
        if gbox:
            machine = {**machine, **gbox, "machine_id": machine_id}
        else:
            machine["machine_id"] = machine_id

    slots = []
    if machine_id:
        for rec in await integration.current_estate(db, machine_id):
            parts = parse_instance(rec["instance_id"])
            name, colour = module_display(parts["e_number"])
            slots.append(
                {
                    "machine_id": rec["machine_id"],
                    "depth_code": rec["depth_code"],
                    "instance_id": rec["instance_id"],
                    "e_number": parts["e_number"],
                    "version": parts["version"],
                    "serial": parts["serial"],
                    "module_name": name,
                    "module_color": colour,
                    "status_class": "ok",
                    "status_label": "growing",
                }
            )

    calibrations = []
    for c in await docs.expiring(db, within_days=30):
        cls, label, when = _cal_status(c.get("valid_until"))
        calibrations.append({**c, "status_class": cls, "status_label": label, "when": when})

    versions = await profiles.versions(db, machine_id) if machine_id else []
    active = await profiles.active_version(db, machine_id) if machine_id else None
    profile = {
        "template": (versions[0].get("source_template_ref") if versions else None),
        "erp_latest": (versions[0]["version_tag"] if versions else None),
        "erp_history": " · ".join(v["version_tag"] for v in versions[1:6]),
        "gw_active": (active["version_tag"] if active else None),
    }

    sp_rows = []
    async for p in db[FOUNDATION["sp_stock"]].find({}):
        low = p.get("quantity", 0) < 3
        sp_rows.append(
            {
                "sp_number": p["sp_number"],
                "spec": p.get("location") or "in stock",
                "quantity": p.get("quantity", 0),
                "qty_note": "on hand",
                "status_class": "warn" if low else "ok",
                "status_label": "low" if low else "in stock",
            }
        )

    counts = {
        "instances": await db[FOUNDATION["module_instance"]].count_documents({}),
        "installed": await db[FOUNDATION["integration_record"]].count_documents(
            {"removed_at": None}
        ),
        "expiring": len(calibrations),
        "profile_versions": len(versions),
    }

    return {
        "request": request,
        "operator": settings.operator_name,
        "active": "overview",
        "machine": machine,
        "slots": slots,
        "calibrations": calibrations,
        "profile": profile,
        "sp": sp_rows,
        "counts": counts,
    }


@router.get("/", response_class=HTMLResponse)
async def overview(request: Request):
    ctx = await _overview_context(request)
    return templates.TemplateResponse("overview.html", ctx)


@router.post("/instances/allocate", response_class=HTMLResponse)
async def allocate(request: Request):
    """Demo allocation: issue three serials of E0002 v2.1.0 (the serial authority)."""
    db = _db(request)
    e_number, version = "E0002", "020100"
    issued = await serials_svc.allocate(db, e_number, version, quantity=3)
    vv = decode_version(version)
    return templates.TemplateResponse(
        "_allocate_result.html",
        {
            "request": request,
            "serials": issued,
            "e_number": e_number,
            "version": f"{vv.major}.{vv.minor}.{vv.patch}",
        },
    )


async def _chrome(request: Request, active: str) -> dict:
    """Shared page chrome (operator badge + nav counts)."""
    db = _db(request)
    expiring = await docs.expiring(db, within_days=30)
    return {
        "request": request,
        "operator": settings.operator_name,
        "active": active,
        "counts": {
            "instances": await db[FOUNDATION["module_instance"]].count_documents({}),
            "expiring": len(expiring),
        },
    }


@router.get("/instances", response_class=HTMLResponse)
async def instances_page(request: Request):
    db = _db(request)
    ctx = await _chrome(request, "instances")
    rows = []
    async for m in db[FOUNDATION["module_instance"]].find({}).sort("_id", 1):
        name, colour = module_display(m["e_number"])
        installed = m.get("status") == "installed"
        rows.append(
            {
                "instance_id": m["_id"],
                "module_name": name,
                "module_color": colour,
                "status_class": "ok" if installed else "empty",
                "status_label": m.get("status", "in-inventory"),
            }
        )
    ctx["rows"] = rows
    return templates.TemplateResponse("instances.html", ctx)


@router.get("/calibration", response_class=HTMLResponse)
async def calibration_page(request: Request):
    db = _db(request)
    ctx = await _chrome(request, "calibration")
    rows = []
    async for c in db[FOUNDATION["lifecycle_doc"]].find({}).sort("valid_until", 1):
        cls, label, when = _cal_status(c.get("valid_until"))
        rows.append({**c, "status_class": cls, "status_label": label, "when": when})
    ctx["rows"] = rows
    return templates.TemplateResponse("calibration.html", ctx)


@router.get("/stock", response_class=HTMLResponse)
async def stock_page(request: Request):
    db = _db(request)
    ctx = await _chrome(request, "stock")
    rows = []
    async for p in db[FOUNDATION["sp_stock"]].find({}).sort("sp_number", 1):
        low = p.get("quantity", 0) < 3
        rows.append(
            {
                "sp_number": p["sp_number"],
                "spec": p.get("location") or "in stock",
                "quantity": p.get("quantity", 0),
                "status_class": "warn" if low else "ok",
                "status_label": "low" if low else "in stock",
            }
        )
    ctx["rows"] = rows
    return templates.TemplateResponse("stock.html", ctx)


@router.get("/warehouse/{key:path}")
async def warehouse_object(key: str):
    """Redirect to a short-lived presigned URL for a warehouse object key.

    The ERP indexes the object store and hands out keys (ADR-0021 d7); the bytes
    are served by the warehouse, never proxied through here.
    """
    url = await Warehouse().presigned_get(key, expires=300)
    return RedirectResponse(url, status_code=307)


@router.get("/healthz")
async def healthz() -> dict[str, str]:
    return {"status": "ok"}
