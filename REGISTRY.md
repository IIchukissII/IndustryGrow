<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow identifier registry

The authoritative map from opaque identifiers to their meaning, per ADR-0017
(E-numbers) and ADR-0019 (SP numbers). Identifiers carry no meaning in
themselves; this registry is where meaning lives. It is the repo-side, public
type registry (ADR-0017 deferred "Registry and store location"); the
instance/integration layer — serials, provisioning, integration identifiers —
is platform-side and is not recorded here.

Conventions:
- An **E-number** (`Exxxx`) identifies a buildable/documentable assembly the
  project designs (ADR-0017 decision 3). Version (`-VVVVVV`) and serial
  (`-NNNNNN`) are the design's semver and the manufactured instance; neither
  appears here — this registry is type-level.
- An **SP number** (`SPxxxx`) identifies a purchased part by a vendor-free
  characteristic spec (ADR-0019). It has no project version or serial. The
  concrete vendor SKU and price live downstream in the BOM (ADR-0000), never here.
- Numbers are assigned sequentially at design/selection commit, opaque, and not
  pre-reserved by class (ADR-0017 decision 5; ADR-0019 decision 6).

## E-numbers (designed assemblies)

| E-number | Designation | Discipline | Notes |
|----------|-------------|------------|-------|
| `E0001` | Universal carrier | electrical | One bare design, one assembly — no real variant (ADR-0017 decision 4). Hosts the WeAct core board (`SP0005`). |
| `E0002` | M01-CLIMATE sensor module | electrical | Module-ID strap `0b001`. Air environment: SHT45, BME688, SCD41, FS3000. |
| `E0003` | M02-LIGHT sensor module | electrical | Module-ID strap `0b010`. Photic: AS7341 + UV-A sensor. |
| `E0004` | M03-ANALYTICS sensor module | electrical (mixed-signal) | Module-ID strap `0b011`. Hydroponic solution: pH (LMP7721 front-end), EC (AD5933), DS18B20, ADuM isolation. |
| `E0005` | M04-PLANT sensor module | electrical | Module-ID strap `0b100`. Plant-level: MLX90640 thermal imager. |
| `E0006` | M05-SAFETY / cabinet distribution + monitoring board | electrical | Module-ID strap `0b101`. Sense-only. Single INA226 on the `+12 V` SELV bus, TMP117, reed, leak, on-board input fuse, DIN-meter S0 input (ADR-0018). |
| `E0007` | Distribution case — cabinet enclosure + mains distribution wiring | electrical | The physical cabinet that houses the M05-SAFETY board (`E0006`) plus the DIN-rail purchased parts: energy meter (`SP0001`), mains MCB (`SP0002`), `+12 V` SELV supply (`SP0003`), and the gateway Raspberry Pi (`SP0004`). Mains infeed/protection, `+12 V` rail fan-out, and X2 field-wiring termination. Schematic `E0007-000001-S` (QElectroTech); see ADR-0018. |

> E0002–E0006 are proposed in M01→M05 order; set them to the actual commit
> order. The contiguity is incidental, not a reservation.

## SP numbers (purchased parts)

| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |
|-----------|-----------------------------------|-------------------|-------|
| `SP0001` | DIN-rail energy meter, S0 pulse output (DIN EN 62053-31) | no | Phase count and pulse constant (imp/kWh) chosen per deployment in the BOM (ADR-0018). The S0-counting contract is the commitment; the meter is swappable. |
| `SP0002` | DIN-rail miniature circuit breaker (MCB), cabinet mains input | no | Rating and curve per deployment in the BOM. Distinct from the board's on-board `+12 V` input fuse, which is an MPN line in E0006's BOM, not an SP device. |
| `SP0003` | DIN-rail SELV power supply, `+12 V` output, OVP/OCP | no | Output power/current per deployment in the BOM. Phase 1 is `+12 V` only; a `+24 V` supply for the power section is a separate later SP entry (ADR-0018). |
| `SP0004` | Gateway SBC — Raspberry Pi 3B+ / 4 / 5 class | yes (vendor serial / gateway identity) | The one SP part with per-instance identity: its vendor serial and the ATECC-bound gateway certificate are the instance key (ADR-0019 decision 2; ADR-0004 / ADR-0007). Specific model in the BOM. |
| `SP0005` | STM32F405RGT6 core board (WeAct-class) | no | Hosted on every carrier (E0001). Resolves ADR-0017's WeAct deferred item (ADR-0019 decision 7). |

