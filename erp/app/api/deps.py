# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API dependencies — the two-caller-class auth of ADR-0022.

- Human / provisioning tooling: a **scoped operator token** (decision 3),
  ``token -> role`` from config. Interim, shaped toward a stage-11 JWT claim.
- Gateway machines: **mTLS** (decision 2). The TLS-terminating proxy validates
  the ATECC-anchored client certificate against the operator root CA and forwards
  the verified machine identity in a header; the app trusts it only because it
  sits behind that proxy. Identity comes from the cert, never a request body.
"""

from __future__ import annotations

from collections.abc import Callable, Coroutine
from typing import Any

from fastapi import Header, HTTPException, Request, status
from motor.motor_asyncio import AsyncIOMotorDatabase

from app.config import settings
from app.services.warehouse import Warehouse

# Write roles; readonly may only GET.
_WRITE_ROLES = frozenset({"operator", "provisioning"})


def get_db(request: Request) -> AsyncIOMotorDatabase:
    return request.app.state.db.db


def get_warehouse(request: Request) -> Warehouse:
    """The process-wide warehouse client (created in the lifespan).

    A dependency rather than a direct construction so a test can substitute a
    fake and assert the blob-first ordering without touching a real bucket.
    """
    return request.app.state.warehouse


def _token_role(authorization: str | None, x_operator_token: str | None) -> str | None:
    token = None
    if authorization and authorization.lower().startswith("bearer "):
        token = authorization[7:].strip()
    token = token or x_operator_token
    return settings.api_tokens.get(token) if token else None


def require_role(*allowed: str) -> Callable[..., Coroutine[Any, Any, str]]:
    """Dependency factory: require a token whose role is in ``allowed``."""

    async def dependency(
        authorization: str | None = Header(default=None),
        x_operator_token: str | None = Header(default=None),
    ) -> str:
        role = _token_role(authorization, x_operator_token)
        if role is None:
            raise HTTPException(status.HTTP_401_UNAUTHORIZED, "missing or invalid operator token")
        if allowed and role not in allowed:
            raise HTTPException(status.HTTP_403_FORBIDDEN, f"role '{role}' not permitted here")
        return role

    return dependency


# Any authenticated caller (read).
require_read = require_role("operator", "provisioning", "readonly")
# Write callers.
require_write = require_role(*_WRITE_ROLES)
# Provisioning-station-only (serial<->ATECC binding).
require_provisioning = require_role("provisioning", "operator")


async def gateway_identity(x_client_gbox: str | None = Header(default=None)) -> str:
    """Gateway machine identity from the mTLS-terminating proxy (ADR-0022 d2)."""
    if not x_client_gbox:
        raise HTTPException(status.HTTP_401_UNAUTHORIZED, "gateway mTLS identity required")
    return x_client_gbox
