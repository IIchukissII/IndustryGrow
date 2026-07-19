# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""[F] foundational entities (ADR-0021 decisions 4-9).

These migrate into IndustryFlow's ``production_unit`` core at stage 11, so every
one carries a constant ``tenant_id`` (decision 16) even though the running
instance is single-tenant. Keys follow the ADR-0017 identifier grammar — the
stable anchor the stage-11 migration is keyed on.
"""

from __future__ import annotations

from datetime import UTC, datetime
from enum import StrEnum

from pydantic import BaseModel, ConfigDict, Field

from app.config import settings


def _utcnow() -> datetime:
    return datetime.now(UTC)


def _tenant() -> str:
    return settings.operator_uuid


class InstanceStatus(StrEnum):
    IN_INVENTORY = "in-inventory"
    INSTALLED = "installed"
    RETIRED = "retired"


class RemovalReason(StrEnum):
    REPLACED = "replaced"
    REMOVED = "removed"
    RETIRED = "retired"


class DocType(StrEnum):
    QP = "QP"  # quality protocol
    QR = "QR"  # quality report
    CP = "CP"  # calibration protocol
    CC = "CC"  # calibration certificate
    PR = "PR"  # provisioning record


class DocStatus(StrEnum):
    VALID = "valid"
    EXPIRED = "expired"
    SUPERSEDED = "superseded"


class _Base(BaseModel):
    model_config = ConfigDict(populate_by_name=True, use_enum_values=True)


class Machine(_Base):
    """A deployed cabinet ``GBOX_NNNN`` (ADR-0017 d6)."""

    machine_id: str = Field(alias="_id")  # e.g. "GBOX_0001"
    tenant_id: str = Field(default_factory=_tenant)
    created_at: datetime = Field(default_factory=_utcnow)
    retired_at: datetime | None = None
    notes: str | None = None


class SerialCounter(_Base):
    """Gap-free serial sequence, one per module + version (ADR-0021 d4)."""

    counter_id: str = Field(alias="_id")  # "E0002-020100"
    last_serial: int = 0


class ModuleInstance(_Base):
    """A manufactured E-instance ``Exxxx-VVVVVV-NNNNNN`` (ADR-0021 d4)."""

    instance_id: str = Field(alias="_id")  # full identity key
    tenant_id: str = Field(default_factory=_tenant)
    e_number: str
    version: str
    serial: str
    status: InstanceStatus = InstanceStatus.IN_INVENTORY
    produced_at: datetime = Field(default_factory=_utcnow)


class InstanceIdentity(_Base):
    """The serial<->ATECC608 binding — the structured -PR content (ADR-0021 d5).

    Public material only; the private key never leaves the ATECC608.
    """

    instance_id: str = Field(alias="_id")  # 1:1 with ModuleInstance
    tenant_id: str = Field(default_factory=_tenant)
    cert_serial: str
    public_key_fingerprint: str  # ECDSA P-256 (ADR-0007)
    cert_not_before: datetime
    cert_not_after: datetime
    pr_object_key: str  # warehouse key of the -PR blob
    provisioned_at: datetime = Field(default_factory=_utcnow)


class IntegrationRecord(_Base):
    """One installation of an instance at a machine position, with history.

    A ``removed_at`` of ``None`` marks the current placement; the partial-unique
    index on (machine_id, depth_code) where removed_at is null enforces exactly
    one current instance per position (ADR-0017 d13; ADR-0021 d6).
    """

    tenant_id: str = Field(default_factory=_tenant)
    machine_id: str  # "GBOX_0001"
    depth_code: str  # "DDDDDD"
    instance_id: str  # "Exxxx-VVVVVV-NNNNNN"
    installed_at: datetime = Field(default_factory=_utcnow)
    removed_at: datetime | None = None
    removal_reason: RemovalReason | None = None


class LifecycleDoc(_Base):
    """Index over a per-instance lifecycle document — metadata + warehouse key.

    The blob itself stays in the object store (ADR-0021 d7); this never holds it.
    """

    tenant_id: str = Field(default_factory=_tenant)
    instance_full_id: str  # "Exxxx-VVVVVV-NNNNNN"
    doc_type: DocType
    doc_date: datetime | None = None
    object_key: str  # warehouse key, e.g. "E0004-...-CC-20260529"
    valid_until: datetime | None = None  # for -CC calibration validity
    status: DocStatus = DocStatus.VALID


class SPStock(_Base):
    """Purchased-part stock level (ADR-0021 d9). Spec -> REGISTRY.md; price -> BOM."""

    tenant_id: str = Field(default_factory=_tenant)
    sp_number: str  # "SP0003"
    quantity: int = Field(ge=0, default=0)
    location: str | None = None


class SPPlacement(_Base):
    """Where a purchased part sits (ADR-0021 d9)."""

    tenant_id: str = Field(default_factory=_tenant)
    machine_id: str
    sp_number: str
    vendor_serial: str | None = None  # e.g. SP0004 gateway identity (ADR-0019 d2)
    installed_at: datetime = Field(default_factory=_utcnow)
    removed_at: datetime | None = None
