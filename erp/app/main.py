# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FastAPI application entry point (ADR-0021)."""

from __future__ import annotations

from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles

from app.config import settings
from app.db import Database, ensure_indexes


@asynccontextmanager
async def lifespan(app: FastAPI):
    db = Database(settings.mongo_uri, settings.mongo_db, mock=settings.mongo_mock)
    if not settings.mongo_mock:
        await ensure_indexes(db)
    if settings.seed_on_start:
        from app.seed import seed

        await seed(db.db)
    app.state.db = db
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

    static_dir = Path(__file__).parent / "web" / "static"
    if static_dir.exists():
        app.mount("/static", StaticFiles(directory=static_dir), name="static")

    from app.api.routes import router as api_router
    from app.web.routes import router as web_router

    app.include_router(api_router)  # JSON API (ADR-0022) at /api/v1
    app.include_router(web_router)  # server-rendered console (legacy)
    return app


app = create_app()


def run() -> None:
    import uvicorn

    uvicorn.run("app.main:app", host=settings.host, port=settings.port, reload=settings.reload)
