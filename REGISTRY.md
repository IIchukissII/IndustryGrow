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

> **This document is read by software** (the instance/integration ERP and its
> console) and therefore has a **canonical form**, decided in ADR-0023: the two
> registry tables below — under the headings `## E-numbers` and `## SP numbers`
> — keep their column order, and each entry's first cell is the bare identifier
> alone in backticks. Prose, notes, and every other section stay free-form.
> Adding an entry needs no change anywhere else; restructuring those two tables
> does. CI checks the form (`python -m app.services.registry`), because a
> consumer that can no longer parse this file fails silently rather than loudly.

## E-numbers (designed assemblies)

| E-number | Designation | Discipline | Notes |
|----------|-------------|------------|-------|
| `E0001` | Universal carrier | electrical | One bare design, one assembly — no real variant (ADR-0017 decision 4). Hosts the WeAct core board (`SP0005`). |
| `E0002` | M01-CLIMATE sensor module | electrical | Module-ID strap `0b001`. Air *state* at the canopy: SHT45, BME688, SCD41 — all board-mounted. Airflow moved to `E0008` (ADR-0014 rev 2). `E0002-000001` fabricated; bench bring-up 2026-08-24, all ten subjects published; node draw measured (V3, 2026-08-26). Specification: `spec/M01-CLIMATE-specification.md`. |
| `E0003` | M02-LIGHT sensor module | electrical | Module-ID strap `0b010`. Photic: AS7343 14-channel spectral + TSL2585 UV-A, separated by a TCA9543A I2C switch since both answer at 0x39 (ADR-0014 rev 6). `E0003-000001` carries this complement in both schematic and layout. Not yet fabricated. Specification: `spec/M02-LIGHT-specification.md`. |
| `E0004` | M03-ANALYTICS sensor module | electrical (mixed-signal) | Module-ID strap `0b011`. Hydroponic solution: pH (LMP7721 front-end), EC (AD5933), DS18B20, ADuM isolation. |
| `E0005` | M04-PLANT sensor module | electrical | Module-ID strap `0b100`. Plant-level: MLX90640 thermal imager. |
| `E0006` | M05-SAFETY / cabinet distribution + monitoring board | electrical | Module-ID strap `0b101`. Sense-only. Single INA226 on the `+12 V` SELV bus, TMP117, reed, leak, on-board input fuse, DIN-meter S0 input (ADR-0018). Node draw measured 0.25 W with carrier. Specification: `spec/M05-SAFETY-specification.md`. |
| `E0007` | Distribution case — cabinet enclosure + mains distribution wiring | electrical | The physical cabinet that houses the M05-SAFETY board (`E0006`) plus the DIN-rail purchased parts: energy meter (`SP0001`), mains MCB (`SP0002`), `+12 V` SELV supply (`SP0003`), and the gateway Raspberry Pi (`SP0004`). Mains infeed/protection, `+12 V` rail fan-out, and X2 field-wiring termination. Schematic `E0007-000001-S` (QElectroTech); see ADR-0018. |
| `E0008` | M06-VENTILATION sensor module | electrical | Module-ID strap `0b110`. Air *transport* through the cabinet: FS3000 velocity (moved here from `E0002`), two SDP8xx-class differential-pressure ranges (filter loading, envelope↔ambient), SHT45 for in-stream density compensation. Introduced by ADR-0014 rev 2; not yet laid out. Specification: `spec/M06-VENTILATION-specification.md`. |
| `E0009` | M07-AMBIENT sensor module | electrical | Module-ID strap `0b111`. Boundary conditions one step outside the growing enclosure: SHT45, BMP390-class barometric, SCD41 reference, irradiance. Two **populated-BOM variants of this one design** (ADR-0014 rev 2): *indoor* — the room around a cabinet, core sensors only; *outdoor* — the weather, adding the deployment-scale wind group (purchased anemometer + Gray-coded vane, LSM303AGR heading / installation-integrity monitor) and outdoor qualification. One PCB layout, one module class, one strap, one firmware image — but **two assembly E-numbers**, since a wind group fitted or omitted at build is a real populate variant (ADR-0017 rev 2 decision 4). `E0009` names the first; the second is assigned at design commit, not pre-reserved. Introduced by ADR-0014 rev 2; not yet laid out. Specification: `spec/M07-AMBIENT-specification.md`. |
| `E0010` | Service tool - local operator and diagnostic interface | electrical | Optional accessory, never equipment: the cabinet operates, controls and records identically without it (ADR-0030 d1). Joins the bus listen-only, executes a carried `-M-` protocol step by step, and emits the per-instance record it does not keep. Realization open (`O-76`); `E0010-000001` names the first configuration. Emitted records identify the tool by its board serial. Specification: `spec/SERVICE-TOOL-specification.md`. |

