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

## Alternatives considered

**A. Give purchased parts E-numbers** (the "own E-number" option ADR-0017 decision 3 left open). *Rejected:* E's version is our design's semver and E's serial is our ATECC-bound manufactured instance; a purchased part has neither, so the E fields would be empty or foreign and E semantics would erode.

**B. No identifier for purchased parts — track them only as MPN strings in BOMs.** *Rejected:* loses a stable key for cost/inventory roll-up and for referencing one part from several documents; the swappable-spec pattern (ADR-0018 meter) would have nowhere to attach.

**C. Encode the manufacturer or SKU in the SP number.** *Rejected:* binds the identifier to a vendor that is by design swappable. The SKU is a downstream value (ADR-0000); the SP number is the stable spec key above it.

**D. Give every SP instance a project serial.** *Rejected:* most COTS (a fuse) are not individually tracked; where instance tracking matters (the Pi), the vendor's own serial is the instance key. A parallel project serial authority for bought parts is needless.

## Consequences

### Positive

- Cost and inventory roll-up gets a stable per-type key; designed (E) vs purchased (SP) is explicit at a glance.
- The spec-is-the-commitment / vendor-is-swappable pattern (ADR-0018 meter) is now general, not per-component.
- ADR-0017 stays focused on designed assemblies; this lives separately and removes its WeAct deferred item.

### Negative

- A second number space and a second registry section to maintain — mitigated by reusing E's opaque-key + registry pattern.
- The threshold (decision 4) is a judgment that must be applied consistently, or the SP catalog drifts toward either bloat or gaps.
- Spec-not-SKU discipline must be held in the BOM: an SP number must point to a spec a part satisfies, not be quietly treated as a single fixed SKU.

## Relationship to other ADRs

- **ADR-0017** — sibling identification scheme; SP is the purchased-part type-root on the same identity axis, and this ADR resolves 0017's WeAct deferred item.
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