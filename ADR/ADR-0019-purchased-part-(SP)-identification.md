<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0019 — Purchased-part (SP) identification

- **Status:** Accepted
- **Date:** accepted 2026-06-13; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0017 (the identity axis this adds a second root to; its document layers and E-number rules), ADR-0000 (SKU and price stay downstream in the BOM), ADR-0018 (the cabinet parts and the spec-over-SKU pattern of its energy meter), ADR-0014 (actuator and sensor boards are E-modules), ADR-0031 (actuator classes whose devices are purchased parts), ADR-0021 (holds SP stock and instances), ADR-0023 (how the SP registry is consumed)

## Context

ADR-0017 identifies what the project designs: an E-module's version is the design's semantic version and its serial is a manufactured instance bound to a secure element. Power supplies, energy meters, breakers, the gateway computer, the core board and actuator devices are bought, not designed; they need a stable key for part lists, inventory and cost accounting, and have no project version or serial to fill E fields with.

The vendor part is swappable; the characteristic it must meet is the durable commitment, the pattern ADR-0018 applies to the energy meter. The live SKU and price are BOM values (ADR-0000 decision 2), and not every purchased component warrants a number.

## Decision

1. **A purchased (COTS) part is identified by an `SP` number, `SPxxxx` (`SP` plus four digits; "Supplied Part").** It is a second type root on the identity axis of ADR-0017, parallel to the E-module, and opaque, with its meaning in the registry.
2. **An SP identifier has no project version and no project serial.** The supplier owns versioning, so the SP identifier is type-level. Where a purchased unit is tracked as an instance, its key is the vendor's own serial or device identity, recorded in the BOM and, where applicable, the provisioning record.
3. **An SP number names a vendor-free characteristic spec, not a manufacturer part.** The concrete SKU and price live in the `L` document; any part that meets the spec substitutes without changing the number.
4. **A purchased item is SP-numbered when it is a unit of procurement, inventory or cost accounting, or a designated traceable sub-assembly.** Components populated onto a project PCB stay MPN lines in that board's `L` document. This holds the SP catalog at procurement granularity.
5. **Position is not encoded in an SP number.** A purchased part is placed by a reference designator in its schematic or cabinet drawing, not by a depth code; SP items are not bus participants and have no gateway-side position twin.
6. **An actuator node (carrier plus actuator-module PCB) is an E-module; the device it drives or switches is an SP part.** Pumps, fixtures, heating elements, SSRs and fans are bought. Numbers are assigned at design or selection commit, never pre-reserved by class (ADR-0017 decision 5).
7. **The WeAct-class core board is an SP part, not an E-number.** It is a purchased sub-assembly meeting decision 4.
8. **An SP part's documents use ADR-0017's document-layer letters on the SP root, with no version field: `SPxxxx-<layer>[-<slug>]`.** The supplier owns the part's versioning (decision 2), so an SP document is type-level; vendor models of one spec share one document set, with model-specific content as sections. The form constrains identifier parsing and prefix filtering, so it is a decision, not a registry convention.
9. **A designed accessory that exists only to serve one specific part is a `D`-layer document on that part's root, not its own E-number: `<parent-root>-D-<slug>[-src].<ext>`.** Encoding the served part in the key prefix makes the part's whole dossier one prefix listing (ADR-0017 decision 15), and a root for an artifact never seen without its parent adds identity with no retrieval benefit.
   - A revision takes a new slug, not a version field; under an E parent, `VVVVVV` versions the parent, not the accessory.
   - A designed artifact that stands alone, serves more than one parent, or is serialized as inventory is an assembly and takes its own E-number (ADR-0017 decisions 3, 5). The test is independence of existence, not physical attachment: the carrier hosts the core board and the distribution case houses the gateway computer, and both are assemblies.
   - Licensing follows the parent artifact's default.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **A.** Give purchased parts E-numbers | E's version and serial would be empty or foreign for a bought part; E semantics erode |
| 1 | **B.** No identifier for purchased parts; MPN strings in BOMs only | No stable key for cost and inventory roll-up or for citing one part from several documents; the spec-over-SKU pattern has nowhere to attach |
| 2 | **D.** Give every SP instance a project serial | Most COTS parts are not tracked individually; where tracking matters, the vendor serial is the key |
| 3 | **C.** Encode the manufacturer or SKU in the SP number | Binds the identifier to a vendor that is swappable by design; the SKU is a downstream value |
| 8 | A parallel document-layer vocabulary for SP parts | Two vocabularies for one classifier; ADR-0017's letters already cover an SP part's documents |
| 8 | Fork the document set per vendor model of one spec | Contradicts decision 3; one spec has one document set |
| 9 | **E.** Give every designed accessory its own E-number with a version field | Multiplies roots for artifacts with no existence apart from their parent and moves the served part into registry metadata, losing the single-prefix dossier; slug revisions suffice for low-churn accessories |

## Consequences

**Positive**

- Cost and inventory roll-up has a stable per-type key, and designed (E) versus purchased (SP) is explicit in the identifier.
- The spec-over-SKU pattern applies to every purchased part, not one component.
- ADR-0017 stays scoped to designed assemblies.

**Negative**

- A second number space and registry section must be maintained.
- The threshold of decision 4 is a judgment that must be applied consistently or the catalog drifts toward bloat or gaps.
- An SP number must be held to a spec in the BOM, not treated as one fixed SKU.
- An accessory under an SP root means the E prefixes no longer enumerate every artifact the project authored.
- Accessory revision history is slug-encoded, coarser than a semantic version and dependent on slug discipline.

## Deferred

| Open question | Settled by |
|---|---|
| Reference-designator convention for cabinet schematics | The cabinet schematic's drafting convention |
| Whether any SP item needs a depth code | A purchased item becoming a positioned, tracked machine element |

## References

- `REGISTRY.md` — the SP numbers and the live SP documents.
- ADR-0017 — the identity axis and document layers.
- ADR-0018 — the energy-meter spec-over-SKU pattern.
