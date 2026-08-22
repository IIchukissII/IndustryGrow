<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Contributing to IndustryGrow

Thanks for your interest in contributing. This document covers how contributions
are licensed and the Developer Certificate of Origin (DCO) you agree to when you
submit one.

The design is driven by Architecture Decision Records in [`ADR/`](ADR/) — read
the relevant ADR before proposing a change to that area. Changes land through a
pull request against `main`; the [REUSE Compliance](.github/workflows/reuse.yml)
check must pass before merge.

## Licensing of contributions

IndustryGrow is **open-core**: different parts of the repository carry different
licenses (see [`LICENSE.md`](LICENSE.md) for the authoritative mapping and
[`LICENSES/`](LICENSES/) for the full texts).

Contributions are **inbound = outbound**: by submitting a change you agree that
your contribution is licensed under the **same license as the part of the
repository it touches**, namely:

| Part of the repository | License | SPDX ID |
|------------------------|---------|---------|
| Hardware designs (`store/`) | CERN Open Hardware Licence v2 – Strongly Reciprocal | `CERN-OHL-S-2.0` |
| ADRs & documentation (`ADR/`, `README.md`, `REGISTRY.md`, etc.) | Creative Commons Attribution-ShareAlike 4.0 | `CC-BY-SA-4.0` |
| Reference firmware *(when added)* | GNU Affero GPL v3 or later | `AGPL-3.0-or-later` |
| DSDL / protocol layer *(when added)* | Apache License 2.0 | `Apache-2.0` |

You retain copyright to your contributions; this is not a copyright assignment.

### SPDX / REUSE headers

