<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->
# Component registry (E-numbers)

Canonical map of `Exxxx` module designations to their meaning, per
**ADR-0017** (Component, document, and instance identification scheme).
Identifiers are opaque by design — this registry holds the meaning.

Identifier recap (ADR-0017):

- **Documentation** (type-level): `Exxxx-VVVVVV-L` — `L` is a document-layer letter;
  see the code legend in ADR-0017 for the layer letters and lifecycle suffixes.
- **Production & QC** (one instance): `Exxxx-VVVVVV-NNNNNN[-suffix]`.
- **Integration** (installed): `GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN`.
- `VVVVVV` = `major.minor.patch`, two digits each (e.g. `v0.0.1` → `000001`).

Documents are stored **flat** in `store/`; the hierarchy lives entirely in the
identifier, so the store is filtered by identifier pattern (e.g. all
`E0001-000001-D-*` is the carrier fab package, `E0001-000001-L.csv` its BOM).

| E-number | Name | Discipline | Bare design | Notes |
|----------|------|------------|-------------|-------|
| `E0001`  | Carrier — universal node host board | Electrical | own layout | CAN transceiver, ATECC608, sensor-module header (ADR-0002 rev 3, ADR-0017 decision 4). One assembly; no real variant. |
| `E0002`  | M05-SAFETY — cabinet power distribution + monitoring node | Electrical | own layout | sense-only: 1× INA226 on the +12 V sensor bus; actuator energy via DIN kWh meter / S0; TMP117 reported cabinet temp; reed (door) and leak strip report/alert only (ADR-0018, ADR-0014 M05). The over-temp trip lives at the heating actuator, not M05 (ADR-0018 decision 10). straps `0b101`. No switching, no interlock, no isolation. |

## Active versions

| Identifier base | Component | Version | Source project | Released documents |
|-----------------|-----------|---------|----------------|--------------------|
| `E0001-000001`  | Carrier   | v0.0.1  | `store/E0001-000001.kicad_{pro,prl,sch,pcb}` | `…-L.csv` (BOM), `…-D.png` (render), `…-D-pos.csv` (placement), `…-D-*.{gtl,gbl,gto,gbo,gts,gbs,gtp,gbp,gm1,drl}` (fab package) |
| `E0002-000001`  | M05-SAFETY | v0.0.1 | (pending — layout not yet committed) | `…-L.md` (working design BOM); `…-L.csv` and fab package to follow at layout commit |

> M05-SAFETY is registered at the BOM stage, ahead of layout commit; its design
> documents (`-S`/`-D`/`…-L.csv` fab package) land when the KiCad project is committed.
> Reserved per ADR-0017 but not yet present for `E0001`/`E0002`: `-S` (exported
> schematic), `-P` (protocol), `-M` (manual), `-I` (interface / Cyphal DSDL),
> and any production/QC instances (`Exxxx-VVVVVV-NNNNNN`) with `-QP/-QR/-CP/-CC/-PR`
> lifecycle suffixes. Sensor modules M01–M04 (ADR-0014) get their own E-numbers
> when their designs are committed. **Next free: `E0003`.**