<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0001: IndustryGrow — open-core cultivation platform built on IndustryFlow

- **ID:** ADR-0001 (rev 1)
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow

## Context and problem

**IndustryGrow** is a modular cultivation platform that scales from an apartment-sized cabinet to a several-hundred-square-meter commercial facility. It uses IndustryFlow as its underlying industrial-IoT data platform and contributes specific extensions to it (production_unit entity, cultivation-domain DSDL types, plugin interfaces).

I am a mechatronics engineer and the author of IndustryFlow. The decision before this ADR is the *shape* of IndustryGrow as a project: who its users are, what is open vs commercial, what artifacts it produces, and what the first deployment looks like.

The decision is made now rather than later because every downstream choice — protocol selection, hardware tiering, security architecture, repository structure, licensing — depends on whether IndustryGrow is a personal artifact, a community open-source project, a commercial product, or a hybrid.

## Decision drivers

- The system must scale: from a single furniture-grade cabinet in an apartment to commercial cultivation facilities. Architecture must support both ends; implementation starts at the small end.
- The first deployment (a strawberry cabinet in my apartment) must validate the self-build path — the same path any community member or third-party deployer would take.
- IndustryFlow has no end-to-end real deployment yet. IndustryGrow becomes its first.
- Community contribution is a content-distribution mechanism, especially for cultivation knowledge (profiles, recipes, nutrient mixes). A "GitHub for cultivation know-how" is a strategically valuable layer.
- Commercial sustainability matters. This is not a hobby in the long run; it must support its own development.
- Aesthetics, modularity, and replaceability are non-negotiable for the apartment-scale deployment and remain valuable at commercial scale.
- I prefer to commit framing decisions before technological ones. Solution before implementation.

## Decision

IndustryGrow is an **open-core cultivation platform** with the following structure:

1. **Open core (AGPL-3.0-or-later).** The platform itself — data model, ingestion, storage, basic alerting, observability, basic ML (threshold and standard anomaly detection), reference firmware, hardware reference designs, DSDL types, Cyphal stack, basic control loops. Anyone may self-host, modify, and operate IndustryGrow for any purpose, including commercial. The AGPL clause requires service-providers to share their modifications, which protects against AWS-style strip-mining of the open core. Enterprise customers needing closed integration purchase a commercial license — classical dual-licensing pattern.

2. **DSDL and protocol layer (Apache 2.0).** Permissively licensed so that third parties — including potential commercial competitors — can build interoperable smart-node hardware and firmware. This is deliberate: type-system fragmentation hurts the whole ecosystem.

3. **Hardware reference designs (CERN-OHL-S).** Schematics and PCB layouts for both prototype-grade and production-grade nodes are published under strong-reciprocal open hardware license, paired ideologically with AGPL. Manufacturing is a separate commercial activity (open hardware + commercial manufacturing — the Framework / MNT Reform pattern).

4. **Commercial closed modules (proprietary EULA).** Two categories, both built against open-core plugin interfaces, never as patches to the core:
   - **Predictive ML modules** — botrytis risk forecasting, yield prediction, transpiration-based optimization, cross-deployment learning, A/B-spectrum analysis.
   - **Advanced control modules** — Model Predictive Control for climate, adaptive nutrient dosing, autonomous spectrum optimization, autopilot for whole-cabinet operation.

5. **Community-contributed content (open by default).** Cultivation profiles, nutrient recipe mixes, hardware mods, integration examples — published to a shared registry by community members. The registry pattern is "GitHub for cultivation know-how": anyone can fork, customize, and contribute back. The strawberry day-neutral profile (ADR-0003) is one seed entry; lettuce, basil, microgreens, and other profiles come from the community.

6. **First deployment: strawberry cabinet.** Continuous staggered cultivation, ~9 slots, 2-week cadence. Built from open-core artifacts only (no commercial modules) to validate the self-build path that defines IndustryGrow's accessibility.

7. **Data model: machine = cabinet, modules = functional subsystems, slots = production units.** Plant positions are first-class entities in IndustryFlow's data model, requiring platform extension (tracked as ADR-IF-0001).

8. **Field instrumentation via modular smart nodes** on a Cyphal/CAN bus, with phased hardware tiering (prototype-grade BluePill carriers for self-builders, production-grade STM32G4 PCBs for commercial deployments — both speak the same DSDL). Details in ADR-0002.

   > **Amended by ADR-0002 rev 3 (2026-05-16):** the phased two-tier hardware model (BluePill prototype / STM32G4 production) was rejected in favour of a single uniform smart-node platform — a WeAct STM32F4 core board on one carrier PCB — across all deployment scales. Uniformity superseded tiering; see ADR-0002 rev 3 decision 2 and alternative A. The "both grades" wording in the deferred-decisions list below, and the hardware-tiering scope of the planned ADR-0010, are superseded accordingly.

## Alternatives considered