### Document layers on the SP axis (naming convention)

ADR-0017 decision 9 defines the document-layer letter set `S / D / L / P / M / I`,
but its document-layer form `Exxxx-VVVVVV-L` is defined for **E-modules only**;
ADR-0019 left SP document naming open. SP document files use:

```
SPxxxx-<layer>[-<slug>]        e.g.  SP0004-M-gateway-bringup   (Manual)
                                      SP0004-L                   (gateway BOM)
```

- **`<layer>`** is one of `S/D/L/P/M/I` (ADR-0017 d9; `M` = Manual). The letters
  are reused as-is; only the root differs (`SPxxxx` instead of `Exxxx`).
- **No version field.** The supplier owns versioning, so an SP identifier carries
  no `VVVVVV` (ADR-0019 decision 2): write `SP0004-M-…`, not `SP0004-VVVVVV-M`.
- **Vendor variants are BOM lines, not identifiers.** Where a purchased part has
  vendor versions (e.g. SP0004 = Raspberry Pi 3B+ / 4 / 5, ADR-0002 rev 3 d6),
  the variant does **not** fork the SP number (ADR-0019 d2/d3): the chosen model
  is a BOM line, and **one `M` Manual per SP spec** covers all variants, with
  model-specific steps as sections. So there is no `SP0004-RPI5-M-…`.
- **`<slug>`** is an optional kebab-case descriptor, mirroring the descriptive
  suffix already used on E-documents (`E0001-000001-D-Top_Layer`).
- This is a **naming convention recorded here** (the identifier-convention home),
  not an ADR decision (maintainer call, 2026-06-16). Promote it to an ADR-0019
  amendment if it ever needs to constrain downstream tooling (ADR-0000 d1).

### Firmware document layer `F` (E-modules)

ADR-0017 decision 9 fixes the document-layer set `S / D / L / P / M / I`;
**ADR-0017 rev 1 (decision 16) adds `F` (Firmware)** for the built node image and
its source snapshot. The firmware is one codebase shared by every node type, so
`F` roots on the **carrier `E0001`**, not any one node module — the *why* is
decision 16 / alternative G; this registry records the *what*:

```
E0001-VVVVVV-F[.hex|-src.zip]   e.g.  E0001-000001-F.hex   (built image)
                                      E0001-000001-F-src.zip (source snapshot)
```

- **`F` = Firmware** — the built image and its source snapshot for the shared node
  codebase. Filed under **`E0001`** (the universal carrier whose one codebase every
  node runs, the personality selected at runtime by the module-ID strap), **not** a
  node module — filing per-module would bump N module `F`-versions for one
  shared-code change (ADR-0017 rev 1, alternative G).
- **`VVVVVV` is the firmware (codebase) version**, independent of the `E0001`
  *board* design version — a firmware release bumps `F`'s version without
  re-spinning the carrier PCB.
- If per-type binaries are ever built from the one codebase they are version
  *variants* (`E0001-VVVVVV-F-<type>`), not separate `F` roots (decision 16).
- The artifacts are produced by `firmware/tools/release.sh` and licensed
  AGPL-3.0-or-later (annotated in `REUSE.toml`, overriding the CERN-OHL-S
  `store/**` hardware default).

## Blocked / superseded versions

Per ADR-0017 decision 17, **withdrawn design artifacts** — *blocked* (defective) or
*superseded* (replaced) — are archived as a single object `Exxxx-VVVVVV-{BLOCKED,SUPERSEDED}.zip`
and their loose per-file objects removed. Withdrawal is scoped to the defect: when only one
production stage is bad (e.g. the layout), the still-valid sources stay loose. The status token
lives in the object key (the *what-status*); this table is the human-readable *why* and the exact
file boundary (ADR-0000 d2). This is the one place a *version* is named in this registry — an
explicit exception to the type-level rule above, because a withdrawal is a published fact about
that version (ADR-0017 d17); live versions and serials otherwise remain off this registry.

