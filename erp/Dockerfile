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

# WeasyPrint lays out the PDF reports (app/services/reports.py); it renders text
# through Pango and HarfBuzz rather than bundling a rasteriser. The two font
# packages are the faces the report stylesheet names — without them every page
# falls back to whatever fontconfig finds, which in a slim image is nothing.
RUN apt-get update && apt-get install -y --no-install-recommends \
      libpango-1.0-0 libpangoft2-1.0-0 libharfbuzz0b libfribidi0 libfontconfig1 \
      fonts-liberation2 fonts-dejavu-core && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

COPY erp/pyproject.toml ./
COPY erp/app ./app
RUN pip install --no-cache-dir .

COPY --from=console /repo/erp/frontend/dist ./console

# The type registry travels with the image (ADR-0023 d1): the ERP reads meaning
# from it rather than holding a copy. This baked copy is the fallback for running
# the image on its own; docker-compose.yml points ERP_REGISTRY_PATH at the repo
# mount instead, so a `git pull` updates the registry without a rebuild. That
# mount is a *directory* for a reason — see the comment on it.
COPY REGISTRY.md ./REGISTRY.md
# The mark printed on PDF reports. img/ is copied into the console stage for the
# bundle; the runtime needs its own copy because the report is rendered here.
COPY img/industrygrow-mark.svg ./img/industrygrow-mark.svg

EXPOSE 8021
ENV ERP_HOST=0.0.0.0 ERP_PORT=8021 \
    ERP_CONSOLE_DIR=/srv/console \
    ERP_REGISTRY_PATH=/srv/REGISTRY.md \
    ERP_REPORT_LOGO=/srv/img/industrygrow-mark.svg
# --no-proxy-headers: the gateway mTLS check tests the transport peer against the
# trusted-proxy list (ADR-0022 d2, app/services/mtls.py). Uvicorn's proxy-header
# mode would rewrite that address from X-Forwarded-For, i.e. from the request.
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8021", "--no-proxy-headers"]
