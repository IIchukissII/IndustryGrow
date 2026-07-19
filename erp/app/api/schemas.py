# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API request/response models (ADR-0022). Identifier fields are validated
against the ADR-0017 grammar so the API speaks the keys precisely."""

from __future__ import annotations

import re
from datetime import datetime
from typing import Any

from pydantic import BaseModel, Field, field_validator

from app.models import identifiers

# ---- requests -------------------------------------------------------------


class AllocateRequest(BaseModel):
    e_number: str
    version: str
    quantity: int = Field(default=1, ge=1, le=1000)

    @field_validator("e_number")
    @classmethod
    def _ok_module(cls, v: str) -> str:
        if not re.fullmatch(identifiers.E_MODULE, v):
            raise ValueError("e_number must be Exxxx (E + 4 digits)")
        return v

    @field_validator("version")
    @classmethod
    def _ok_version(cls, v: str) -> str:
        if not re.fullmatch(identifiers.VERSION, v):
            raise ValueError("version must be 6 digits (VVVVVV)")
        return v


class ProvisionRequest(BaseModel):
    """The structured -PR content — public certificate material only (ADR-0022 d5)."""

    cert_serial: str
    public_key_fingerprint: str
    cert_not_before: datetime
    cert_not_after: datetime
    pr_object_key: str


class InstallRequest(BaseModel):
    instance_id: str

    @field_validator("instance_id")
    @classmethod
    def _ok_instance(cls, v: str) -> str:
        if not identifiers.INSTANCE_RE.match(v):
            raise ValueError("instance_id must be Exxxx-VVVVVV-NNNNNN")
        return v


class ProfileVersionRequest(BaseModel):
    """A whole profile version — setpoints + model as one artifact (ADR-0022 d8)."""

    version_tag: str
    payload: dict[str, Any]
    signed_hash: str | None = None
    source_template_ref: str | None = None


class ActiveProfileRequest(BaseModel):
    version_tag: str


class SPStockRequest(BaseModel):
    sp_number: str
    quantity: int = Field(ge=0)
    location: str | None = None

    @field_validator("sp_number")
    @classmethod
    def _ok_sp(cls, v: str) -> str:
        if not re.fullmatch(r"SP\d{4}", v):
            raise ValueError("sp_number must be SPxxxx")
        return v


# ---- responses ------------------------------------------------------------


class AllocateResponse(BaseModel):
    serials: list[str]
    next_serial: int


class InstanceOut(BaseModel):
    instance_id: str
    e_number: str
    version: str
    serial: str
    status: str


class IntegrationOut(BaseModel):
    machine_id: str
    depth_code: str
    instance_id: str
    installed_at: datetime | None = None
    removed_at: datetime | None = None
    removal_reason: str | None = None


class LifecycleDocOut(BaseModel):
    instance_full_id: str
    doc_type: str
    object_key: str
    valid_until: datetime | None = None
    status: str


class ProfileOut(BaseModel):
    machine_id: str
    version_tag: str
    created_at: datetime | None = None


class Ack(BaseModel):
    ok: bool = True
    detail: str | None = None
