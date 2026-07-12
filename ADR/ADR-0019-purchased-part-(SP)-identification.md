<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0019: Purchased-part (SP) identification

- **ID:** ADR-0019
- **Status:** Accepted
- **Date:** 2026-06-13
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0017, ADR-0000, ADR-0014, ADR-0018
- **Resolves:** ADR-0017 deferred item "WeAct core board identification"

## Context and problem

ADR-0017 identifies what the project *designs*: the carrier and M01–M05 are E-modules, with an identity axis `Exxxx-VVVVVV-NNNNNN` whose version is our design's semantic version and whose serial is our manufactured, ATECC608-bound instance. It deliberately left open how to identify what the project *buys* — flagging the WeAct core board as "own E-number or vendor-part-number tracking (deferred)" (decision 3).

The gap became concrete with the cabinet power-distribution work (ADR-0018): a power supply, a DIN kWh meter, a fuse-holder, the gateway Raspberry Pi, and — when actuators arrive — pumps, SSRs, fixtures, and heating elements are real cabinet line items that need a stable key for part lists, inventory, and cost accounting. None is a thing we design, version, or serialize; forcing them into the E-number space would fill its version and serial fields with foreign or empty data and corrupt E semantics.

This ADR adds a parallel, deliberately minimal identifier for purchased (COTS) parts. It is a separate record rather than an expansion of ADR-0017, to keep that scheme focused on designed assemblies.

## Decision drivers

- **Designed vs purchased is a real ontological split.** Our version and serial semantics (ADR-0017) apply only to things we design and build.
- **A stable key for cost and inventory.** Part lists and procurement need one key per purchased type to roll up quantity and cost.
- **Spec, not SKU, is the commitment.** The vendor part is swappable; the characteristic it must meet is durable — the pattern ADR-0018 already uses for the kWh meter (S0 contract is the commitment; meter is a swappable line item).
- **Single source of truth (ADR-0000).** The live SKU and price are downstream values in the BOM; the identifier holds the spec, not the order data.
- **No over-engineering.** Not every purchased component earns a number; the catalog stays at procurement/inventory granularity.

## Decision

1. **Purchased (COTS) parts are identified by an `SP` number — `SPxxxx`** (`SP` + four digits): a second type-root on the identity axis (ADR-0017), parallel to the E-module, for things the project buys rather than designs. Like the E-number it is opaque; its meaning lives in the registry. (`SP` = "Supplied Part" — registry gloss, confirm.)

2. **No project version, no project serial.** The supplier owns versioning, so the SP identifier carries no `VVVVVV` field; it is type-level. Where a purchased unit must be tracked as an individual instance (e.g., the gateway Raspberry Pi), the instance key is the vendor's own serial / device identity recorded in the BOM and, where applicable, the provisioning record — never a project `NNNNNN`.

3. **An SP number names a generic characteristic spec, not a manufacturer part.** The vendor-free spec (what any acceptable part must satisfy) is the authority; the concrete vendor SKU and price live downstream in the `L` (List/BOM) document per ADR-0000. A part may be swapped for any other that meets the spec without changing the SP number — the swappable-line-item pattern ADR-0018 applies to the kWh meter.

4. **Granularity threshold — what earns an SP number.** A purchased item is SP-numbered when it is a unit of procurement, inventory, or cost accounting, or a designated traceable sub-assembly: power supply, DIN kWh meter, gateway Raspberry Pi, pumps, SSRs, fuse-holders, the WeAct STM32F4 core board, and the like. Ordinary purchased components populated onto a project PCB (INA226, TMP117, passives) are *not* SP-numbered; they remain MPN line items inside that board's `L` document. This keeps the SP catalog at procurement granularity and avoids the catalog growth ADR-0017 cautions about for assembly E-numbers.

5. **Position is not encoded in the SP number.** A purchased part is placed by a reference designator in a schematic or cabinet — its position in one document — not by the integration depth code `DDDDDD`. SP items are generally not Cyphal-bus participants, so they have no gateway-side `(module_class, node_role, zone)` twin and no depth code. Identity (what it is) and position (where it sits) stay separate exactly as in ADR-0017.

