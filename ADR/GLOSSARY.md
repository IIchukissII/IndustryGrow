<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow / IndustryFlow — Glossary (controlled vocabulary)

> Status: Draft
> Scope: terminology only. This document binds **words to meanings**; it does
> not make or restate engineering decisions.

## Authority

- This glossary is the source of truth for **terms**: each reserved word has
  exactly one meaning and one set of forbidden uses.
- **ADRs remain the source of truth for decisions and rationale.** Every entry
  cites the ADR that established the concept; the glossary never originates a
  decision and never restates a decided value.
- Precedence:
  - On the **meaning of a word** → the glossary governs. An ADR, schematic,
    BOM, or code that uses a reserved word for another concept is
    non-compliant and must be corrected.
  - On a **decision or value** → the cited ADR governs. If the glossary and an
    ADR disagree on a value, the glossary is wrong by construction (it should
    point, not restate) and is fixed.

## Inclusion criterion

A term belongs here only if it is **reserved** (exactly one meaning),
**collides** with a neighbouring usage, or is a **load-bearing identifier
token**. Ordinary domain nouns stay defined in their originating ADR. Keep this
list small; growth is a smell.

## Scope tags

`[F]` foundational / platform-neutral — belongs to the IndustryFlow foundation
and moves with it if the foundation is extracted as a separate stratum.
`[D]` domain / CEA-specific — stays with IndustryGrow.

All current entries are `[F]`: type / instance / position / document
identification is platform-foundational. Domain terms (crop, profile, VPD,
photoperiod, …) enter as `[D]` when added.

---

## Part 1 — Reserved terms (exactly one meaning)

**Axis** `[F]` — one of the two orthogonal dimensions of identity: the
**identity axis** (what a thing is / which copy) and the **position axis**
(where it physically sits). They vary independently and meet only in the
integration identifier.
*Reconciliation:* ADR-0017 separates *three* coordinates — type, instance,
position. This is the same model: the identity axis carries **type** and
**instance** as its two levels; the position axis is the third coordinate.
"Two axes" and "three separated coordinates" describe one structure.
Origin: ADR-0017.

**Tree** `[F]` — the rendering of an axis as a rooted graph; figures only.
Non-normative: "grove" (many instance-trees joined to one machine at
integration) is intuition, never a defined term. Origin: ADR-0017.

**Vertex** `[F]` — a position in an identity or position tree. Use this word
for tree positions. Do **not** use "node" (reserved — Part 2) or "entry"
(already overloaded: entry point, log entry, power entry) for a tree position.
Origin: this glossary.

**E-module** `[F]` — a buildable / documentable assembly bearing an E-number.
Origin: ADR-0017 (decision 3).

**Serial** `[F]` — the per-instance enumerator `NNNNNN` on the identity axis.
Position-free by construction: a serial never encodes where an instance sits.
Origin: ADR-0017 (decision 8).

**Depth** `[F]` — the position address `DDDDDD`: where a thing sits within a
machine. Origin: ADR-0017 (decision 7).

**Sub-position** `[F]` — a nesting tier within the position axis. The depth code
has the tiers `main`, `sub-position 1`, `sub-position 2`. Never "sub-module": a
sub-position carries no E-number. Origin: ADR-0017 (decisions 3, 7).

**Machine** `[F]` — a deployed cabinet; the IndustryFlow `machine` entity,
designated `GBOX_NNNN`; the root of the position axis. It *contains*
production_units; it is not one. Origin: ADR-0017 (decision 6), ADR-0001
(decision 7).

**production_unit / slot** `[F]` — the IndustryFlow data-model entity for a
**slot**: a growing position within a machine (~9 in the strawberry cabinet).
A *position-axis* concept — the platform-side counterpart of a depth position
(ADR-0017 decision 7), **not** of the machine. "slot" is the informal synonym.
Never use "slot" for a generic depth vertex — that is a *position*.
Origin: ADR-0001 (decision 7), ADR-IF-0001 (planned).

**Document layer** `[F]` — the document-type letter `S / D / L / P / M / I`
selecting one artifact about an identity. Always written **"document layer"**,
never bare "layer" (overloaded — Part 2). A classifier; not an axis and not a
nesting tier. Origin: ADR-0017 (decision 9).

**Lifecycle suffix** `[F]` — the per-instance record classifier
`QP / QR / CP / CC / PR` appended to an instance identifier; the instance-level
analogue of a document layer. Origin: ADR-0017 (decisions 10–14).

**Integration identifier** `[F]` — the mutable cross-reference
`GBOX_NNNN-DDDDDD-Exxxx-VVVVVV-NNNNNN` joining a position vertex (a slot or a
node position) to an instance. Re-assigned whenever an instance is moved,
removed, or replaced; no suffix slot by design. Origin: ADR-0017, ADR-0016.

---

## Part 2 — Disambiguated words (multiple legitimate senses)

These words are *not* reserved to one meaning — each carries several legitimate
senses across the project. Never use the bare word where an adjacent qualifier
does not fix the sense.

**layer** —
- *document layer* (`S/D/L/P/M/I`) — the identifier sense; always written
  "document layer" (Part 1).
- *PCB layer* — copper-layer count ("2-layer PCB"). Fabrication.
- *physical / application layer* — OSI / protocol stack (ADR-0002).
- *architectural stratum* — "platform layer", "foundational layer".
- *boundary layer* — fluid dynamics (ADR-0014, leaf boundary layer).
Rule: bare "layer" is never the document sense; the document sense is always
"document layer".

**level** — not reserved. Two everyday senses, both kept:
- *granularity qualifier* — "cabinet-level", "chip-level", "board-level",
  "deployment-level". The dominant use; left free.
- *generic tier* — informally, a tier of the depth code. The canonical tier
  names are `main` and `sub-position` (Part 1); prefer those in normative text.
Never use "level" to mean a document layer.

**module** — four senses; the most overloaded word in the project. Always
qualify:
- *IndustryFlow `module`* — a data-model entity = a functional subsystem (a
  position concept). Write "`module` entity" or "IndustryFlow `module`".
- *sensor module / actuator module* — a hardware PCB (M01–M05; actuator boards).
- *E-module* — a buildable assembly with an E-number (Part 1).
- *commercial / ML / control module* — a software plugin (ADR-0001).

**node** —
- *field-bus participant* — a Cyphal/CAN node (carrier + sensor module). The
  primary, reserved sense (ADR-0014): "smart node", "sensor node", "Node-ID".
- *circuit node* — an electrical net / junction ("input node", ADR-0014). EE;
  always written "circuit node" / "input node".
Never use "node" for a tree vertex — that is a *vertex* (Part 1).

---

*Backref decision numbers verified against the live ADR-0017 text
(dec. 1, 3, 6, 7, 9, 11). Remaining suffix-set decisions to be confirmed when
the rest of the ADR set is in scope.*
