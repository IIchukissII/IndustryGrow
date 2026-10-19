<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0001 — IndustryGrow as an open-core cultivation platform on IndustryFlow

- **Status:** Accepted
- **Date:** accepted 2026-05-16; changed 2026-10-19
- **Related:** ADR-0002 (realises decision 8), ADR-0003 (the seed profile of decision 5), ADR-0006 (cabinet form for decision 6), ADR-0021 (core-plus-layer integration of decision 7)

## Context

IndustryGrow is a modular cultivation platform meant to scale from an apartment-sized cabinet to a commercial facility of several hundred square metres. It is the domain layer over IndustryFlow, the industrial-IoT core platform, and it extends that core with a slot entity, cultivation DSDL types and plugin interfaces. IndustryFlow has no other end-to-end deployment.

Every downstream choice — protocol, hardware, security, repository structure, licensing — depends on whether the project is personal, community open source, commercial, or a hybrid. Three forces bound the answer: development must fund itself; cultivation knowledge spreads fastest when the community can contribute it; and the first deployment must exercise the same self-build path a community member or third-party deployer takes. Aesthetics, modularity and replaceability are required at apartment scale and remain valuable at commercial scale.

## Decision

1. **The open core is licensed AGPL-3.0-or-later.** It covers the data model, ingestion, storage, basic alerting, observability, basic ML (thresholds, standard anomaly detection), reference firmware and basic control loops, and may be self-hosted, modified and operated for any purpose. The network clause obliges a service provider to share its modifications; a customer needing closed integration buys a commercial licence (dual licensing).
2. **The DSDL and protocol layer is licensed Apache-2.0.** Third parties, competitors included, can then build interoperable node hardware and firmware. Type-system fragmentation costs every participant in the ecosystem.
3. **Hardware reference designs are licensed CERN-OHL-S.** Schematics and layouts are strong-reciprocal open hardware, the counterpart of the AGPL core. Manufacturing is a separate commercial activity.
4. **Commercial modules are closed and built against open-core plugin interfaces, never as patches to the core.** Two categories: predictive ML (disease-risk forecasting, yield prediction, transpiration-based optimisation, cross-deployment learning, spectrum analysis) and advanced control (model-predictive climate control, adaptive dosing, autonomous spectrum optimisation, whole-cabinet autopilot). They differentiate on capability, not on lock-in.
5. **Community content is open by default and distributed through a shared registry.** Cultivation profiles, nutrient recipes, hardware modifications and integration examples can be forked, customised and contributed back. The strawberry day-neutral profile (ADR-0003) is the seed entry.
6. **The first deployment is a strawberry cabinet built from open-core artifacts only.** Continuous staggered cultivation on about nine slots at a two-week cadence. Excluding commercial modules validates the self-build path.
7. **Data model: the cabinet is an IndustryFlow `machine`, functional subsystems are `module` entities, slots are `production_unit` entities.** Plant positions are first-class platform entities, which requires the IndustryFlow extension ADR-IF-0001.
8. **Field instrumentation is modular smart nodes on a Cyphal/CAN bus, on one uniform hardware platform at every deployment scale.** The node platform and the bus are ADR-0002's. Uniform hardware keeps one design, one firmware and one inventory across self-build and commercial deployments.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | **A.** Personal or decorative cabinet with no platform ambition | The architecture already exceeds personal scope, validating IndustryFlow needs a generalisable shape, and a second cabinet is planned regardless |
| — | **B.** Pure closed-source product | No community contribution path, harder adoption, and conflicts with the open IndustryFlow foundation; closed-only platforms in the domain have not produced network effects |
| — | **C.** Pure open source, no commercial layer | No sustainable funding; open-source-only agtech platforms struggle to sustain development pace |
| — | **E.** Commercial first, source released later | Makes community contribution a token and starts the project on the wrong foundation |
| 1 | **D.** Business Source License instead of AGPL | More enterprise-friendly but less open; AGPL is the more established, less contested pattern among developers. Migration remains possible if AGPL proves too restrictive |
| 8 | Two hardware tiers: prototype-grade carriers for self-builders, production-grade boards for commercial deployments, one DSDL | Two carriers double engineering and documentation, mixed-grade buses impose lowest-common-denominator caveats, and clone-board supply is a long-tail risk (ADR-0002) |

## Consequences

**Positive**

- IndustryFlow gains its first real deployment, validating its data model, ingestion, alerting and ML pipeline.
- The system is replaceable component by component, from sensor to platform.
- Community-contributed cultivation knowledge accumulates as a shared commons.
- The open-core model with commercial modules is a pattern investors and grant programmes recognise.
- Growth from one cabinet to many needs no redesign.
- The AGPL network clause protects the core against extractive hosting by cloud vendors.

**Negative**

- Reaching a publicly releasable open core costs several times the effort of a personal cabinet.
- Open core requires a plugin architecture, entitlement infrastructure and licensing tooling that do not yet exist.
- Customers needing closed integration require a commercial licence, which adds a sales process.
- A community registry needs moderation, quality standards and governance as an ongoing cost.
- The platform builder is also the first deployer, so features serving only the seed cabinet are flagged as such and post-deployment reviews are written as by a third party.
- IndustryFlow's roadmap is coupled to IndustryGrow's needs, and both depend on one author.

## Deferred

| Open question | Settled by |
|---|---|
| Deployment topology and operational scale | ADR-0008 |
| Cultivation profile schema, contribution workflow, registry design | ADR-0009 |
| Operational policy for commercial managed deployments | ADR-0010 |
| Plugin architecture for commercial modules: interfaces, host pattern, isolation | ADR-0011 |
| Entitlement and licence verification | ADR-0012 |
| Repository structure and licensing per artifact | ADR-0013 |
| IndustryFlow slot entity (`production_unit`) | ADR-IF-0001 |

## References

- ADR-0002 — field bus and uniform node platform.
- `REUSE.toml`, `LICENSES/` — the per-path licence assignment.
- IndustryFlow: https://github.com/IIchukissII/industryflow
- AGPL-3.0-or-later, Apache-2.0, CERN-OHL-S-2.0 licence texts.
- Precedents: GitLab, Sentry, MongoDB, Elastic, HashiCorp (open core); Framework, MNT Reform, System76 (open hardware with commercial manufacturing).