6. **Designed-vs-purchased boundary for actuators.** An actuator *node* — carrier + actuator-module PCB, our design — is an E-module and receives an E-number when designed (actuator-taxonomy ADR, deferred per ADR-0014 decision 9). The actuator *device* it drives or switches — pump, peristaltic head, LED fixture, heating element, SSR, fan — is purchased and SP-numbered. No SP or E numbers are assigned now: Phase 1 has no actuators, and identifiers are assigned at design/selection commit, not pre-reserved by class (ADR-0017 opaque-identifier principle).

7. **Resolves ADR-0017's WeAct deferred item.** The WeAct STM32F4 core board is a purchased sub-assembly and receives an `SP` number (decision 4); it is not given an E-number.

8. **Purchased parts carry documents on the same document-layer set as E-modules, on the `SP` root and without a version field** *(added 2026-07-12)*. An SP part needs its own documents — a bring-up manual, a BOM, an interface note — and these reuse the document-layer letters `S/D/L/P/M/I` defined for E-modules (ADR-0017 decision 9) rather than a parallel vocabulary. The form is `SPxxxx-<layer>[-<slug>]`, e.g. `SP0004-M-gateway-bringup` (Manual) or `SP0004-L` (BOM). There is **no `VVVVVV` field** — the supplier owns the part's versioning (decision 2), so an SP document is type-level like the SP identifier itself, and a vendor model of one spec (Pi 3B+/4/5) does not fork it (decision 3): one document set per spec, model-specific content as sections. This form was previously a maintainer-call recorded only in `REGISTRY.md` (2026-06-16); it is promoted here because it now underpins decision 9 and constrains identifier tooling (regex parsing, prefix filtering) — the very trigger that registry note named for promotion. `REGISTRY.md` retains only the list of live SP documents (the *what*, ADR-0000 d2); this decision is the form and the *why*.

9. **A designed accessory that serves only one specific part rolls up under that part's root; it does not receive its own E-number** *(added 2026-07-12)*. Some artifacts the project designs and revises — an enclosure, bracket, mount, adapter, gasket — have **no independent existence**: each exists solely to fit or serve **one** specific part, whether a purchased `SP` part or a designed `E` assembly. Such an accessory is filed as a document on the root of the part it serves, on the `D` (Drawing) layer (decision 8; ADR-0017 d9), with a descriptive slug:

   ```
   <parent-root>-D-<slug>[-src].<ext>     e.g.  SP0004-D-rp5-case-src.zip
   ```

   - **Why not its own root.** Two reasons, both consequences of the artifact having no life apart from its parent. *Root economy:* it never appears without the parent, so a standalone E-number multiplies identity with no retrieval benefit — the same anti-proliferation reasoning that keeps vendor variants as BOM lines (decision 3) and populate-variants as assembly-E on a shared bare design (ADR-0017 d4). *Dossier grouping:* the load-bearing fact is *which part it serves*, and encoding it in the key prefix makes the part's whole dossier — its BOM, manual, and accessories — one prefix list (`ListObjectsV2(Prefix="SP0004-")`, ADR-0017 d15), which a relationship buried in registry metadata does not give.
   - **Revisions ride in the slug.** A revised accessory design takes a new slug (`-rp5-case-rev2`), not a version field: a purchased-part root is version-less (decision 2), and even under an `E` parent the parent's `VVVVVV` versions the *parent's* design, not the accessory's independent revision stream. Slug-revisioning is coarser than a semantic version but suffices for the low-churn mechanical accessories this rule covers.
   - **The boundary — when it keeps its own E-number.** The rule is narrow. A designed artifact that stands alone, is used with more than one parent, or is itself instantiated/serialized as inventory is a designed **assembly** and takes its own E-number (ADR-0017 d3/d5) — it is not an accessory. The universal carrier `E0001` *hosts* the WeAct board `SP0005`, and the distribution case `E0007` *houses* the gateway Pi `SP0004`, yet both are standalone, serialized, multi-deployment assemblies — emphatically not accessories to the parts they carry. The test is independence of existence, not physical attachment.
   - **Licensing** inherits the parent artifact's `store/**` default (`REUSE.toml`).

Decisions 8 and 9 are recorded as additive in-place amendments (dated 2026-07-12): a naming rule promoted from a registry maintainer-call (8) and a new identification rule that bounds the designed-vs-purchased split for accessories (9). Neither changes a decision already on record, so per ADR-0000 d5 they are added in place without a revision bump; the maintainer accepts them through review-and-merge (ADR-0000 d7).

## Alternatives considered

