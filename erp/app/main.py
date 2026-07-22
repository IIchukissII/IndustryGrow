# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FastAPI application entry point (ADR-0021)."""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import FastAPI

from app.config import settings
from app.db import Database, ensure_indexes
from app.services.warehouse import Warehouse


@asynccontextmanager
async def lifespan(app: FastAPI):
    db = Database(settings.mongo_uri, settings.mongo_db, mock=settings.mongo_mock)
    if not settings.mongo_mock:
        await ensure_indexes(db)
    if settings.seed_on_start:
        from app.seed import seed

        await seed(db.db)
    app.state.db = db
    # One warehouse client for the process: boto3 clients are thread-safe and
    # hold the connection pool, so per-request construction would discard it.
    app.state.warehouse = Warehouse()
    try:
        yield
    finally:
        db.close()


def create_app() -> FastAPI:
    app = FastAPI(
        title="IndustryGrow — Instance & Integration ERP",
        version="0.1.0",
        lifespan=lifespan,
    )

    from app import console
    from app.api.routes import router as api_router

    app.include_router(api_router)  # JSON API (ADR-0022) at /api/v1
    app.include_router(console.router)  # /healthz + the warehouse redirect
    console.mount(app)  # the built SPA at / — last, it claims the root
    return app


app = create_app()


def run() -> None:
    import uvicorn

    uvicorn.run("app.main:app", host=settings.host, port=settings.port, reload=settings.reload)
