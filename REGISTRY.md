# Component registry (E-numbers)

Canonical map of `Exxxx` module designations to their meaning, per
**ADR-0017** (Component, document, and instance identification scheme).
Identifiers are opaque by design — this registry holds the meaning.

Identifier recap (ADR-0017):

- **Documentation** (type-level): `Exxxx-VVVVVV-L` — `L` ∈ `S` Schema, `D` Drawing,
  `L` List/BOM, `P` Protocol, `M` Manual, `I` Interface.
- **Production & QC** (one instance): `Exxxx-VVVVVV-NNN[-suffix]`.
- **Integration** (installed): `GBOX_NNN-DDDDDD-Exxxx-VVVVVV-NNN`.
- `VVVVVV` = `major.minor.patch`, two digits each (e.g. `v0.0.1` → `000001`).

Documents are stored **flat** in `store/`; the hierarchy lives entirely in the
identifier, so the store is filtered by identifier pattern (e.g. all
`E0001-000001-D-*` is the carrier fab package, `E0001-000001-L.csv` its BOM).

| E-number | Name | Discipline | Bare design | Notes |
|----------|------|------------|-------------|-------|
| `E0001`  | Carrier — universal node host board | Electrical | own layout | CAN transceiver, ATECC608, sensor-module header (ADR-0002 rev 3, ADR-0017 decision 4). One assembly; no real variant. |

## Active versions

| Identifier base | Component | Version | Source project | Released documents |
|-----------------|-----------|---------|----------------|--------------------|
| `E0001-000001`  | Carrier   | v0.0.1  | `store/E0001-000001.kicad_{pro,prl,sch,pcb}` | `…-L.csv` (BOM), `…-D.png` (render), `…-D-pos.csv` (placement), `…-D-*.{gtl,gbl,gto,gbo,gts,gbs,gtp,gbp,gm1,drl}` (fab package) |

> Reserved per ADR-0017 but not yet present for `E0001`: `-S` (exported
> schematic), `-P` (protocol), `-M` (manual), `-I` (interface / Cyphal DSDL),
> and any production/QC instances (`Exxxx-VVVVVV-NNN`) with `-QP/-QR/-CP/-CC/-PR`
> lifecycle suffixes. Sensor modules M01–M05 (ADR-0014) get their own E-numbers
> when their designs are committed.
