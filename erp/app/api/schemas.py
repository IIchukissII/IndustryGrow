# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""API request/response models (ADR-0022). Identifier fields are validated
against the ADR-0017 grammar so the API speaks the keys precisely."""

from __future__ import annotations

import re
from datetime import datetime

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


class MachineProvisionRequest(BaseModel):
    """A machine's provisioning binding — public material only (ADR-0022 rev 1 d12).

    Not ProvisionRequest with a different key: a machine binding carries the two
    facts that identify the *unit* (the SP0004 vendor serial and the ATECC die
    serial) alongside the certificate metadata, and it carries no ``pr_object_key``
    — whether a machine gets a lifecycle document blob at all is deferred (d12).

    Upserted, latest-wins: gateway certificates are short-lived and auto-renewed
    (ADR-0007 d7), so re-certification writes this again. What a certificate *used
    to be* is not a question this API answers.
    """

    vendor_serial: str
    atecc_serial: str | None = None
    public_key_fingerprint: str
    cert_serial: str
    cert_not_before: datetime
    cert_not_after: datetime


class MachineRequest(BaseModel):
    """Enrolling a cabinet. The identifier is the path, not the body — machines
    are enumerated and named by the operator (ADR-0017 d6), so there is nothing
    for the server to issue."""

    notes: str | None = None


class MachineOut(BaseModel):
    machine_id: str
    notes: str | None = None


class InstallRequest(BaseModel):
    instance_id: str

    @field_validator("instance_id")
    @classmethod
    def _ok_instance(cls, v: str) -> str:
        if not identifiers.INSTANCE_RE.match(v):
            raise ValueError("instance_id must be Exxxx-VVVVVV-NNNNNN")
        return v


class ProfileVersionRequest(BaseModel):
    """A whole profile version — setpoints + model as one artifact (ADR-0022 d8).

    ``document_b64`` is the artifact's **exact bytes**, base64-encoded so they
    survive the JSON hop unaltered. ADR-0025 d6 requires those bytes to travel
    verbatim from signing to verification, so the ERP takes them opaquely and
    never parses and re-serialises them — a store that rebuilt the document on
    read would break every signature it holds.

    ``signature`` is optional here and not optional to *activate*: a version may
    be parked unsigned, but ADR-0025 d11 forbids recording one active, because a
    gateway could not apply it (ADR-0015 d7).
    """

    version_tag: str
    document_b64: str
    signature: str | None = None
    source_template_ref: str | None = None


class ActiveProfileRequest(BaseModel):
    version_tag: str


class FirmwareIntentRequest(BaseModel):
    """The release an operator intends for a machine (ADR-0022 d14).

    A release is named by its object-key root, never by a slot image: which slot
    a given node writes is the node's to choose (ADR-0029 d3), and an operator
    selecting one would be choosing something they cannot know.
    """

    release_root: str

    @field_validator("release_root")
    @classmethod
    def _ok_release(cls, v: str) -> str:
        if not re.fullmatch(rf"{identifiers.E_MODULE}-{identifiers.VERSION}-F", v):
            raise ValueError("release_root must be Exxxx-VVVVVV-F (ADR-0029 d13)")
        return v


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
    # `020100` is not a number, it is `major.minor.patch` at two digits each
    # (ADR-0017 d1). Decoded here for the same reason the store keys are: the API
    # speaks the grammar (ADR-0022 d13) and a caller should never have to know
    # where the field boundaries fall.
    version_label: str | None = None


class IntegrationOut(BaseModel):
    """An instance at a position — the one place the two axes meet.

    The identifier `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` is a *mutable
    cross-reference* (ADR-0017 conceptual model), so both sides arrive read into
    their own fields: nothing here should have to be recovered by splitting a
    string, least of all the boundary between where a thing sits and what it is.
    """

    machine_id: str
    depth_code: str
    instance_id: str
    installed_at: datetime | None = None
    removed_at: datetime | None = None
    removal_reason: str | None = None

    # Position side. `DDDDDD` is main / sub-L1 / sub-L2 at two digits each — a
    # three-level position in the machine, not a six-digit number (d1, d7).
    depth_levels: list[int] | None = None
    depth_label: str | None = None

    # Identity side, from the instance the position holds.
    e_number: str | None = None
    version: str | None = None
    version_label: str | None = None
    serial: str | None = None


class LifecycleDocOut(BaseModel):
    instance_full_id: str
    doc_type: str
    object_key: str
    valid_until: datetime | None = None
    status: str


class StoreDocOut(BaseModel):
    """A type-layer document the repository owns and store_sync mirrors.

    Not an ERP entity: the ERP indexes none of these and owns none of them. The
    listing is the repository's `store/` directory, served read-only (ADR-0022 d1,
    2026-07-26 clarification — the same shape ADR-0023 gave `REGISTRY.md`).

    The key is returned **read into its fields** as well as whole. An identifier
    is the object key (ADR-0017 d15), so its structure is the only structure a
    type-layer document has, and the API is where the grammar is spoken
    (ADR-0022 d13) — a client that re-derived these fields from the string would
    be a second, drifting implementation of the scheme.
    """

    object_key: str
    kind: str
    size_bytes: int

    # ADR-0017 d1 / d17 / d18 and ADR-0019 d8 fields. `root` is null for an
    # object in store/ that carries no identifier at all — reported rather than
    # guessed at, because an unfiled object is worth seeing.
    root: str | None = None
    root_kind: str | None = None
    version: str | None = None
    version_label: str | None = None
    layer: str | None = None
    layer_label: str | None = None
    slug: str | None = None
    status: str | None = None
    packaged: bool = False


class DocumentUrlOut(BaseModel):
    """A time-limited retrieval URL for one indexed document (ADR-0022 d7).

    The ERP never returns blob content; d7 offers exactly two things — the object
    key, or this. ``expires_in`` is returned so a caller can tell an operator how
    long the grant lasts rather than handing over a link of unknown lifetime.
    """

    object_key: str
    url: str
    expires_in: int


class ProfileOut(BaseModel):
    machine_id: str
    version_tag: str
    created_at: datetime | None = None
    #: Whether this is the version recorded active on the gateway — a record of a
    #: pull that happened, never a deploy state the ERP drives (ADR-0022 d8).
    active: bool = False


class FirmwareIntentOut(BaseModel):
    """A machine's intended firmware release — and deliberately nothing more.

    There is no ``running_version``, no ``updated_at``, no per-node state, and
    there will not be. Whether the machine's nodes have reached this release is
    observed on the bus by the gateway (ADR-0029 d15) and is excluded from this
    API by ADR-0022 d9 — consumers show it as a gap (ADR-0022 alternative Q).

    ``artifact_keys`` are the objects a gateway may fetch for this release: the
    two slot images, header plus body, the only form a node can be given over the
    bus (ADR-0029 d13).
    """

    machine_id: str
    release_root: str
    selected_at: datetime | None = None
    selected_by: str | None = None
    artifact_keys: list[str] = []
    # The version code read out of the release root, as decision 13 requires of
    # every identifier this API returns.
    version: str | None = None
    version_label: str | None = None


class FirmwareReleaseOut(BaseModel):
    """A firmware release the repository publishes and `store_sync` mirrors.

    Not an ERP entity — the same read-through shape as the store-document listing
    (ADR-0022 d1). What makes a release selectable is that the repository carries
    both of its slot images, so this lists exactly what an operator may intend.
    """

    release_root: str
    version: str | None = None
    version_label: str | None = None
    artifact_keys: list[str] = []


class MachineIdentityOut(BaseModel):
    """A machine's recorded certificate — what is on record, not what is live.

    ``expires_in_days`` is computed from the recorded validity, so it answers
    "when does the certificate the ERP knows about expire", which is only the
    live one if re-certification wrote the binding again (ADR-0022 rev 1 d12).
    """

    machine_id: str
    vendor_serial: str
    atecc_serial: str | None = None
    public_key_fingerprint: str
    cert_serial: str
    cert_not_before: datetime
    cert_not_after: datetime
    expires_in_days: int
    provisioned_at: datetime | None = None


class GatewayChannelOut(BaseModel):
    """Everything the ERP owns about one machine's gateway channel (card 15).

    Deliberately incomplete, and the field names say where: there is no
    ``last_pulled_at`` and there will not be one. Whether the gateway has actually
    collected its profile is operational, lives in the gateway's own journal, and
    is excluded from this API by ADR-0022 d9 (see d8, rev 1).
    """

    machine_id: str
    identity: MachineIdentityOut | None = None
    active_version: str | None = None
    active_since: datetime | None = None
    stored_versions: int = 0
    #: Versions stored but not activatable because they carry no signature
    #: (ADR-0025 d11). An operator-fixable state, so it is worth surfacing.
    unsigned_versions: int = 0


class ModuleOut(BaseModel):
    e_number: str
    designation: str
    discipline: str
    notes: str


class PartOut(BaseModel):
    sp_number: str
    role: str
    instance_tracked: bool
    notes: str


class CatalogOut(BaseModel):
    """The type registry as the ERP reads it — meaning lives in REGISTRY.md
    (ADR-0021 d11), this is only the parsed view of it."""

    modules: list[ModuleOut]
    parts: list[PartOut]


class Ack(BaseModel):
    ok: bool = True
    detail: str | None = None
