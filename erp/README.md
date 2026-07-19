<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: AGPL-3.0-or-later
-->

# IndustryGrow — Instance & Integration ERP

The **pre-cloud system of record** for the instance/integration layer
([ADR-0021](../ADR/ADR-0021-instance-and-integration-erp.md)). A compact,
self-hostable, single-container application: the queryable **metadata layer** over
the flat object-store **warehouse**.

- **meta** → **MongoDB** (this app's document store)
- **warehouse** → S3-compatible object store (the blobs; identifiers *are* keys, ADR-0017 d15)
- **app** → FastAPI + HTMX + Jinja2, one container

It operates **single-tenant** now and re-layers into IndustryFlow at stage 11:
the `foundation.*` `[F]` collections migrate into IndustryFlow's `production_unit`
core; the `domain.*` `[D]` collections stay the IndustryGrow layer.

## What it owns (and only this)

| Area | Collections |
|------|-------------|
| Machines & instances · **serial authority** | `foundation.machine`, `foundation.serial_counter`, `foundation.module_instance` |
| Identity binding (`-PR`, ATECC608) | `foundation.instance_identity` |
| Integration history (mutable cross-reference) | `foundation.integration_record` |
| Lifecycle-document index (+ warehouse keys) | `foundation.lifecycle_doc` |
| SP stock & placement | `foundation.sp_stock`, `foundation.sp_placement` |
| GBOX domain + deployment profiles | `domain.gbox`, `domain.profile_version`, `domain.gbox_profile_deployment` |

## What it must NOT hold (boundaries — enforce in review)

These are deliberate *absences*. Each would create a second source of truth:

- **No telemetry / operational / forensic trail** — stays platform-side (ADR-0004 rev 1 d10; ADR-0021 d10).
- **No type meaning** — `Exxxx`/`SPxxxx` meaning lives in `REGISTRY.md` (ADR-0021 d11); the ERP references it.
- **No SKU / price / supplier** — those live in the BOM (ADR-0000; ADR-0021 d9, d11).
- **No blobs** — the warehouse holds them; the ERP holds keys only (ADR-0021 d7).
- **No community profile *templates*** — those live in the public registry (ADR-0001 d5); the ERP stores deployment-specific *instance* versions.
- **No "deploy profile" action** — the gateway's pull into `active-profile.json` is the single mutation channel (ADR-0015 d4; ADR-0021 d12). This app is a *store*, not a deploy path.
- **No tenancy machinery** — single-tenant now; multitenancy is IndustryFlow's `[F]` concern (ADR-0021 d16).

## The warehouse (object store)

The blob store is **S3-compatible** and endpoint-swappable — the *same* five
`ERP_WAREHOUSE_*` vars target every backend, only the values change:

- **MinIO** — dev, or the shared instance IndustryFlow runs (`http://minio:9000`)
- **AWS S3** — later
- **Cloudflare R2** — `https://<ACCOUNT_ID>.r2.cloudflarestorage.com`, region `auto`

Migrating between them is a config change, not a code change.

### Sync the repo `store/` into the warehouse

`store/` is the public, type-level slice of the object store rendered as a flat
git keyspace — identifiers *are* the object keys (ADR-0017 d15). Load it into S3:

```sh
python -m app.store_sync              # uploads every store/ object under its key
```

### Secrets with 1Password

Keep the access key / secret out of files. Create the item once, then inject:

```sh
op vault create IndustryGrow    # once

op item create --vault IndustryGrow --category "API Credential" --title warehouse \
  'endpoint[text]=https://<ACCOUNT_ID>.r2.cloudflarestorage.com' \
  'bucket[text]=industrygrow' \
  'access_key_id[text]=<ACCESS_KEY_ID>' \
  'secret_access_key[password]=<SECRET_ACCESS_KEY>' \
  'region[text]=auto' \
  'account_id[text]=<ACCOUNT_ID>'

# then run with secrets injected into the environment, nothing on disk:
op run --env-file=.env.op.tpl -- uvicorn app.main:app --port 8021
op run --env-file=.env.op.tpl -- python -m app.store_sync
```

`.env.op.tpl` holds only `op://` references (safe to commit); `.env` is gitignored.

## Run it

**Integrated** (default — bring your own warehouse: IndustryFlow MinIO / S3 / R2):

```sh
export ERP_WAREHOUSE_ENDPOINT=…    # or use `op run` above
docker compose up --build          # app :8021 + mongo :27017
docker compose exec erp python -m app.store_sync   # load store/ into the warehouse
docker compose exec erp python -m app.seed         # load the GBOX_0001 estate
# open http://localhost:8021
```

**Standalone dev** (adds a local MinIO):

```sh
docker compose --profile standalone up --build     # + minio :9000/:9001
```

### Local (no Docker)

```sh
python -m venv .venv && . .venv/bin/activate
pip install -e ".[dev]"
# needs a MongoDB on localhost:27017 (docker run -p 27017:27017 mongo:7)
python -m app.seed
uvicorn app.main:app --reload --port 8021
```

## Develop

```sh
ruff format . && ruff check .    # lint + format (single tool)
pytest -q                        # unit tests (mongomock — no live Mongo needed)
```

## API (ADR-0022)

The JSON API is served at `/api/v1` (OpenAPI at `/docs`). Two caller classes:

- **Gateway machines → mTLS.** The TLS-terminating proxy validates the ATECC-anchored
  client cert (ADR-0007) and forwards the verified `GBOX_NNNN` identity; the app trusts
  that header only behind the proxy. The gateway *pulls* its active profile.
- **Operators / tooling → scoped token.** `Authorization: Bearer <token>`; `ERP_API_TOKENS`
  maps token → role (`operator` / `provisioning` / `readonly`). Interim, → JWT at stage 11.

Deliberate absences (ADR-0022): **no profile deploy/push** (record-only; gateway pulls),
no telemetry intake, no type-meaning/SKU writes, and document upload is **allowlisted** to
`{QP,QR,CP,CC,PR}` — type-layer docs go through `store_sync`.

## UI — Vite + TypeScript SPA (`frontend/`)

```sh
cd frontend
npm install
npm run dev        # http://localhost:5173, proxies /api → :8021
npm run build      # -> frontend/dist (serve behind the API in prod)
```

Set the operator token in the sidebar (default `dev-operator-token`). The SPA renders the
tree-of-life console over the API — Overview (integration map, calibration) and Instances
(serial allocation).

## Licensing

Application: **AGPL-3.0-or-later** (ADR-0021 d14, open core). The data it holds —
machines, serials, ATECC bindings, integration history, profiles, SP stock — is
**operator-private production data** and is never published.
