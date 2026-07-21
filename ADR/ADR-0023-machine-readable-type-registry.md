<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0023: The type registry as a machine-readable interface

- **ID:** ADR-0023
- **Status:** Proposed
- **Date:** 2026-07-21
- **Project:** IndustryGrow
- **Parent:** ADR-0017 (rev 1)
- **Companions:** ADR-0000 (rev 1), ADR-0019, ADR-0021 (rev 1), ADR-0022
- **Relates to:** ADR-0021 decision 11 — the ERP references type meaning and never holds it

## Context and problem

ADR-0017 decision 3 makes `REGISTRY.md` the authoritative map from an opaque `Exxxx` to its meaning, and ADR-0019 does the same for `SPxxxx`. ADR-0021 decision 11 and ADR-0022 decision 9 then forbid the ERP from holding type meaning: `Exxxx`/`SPxxxx` are foreign keys *into* the registry, and no ERP route accepts a description for one.

Those records say where meaning lives and who may not own it. They do not say how a consumer **obtains** it. That gap was filled by habit, and the habit was to copy: the ERP console shipped a hardcoded `E0002 → "M01-CLIMATE"` table in TypeScript, and the server carried a second copy of the same table in Python. Both were labelled "display-only", which is precisely how a duplicate source of truth gets past review — the copy is introduced as a convenience, not as a claim of authority.

It drifted, exactly as ADR-0000 predicts. `E0007` was added to the registry (ADR-0018, the distribution case) and appears in neither copy. Nothing failed; the console simply rendered a bare identifier and nobody was told. This is ADR-0000's canonical anti-pattern — a fact with more than one place to go wrong — reached without a single reviewer approving a duplicate registry, because each copy looked like presentation code.

Removing the copies means software must read `REGISTRY.md` directly. That changes the document's standing: a file that is parsed is an **interface**, and an interface has a form whether or not anyone decided one. Today's form is accidental — two Markdown tables that happen to be shaped consistently — so an innocent editorial change (renaming a column, reflowing a row, adding a table that also holds identifiers) would silently empty the catalog of every consumer. The registry must therefore either stop being human-authored prose or acquire a decided, checkable form. This ADR chooses the second.

## Decision drivers

- **Single source of truth (ADR-0000 decision 3).** The failure being corrected is duplication; the fix must not introduce a second file that can disagree with the first.
- **The registry is a document people read and edit.** It is public, prose-annotated, and reviewed in PRs. Its human affordances are the reason it works and must survive the change.
- **A parsed document is an interface, and an undecided interface breaks silently.** The failure mode to design out is *quiet* — a malformed table yields an empty catalog, not an error.
- **Presentation is not meaning.** The hardcoded tables fused a designation (registry) with a leaf colour (design system). Only the first belongs to the registry.
- **Type meaning must stay outside the ERP's store (ADR-0021 decision 11).** A cached copy in Mongo is the same duplication with a longer half-life.
- **Growth is by append.** Identifiers are assigned sequentially and never reused (ADR-0017 decision 5); a new type must reach consumers with no code change on either side.

## Decision

### The registry stays one human-authored document

1. **`REGISTRY.md` remains the single authoritative type registry, human-authored and human-reviewed.** No generated companion file, no structured mirror, no registry data in any consumer's source. Consumers read *this* document. Where a consumer needs the registry over a network, the ERP serves a read-through view of it (decision 4) — a view is not a copy, because it holds nothing.

### Canonical form

2. **The registry has a canonical form, and only that form is the registry.** It is:
   - Exactly two registry sections, identified by their headings: **`## E-numbers`** (designed assemblies, ADR-0017) and **`## SP numbers`** (purchased parts, ADR-0019).
   - Within each, one Markdown table whose column order is fixed: `E-number | Designation | Discipline | Notes` and `SP-number | Role / generic spec (vendor-free) | Instance-tracked? | Notes`.
   - One entry per row. **Column 1 contains the bare identifier alone, in backticks** — `` `E0002` ``, `` `SP0004` `` — with no version, no serial, and no accompanying prose. This is what makes an entry mechanically distinguishable from a mention.
   - `Instance-tracked?` begins with `yes` or `no`; the parenthetical after `yes` is prose.
   - `Notes` is free prose and carries no contract. A consumer may display it; none may parse meaning out of it.

3. **Identifiers appearing anywhere else in the document are mentions, not entries.** The document-layer and withdrawn-artifact tables key on *versioned* identifiers (`E0001-000002`) and are deliberately outside the two registry sections; they describe artifacts, not types. Consumers parse the two sections and nothing else, so the rest of the document stays free prose that can be restructured at will.

### Consumers

4. **Consumers read the registry read-through and never persist a copy.** The ERP parses it on demand and serves it as a derived catalog (`GET /api/v1/catalog`); it is never written into Mongo, never seeded, never mirrored into the warehouse. A restart or a `git pull` is the whole cache-invalidation story.

5. **Unknown identifiers degrade, never resolve.** A consumer given an identifier absent from the registry displays the identifier itself. It must not fall back to a built-in table, guess from the number, or invent a label — a local fallback table is the duplication this record removes, reintroduced as error handling.

6. **The registry holds meaning only; presentation stays with the consumer.** No colours, icons, sort hints, or UI affordances enter `REGISTRY.md`. Where the console needs a per-module hue it derives one deterministically from the identifier against its own design palette, so a newly registered type renders correctly with no change in either place.