> The `E0002`–`E0006` contiguity follows M01→M05 design order, and `E0008`–`E0009`
> continues it for M06→M07 only because they were committed in that order; both are
> incidental, not a reservation by class (ADR-0017 decision 5). `E0007` sits between
> them and is not a sensor module at all — the plainest evidence that the sequence
> carries no class meaning.

## SP numbers (purchased parts)

| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |
|-----------|-----------------------------------|-------------------|-------|
| `SP0001` | DIN-rail energy meter, S0 pulse output (DIN EN 62053-31) | no | Phase count and pulse constant (imp/kWh) chosen per deployment in the BOM (ADR-0018). The S0-counting contract is the commitment; the meter is swappable. |
| `SP0002` | DIN-rail miniature circuit breaker (MCB), cabinet mains input | no | Rating and curve per deployment in the BOM. Distinct from the board's on-board `+12 V` input fuse, which is an MPN line in E0006's BOM, not an SP device. |
| `SP0003` | DIN-rail SELV power supply, `+12 V` output, OVP/OCP | no | Output power/current per deployment in the BOM. Phase 1 is `+12 V` only; a `+24 V` supply for the power section is a separate later SP entry (ADR-0018). |
| `SP0004` | Gateway SBC — Raspberry Pi 3B+ / 4 / 5 class | yes (vendor serial / gateway identity) | The one SP part with per-instance identity: its vendor serial and the ATECC-bound gateway certificate are the instance key (ADR-0019 decision 2; ADR-0004 / ADR-0007). Specific model in the BOM. |
| `SP0005` | STM32F405RGT6 core board (WeAct-class) | no | Hosted on every carrier (E0001). Resolves ADR-0017's WeAct deferred item (ADR-0019 decision 7). Vendor snapshot: `SP0005-D-coreboard-snapshot.zip`. |

### Document layers on the SP axis (naming convention)

ADR-0017 d9's document-layer form `Exxxx-VVVVVV-L` is defined for **E-modules
only**, and ADR-0019 left SP document naming open. SP documents reuse the same
layer letters on the `SP` root:

```
SPxxxx-<layer>[-<slug>]        e.g.  SP0004-M-gateway-bringup   (Manual)
                                      SP0004-L                   (gateway BOM)
```

- **`<layer>`** — one of `S/D/L/P/M/I` (ADR-0017 d9; `M` = Manual), reused as-is;
  only the root differs.
- **No version field.** The supplier owns versioning, so an SP identifier carries
  no `VVVVVV` (ADR-0019 d2): `SP0004-M-…`, not `SP0004-VVVVVV-M`.
- **Vendor variants are BOM lines, not identifiers.** A part's vendor versions
  (e.g. SP0004 = Raspberry Pi 3B+ / 4 / 5) do not fork the SP number (ADR-0019
  d2/d3): the chosen model is a BOM line, and one `M` Manual per SP spec covers
  all variants (model-specific steps as sections). So no `SP0004-RPI5-M-…`.
- **`<slug>`** — an optional kebab-case descriptor, as on E-documents
  (`E0001-000001-D-Top_Layer`).

