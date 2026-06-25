<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Licensing

IndustryGrow is an open-core project; different parts of the repository carry
different licenses, per **ADR-0001** (open-core framing). Full license texts
live in [`LICENSES/`](LICENSES/), named by SPDX identifier (REUSE convention).

## What applies to what

| Part of the repository | Contents | License | SPDX ID |
|------------------------|----------|---------|---------|
| `store/` | Hardware reference designs — carrier PCB (`E0001-000001.*`), BOM, gerbers, fab data | CERN Open Hardware Licence v2 – Strongly Reciprocal | [`CERN-OHL-S-2.0`](LICENSES/CERN-OHL-S-2.0.txt) |
| `ADR/`, `README.md`, `REGISTRY.md`, this file | Architecture decision records and project documentation | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |

## Declared but not yet present in this repository

Per ADR-0001, when the corresponding artifacts are added they will be licensed:

- **Reference firmware** → `AGPL-3.0-or-later`
- **DSDL / protocol layer** → `Apache-2.0`
- **Commercial closed modules** → proprietary EULA (not part of this open repository)

Their license texts will be added to `LICENSES/` and mapped here when that code
lands. The WeAct STM32F4 core board snapshot, if vendored, retains its upstream
open-hardware license.

## SPDX / REUSE

Per-file SPDX information follows the [REUSE](https://reuse.software) specification:

- Markdown documents carry inline `SPDX-FileCopyrightText` / `SPDX-License-Identifier`
  headers (HTML comments).
- Files that cannot carry a comment — the KiCad sources and generated fab outputs in
  `store/`, and the images in `img/` — are annotated in [`REUSE.toml`](REUSE.toml).

This document remains the human-readable summary; `REUSE.toml` plus the in-file
headers are the machine-readable source of truth.
