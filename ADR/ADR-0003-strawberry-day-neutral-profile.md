<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0003 — Strawberry day-neutral reference cultivation profile

- **Status:** Proposed
- **Date:** proposed 2026-05-16; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0006 (hydroponic topology and loop count for decisions 14–17), ADR-0009 (profile schema and registry this instance anticipates), ADR-0014 (sensing that enforces these targets), ADR-0015 (the gateway that executes the profile), ADR-0016 (biological and apparatus subspaces), ADR-0032 (climate apparatus that holds decisions 6–8)

## Context

A cultivation profile is a configurable artifact loaded into a machine: the biological and environmental parameters for one crop under one operating mode. Profiles are community content, open by default (ADR-0001 dec 5); tuned variants produced by commercial control modules (ADR-0001 dec 4) are derived works and may be closed by their authors. This record is the first reference profile: day-neutral strawberries under continuous staggered cultivation, loaded into the seed cabinet (ADR-0001 dec 6). It sets the first deployment's parameters, gives contributors a worked example, and tests the profile schema against a non-trivial crop.

A deployment is roots and leaves. Roots are the apparatus that realises cultivation, the apparatus subspace of ADR-0016; leaves are the biological parameters the plant responds to, its biological subspace. A profile specifies the leaves and binds to the roots without re-specifying them. The profile instance holds every value (ADR-0000 dec 2); this record holds the structure and the reason for each section.

The constraints: a two-week harvest cadence; a closed indoor volume with no pollinators; apartment scale, with small reservoirs, low noise tolerance and no exhaust or humidity sink; and a baseline defined well enough that A/B experiments can measure deviations from it.

## Decision

1. **A day-neutral cultivar with documented controlled-environment behaviour.** The operating mode is cadence-driven, not seasonal; selection weighs flavour, fruit size and disease resistance.
2. **Day-neutral fallback cultivars with comparable controlled-environment data.** They hedge supply.
3. **A fixed per-plant cycle: vegetative, fruiting, decline.** Phase durations are in the profile instance.
4. **A staggered multi-slot rolling pipeline.** Slots are offset so one slot completes and is replanted each cadence interval; steady-state allocation favours fruiting.
5. **New plants come from internal propagation.** Mother plants in a separate, cooler zone produce runners, rooted before transplant.
6. **A day/night temperature differential, not a constant.** Without the night drop the fruit accumulates less sugar.
7. **Humidity is regulated by VPD, not RH.** VPD is the moisture-driving pressure; its band suppresses *Botrytis cinerea* while supporting transpiration, and RH follows from it.
8. **CO₂ runs at ambient in the reference build.** Enrichment is a separate profile variant.
9. **A long photoperiod.** Day-neutral varieties tolerate any photoperiod, and a long day maximises daily light integral (DLI).
10. **Ramped sunrise and sunset in intensity and spectrum at each photoperiod boundary.**
11. **A phase-dependent spectrum.** The vegetative spectrum favours compact growth; flowering and fruiting add a red boost (flowering and fruit set via phytochrome), a far-red trickle (fruit set) and a UV-A trace (flavonoid pathways, sugar and aroma).
12. **A DLI target at the canopy of fruiting plants.**
13. **Pollination by pulsed airflow over the flowering zone during the photoperiod.** It needs no insects and no manual brushing, consistent with autonomous operation.
14. **Hydroponic delivery.** It exposes an EC/pH control signal that substrate does not; the topology is ADR-0006's.
15. **Phase-dependent EC, pH and formulation: N emphasis in vegetative, K emphasis in flowering and fruiting.** These are biological targets (leaves), not hardware.
16. **Calcium is prioritised.** Deficiency causes tipburn and poor fruit; the flowering and fruiting formulation holds Ca:K above the leafy-greens hydroponic baseline.
17. **Dosing is a two-part A/B concentrate by peristaltic pumps, switched per slot phase.** These are the roots that deliver decision 15; mix specifications belong to the recipe registry (ADR-0009).

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **A.** June-bearing varieties | One flush per season is incompatible with a continuous cadence |
| 6 | **B.** Constant temperature, no day/night differential | The night drop is required for sugar accumulation |
| 7 | **C.** RH-based humidity control | RH does not capture the moisture-driving pressure |
| 8 | **G.** CO₂ enrichment to 800–1200 ppm from the start | Adds tank, regulator, monitoring and leak safety outside the initial build's scope; a candidate profile variant |
| 9 | **D.** 12/12 photoperiod | Not required by day-neutral varieties; caps DLI with no benefit |
| 13 | **E.** Manual brush pollination | Needs daily presence, fails on absence, contradicts autonomy |
| 13 | Insect pollinators | The closed indoor volume has none, and introducing them conflicts with an apartment cabinet |
| 14 | **F.** Substrate (coco coir, peat) | Quieter and lower-maintenance but gives no EC/pH signal to control; remains a candidate separate profile |

## Consequences

**Positive**

- Every subsystem has a testable target, and A/B experiments run against a defined baseline.
- Contributors have a worked example for authoring profiles.
- The profile is open, so corrections from operation flow back as updates.

**Negative**

- Values are literature-derived, not fitted to this cabinet's geometry, lighting and airflow; the first one or two cycles are calibration at reduced yield.
- Hydroponic operation is data-rich and maintenance-active.
- VPD-first control needs paired temperature and humidity sensing and a derived computation on the node.
- Decision 11 needs a luminaire driver with at least four independently controlled channels.

## Deferred

| Open question | Settled by |
|---|---|
| Nutrient salt-mix recipes | Recipe registry (ADR-0009) |
| Spectrum channel ratios | Luminaire selection (`O-114`) |
| Night VPD band: its dry edge is unreachable with removal-only humidity control (ADR-0032 dec 4) | Profile revision (`O-108`) |
| Mother-plant runner-induction protocol: temperature schedule, daylength manipulation | Profile revision |

## References

- `profiles/strawberry-day-neutral-v1.json` — the profile instance; every value.
- `spec/E0011-R-specification.md` — climate apparatus, `O-108`, `O-114`.
- Controlled-environment literature on day-neutral cultivars; VPD-based humidity management.
- CC-BY-SA 4.0: https://creativecommons.org/licenses/by-sa/4.0/
