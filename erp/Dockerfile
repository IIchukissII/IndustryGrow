# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# One container: the API and the operator console it drives (ADR-0021 d15).
#
# The build context is the *repository root*, not erp/ — the console imports the
# tree-of-life mark from img/, which has a single home in the repo rather than a
# copy under frontend/. Build it as:  docker build -f erp/Dockerfile .
# (docker-compose.yml already does.)

# ---- console: build the SPA bundle ---------------------------------------
FROM node:22-alpine AS console

# Mirror the repo layout so `import "../../../img/industrygrow-logo.svg"`
# resolves exactly as it does in a checkout.
WORKDIR /repo/erp/frontend
COPY erp/frontend/package.json erp/frontend/package-lock.json ./
RUN npm ci
COPY img /repo/img
COPY erp/frontend ./
RUN npm run build

# ---- app -----------------------------------------------------------------
FROM python:3.12-slim

WORKDIR /srv

COPY erp/pyproject.toml ./
COPY erp/app ./app
RUN pip install --no-cache-dir .

COPY --from=console /repo/erp/frontend/dist ./console

# The type registry travels with the image (ADR-0023 d1): the ERP reads meaning
# from it rather than holding a copy. Mount over it to update the registry
# without a rebuild.
COPY REGISTRY.md ./REGISTRY.md

EXPOSE 8021
ENV ERP_HOST=0.0.0.0 ERP_PORT=8021 \
    ERP_CONSOLE_DIR=/srv/console \
    ERP_REGISTRY_PATH=/srv/REGISTRY.md
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8021"]