> **Reissue is the other half of supersession.** A successor version reissues the **full** face
> set under its own identifier — schematic, BOM, pin map, layout, and fabrication outputs — even
> for faces byte-identical to the prior version (only the embedded version field differs). Each
> artifact's object key *is* its full identifier (ADR-0017 decision 15: one object per identifier),
> so versions never share a face by reference; the superseded version's now-stale loose faces are
> then archived as below. (Example: `E0001-000002` reissues `-L.csv` and `-D-pinmap.md`
> byte-for-byte from `000001`, whose loose faces move into `E0001-000001-SUPERSEDED.zip`.)

| Version | Status | Archive object | Scope & reason |
|---------|--------|----------------|----------------|
| `E0001-000001` (carrier v0.0.1) | `BLOCKED` | `E0001-000001-BLOCKED.zip` | **Layout only.** The PCB **mirrors the WeAct core-board socket footprint** — reverses the pin order on the sockets, so every WeAct signal lands on the wrong net; the board as laid out is unbuildable. The archive holds the defective layout (`.kicad_pcb`) and every fabrication output derived from it (gerbers, drills, placement `-D-pos`, render `-D.png`). Pre-fabrication: no instances were ever built. |
| `E0001-000001` (carrier v0.0.1) | `SUPERSEDED` | `E0001-000001-SUPERSEDED.zip` | **Sources, by the relayout `E0001-000002`.** The v0.0.1 design faces kept loose after the layout was `BLOCKED` — schematic (`.kicad_sch`), project files (`.kicad_pro`, `.kicad_prl`), BOM (`-L.csv`), and pin map (`-D-pinmap.md`) — are replaced by `E0001-000002`, which reissues the full face set. No defect: the relayout corrects the mirrored footprint that blocked the v0.0.1 *layout*; these sources were always valid. The firmware `-F.*` objects stay loose (independent axis, ADR-0017 d16). |

> **Was kept loose** while the relayout was in progress (the basis for the corrected
> `E0001-000002`): the **schematic** `E0001-000001.kicad_sch` and its project files
> (`.kicad_pro`, `.kicad_prl`), the **BOM** `E0001-000001-L.csv`, and the **pin map**
> `E0001-000001-D-pinmap.md`. With `E0001-000002` released these are now **superseded** and
> archived in `E0001-000001-SUPERSEDED.zip` (row above). The carrier's **firmware**
> (`E0001-000001-F.hex`, `E0001-000001-F-src.zip`) is **not** withdrawn — the `F` layer is the
> independently-versioned shared codebase rooted on the carrier (ADR-0017 d16), not the board
> design, so it **stays loose** despite sharing the `E0001-000001` prefix.

### Procedure — archiving withdrawn artifacts

1. **Confirm the withdrawal and its scope.** Decide the status (`BLOCKED` = defective, must never
   be used; `SUPERSEDED` = replaced by a newer version, no defect) and which artifacts are actually
   dead. Localize to the defect: if only the layout is wrong, keep the schematic, BOM, and pin map
   loose as the basis for the relayout. **Close any editor** holding the files first (a KiCad
   session leaves `~*.lck` lock files — archiving while open risks the editor rewriting them).
2. **Bundle the withdrawn artifacts** into `Exxxx-VVVVVV-<STATUS>.zip` — for a bad layout, the
   layout source (`.kicad_pcb`) and every generated fabrication output (gerbers, drills, placement
   `-D-pos`, render `-D.png`). **Exclude** the firmware `-F.*` objects (a separate axis,
   ADR-0017 d16) and any still-valid sources you are keeping loose.
3. **`git rm` the loose per-file objects** now inside the archive, leaving the one `.zip` plus the
   objects you kept loose.
4. **Record it** by adding a row to the table above with the scope and concrete reason.
5. **Licensing.** The `.zip` is hardware design content, so it is covered by the `store/**`
   CERN-OHL-S default in `REUSE.toml` (it does not match the `-F-src.zip` AGPL override) — no
   `REUSE.toml` change is needed.
6. **Ship** via branch → PR; the maintainer accepts (ADR-0000 d7).

## Governing ADRs

- ADR-0017 (rev 1) — component / document / instance identification (E-numbers, two-axis model; firmware `F` layer rooted on the carrier E0001, decision 16; withdrawn-version archival, decision 17).
- ADR-0019 — purchased-part (SP) identification.
- ADR-0000 — single source of truth; vendor SKU and price live in the BOM, not here.
- ADR-0014 — sensor-module taxonomy (module-ID straps, M01–M05).
- ADR-0018 — cabinet power distribution (E0006 / M05, the S0 meter, the SELV supply).
