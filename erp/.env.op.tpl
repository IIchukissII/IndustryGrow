# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# 1Password secret-injection template. The values are op:// references, not
# secrets — safe to commit. Populate the referenced item once (see README
# "Secrets with 1Password"), then either render a real .env:
#
#     op inject -i .env.op.tpl -o .env
#
# or run the app with secrets injected into the environment, nothing on disk:
#
#     op run --env-file=.env.op.tpl -- uvicorn app.main:app --port 8021
#     op run --env-file=.env.op.tpl -- python -m app.store_sync
#
# Swap MinIO -> AWS S3 -> Cloudflare R2 by changing only the item's fields.

ERP_WAREHOUSE_ENDPOINT=op://IndustryGrow/warehouse/endpoint
ERP_WAREHOUSE_BUCKET=op://IndustryGrow/warehouse/bucket
ERP_WAREHOUSE_ACCESS_KEY=op://IndustryGrow/warehouse/access_key_id
ERP_WAREHOUSE_SECRET_KEY=op://IndustryGrow/warehouse/secret_access_key
ERP_WAREHOUSE_REGION=op://IndustryGrow/warehouse/region