**A. Give purchased parts E-numbers** (the "own E-number" option ADR-0017 decision 3 left open). *Rejected:* E's version is our design's semver and E's serial is our ATECC-bound manufactured instance; a purchased part has neither, so the E fields would be empty or foreign and E semantics would erode.

**B. No identifier for purchased parts — track them only as MPN strings in BOMs.** *Rejected:* loses a stable key for cost/inventory roll-up and for referencing one part from several documents; the swappable-spec pattern (ADR-0018 meter) would have nowhere to attach.

**C. Encode the manufacturer or SKU in the SP number.** *Rejected:* binds the identifier to a vendor that is by design swappable. The SKU is a downstream value (ADR-0000); the SP number is the stable spec key above it.

**D. Give every SP instance a project serial.** *Rejected:* most COTS (a fuse) are not individually tracked; where instance tracking matters (the Pi), the vendor's own serial is the instance key. A parallel project serial authority for bought parts is needless.

**E. Give every designed accessory its own E-number** (the strict reading of the designed-vs-purchased split: anything the project designs and versions is an E-module, so a printed case earns an E-number with a first-class `VVVVVV` field, keeping the split pure). *Rejected (decision 9):* it multiplies identity roots for artifacts that have no existence apart from their parent, and demotes the one fact that matters about such an artifact — the part it serves — to registry metadata, forfeiting the single-prefix part dossier. The version field it would buy is met adequately by slug-revisioning for low-churn accessories. This is a deliberate trade of designed/purchased *purity* for root economy and retrieval; decision 9's boundary keeps the trade bounded to artifacts with no independent existence, so a standalone or serialized designed assembly still takes its own E-number.

## Consequences

### Positive

- Cost and inventory roll-up gets a stable per-type key; designed (E) vs purchased (SP) is explicit at a glance.
- The spec-is-the-commitment / vendor-is-swappable pattern (ADR-0018 meter) is now general, not per-component.
- ADR-0017 stays focused on designed assemblies; this lives separately and removes its WeAct deferred item.

### Negative

- A second number space and a second registry section to maintain — mitigated by reusing E's opaque-key + registry pattern.
- The threshold (decision 4) is a judgment that must be applied consistently, or the SP catalog drifts toward either bloat or gaps.
- Spec-not-SKU discipline must be held in the BOM: an SP number must point to a spec a part satisfies, not be quietly treated as a single fixed SKU.
- A designed accessory filed under a version-less `SP` root (decision 9) means `^E\d{4}` no longer enumerates literally every artifact the project authored; "everything about part X" (`^SP0004-`) is the grouping optimized for instead — a deliberate choice (decision 9, alternative E).
- Accessory design-revision history is slug-encoded (`-rev2`) rather than carried in a semantic-version field: coarser, and dependent on slug discipline being held consistently.

## Relationship to other ADRs

- **ADR-0017** — sibling identification scheme; SP is the purchased-part type-root on the same identity axis, and this ADR resolves 0017's WeAct deferred item. SP documents reuse its document-layer set (d9) per decision 8; the designed-accessory carve-out from its E-number rules (d3/d5) is decision 9.
- **ADR-0000** — the live SKU and price stay downstream in the BOM; the SP record holds the spec/decision, not the order value.
- **ADR-0018** — the cabinet whose PSU, meter, and fuse-holder motivated this; the kWh-meter swappable-line-item / S0-contract pattern is the template for spec-over-SKU.
- **ADR-0014** — actuator *boards* are E-modules; the actuator *devices* they drive are SP parts.
- **ADR-0002** — the WeAct STM32F4 core board, previously a "COTS sub-component," is now an SP part.

## Deferred decisions

- `SP` registry gloss and the registry section holding `SPxxxx → spec / meaning` (shares ADR-0017's registry location/tooling question).
- Reference-designator convention for cabinet schematics (IEC 81346 letters) — a drafting convention, recorded with the schematic, not here.
- Whether any SP item ever needs a depth code — none does now; revisit only if a purchased item becomes a positioned, tracked machine element.

## References

- ADR-0000: Decision records and single-source-of-truth discipline.
- ADR-0002 (rev 3): Field bus architecture — WeAct core board.
- ADR-0014: Sensor node taxonomy — actuator boards as E-modules (decision 9).
- ADR-0017: Component, document, and instance identification — E-module identity axis; WeAct deferred item.
- ADR-0018 (rev 1): Cabinet power distribution — swappable COTS meter, S0 contract.