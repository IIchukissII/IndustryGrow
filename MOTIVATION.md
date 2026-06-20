<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow — Motivation

- **Status:** Statement
- **Date:** 2026-06-20
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0016; `project/RESEARCH.md`; `project/ROADMAP.md`

---

A growing space is an unsolved system.

It can be measured, heated, lit, and watered. Models of the pieces exist —
photosynthesis, transpiration, crop growth — yet none stitches the physics of a
given environment to the biology of the plant inside it, none carries from one
deployment to the next, and none stays open to inspection. No method reliably
produces such a model. The knowledge that makes cultivation succeed lives in a
grower's intuition or a vendor's black box — not in anything that can be
inspected, reproduced, or improved.

The gap is structural, and it has five faces.

There is no full model. The physics of the environment and the biology of the
plant are modeled in separate literatures; the join between them — the thing an
operator needs — does not exist. Every deployment re-derives it empirically and
discards the result.

The methodology is fragmentary. No agreed procedure takes an arbitrary growing
space and returns a validated model of it. Excitation, sensor placement,
identification, and drift detection are each solved in isolation in adjacent
disciplines and assembled nowhere.

Most processes are unobserved. The variables that drive the outcome —
microclimate gradients, transpiration coupling, cultivar response, actuators that
physically fight one another — are studied under conditions that do not transfer,
or not written down at all. The state of a deployment is mostly unmeasured.

Data is scarce. Real sites instrument minimally, experiments are bound to the
length of a crop cycle, and cross-deployment datasets barely exist. The learning
loop is gated by biology: one cycle is one data point.

The fields do not meet. Identification, estimation, control, fluid dynamics,
horticulture, and agronomy each optimize their own slice. The interconnections
between them are where the value lies and where the literature is thinnest.

The pressure to close this gap is now. Climate change pushes production into
controlled environments, which are energy-intensive and therefore viable only
under efficient, model-based operation. Cities separate food from where it is
eaten, and local cultivation scales only when a site can be commissioned without
a specialist. Commodity hardware and open protocols put the instrument within
reach for the first time.

IndustryGrow is that instrument.

It turns a growing space into a measured, controllable, identifiable system. It
does not assert the missing model; it produces it — surveying a deployment
densely, identifying a reduced-order model of its dynamics, operating it lean on
the minimum sensing that keeps it observable, and carrying the model forward
inside a versioned profile. The contribution is integration and method, not
novelty in any single component. One architecture and one data vocabulary place
every discipline on a common substrate, so their joins can be built and studied.

The same architecture serves three horizons. The cabinet proves the method. The
deployment turns it into practice. The city is the far reading — distributed
cultivation as a managed utility, modular units sharing a common library of
models and profiles, responsive to local energy, grounding municipal food
resilience.
