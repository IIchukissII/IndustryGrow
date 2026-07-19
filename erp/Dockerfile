# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
FROM python:3.12-slim

WORKDIR /srv

COPY pyproject.toml ./
COPY app ./app
RUN pip install --no-cache-dir .

EXPOSE 8021
ENV ERP_HOST=0.0.0.0 ERP_PORT=8021
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8021"]
