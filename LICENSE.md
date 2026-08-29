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
| `store/` | Hardware reference designs — carrier and sensor-module PCBs, BOMs, placement, renders, and the `-S-src.zip` / `-D-src.zip` design-source and `-D-fab.zip` fabrication packages (ADR-0017 d18/d19) | CERN Open Hardware Licence v2 – Strongly Reciprocal | [`CERN-OHL-S-2.0`](LICENSES/CERN-OHL-S-2.0.txt) |
| `store/E0001-*-F*.hex`, `-F*.img`, `-F-src.zip` | Firmware release artifacts (ADR-0017 `F` layer) — reference firmware, not hardware design | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `firmware/` | Reference smart-node firmware (C / libcanard), build and release tooling | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `firmware/dsdl/` | DSDL type vocabulary (`industryflow.greenhouse.*`) — the protocol layer, kept permissive so any implementation can speak it | Apache License 2.0 | [`Apache-2.0`](LICENSES/Apache-2.0.txt) |
| `erp/` | Instance-and-integration ERP application and operator console (ADR-0021 d14) | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `pki/` | Operator CA bootstrap tooling and ceremony runbook (ADR-0024) | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `signing/` | Cultivation-profile signing tool and runbook (ADR-0025) | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `gateway/` | Gateway host provisioning and hardening material (ADR-0004) — see the note below | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |
| `gateway/provision_identity.py`, `gateway/profile_client.py`, `gateway/files/systemd/industrygrow-profile-pull.*`, `gateway/files/app/gateway_timesync.py`, `gateway/files/systemd/industrygrow-timesync.service`, `gateway/busload.py` | Gateway identity provisioning (ADR-0007 d9), the profile-pull client (ADR-0015 d5-7, ADR-0025), the bus time master (ADR-0002 d11) and the bus-load analyser | GNU Affero General Public License v3.0 or later | [`AGPL-3.0-or-later`](LICENSES/AGPL-3.0-or-later.txt) |
| `ADR/`, `README.md`, `REGISTRY.md`, `project/`, `spec/`, this file | Architecture decision records, module specifications, and project documentation | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |
| `profiles/`, `img/`, `ADR/figures/`, `project/figures/` | Cultivation profile instances (ADR-0003), logos, and figures | Creative Commons Attribution-ShareAlike 4.0 International | [`CC-BY-SA-4.0`](LICENSES/CC-BY-SA-4.0.txt) |

Documentation-layer documents that live flat in `store/` under the ADR-0017
identifier scheme (e.g. `SP0004-M-gateway-bringup.md`) carry their own inline
`CC-BY-SA-4.0` header, which wins over the `CERN-OHL-S-2.0` default for that
directory. See `REUSE.toml`.

> **Open question — most of `gateway/` is licensed as documentation, but it is
> software.** The pre-existing files under `gateway/` (`provision.sh`, `deploy.ps1`,
> `gateway_selftest.py`, the systemd units, the nftables ruleset) carry
> `CC-BY-SA-4.0`. Creative Commons advises against CC licences for software, and
> ADR-0001 routes platform code to `AGPL-3.0-or-later`. This table records what the
> files *say* today rather than what they arguably should say; relicensing them is a
> decision for the maintainers and contributors, not a documentation fix.
>
> The newer files under `gateway/` are therefore listed separately: they are new
> work, so they were licensed `AGPL-3.0-or-later` per ADR-0001 from the start rather
> than inheriting a label the note above already calls wrong. That leaves the
> directory mixed, which the per-file SPDX headers handle, and it narrows the
> relicensing question to the files that actually need contributor consent instead
> of growing it.

## Third-party content

Content the project did not author keeps its own terms, annotated in
[`REUSE.toml`](REUSE.toml) with `precedence = "override"` so it wins over the
directory defaults above. **Neither item below carries a licence grant** — both are
published by their originator with no licence stated, so neither is covered by this
project's licences and neither may be treated as though it were.

| Object | Origin | SPDX ID |
|--------|--------|---------|
| `store/SP0005-D-coreboard-snapshot.zip` | WeAct STM32F4 core-board schematic, board outline and 3D model, retained per ADR-0002 rev 3 d4 | [`LicenseRef-WeAct-unstated`](LICENSES/LicenseRef-WeAct-unstated.txt) |

The SnapEDA EDA library content for the ams OSRAM AS7331 — symbol, footprint and 3D
model — was removed on 2026-08-23. ADR-0014 rev 6 withdrew the part from M02-LIGHT and
`E0003-000001` no longer references any of it, so the repository carries no SnapEDA
content and `LicenseRef-SnapEDA-unstated` is retired.

## Declared but not present in this repository

Per ADR-0001, **commercial closed modules** carry a proprietary EULA, are built only
against open-core plugin interfaces, and are not part of this repository.

## SPDX / REUSE

Per-file SPDX information follows the [REUSE](https://reuse.software) specification:

- Markdown documents carry inline `SPDX-FileCopyrightText` / `SPDX-License-Identifier`
  headers (HTML comments).
- Files that cannot carry a comment — the design-source and fabrication packages and
  generated outputs in `store/`, the images in `img/` and the figure directories, the
  strict-JSON profiles in `profiles/`, and `.gitmodules` / `.gitignore` / `REUSE.toml`
  itself — are annotated in [`REUSE.toml`](REUSE.toml).
- `REUSE.toml` also sets a per-tree default with `precedence = "closest"` for
  `store/**` and `erp/**`, so a file carrying its own header always wins over the
  directory default.

This document remains the human-readable summary; `REUSE.toml` plus the in-file
headers are the machine-readable source of truth.
