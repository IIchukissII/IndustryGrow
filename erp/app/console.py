# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The operator console — the built SPA, served by this app (ADR-0021 d15).

One container: the JSON API (ADR-0022) and the console it drives ship together,
same origin, so the browser needs no CORS and the Vite dev proxy is a
development convenience rather than a deployment dependency.

The console is a *build artifact* (`frontend/dist`, produced by `npm run build`),
not source in this package. When it is absent — a source checkout that has never
been built — `/` explains how to produce it instead of 404ing.

Two non-SPA routes live here because they belong to neither the API nor the
bundle: the liveness probe, and the warehouse redirect. The latter hands out a
short-lived presigned URL for an object key: the ERP indexes the object store
and holds keys only (ADR-0021 d7), so the bytes are served by the warehouse and
never proxied through this process.
"""

from __future__ import annotations

from pathlib import Path

from fastapi import APIRouter, Depends, FastAPI
from fastapi.responses import HTMLResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles

from app.api.deps import get_warehouse
from app.config import settings
from app.services.warehouse import Warehouse

router = APIRouter()

_NOT_BUILT = """<!doctype html>
<title>IndustryGrow ERP — console not built</title>
<body style="font:16px/1.6 system-ui;max-width:44rem;margin:12vh auto;padding:0 2rem;
             background:#0b0f14;color:#c8d3e0">
<h1 style="font-weight:600">Console not built</h1>
<p>The API is running at <a style="color:#67e8f9" href="/docs">/docs</a>, but the
operator console bundle is missing. Build it:</p>
<pre style="background:#121821;padding:1rem;border-radius:.5rem">cd erp/frontend
npm install
npm run build</pre>
<p>…or run the Vite dev server (<code>npm run dev</code>) on :5173, which proxies
<code>/api</code> here.</p>
</body>
"""


def console_dir() -> Path:
    """Where the built bundle lives — configurable so the container can place it
    outside the source tree (`ERP_CONSOLE_DIR`)."""
    path = Path(settings.console_dir)
    return path if path.is_absolute() else (Path(__file__).parent.parent / path).resolve()


@router.get("/healthz")
async def healthz() -> dict[str, str]:
    return {"status": "ok"}


@router.get("/warehouse/{key:path}")
async def warehouse_object(key: str, warehouse: Warehouse = Depends(get_warehouse)):
    """Redirect to a short-lived presigned URL for a warehouse object key."""
    url = await warehouse.presigned_get(key, expires=300)
    return RedirectResponse(url, status_code=307)


def mount(app: FastAPI) -> None:
    """Serve the built console: the index at `/`, its bundle under `/assets`.

    Deliberately *not* a `StaticFiles` mount at `/`. A root mount is a catch-all:
    it answers every path no route claimed, so an unknown API path stops being a
    404 and becomes whatever the static handler says (405 for a POST, the SPA's
    own HTML for a GET). The console has no client-side router — navigation is
    in-memory — so it needs no deep-link fallback and nothing is lost by being
    exact about the two things it actually serves.
    """
    dist = console_dir()
    index = dist / "index.html"

    if not index.exists():

        @app.get("/", response_class=HTMLResponse, include_in_schema=False)
        async def _console_missing() -> str:
            return _NOT_BUILT

        return

    assets = dist / "assets"
    if assets.is_dir():
        app.mount("/assets", StaticFiles(directory=assets), name="console-assets")

    @app.get("/", response_class=HTMLResponse, include_in_schema=False)
    async def _console_index() -> HTMLResponse:
        return HTMLResponse(index.read_text(encoding="utf-8"))
