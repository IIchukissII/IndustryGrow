# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The warehouse — the S3-compatible object store for blobs (ADR-0017 d15).

The ERP is the queryable index *over* this store; it records object keys, never
blob bytes (ADR-0021 d7, d11). Identifiers are the object keys — the flat
keyspace of ADR-0017 d15, so listing is a prefix scan with no separate index.

Referential-integrity rule (ADR-0021 deferred "ERP <-> object store"): write the
blob to the warehouse *first*, then record its key in Mongo. A recorded key
therefore always resolves; a crash between the two leaves an orphan blob (a
prefix-scan cleanup finds it), never a dangling index row.
"""

from __future__ import annotations

import asyncio

import boto3
from botocore.config import Config

from app.config import settings


class Warehouse:
    def __init__(self) -> None:
        self._bucket = settings.warehouse_bucket
        self._client = boto3.client(
            "s3",
            endpoint_url=settings.warehouse_endpoint,
            aws_access_key_id=settings.warehouse_access_key,
            aws_secret_access_key=settings.warehouse_secret_key,
            region_name=settings.warehouse_region,
            config=Config(signature_version="s3v4"),
        )

    async def put(
        self, key: str, body: bytes, content_type: str = "application/octet-stream"
    ) -> str:
        """Store a blob under its identifier key. Returns the key."""
        await asyncio.to_thread(
            self._client.put_object,
            Bucket=self._bucket,
            Key=key,
            Body=body,
            ContentType=content_type,
        )
        return key

    async def put_file(self, key: str, path: str, content_type: str | None = None) -> str:
        """Upload a file from disk under ``key`` (streams; used by the store sync)."""
        extra = {"ContentType": content_type} if content_type else {}
        await asyncio.to_thread(
            self._client.upload_file, str(path), self._bucket, key, ExtraArgs=extra
        )
        return key

    async def ensure_bucket(self) -> None:
        """Create the bucket if it does not exist (first-run convenience)."""

        def _ensure() -> None:
            try:
                self._client.head_bucket(Bucket=self._bucket)
            except self._client.exceptions.ClientError:
                self._client.create_bucket(Bucket=self._bucket)

        await asyncio.to_thread(_ensure)

    async def presigned_get(self, key: str, expires: int = 3600) -> str:
        """A time-limited read URL for an object key."""
        return await asyncio.to_thread(
            self._client.generate_presigned_url,
            "get_object",
            Params={"Bucket": self._bucket, "Key": key},
            ExpiresIn=expires,
        )

    async def exists(self, key: str) -> bool:
        try:
            await asyncio.to_thread(self._client.head_object, Bucket=self._bucket, Key=key)
        except self._client.exceptions.ClientError:
            return False
        return True

    async def list_prefix(self, prefix: str) -> list[str]:
        """Prefix scan — the ADR-0017 d15 filtering primitive."""
        resp = await asyncio.to_thread(
            self._client.list_objects_v2, Bucket=self._bucket, Prefix=prefix
        )
        return [obj["Key"] for obj in resp.get("Contents", [])]
