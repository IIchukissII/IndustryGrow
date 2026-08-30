# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""[D] domain entities (ADR-0021 decisions 8, 12-13).

These stay the IndustryGrow layer at stage 11. They reference
``foundation.machine`` by id and hold nothing IndustryFlow needs to ingest.
"""

from __future__ import annotations

from datetime import UTC, datetime

from pydantic import BaseModel, ConfigDict, Field


def _utcnow() -> datetime:
    return datetime.now(UTC)


class _Base(BaseModel):
    model_config = ConfigDict(populate_by_name=True)


class GBox(_Base):
    """Grow-cabinet-specific attributes, 1:1 with foundation.machine."""

    machine_id: str = Field(alias="_id")  # -> foundation.machine._id
    slot_count: int | None = None
    cultivar: str | None = None
    location_label: str | None = None
    stagger_cadence_days: int | None = None


class ProfileVersion(_Base):
    """A deployment-specific profile version — one whole artifact (ADR-0021 d13).

    ``document_b64`` holds setpoints + state-space matrices + Kalman gains +
    identification metadata together; model parameters are never split into a
    parallel subsystem (ADR-0016 alt D). Storing a version here does NOT deploy
    it — the gateway's single mutation channel does (decision 12).

    It is held as opaque base64 rather than a parsed document because the
    signature covers the artifact's exact bytes (ADR-0025 d6). That costs the
    ERP the ability to query profile contents server-side, which ADR-0025
    records as a deliberate consequence.
    """

    machine_id: str
    version_tag: str  # monotonic per cabinet, e.g. "v8"
    document_b64: str  # the whole profile, verbatim, never split and never re-serialised
    signature: str | None = None  # base64 ECDSA-P256/SHA-256 over the decoded bytes
    source_template_ref: str | None = None  # community template key (reference only)
    created_at: datetime = Field(default_factory=_utcnow)
    created_by: str | None = None


class FirmwareIntent(_Base):
    """The firmware release an operator intends for a machine (ADR-0021 d18).

    ``release_root`` is an object-key root, ``Exxxx-VVVVVV-F`` (ADR-0029 d13), so
    the record names the artifact set without copying any of it — the images stay
    in the warehouse like every other blob (ADR-0021 d7).

    There is no field for what the machine's nodes are *running*, and adding one
    would be the operational intake ADR-0022 d9 excludes: that observation is the
    gateway's, taken from the bus (ADR-0029 d15). One release per machine, because
    one image serves every node type (ADR-0017 d16).
    """

    machine_id: str = Field(alias="_id")  # -> foundation.machine._id
    release_root: str
    selected_at: datetime = Field(default_factory=_utcnow)
    selected_by: str | None = None


class GBoxProfileDeployment(_Base):
    """Which profile version is / was active on which GBOX (ADR-0021 d8).

    The record of "which version is active where" — not the deploy path. A
    ``deactivated_at`` of ``None`` marks the active record; the partial-unique
    index enforces one active deployment per machine.
    """

    machine_id: str
    version_tag: str
    activated_at: datetime = Field(default_factory=_utcnow)
    deactivated_at: datetime | None = None
    activated_by: str | None = None
