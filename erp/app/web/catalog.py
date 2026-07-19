# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Display-only module labels + leaf colours for the console.

This is a UI convenience, not a source of truth: the authoritative type meaning
of each E-number lives in REGISTRY.md (ADR-0021 d11), never in the ERP's store.
Colours mirror the tree-of-life crown nodes.
"""

from __future__ import annotations

MODULE_DISPLAY: dict[str, tuple[str, str]] = {
    "E0001": ("E0001 carrier", "#9fb2c9"),
    "E0002": ("M01-CLIMATE", "#67e8f9"),
    "E0003": ("M02-LIGHT", "#fcd34d"),
    "E0004": ("M03-ANALYTICS", "#c4b5fd"),
    "E0005": ("M04-PLANT", "#86efac"),
    "E0006": ("M05-SAFETY", "#fb7185"),
}


def module_display(e_number: str) -> tuple[str, str]:
    return MODULE_DISPLAY.get(e_number, (e_number, "#9fb2c9"))
