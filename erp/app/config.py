# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Application settings (ADR-0021).

Single-tenant in operation: one operator UUID, applied as a constant to every
[F] document so the stage-11 migration is a "tenant N" insertion, not a remodel
(ADR-0021 decision 16). No tenancy machinery — that is IndustryFlow's concern.
"""

from __future__ import annotations

from functools import lru_cache

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="ERP_", env_file=".env", extra="ignore")

    # Operator identity — the single tenant (ADR-0021 d16).
    operator_uuid: str = Field(default="00000000-0000-0000-0000-000000000001")
    operator_name: str = Field(default="OP-STRAWBERRY-01")

    # MongoDB — the document metadata store (ADR-0021 d15, rev 1).
    mongo_uri: str = Field(default="mongodb://localhost:27017")
    mongo_db: str = Field(default="industrygrow_erp")

    # Warehouse — S3-compatible object store for blobs (ADR-0017 d15).
    # The ERP records object keys only; the blobs never enter Mongo.
    warehouse_endpoint: str = Field(default="http://localhost:9000")
    warehouse_bucket: str = Field(default="industrygrow")
    warehouse_access_key: str = Field(default="minioadmin")
    warehouse_secret_key: str = Field(default="minioadmin")
    warehouse_region: str = Field(default="us-east-1")

    # Web.
    host: str = "0.0.0.0"
    port: int = 8021
    reload: bool = False


@lru_cache
def get_settings() -> Settings:
    return Settings()


settings = get_settings()