### Conformance

7. **Conformance to the canonical form is checked in CI, and a violation fails the check.** The check parses `REGISTRY.md` and fails on a missing registry section, a changed column order, an entry whose first cell is not a bare backticked identifier, a duplicate identifier, or an empty catalog. The point is to convert the silent failure into a loud one at the moment the registry is edited — the guarantee consumers rely on when they stop carrying their own tables.

## Alternatives considered

**A. Keep a table in each consumer (the status quo).** *Rejected:* it is the defect. Two copies existed, both drifted, and `E0007` was missing from both with no signal. Labelling a copy "display-only" does not make it stop being a second source of truth (ADR-0000 decision 3).

**B. Make a structured file (`REGISTRY.toml`/`.json`) authoritative and generate the Markdown from it.** *Rejected:* it inverts ADR-0017 decision 3 — the registry that contributors read, cite, and review in PRs would become a build artifact of a file they do not read. It buys robust parsing at the cost of a generator, a drift check, and a second thing to keep honest, to protect a table format that a CI check (decision 7) protects for far less. Reconsider if the registry ever needs fields prose cannot carry.

**C. Cache the parsed catalog in the ERP's MongoDB, refreshed on change.** *Rejected:* a synchronised copy is still a copy, and one that survives the registry being corrected. It puts type meaning inside the store ADR-0021 decision 11 keeps it out of, and buys nothing — parsing one small document is not a cost worth a consistency problem.

**D. Sync `REGISTRY.md` into the object-store warehouse and read it from there.** *Rejected:* the warehouse's organising principle is that identifiers *are* object keys (ADR-0017 decision 15), and the registry is not identifier-keyed — it is the document that explains the keys. It would also put a network fetch in the console's boot path to read a file that ships with the repo.

**E. Parse the document as it is, without deciding a form.** *Rejected:* the form would exist regardless, as an undocumented assumption inside a parser, and an editor reformatting a table would break the console with no warning and no rule to have consulted. An interface nobody wrote down is the unwritten-discipline failure ADR-0000 exists to close.

**F. Put the console's leaf colours in the registry.** *Rejected:* the registry is the meaning of an identifier, not its appearance (decision 6). Design tokens belong to the design system, and admitting one UI concern invites the rest.

## Consequences

### Positive

- **Type meaning has exactly one home again,** and the ERP's stated boundary (ADR-0021 decision 11) is now structural rather than aspirational — there is no table left to drift.
- **A new type reaches every consumer by being registered.** Adding a row to `REGISTRY.md` is the whole change; `E0007`-style silent gaps cannot recur.
- **The silent failure becomes a loud one.** A malformed registry fails CI at edit time instead of quietly emptying a catalog at runtime.
- **The registry stays the readable, public, prose-annotated document it is,** with no generator and no build step.
- **Meaning and presentation are separated,** so the design system can restyle without touching the registry and the registry can grow without touching the design system.

### Negative

- **`REGISTRY.md` now has a form that editors must respect.** Column order, section headings, and the bare-identifier cell are load-bearing; restructuring the two registry tables is a code-affecting change. This is the intended trade — the CI check makes the constraint visible rather than a trap.
- **Software now depends on a document in the repository.** A deployment must carry `REGISTRY.md` (the ERP container copies it, and it can be mounted to update it without a rebuild), which is a new packaging obligation.
- **Markdown-table parsing is inherently less robust than a data format.** Decision 7's check is what makes it tolerable; if the registry ever needs nested or typed fields, alternative B becomes the right answer.
- **Prose still holds data.** The module-ID strap (`0b001`) lives in a `Notes` cell today, so anything needing it must still read prose or wait for the registry to gain a column (below).

## Relationship to other ADRs

- **ADR-0017 (rev 1)** — decision 3 makes `REGISTRY.md` authoritative for `Exxxx`; this record decides the form in which that authority is consumed, and adds no new meaning.
- **ADR-0019** — the same, for `SPxxxx`; the `Instance-tracked?` column is read per its decision 2.
- **ADR-0021 (rev 1)** — decision 11 forbids the ERP holding type meaning; decisions 4–5 here are how the ERP satisfies it without a local table.
- **ADR-0022** — decision 9's "no type-meaning writes" gains its read-side counterpart: a read-through catalog route that stores nothing.
- **ADR-0000 (rev 1)** — decision 3 (single source of truth) is the driver; this record removes a duplication that reached `main` because each copy looked like presentation.

## Deferred decisions

- **Whether firmware and gateway consume the registry the same way.** Both embed type knowledge (module-ID straps, node taxonomy per ADR-0014); whether they read the registry at build time or stay independent is not decided here.
- **Promoting data out of `Notes` into columns** — the module-ID strap is the concrete case. It is registry data in prose today; giving it a column is an ADR-0017 change, not a form change.
- **Registry entries for withdrawn or superseded types.** ADR-0017 decision 17 covers withdrawn *artifacts*; a type has never been withdrawn, and the canonical form says nothing about how one would be marked.

## References

- ADR-0000 (rev 1), ADR-0014, ADR-0017 (rev 1), ADR-0018, ADR-0019, ADR-0021 (rev 1), ADR-0022, `REGISTRY.md`, `GLOSSARY.md`.
