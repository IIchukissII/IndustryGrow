<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0000: Decision records and the single-source-of-truth discipline

- **ID:** ADR-0000
- **Status:** Proposed
- **Date:** 2026-06-12
- **Project:** IndustryGrow
- **Parent:** — (root; this ADR governs the form of all other ADRs)

## Context and problem

IndustryGrow's reasoning is spread across many artifacts: ADRs, BOMs, schematics, pin-maps, a component/identifier registry, and cultivation profiles. Several existing ADRs already behave as if a documentation discipline is in force — ADR-0004 rev 1 moved the audit trail to IndustryFlow specifically to avoid a second record of the same events, and ADR-0017 rejects its alternative E because it would create "two sources of truth" — but that discipline has never been written down. It is applied by habit, not by a rule a contributor could read, cite, or enforce in review.

An unwritten discipline has two failure modes. A new contributor cannot infer it and will, in good faith, copy a value into a second document "for convenience." And the author cannot point to it when reviewing such a change, so the drift is caught by memory rather than by process. Both failures are the same underlying problem: a fact that lives in more than one place has more than one place to go wrong.

This ADR names the discipline the project already half-follows and makes it the explicit root of the decision record. It is deliberately the lowest-numbered ADR because it governs the form of every other ADR rather than any technical subsystem.

## Decision drivers

- **Rationale decays slowest when captured once.** The *why* behind a decision is the part hardest to reconstruct later and easiest to lose; it needs exactly one durable home.
- **Duplication is the dominant source of documentation entropy.** N copies of a fact are N independent opportunities to drift; the only copy count that cannot drift is one. More dispersion, more entropy.
- **A contributor needs one place to look and one place to change.** If a value can be edited in two documents, "where do I change this?" has no correct answer and review cannot be mechanical.
- **The discipline already exists implicitly.** ADR-0004 rev 1 and ADR-0017 apply it without naming it; naming it makes it reviewable and inheritable rather than personal.

## Decision

1. **Architectural and design decisions are recorded as ADRs.** A decision that constrains downstream artifacts — protocol, hardware, security, identification, control architecture — is captured in an ADR before it is propagated into BOMs, schematics, or code. Discussion precedes the ADR; the ADR formalizes the outcome, never the reverse.

2. **ADRs own the *why*; downstream documents own the *what*.** An ADR holds context, decision drivers, the decision, rejected alternatives, and consequences — the rationale. Concrete values (part numbers, resistor values, pin assignments, footprints, setpoints) live in the document whose job is that value: BOM, schematic, pin-map, registry, profile. An ADR states *that* a 100 mΩ shunt was chosen and *why*; the BOM owns the live LCSC part number.

3. **Single source of truth: every fact has exactly one authoritative home.** No value is mirrored into a second document. Where another document needs a value, it references the authoritative source rather than copying it. The cheapest invariant against drift is zero copies, so that is the invariant.

4. **Downstream documents must never silently override an ADR decision.** If a procurement or schematic document needs to diverge from a recorded decision, the divergence is resolved by amending the ADR, not by quietly changing the downstream document. A silent override is the canonical anti-pattern this discipline exists to forbid.

5. **A revision is a supersession, not a silent edit.** A substantive change to a decision already on record produces a new revision: the title gains `(rev N)`, the metadata gains a `Supersedes:` line, and the reason is woven into *Context and problem* and documented in *Alternatives considered*. Revision history records the substantive reason for the change. A clarifying addition that changes no existing decision may be made in place on a still-Proposed draft without a revision bump.

6. **The governance root is not mirrored into the ADRs it governs.** This ADR applies to every ADR by being the root; individual ADRs do not back-reference it in their metadata. Enumerating "governed by ADR-0000" in each ADR would be exactly the mirroring this ADR forbids. The relationship is inherited, not copied.

## Alternatives considered

**A. Leave the discipline as unwritten convention.** *Rejected:* an unwritten rule cannot be cited in review, cannot be inferred by new contributors, and is enforced by the author's memory rather than by process — the failure mode that motivates this ADR.

**B. Allow controlled duplication with a "primary copy" marker.** Permit a value in several documents, one marked authoritative. *Rejected:* markers rot, copies drift between syncs, and a reader cannot tell a stale copy from a current one without re-checking the marker every time. Zero copies is cheaper to guarantee than one-authoritative-among-many.

**C. Fold this governance into ADR-0001 (framing).** *Rejected:* ADR-0001's scope is the project's product, licensing, and business shape; mixing documentation methodology into it corrupts that scope — itself a small act of the dispersion this ADR opposes. Methodology gets its own root.

**D. Keep the discipline only in tooling configuration or author working notes.** *Rejected:* those are not the repository's source of truth and are not authoritative for contributors. A rule about where truth lives must itself live in the source of truth.

## Consequences

### Positive

- There is one place to look for a decision's rationale and one place to change a value; the review question "is this duplicated?" becomes mechanical.
- The discipline that ADR-0004 and ADR-0017 already apply is now named, citable, and inheritable rather than personal habit.
- Rationale survives changes of tooling, memory, and contributors, because it is captured once in a durable artifact.

### Negative

- Every substantive change to a recorded decision now carries supersession ceremony (rev title, `Supersedes:`, narrative rationale) rather than a quick in-place edit. This is intentional friction — the cost of a non-drifting record.
- Contributors must learn the *why/what* split and resist the natural convenience of copying a value into the document they happen to be editing.
- Cross-references replace inline copies, adding a layer of indirection: obtaining a value sometimes means following a reference to its authoritative home rather than reading it where it is used.

## Deferred decisions

- **Status lifecycle.** All current ADRs are `Proposed`; the transitions `Proposed → Accepted → Superseded` and who effects them are not yet formalized.
- **Cross-reference and duplication tooling.** A linter that flags a value duplicated across documents, or a checker that validates references resolve, is desirable but unspecified.
- **The authoritative-home registry.** A concise map of which document owns which class of fact (values → BOM, pins → pin-map, identifiers → registry, rationale → ADR) could be maintained, but its location and format are open.
- **ADR template.** A skeleton enforcing the section structure shared across the existing ADRs is implied but not yet written.

## References

- ADR-0001: IndustryGrow framing — product, licensing, and data-model scope (distinct from this methodology root).
- ADR-0004 (rev 1): Gateway host hardening — applies single-source-of-truth by moving the audit trail platform-side (decisions 10, 16; alternative A).
- ADR-0017: Component, document, and instance identification — applies single-source-of-truth in its driver "do not duplicate the operational audit trail" and rejected alternative E.
- M. Nygard, "Documenting Architecture Decisions" (2011) — the ADR practice this record formalizes for the project.
- MADR (Markdown Any Decision Records) — format lineage for the ADR structure in use.
