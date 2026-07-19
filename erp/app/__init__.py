# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""IndustryGrow instance & integration ERP (ADR-0021).

The pre-cloud system of record for machines, instances, integration history,
the lifecycle-document index, deployment profiles, and SP stock. A compact
document metadata store (MongoDB) over the flat object-store warehouse.

Collection groups mirror the ADR-0021 [F]/[D] split:
  foundation.*  [F]  migrates into IndustryFlow production_unit at stage 11
  domain.*      [D]  stays the IndustryGrow layer over the core
"""

__version__ = "0.1.0"
