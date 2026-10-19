<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0000 — Decision records and the single-source-of-truth discipline

- **Status:** Accepted
- **Date:** accepted 2026-06-12; changed 2026-10-19
- **Parent:** — (root; governs the form of every other ADR)
- **Related:** ADR-0001 (origin of the IndustryFlow core / IndustryGrow layer split that decision 8 names)

## Context

The project's reasoning spans ADRs, BOMs, schematics, pin-maps, the identifier registry and cultivation profiles. A fact held in more than one of them has more than one place to drift, and a discipline that is not written down cannot be cited in review or inferred by a new contributor.

The standard for the record itself is M. Nygard, "Documenting Architecture Decisions" (2011): a record states the decision in force and why; the history of the decision lives in version control.

## Decision

1. **Architectural and design decisions are recorded as ADRs.** A decision that constrains downstream artifacts — protocol, hardware, security, identification, control — is captured in an ADR before it reaches BOMs, schematics or code. The ADR formalises the outcome of discussion, never the reverse.
2. **ADRs own the why; downstream documents own the what.** An ADR holds context, decision, rejected alternatives and consequences. Concrete values — part numbers, component values, pin assignments, setpoints — live in the document whose job is that value.
3. **Every fact has exactly one authoritative home.** No value is mirrored into a second document; a document that needs it references its home. Zero copies is the only count that cannot drift.
4. **A downstream document never silently overrides an ADR.** A divergence is resolved by changing the ADR deliberately, or by changing the downstream document back.
5. **An ADR states the decision in force; its history lives in version control.** A decision that is untrue or whose terms change is edited in place to the current truth, the replaced choice becomes an Alternatives row with the reason it is rejected, and the `Date` field records the change. A decision reversed or replaced as a whole takes a new ADR, and the old one is marked `Superseded by ADR-NNNN` and kept on file.
6. **The governance root is not mirrored into the ADRs it governs.** Individual ADRs do not back-reference this record; the relationship is inherited, not copied.
7. **Status lifecycle and accepting authority.** An ADR is `Proposed` while under discussion, `Accepted` once the maintainers judge it binding, and `Superseded` when a later ADR replaces it. Acceptance records agreement, not implementation, and is effected through review and merge.
8. **ADRs are namespaced by owning project.** An IndustryGrow decision is `ADR-NNNN` in this repository; an IndustryFlow decision is `ADR-IF-NNNN` in the IndustryFlow repository. A decision is filed under the project that owns it, and a cross-project citation points across the boundary without creating a local placeholder.
9. **Decision numbers are stable.** Code and documents cite `ADR-NNNN dec N`, so a decision is never renumbered, merged or split without updating every citation. A new decision takes the next free number; a withdrawn one stays as `N. *(withdrawn: see …)*` and its number is never reused.
10. **Every ADR has one shape.** Metadata (Status, Date, optional Parent, Related), then Context, Decision, Alternatives, Consequences, Deferred, References; in present tense and abstract register, with file paths, routes, symbols and commands left to the artifacts that own them. A record that needs much more than ~100 lines holds several decisions and is split.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | Leave the discipline as unwritten convention | Cannot be cited in review or inferred by a new contributor; enforced by memory, not process |
| — | Fold this governance into ADR-0001 | ADR-0001 owns product, licensing and data-model scope; methodology mixed into it corrupts that scope |
| — | Keep the discipline only in tooling configuration or author notes | Neither is the repository's source of truth; a rule about where truth lives must live there |
| 3 | Controlled duplication with a "primary copy" marker | Markers rot and copies drift between syncs; a reader cannot tell a stale copy from a current one |
| 5 | Supersede by revision: `(rev N)` title, `Supersedes:` line, revision history and reason narrated in the record | The record fills with history the version control already holds; the decision in force is buried under its own past |
| 5 | Edit in place on a Proposed draft only | Forces revision ceremony on corrections to an Accepted record that change no decision's intent |
| 8 | One shared number space across both projects | Forces cross-repository coordination for every number and blurs core/layer ownership |
| 8 | Record the namespace rule in ADR-0001 or only in the glossary | The rule governs the identity of decision records, which is this record's scope; the glossary points, it does not originate |
| 9 | Bounded in-place amendment: a dated added decision listed under an `Amendments` revision-history entry | The date and the entry are history; version control records when a decision was added |
| 10 | Free-form records under a shared convention | Sections drift between records and length grows without bound |

## Consequences

**Positive**

- A decision's rationale has one place to look and a value has one place to change; "is this duplicated?" is a mechanical review question.
- A reader sees only the decision in force, without reconstructing it from revisions.
- An `ADR-IF-` citation reads unambiguously as a cross-project reference.

**Negative**

- Obtaining a value sometimes means following a reference to its home rather than reading it where it is used.
- The reason a decision changed is read from the change's pull request and commit, not from the record.
- An `ADR-IF-` record cannot be resolved within this repository alone.

## Deferred

| Open question | Settled by |
|---|---|
| A linter that flags a value duplicated across documents or a reference that does not resolve | Maintainers, when duplication is found in review more than occasionally |
| A map of which document owns which class of fact | Maintainers, when a contributor cannot find a value's home |

## References

- ADR-0001 — origin of the core/layer split decision 8 expresses.
- `GLOSSARY.md` — binds the `ADR-` / `ADR-IF-` tokens to decision 8.
- M. Nygard, "Documenting Architecture Decisions" (2011).