This convention is now **ADR-0019 decision 8** (promoted 2026-07-12 from the
2026-06-16 maintainer-call once recorded here, because it underpins d9 and
constrains identifier tooling); this section is its *what*.

### Designed accessories (ADR-0019 d9)

A designed artifact that serves only one specific part — a case, bracket, mount —
has no independent existence and takes **no E-number**; it rolls up under the
served part's root on the `D` layer with a descriptive slug, revisions carried in
the slug (a version-less `SP` root, ADR-0019 d2): `<parent-root>-D-<slug>[-src].<ext>`.

| Object | Serves | Note |
|--------|--------|------|
| `SP0004-D-rp5-case-src.zip` | `SP0004` gateway (Raspberry Pi 5) | Printed-case design source; slug-revisioned (`-rp5-case`). Licensing inherits the `store/**` default. |
| `E0006-000001-D-case-src.zip` | `E0006-000001` M05-SAFETY board | Enclosure design source (Inventor `.ipt` + STEP `.stp`); slug-revisioned (`-case`). Licensing inherits the `store/**` default. |
| `E0002-000001-D-case-src.zip` | `E0002-000001` M01-CLIMATE board | Enclosure design source (Inventor `.ipt` + STEP `.stp`, bottom and top halves; assembly render). Slug-revisioned (`-case`). Licensing inherits the `store/**` default. |

### Vendor reference documents (SP parts)

