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

> The `E0002`–`E0006` contiguity follows M01→M05 design order; it is incidental,
> not a reservation by class (ADR-0017 decision 5).

## SP numbers (purchased parts)

| SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes |
|-----------|-----------------------------------|-------------------|-------|
| `SP0001` | DIN-rail energy meter, S0 pulse output (DIN EN 62053-31) | no | Phase count and pulse constant (imp/kWh) chosen per deployment in the BOM (ADR-0018). The S0-counting contract is the commitment; the meter is swappable. |
| `SP0002` | DIN-rail miniature circuit breaker (MCB), cabinet mains input | no | Rating and curve per deployment in the BOM. Distinct from the board's on-board `+12 V` input fuse, which is an MPN line in E0006's BOM, not an SP device. |
| `SP0003` | DIN-rail SELV power supply, `+12 V` output, OVP/OCP | no | Output power/current per deployment in the BOM. Phase 1 is `+12 V` only; a `+24 V` supply for the power section is a separate later SP entry (ADR-0018). |
| `SP0004` | Gateway SBC — Raspberry Pi 3B+ / 4 / 5 class | yes (vendor serial / gateway identity) | The one SP part with per-instance identity: its vendor serial and the ATECC-bound gateway certificate are the instance key (ADR-0019 decision 2; ADR-0004 / ADR-0007). Specific model in the BOM. |
| `SP0005` | STM32F405RGT6 core board (WeAct-class) | no | Hosted on every carrier (E0001). Resolves ADR-0017's WeAct deferred item (ADR-0019 decision 7). |

### Document layers on the SP axis

The SP document-layer form — `SPxxxx-<layer>[-<slug>]`, the `S/D/L/P/M/I` layer
letters reused from E-modules, no version field, vendor models never forking it —
is **ADR-0019 decision 8** (promoted there from the maintainer-call formerly
recorded here, 2026-06-16). A **designed accessory** filed on an SP root as a
`-D-` document (a printed case, bracket, or mount that serves only that part) is
**ADR-0019 decision 9**. This registry records only the live SP documents (the
*what*, ADR-0000 d2):

- `SP0004-M-gateway-bringup` — gateway bring-up Manual.

### Firmware document layer `F` (E-modules)

ADR-0017 rev 1 (decision 16) adds **`F` (Firmware)** — the built node image and
its source snapshot — to the d9 layer set. The *why* (carrier-rooted, not
per-module) is decision 16 / alternative G; the *what*:

```
E0001-VVVVVV-F[.hex|-src.zip]   e.g.  E0001-000001-F.hex     (built image)
                                      E0001-000001-F-src.zip  (source snapshot)
```

- **Rooted on the carrier `E0001`**, not any node module: the firmware is one
  codebase shared by every node, the personality selected at runtime by the
  module-ID strap.
- **`VVVVVV` is the firmware (codebase) version**, independent of the `E0001`
  *board* version — a firmware release bumps `F` without re-spinning the PCB.
- Per-type binaries, if ever built, are version *variants* (`E0001-VVVVVV-F-<type>`),
  not separate `F` roots (decision 16).
- Produced by `firmware/tools/release.sh`; licensed AGPL-3.0-or-later
  (`REUSE.toml`, overriding the CERN-OHL-S `store/**` default).

### Fabrication outputs on the `D` (Drawing) layer (E-modules)

A board version's generated fabrication outputs — gerbers, drill,
placement/centroid — are the layout-derived `D` (Drawing) layer (ADR-0017 d17),
filed flat, one object per identifier (d15): `Exxxx-VVVVVV-D-<descriptor>.<ext>`.
The carrier v0.0.2 (`E0001-000002`) set:

- copper `-D-Top_Layer.gtl` / `-D-Bottom_Layer.gbl`
- silkscreen `-D-Top_Overlay.gto` / `-D-Bottom_Overlay.gbo`
- soldermask `-D-Top_Solder.gts` / `-D-Bottom_Solder.gbs`
- paste `-D-F_Paste.gtp` / `-D-B_Paste.gbp`
- outline `-D-Edge_Cuts.gm1`; drill `-D-PTH.drl` / `-D-NPTH.drl`
- placement `-D-pos.csv`

The layer is the `-D-` infix, not the extension: the placement `.csv` is `D`, not
`L` — as are the render `-D.png` and pin map `-D-pinmap.md`. No live `.zip` (that
is the withdrawn-set exception, d17); licensing inherits the `store/**` default.

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

The step-by-step archival procedure (confirm scope → bundle → `git rm` the loose objects → record
the row above → ship via PR) lives with the rule in **ADR-0017 decision 17**. This registry holds
only the record: one row per withdrawal, above.

## Governing ADRs

- ADR-0017 (rev 1) — component / document / instance identification (E-numbers, two-axis model; firmware `F` layer rooted on the carrier E0001, decision 16; withdrawn-version archival, decision 17).
- ADR-0019 — purchased-part (SP) identification.
- ADR-0000 — single source of truth; vendor SKU and price live in the BOM, not here.
- ADR-0014 — sensor-module taxonomy (module-ID straps, M01–M05).
- ADR-0018 — cabinet power distribution (E0006 / M05, the S0 meter, the SELV supply).
