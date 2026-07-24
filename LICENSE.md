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
| `store/E0001-*-F.hex`, `-F-src.zip` | Firmware release artifacts (ADR-0017 `F` layer) — reference firmware, not hardware design | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `firmware/` | Reference smart-node firmware (C / libcanard), build and release tooling | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `firmware/dsdl/` | DSDL type vocabulary (`industryflow.greenhouse.*`) — the protocol layer, kept permissive so any implementation can speak it | Apache License 2.0 | [`Apache-2.0`](LICENSES/Apache-2.0.txt) |
| `erp/` | Instance-and-integration ERP application and operator console (ADR-0021 d14) | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `pki/` | Operator CA bootstrap tooling and ceremony runbook (ADR-0024) | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `gateway/` | Gateway host provisioning and hardening material (ADR-0004) — see the note below | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |
| `ADR/`, `README.md`, `REGISTRY.md`, `project/`, this file | Architecture decision records and project documentation | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |
| `profiles/`, `img/`, `ADR/figures/`, `project/figures/` | Cultivation profile instances (ADR-0003), logos, and figures | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |

Documentation-layer documents that live flat in `store/` under the ADR-0017
identifier scheme (e.g. `SP0004-M-gateway-bringup.md`) carry their own inline
`CC-BY-SA-4.0` header, which wins over the `CERN-OHL-S-2.0` default for that
directory. See `REUSE.toml`.

> **Open question — `gateway/` is licensed as documentation, but it is software.**
> Everything under `gateway/` (`provision.sh`, `deploy.ps1`, `gateway_selftest.py`,
> the systemd units, the nftables ruleset) carries `CC-BY-SA-4.0`. Creative Commons
> advises against CC licences for software, and ADR-0001 routes platform code to
> `AGPL-3.0-or-later`. This table records what the files *say* today rather than
> what they arguably should say; changing it is a relicensing decision for the
> maintainers and contributors, not a documentation fix.

## Declared but not yet present in this repository

Per ADR-0001:

- **Commercial closed modules** → proprietary EULA, built only against open-core
  plugin interfaces and not part of this open repository.

The WeAct STM32F4 core board snapshot, if vendored, retains its upstream
open-hardware license.

## SPDX / REUSE

Per-file SPDX information follows the [REUSE](https://reuse.software) specification:

- Markdown documents carry inline `SPDX-FileCopyrightText` / `SPDX-License-Identifier`
  headers (HTML comments).
- Files that cannot carry a comment — the KiCad sources and generated fab outputs in
  `store/`, the images in `img/` and the figure directories, the strict-JSON profiles
  in `profiles/`, and `.gitmodules` / `.gitignore` / `REUSE.toml` itself — are
  annotated in [`REUSE.toml`](REUSE.toml).
- `REUSE.toml` also sets a per-tree default with `precedence = "closest"` for
  `store/**` and `erp/**`, so a file carrying its own header always wins over the
  directory default.

This document remains the human-readable summary; `REUSE.toml` plus the in-file
headers are the machine-readable source of truth.