The repository is [REUSE](https://reuse.software) compliant and CI enforces it.
Every file you add must carry licensing information:

- **Files that support comments** (Markdown, YAML, source code) — add an inline
  header at the top, e.g. for documentation:

  ```
  <!--
  SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
  -->
  ```

- **Files that cannot carry a comment** (KiCad sources, generated fab outputs,
  binaries) — add an entry to [`REUSE.toml`](REUSE.toml) instead.

Use `The IndustryGrow contributors` as the `SPDX-FileCopyrightText` holder unless
you have a specific reason to use your own name. You can check locally with:

```
pip install reuse
reuse lint
```

## Developer Certificate of Origin (DCO)

We use the [Developer Certificate of Origin](https://developercertificate.org/)
instead of a CLA. It is a lightweight way for you to certify that you wrote, or
otherwise have the right to submit, the code or content you contribute.

**Every commit must be signed off.** Add a `Signed-off-by` line with your real
name and email by committing with the `-s` flag:

```
git commit -s -m "Your commit message"
```

This appends:

```
Signed-off-by: Your Name <your.email@example.com>
```

The name and email must match a real identity and your git author information.
Commits without a valid `Signed-off-by` line will not be accepted. If you forget,
amend the last commit with `git commit --amend -s`, or for multiple commits use
`git rebase --signoff <base>`.

By signing off, you certify the following:

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```

## Working with the document store

`store/` is a flat object store: every object's key *is* its identifier, and the
hierarchy lives in the identifier rather than in directories. ADR-0017 owns the
scheme and the rules; the procedures below are the *how*.

### Withdrawing a design artifact set

A version's artifacts are withdrawn when they are **blocked** (defective, never to
be fabricated) or **superseded** (replaced, no defect). They are bundled into one
archive object, `Exxxx-VVVVVV-BLOCKED.zip` or `Exxxx-VVVVVV-SUPERSEDED.zip`
(ADR-0017 decision 17).

1. **Confirm the withdrawal and its scope.** Decide the status, and which artifacts
   are actually dead. Scope to the defect: if only the layout is wrong, the
   schematic, BOM and pin map stay loose as the basis for the relayout. **Close any
   editor holding the files first** — a KiCad session leaves `~*.lck` locks, and
   archiving while open risks the editor rewriting them.
2. **Bundle the withdrawn artifacts.** For a bad layout: the layout source
   (`.kicad_pcb`) and every generated fabrication output (gerbers, drills,
   placement `-D-pos`, render `-D.png`). **Exclude** the firmware `-F.*` objects —
   a different axis, never swept into a board archive — and anything kept loose.
3. **`git rm` the loose per-file objects** now inside the archive.
4. **Record it** by adding a row to the blocked/superseded table in `REGISTRY.md`
   with the scope and the concrete reason.
5. **Check licensing.** The `.zip` is hardware design content under the `store/**`
   CERN-OHL-S default in `REUSE.toml` — it does not match the `-F-src.zip` AGPL
   override, so no `REUSE.toml` change is needed.
6. **Ship** via branch → pull request; a maintainer accepts.

### Fabrication package contents

A board version's gerber and drill set ships as one object,
`Exxxx-VVVVVV-D-fab.zip` (ADR-0017 decision 18). Inside it, members are named
`Exxxx-VVVVVV-<layer>.<ext>` using the KiCad default layer vocabulary:

| Group | Descriptors |
|-------|-------------|
| Copper | `F_Cu`, `B_Cu` |
| Silkscreen | `F_Silkscreen`, `B_Silkscreen` |
| Soldermask | `F_Mask`, `B_Mask` |
| Paste | `F_Paste`, `B_Paste` |
| Outline | `Edge_Cuts` |
| Drill | `PTH`, `NPTH` (plus any drill map, same scheme) |

The `-D-` infix belongs to the store key and is not repeated inside the package.
This internal structure is deliberately not enumerated in `REGISTRY.md`, which
tracks the package by its object key.

The placement `-D-pos.csv`, the render `-D.png`, the pin map `-D-pinmap.md` and the
BOM `-L.csv` stay loose — each is separately consumed, and this split matches the
fab house's own upload flow: one gerber zip, a separate CPL, a separate BOM.

### Design-source package contents

A board version's editable CAD files ship as two objects, one per document layer
(ADR-0017 decision 19): `Exxxx-VVVVVV-S-src.zip` (schematic) and
`Exxxx-VVVVVV-D-src.zip` (layout). There are no loose `.kicad_*` objects.

| Package | Members |
|---------|---------|
| `-S-src.zip` | `Exxxx-VVVVVV.kicad_sch`, `.kicad_pro`, `.kicad_prl` |
| `-D-src.zip` | `Exxxx-VVVVVV.kicad_pcb`, `.kicad_dru` (where fitted), `.kicad_pro`, `.kicad_prl` |

Members keep the bare identifier stem — the infix belongs to the key, and KiCad
resolves a project by shared file stem. `.kicad_pro` / `.kicad_prl` go in both so
each package opens standalone; keep `.kicad_dru` with the layout.

To edit: extract both packages into `store/`, work, close KiCad, repack, delete the
extracted files. `.gitignore` does not mask them, so `git status` shows any left
behind.

**Normalise to LF before packing.** KiCad writes LF, and git no longer normalises
these files now that they live inside archives — whatever endings you pack are
permanent. On Windows `core.autocrlf` converts anything that has passed through a
checkout, so run this first:

```powershell
Get-ChildItem store\E000?-??????.kicad_* | ForEach-Object {
  [IO.File]::WriteAllText($_.FullName, ([IO.File]::ReadAllText($_.FullName) -replace "`r`n","`n")) }
```

```powershell
'E0001-000003','E0002-000001','E0003-000001','E0006-000001' | ForEach-Object {
  $s = @("store\$_.kicad_sch","store\$_.kicad_pro","store\$_.kicad_prl") | Where-Object { Test-Path $_ }
  Compress-Archive -Path $s -DestinationPath "store\$_-S-src.zip" -Force
  $d = @("store\$_.kicad_pcb","store\$_.kicad_dru","store\$_.kicad_pro","store\$_.kicad_prl") | Where-Object { Test-Path $_ }
  Compress-Archive -Path $d -DestinationPath "store\$_-D-src.zip" -Force }
```

The packages open against the store's shared libraries, which stay loose. Do not
sweep them into a package: they carry their own SnapEDA attribution in
`REUSE.toml`, and `reuse lint` does not check inside archives.
