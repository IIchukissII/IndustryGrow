# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Application settings (ADR-0021).

Single-tenant in operation: one operator UUID, applied as a constant to every
[F] document so the stage-11 migration is a "tenant N" insertion, not a remodel
(ADR-0021 decision 16). No tenancy machinery — that is IndustryFlow's concern.
"""

from __future__ import annotations

import json
from functools import lru_cache
from typing import Annotated, Any

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, NoDecode, SettingsConfigDict

# The development operator token. Public in this repository; admitted only under
# ERP_DEV_TOKEN=1 with no ERP_API_TOKENS configured.
DEV_TOKEN = "dev-operator-token"


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="ERP_", env_file=".env", extra="ignore")

    # Operator identity — the single tenant (ADR-0021 d16).
    operator_uuid: str = Field(default="00000000-0000-0000-0000-000000000001")
    operator_name: str = Field(default="OP-STRAWBERRY-01")

    # MongoDB — the document metadata store (ADR-0021 d15.
    mongo_uri: str = Field(default="mongodb://localhost:27017")
    mongo_db: str = Field(default="industrygrow_erp")

    # Warehouse — S3-compatible object store for blobs (ADR-0017 d15).
    # The ERP records object keys only; the blobs never enter Mongo.
    warehouse_endpoint: str = Field(default="http://localhost:9000")
    warehouse_bucket: str = Field(default="industrygrow")
    warehouse_access_key: str = Field(default="minioadmin")
    warehouse_secret_key: str = Field(default="minioadmin")
    warehouse_region: str = Field(default="us-east-1")  # "auto" for Cloudflare R2

    # Lifetime of the retrieval URLs ADR-0022 d7 permits the API to hand back.
    # Short because the grant is for reading one document now, not for holding:
    # the link leaves the ERP's control the moment it is issued, so its lifetime
    # is the only bound left on it.
    document_url_ttl: int = Field(default=900)

    # Repo store/ — the public, type-level flat keyspace synced into the warehouse
    # (ADR-0017 d15: identifiers are object keys). See `python -m app.store_sync`.
    store_dir: str = Field(default="../store")

    # Repo spec/ — the second directory of type-level documents. A specification
    # carries an object key like everything else (ADR-0017 d20, `Exxxx-R-...`) and
    # is mirrored and served identically; it lives in its own directory because it
    # is edited continuously while store/ holds released artifacts. The keyspace is
    # flat and shared, so a key names one document wherever its file sits.
    spec_dir: str = Field(default="../spec")

    # The mark printed on PDF reports (app/services/reports.py). The SVG is
    # inlined into the page and stays vector; a PNG is accepted and embedded.
    # Relative paths resolve against erp/; the container sets an absolute one
    # because it copies img/ to a fixed place. A missing file prints the report
    # without the mark rather than failing it.
    report_logo: str = Field(default="../img/industrygrow-mark.svg")

    # REGISTRY.md — the authoritative type registry (ADR-0017 d3, ADR-0019). The
    # ERP reads it for labels and never copies it (ADR-0021 d11); see
    # app/services/registry.py. Relative paths resolve against the erp/ directory.
    registry_path: str = Field(default="../REGISTRY.md")

    # API — scoped operator tokens (ADR-0022 d3): token -> role, one of
    # {operator, provisioning, readonly}. Interim, shaped toward stage-11 JWT.
    # Set via ERP_API_TOKENS='{"<token>":"operator"}'. Gateway callers use
    # mTLS (ADR-0022 d2), not tokens.
    #
    # Empty by default, and that default is fail-closed: with no tokens the token
    # routes answer 503, as the gateway routes do with no trusted proxy. NoDecode
    # and _parse_api_tokens make an empty ERP_API_TOKENS mean "none" instead of
    # the error pydantic-settings raises for an empty dict field.
    api_tokens: Annotated[dict[str, str], NoDecode] = Field(default={})

    # Dev/test only — ERP_DEV_TOKEN=1 admits DEV_TOKEN as an operator token while
    # ERP_API_TOKENS is empty. Off by default; configured tokens disable it.
    dev_token: bool = False

    # Gateway mTLS channel (ADR-0022 d2). TLS terminates in a reverse proxy that
    # verifies the client chain against the operator root and forwards the result
    # plus the certificate subject DN; the app derives GBOX_NNNN from that DN.
    # These are the *proxy's* addresses as seen on the wire (IPs or CIDRs) —
    # nothing forwarded from anywhere else is believed.
    #
    # Empty by default, and that default is fail-closed on purpose: with no proxy
    # identified, a forwarded header is indistinguishable from a forged one, so
    # the gateway routes answer 503 rather than trust it. Set it when the proxy
    # is actually in front, e.g. ERP_GATEWAY_TRUSTED_PROXIES='["172.20.0.0/16"]'.
    gateway_trusted_proxies: list[str] = Field(default=[])

    # Dev/test only — run with no external MongoDB. The database starts empty and
    # stays that way: the ERP is a system of record, so every row in it is one an
    # operator or a provisioning station put there (ADR-0021 d1). Nothing invents
    # an estate at startup.
    mongo_mock: bool = False

    # Operator console — the built SPA (`erp/frontend/dist`), served at / by this
    # app. Relative paths resolve against the erp/ directory; the container sets
    # an absolute one because the bundle is built in a separate stage.
    console_dir: str = Field(default="frontend/dist")

    # Web.
    host: str = "0.0.0.0"
    port: int = 8021
    reload: bool = False

    @field_validator("api_tokens", mode="before")
    @classmethod
    def _parse_api_tokens(cls, value: Any) -> Any:
        if isinstance(value, str):
            return json.loads(value) if value.strip() else {}
        return value

    def token_roles(self) -> dict[str, str]:
        """The token -> role map in force; empty means the token routes refuse."""
        if self.api_tokens:
            return self.api_tokens
        if self.dev_token:
            return {DEV_TOKEN: "operator"}
        return {}


@lru_cache
def get_settings() -> Settings:
    return Settings()


settings = get_settings()