| Object | Serves | Contents |
|--------|--------|----------|
| `SP0005-D-coreboard-snapshot.zip` | `SP0005` | V1.1 schematic `SP0005-S-v11.pdf`, V1.1 board shape `SP0005-D-outline-v11.pdf`, V1.0 3D `SP0005-D-3d-v10.step`. Upstream [WeActStudio.STM32F4_64Pin_CoreBoard](https://github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard); WeAct copyright, licence unstated (`REUSE.toml`). Required by ADR-0002 rev 3 d4. |

### Design-source packages on the `S` / `D` layers (E-modules)

Per **ADR-0017 decision 19**, a board version's editable CAD files are packaged by
document layer — two objects per version, no loose `.kicad_*` objects. Applied
retroactively to every board in the store.

| Package | Members |
|---------|---------|
| `Exxxx-VVVVVV-S-src.zip` | `Exxxx-VVVVVV.kicad_sch`, `.kicad_pro`, `.kicad_prl` |
| `Exxxx-VVVVVV-D-src.zip` | `Exxxx-VVVVVV.kicad_pcb`, `.kicad_dru` (where fitted), `.kicad_pro`, `.kicad_prl` |

Members keep the bare identifier stem, the `-S-` / `-D-` infix belonging to the key
— KiCad resolves a project by shared file stem. `.kicad_pro` / `.kicad_prl` are
project-level and appear in both, so each package opens standalone. `.kicad_dru`
is design source, not generated output: without it a DRC run reports violations
the board does not have.

| Board version | `.kicad_dru` scope |
|---------------|--------------------|
| `E0001-000003` (carrier v0.0.3) | — |
| `E0002-000001` (M01 v0.0.1) | U5, XDFN4 pad pitch |
| `E0003-000001` (M02 v0.0.1) | — |
| `E0006-000001` (M05 v0.0.1) | — |

`E0007-000001` has no entry: its schematic is QElectroTech (`-S.qet`), one file and
already one object.

The shared libraries — `fp-lib-table`, `sym-lib-table`, `industrygrow.kicad_sym`,
`industrygrow.pretty` — are project-wide, not per-version,
and stay loose. Licensing inherits the `store/**` CERN-OHL-S default.

### Firmware document layer `F` (E-modules)

ADR-0017 rev 1 (decision 16) adds **`F` (Firmware)** — the built node image and
its source snapshot — to the d9 layer set. The *why* (carrier-rooted, not
per-module) is decision 16 / alternative G; the *what*:

```
E0001-VVVVVV-F-<variant>        e.g.  E0001-000001-F-boot.hex    (bootloader)
                                      E0001-000001-F-slot-a.hex  (application, slot A)
                                      E0001-000001-F-slot-b.hex  (application, slot B)
                                      E0001-000001-F-slot-a.img  (the same, as the bus serves it)
                                      E0001-000001-F-slot-b.img
                                      E0001-000001-F-src.zip     (source snapshot)
```

- **Rooted on the carrier `E0001`**, not any node module: the firmware is one
  codebase shared by every node, the personality selected at runtime by the
  module-ID strap.
- **`VVVVVV` is the firmware (codebase) version**, independent of the `E0001`
  *board* version — a firmware release bumps `F` without re-spinning the PCB.
- **A firmware version is three images**, because ADR-0029 d1 partitions the
  flash: the bootloader owns sectors 0-3 and is replaced over SWD only, and the
  application is built once per A/B slot because it links to the slot it runs
  from. They share one `F` version — one codebase, released together.
- **Each application image ships twice**: `.hex` carries its load address and is
  what an operator flashes; `.img` is the same image without one, which is what
  a signature covers and what a node is served over the bus (ADR-0029 d13).
- Per-type binaries, if ever built, are further variants of the same root
  (`E0001-VVVVVV-F-<type>`), not separate `F` roots (decision 16).
- Produced by `firmware/tools/release.sh`; licensed AGPL-3.0-or-later
  (`REUSE.toml`, overriding the CERN-OHL-S `store/**` default).

### Fabrication package on the `D` (Drawing) layer (E-modules)

Per **ADR-0017 decision 18**, a board version's gerber + drill set is one
indivisible object — the **fabrication package** `Exxxx-VVVVVV-D-fab.zip` — not
loose per-file gerber objects (this supersedes the earlier flat-per-file rule,
applied retroactively to `E0001-000002`). Inside the zip the members are named
`Exxxx-VVVVVV-<layer>.<ext>` on the KiCad default vocabulary — `F_Cu`/`B_Cu`,
`F_Silkscreen`/`B_Silkscreen`, `F_Mask`/`B_Mask`, `F_Paste`/`B_Paste`,
`Edge_Cuts`, `PTH`/`NPTH` — the `-D-` infix belonging to the object key, not the
members (d18); the internal structure is not itself registered.

The separately-consumed faces stay **loose**, because each is fetched on its own:
placement/centroid `-D-pos.csv` (the CPL), render `-D.png`, pin map
`-D-pinmap.md`; the BOM `-L.csv` is a different layer and also loose. So a board
carries `-D-fab.zip` + `-D-pos.csv` + `-D.png` (+ `-D-pinmap.md`) + `-L.csv` —
four or five objects, not fourteen.

| Board version | Package | Loose `D` / `L` faces |
|---------------|---------|-----------------------|
| `E0001-000003` (carrier v0.0.3) | `E0001-000003-D-fab.zip` | `-D-pos.csv`, `-D.png`, `-D-pinmap.md`, `-L.csv` |
| `E0002-000001` (M01 v0.0.1) | `E0002-000001-D-fab.zip` | `-D-pos.csv`, `-D.png`, `-L.csv` |
| `E0006-000001` (M05 v0.0.1) | `E0006-000001-D-fab.zip` | `-D-pos.csv`, `-D.png`, `-L.csv` |

The layer is the `-D-` infix, not the extension (the placement `.csv` is `D`, not
`L`; so are the render `-D.png` and pin map `-D-pinmap.md`). Licensing inherits
the `store/**` CERN-OHL-S default — the `.zip` is hardware design content, not the
`-F-src.zip` AGPL override.

## Blocked / superseded versions

Per ADR-0017 decision 17, **withdrawn design artifacts** — *blocked* (defective) or
*superseded* (replaced) — are archived as a single object `Exxxx-VVVVVV-{BLOCKED,SUPERSEDED}.zip`
and their loose per-file objects removed. Withdrawal is scoped to the defect: when only one
production stage is bad (e.g. the layout), the still-valid sources stay loose. The status token
lives in the object key (the *what-status*); this table is the human-readable *why* and the exact
file boundary (ADR-0000 d2). This is the one place a *version* is named in this registry — an
explicit exception to the type-level rule above, because a withdrawal is a published fact about
that version (ADR-0017 d17); live versions and serials otherwise remain off this registry.

| Version | Status | Archive object | Scope & reason |
|---------|--------|----------------|----------------|
| `E0001-000001` (carrier v0.0.1) | `BLOCKED` | `E0001-000001-BLOCKED.zip` | **Layout only.** The PCB **mirrors the WeAct core-board socket footprint** — reverses the pin order on the sockets, so every WeAct signal lands on the wrong net; the board as laid out is unbuildable. The archive holds the defective layout (`.kicad_pcb`) and every fabrication output derived from it (gerbers, drills, placement `-D-pos`, render `-D.png`). |
| `E0001-000001` (carrier v0.0.1) | `SUPERSEDED` | `E0001-000001-SUPERSEDED.zip` | **Sources, by the relayout `E0001-000002`.** The v0.0.1 design faces kept loose after the layout was `BLOCKED` — schematic (`.kicad_sch`), project files (`.kicad_pro`, `.kicad_prl`), BOM (`-L.csv`), and pin map (`-D-pinmap.md`) — are replaced by `E0001-000002`, which reissues the full face set. No defect: the relayout corrects the mirrored footprint that blocked the v0.0.1 *layout*; these sources were always valid. The carrier firmware `-F.*` stays loose — independent axis (ADR-0017 d16), not withdrawn despite sharing the `E0001-000001` prefix. |
| `E0001-000002` (carrier v0.0.2) | `SUPERSEDED` | `E0001-000002-SUPERSEDED.zip` | **Full face set, by `E0001-000003`.** `STRAP_1` (PA6) was unrouted to the MCU in the schematic *and* the layout: net `STRAP_1` held a single pad — sensor-module Header B pin 4 — and the WeAct socket pad for PA6 was unconnected. Module-ID bit 1 therefore always read 0. Benign for M01 (`0b001`) and M05 (`0b101`), both bench-verified on this carrier, but it prevents self-identification for M02 (`0b010`), M03, M06 and M07. No other defect: boards fabricated to v0.0.2 stay serviceable once the link is added by hand, together with clearing `E0001_STRAP1_UNROUTED` / `E0001_MODULE_ID_READABLE_MASK` in firmware. All nine faces are archived because supersession reissues the complete face set (ADR-0017 d17), including faces byte-identical to v0.0.2 (`-L.csv`, `-D-pos.csv`, `-D.png`). The carrier firmware `-F.*` stays loose — independent axis (ADR-0017 d16). |

The step-by-step archival procedure (confirm scope → bundle → `git rm` the loose objects → record
the row above → ship via PR) lives with the rule in **ADR-0017 decision 17**. This registry holds
only the record: one row per withdrawal, above.

## Governing ADRs

- ADR-0017 (rev 1) — component / document / instance identification (E-numbers, two-axis model; firmware `F` layer rooted on the carrier E0001, decision 16; withdrawn-version archival, decision 17).
- ADR-0019 — purchased-part (SP) identification.
- ADR-0000 — single source of truth; vendor SKU and price live in the BOM, not here.
- ADR-0014 (rev 6) — sensor-module taxonomy (M01–M07). The module ID is 8-bit (rev 4); the 3-bit strap carries `0x01`–`0x07` and is the transport for every module built on `E0001-000003`. M02's parts revised in rev 5; its UV part replaced again in rev 6, which `E0003-000001` carries.
- ADR-0018 — cabinet power distribution (E0006 / M05, the S0 meter, the SELV supply).
