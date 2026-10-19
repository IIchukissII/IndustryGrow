<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0023 — The type registry as a machine-readable interface

- **Status:** Accepted
- **Date:** 2026-07-21; changed 2026-10-19
- **Parent:** [ADR-0017](ADR-0017-component-document-and-instance-identification.md)
- **Related:** [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (the SP section and its `Instance-tracked?` column), [ADR-0021](ADR-0021-instance-and-integration-erp.md) (decision 11: the ERP references type meaning and never holds it), [ADR-0022](ADR-0022-instance-integration-erp-api.md) (decision 9: no type-meaning writes; this record is its read side)

## Context

The registry is authoritative for what an `Exxxx` or `SPxxxx` means (ADR-0017 decision 3; ADR-0019), and the ERP must not hold that meaning (ADR-0021 decision 11; ADR-0022 decision 9). A consumer that carries its own copy of the meaning drifts from the registry without failing, because the copy reads as presentation code rather than as a second registry.

A consumer that reads the registry directly makes it an interface, and an interface has a form whether or not one is decided. An undecided form breaks silently: a renamed column or a new identifier-bearing table empties every consumer's catalog with no error. The registry is also a public document people read, annotate and review, and identifiers grow by append (ADR-0017 decision 5), so a new type must reach consumers with no code change.

## Decision

1. **`REGISTRY.md` is the single authoritative type registry, human-authored and human-reviewed.** No generated companion, structured mirror or consumer-side table exists. A network consumer reads the ERP's read-through view (decision 4), which holds nothing.
2. **The registry has a canonical form, and only that form is the registry.**
   - Exactly two registry sections, identified by their headings: `## E-numbers` (designed assemblies) and `## SP numbers` (purchased parts).
   - In each, one Markdown table with fixed column order: `E-number | Designation | Discipline | Notes` and `SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes`.
   - One entry per row; column 1 is the bare identifier alone, in backticks, with no version, serial or prose — what distinguishes an entry from a mention.
   - `Instance-tracked?` begins with `yes` or `no`; text after `yes` is prose.
   - `Notes` is free prose and carries no contract; a consumer may display it and parses no meaning from it.
3. **An identifier anywhere else in the document is a mention, not an entry.** The document-layer and withdrawn-artifact tables key on versioned identifiers and describe artifacts, not types; consumers parse the two registry sections only, so the rest of the document can be restructured freely.
4. **Consumers read the registry read-through and never persist a copy.** The ERP parses it on demand and serves a derived catalog; it is never written into the ERP's database, seeded, or mirrored into the warehouse.
5. **An identifier absent from the registry is displayed as the identifier itself.** A consumer does not fall back to a built-in table, guess from the number, or invent a label; a fallback table is the duplication this record removes.
6. **The registry holds meaning only; presentation stays with the consumer.** No colours, icons, sort hints or UI affordances enter the registry; a consumer that needs a per-module hue derives it deterministically from the identifier against its own palette.
7. **Conformance to the canonical form is checked in CI, and a violation fails the check.** The check fails on a missing registry section, a changed column order, an entry whose first cell is not a bare backticked identifier, a duplicate identifier, or an empty catalog. A malformed registry then fails at edit time rather than emptying a catalog at runtime.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | A table in each consumer, labelled display-only | A second source of truth; copies drift with no signal |
| 1 | A structured file (TOML or JSON) authoritative, Markdown generated from it | The document contributors read and review becomes a build artifact; needs a generator and a drift check to protect what the CI check of decision 7 protects |
| 2 | Parse the document as it is, without deciding a form | The form exists anyway as an undocumented parser assumption; a reformatted table breaks consumers with no rule to consult |
| 4 | Cache the parsed catalog in the ERP's database, refreshed on change | A synchronised copy survives a registry correction and puts type meaning in the store ADR-0021 decision 11 keeps it out of |
| 4 | Sync the registry into the object-store warehouse and read it from there | The warehouse is identifier-keyed (ADR-0017 decision 15) and the registry is not; adds a network fetch for a file that ships with the repository |
| 5 | Fall back to a built-in table or derive a label from the number | Reintroduces the duplicate as error handling |
| 6 | Put consumer colours in the registry | The registry is meaning, not appearance; one UI concern admits the rest |

## Consequences

**Positive**

- Type meaning has one home, and ADR-0021 decision 11 holds structurally.
- A new type reaches every consumer by being registered.
- A malformed registry fails CI at edit time.
- The registry stays a readable, prose-annotated document with no generator.
- The design system can restyle without touching the registry, and the registry can grow without touching the design system.

**Negative**

- Section headings, column order and the bare-identifier cell are load-bearing; restructuring the two registry tables is a code-affecting change.
- A deployment carries the registry document as a packaging obligation.
- Markdown-table parsing is less robust than a data format; if the registry needs nested or typed fields, a structured authoritative file becomes the right choice.
- Data held in `Notes` prose, such as a module's class ID, is not machine-readable.

## Deferred

| Open question | Settled by |
|---|---|
| Whether firmware and gateway consume the registry at build time or stay independent | Maintainers, when their embedded type knowledge drifts from the registry |
| Promoting the module class ID from `Notes` into a column | A change to ADR-0017, not to this form |
| How a withdrawn or superseded type is marked in the canonical form | The first type withdrawal |

## References

- `REGISTRY.md` — the registry and its form notice.
- ERP registry service — the parser, the catalog and the conformance check.
- ADR-0017, ADR-0019, ADR-0021, ADR-0022.