**A. Stay personal / decorative.** A single strawberry cabinet, no platform ambition. *Rejected:* the architecture being designed already exceeds personal scope, the IndustryFlow validation goal demands a generalizable shape, and a second cabinet is in the planning horizon regardless.

**B. Pure closed-source product.** All code, hardware, and content closed. *Rejected:* no community contribution path, harder to seed adoption, conflicts with author's values and with the IndustryFlow open foundation. Closed-only platforms in this domain have not produced the network effects of open-platform players.

**C. Pure open-source, no commercial layer.** Everything AGPL-3, no commercial modules. *Rejected:* no sustainable funding model. Long-term development requires income; existing open-source-only platforms in agtech struggle to sustain development pace.

**D. BSL (Business Source License) instead of AGPL.** Time-bounded restriction on commercial use, converts to Apache 2.0 after 3–4 years. *Considered:* more enterprise-friendly, less ideologically open. *Rejected:* AGPL has a clearer message and a more established pattern in the developer community; BSL still triggers skepticism in some segments. Migration to BSL remains possible if AGPL proves too restrictive in practice.

**E. Commercial-first with optional source release.** Build closed, open the source later as a marketing move. *Rejected:* signals the wrong values, makes community contribution feel like a token, and starts the project with the wrong cultural foundation.

## Consequences

### Positive

- IndustryFlow gets its first real deployment, with a clear validation path for the platform's data model, ingestion, alerting, and ML pipeline.
- The system is replaceable component-by-component, from sensor to platform layer.
- Community contribution mechanism for cultivation knowledge becomes a self-amplifying knowledge commons.
- Commercial modules differentiate on value (predictive intelligence, autonomous control) rather than on lock-in.
- Open-core model is fundable. AGPL+commercial-modules is a pattern that investors and grant programs (including EXIST) recognize.
- The architecture supports growth from one-cabinet to many-cabinet without redesign.
- AGPL protects against extractive cloud-vendor competition.
- IndustryGrow as a name leaves room for additional verticals on the IndustryX family (IndustryHealth, IndustryFarm, etc.) in the future.

### Negative

- Build cost and time are materially higher than personal-project scope. Rough estimate: 3–5× the personal-cabinet effort to reach a publicly-releasable open-core platform.
- Open-core requires real plugin architecture, entitlement infrastructure, and licensing tooling. These are new components, tracked as future ADRs.
- AGPL is enterprise-shy in some segments. Commercial customers who need closed integration must purchase a separate commercial license. This adds a sales motion that does not yet exist.
- Community management is an ongoing cost. A registry of community-contributed content requires moderation, quality standards, and governance.
- The author is the platform builder AND the first deployer. Bias toward "the platform supports my use case" is real. Mitigation: features needed only by my cabinet are flagged as such; post-deployment retrospectives are written as if by a third party.
- IndustryFlow extensions required for IndustryGrow couple the platform's roadmap to this project's needs. Both projects share the author, which mitigates in practice but creates a conceptual single point of failure.

## Deferred decisions

- **ADR-0002:** Field bus protocol, smart-node hardware (both grades), gateway architecture.
- **ADR-0003:** Strawberry day-neutral cultivation profile (reference profile #1).
- **ADR-0004:** Gateway host hardening, audit log, firmware signing.
- **ADR-0005:** DSDL namespace structure and Subject-ID allocation.
- **ADR-0006 (deferred):** Cabinet form factor, materials, mechanical decomposition.
- **ADR-0007 (future):** PKI architecture — community-self-hosted CA pattern and commercial CA infrastructure.
- **ADR-0008 (future):** Deployment topology and operational scale.
- **ADR-0009 (future):** Cultivation profile schema, contribution workflow, registry design.
- **ADR-0010 (future):** ~~Hardware tiering — full specification of production-grade nodes.~~ Repurposed to commercial-operations / managed-deployment policy; the hardware-tiering premise was dropped by ADR-0002 rev 3 (uniform single-tier hardware). See ADR-0002 deferred list and README.
- **ADR-0011 (future):** Plugin architecture for commercial modules — interfaces, host pattern, isolation.
- **ADR-0012 (future):** Entitlement and license verification.
- **ADR-0013 (future):** Repository structure and licensing per artifact.
- **ADR-IF-0001:** IndustryFlow data model extension — production_unit entity.

## References

- IndustryFlow repository: https://github.com/IIchukissII/industryflow
- AGPL-3.0-or-later license text: https://www.gnu.org/licenses/agpl-3.0.html
- CERN-OHL-S open hardware license: https://cern-ohl.web.cern.ch/
- Apache License 2.0: https://www.apache.org/licenses/LICENSE-2.0
- Open-core business model precedents: GitLab, Sentry, MongoDB (pre-SSPL), Elastic (pre-relicense), HashiCorp (pre-BSL).
- Hardware open-source + commercial manufacturing precedents: Framework Computer, MNT Reform, System76.
