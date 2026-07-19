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

## Run it

```sh
docker compose up --build           # app :8021 · mongo :27017 · minio :9000/:9001
docker compose exec erp python -m app.seed   # load the strawberry GBOX_0001 estate
# open http://localhost:8021
```

### Local dev

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

The console has five areas — Overview (estate + integration map), Instances
(with serial allocation), Calibration & Docs, SP Stock — styled as the
tree-of-life living console.

## Licensing

Application: **AGPL-3.0-or-later** (ADR-0021 d14, open core). The data it holds —
machines, serials, ATECC bindings, integration history, profiles, SP stock — is
**operator-private production data** and is never published.
