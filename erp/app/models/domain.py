# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""[D] domain entities (ADR-0021 decisions 8, 12-13).

These stay the IndustryGrow layer at stage 11. They reference
``foundation.machine`` by id and hold nothing IndustryFlow needs to ingest.
"""

from __future__ import annotations

from datetime import UTC, datetime
from typing import Any

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

    ``payload`` holds setpoints + state-space matrices + Kalman gains +
    identification metadata together; model parameters are never split into a
    parallel subsystem (ADR-0016 alt D). Storing a version here does NOT deploy
    it — the gateway's single mutation channel does (decision 12).
    """

    machine_id: str
    version_tag: str  # monotonic per cabinet, e.g. "v8"
    payload: dict[str, Any]  # the whole profile, never split
    signed_hash: str | None = None
    source_template_ref: str | None = None  # community template key (reference only)
    created_at: datetime = Field(default_factory=_utcnow)
    created_by: str | None = None


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
