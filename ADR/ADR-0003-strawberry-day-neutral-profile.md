<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0003: Strawberry day-neutral cultivation profile (reference profile)

- **ID:** ADR-0003
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Profile type:** Cultivar-specific cultivation profile
- **Profile identifier:** `strawberry-day-neutral-v1`
- **Profile license:** CC-BY-SA 4.0 (open, community-contributable)

## Context and problem

ADR-0001 committed IndustryGrow to a model where **cultivation profiles** are configurable artifacts loaded into a machine at deployment time. A profile encodes the biological and environmental parameters for cultivating one crop under one operating mode. Many profiles will exist; the community contributes them through a registry (the GitHub-for-recipes model). Profiles are open by default (CC-BY-SA 4.0); commercial *advanced control modules* may produce tuned variants of profiles informed by ML and A/B testing, and those variants may be closed by their authors.

This ADR specifies the **first reference profile** — for day-neutral strawberries under continuous staggered cultivation. It is the profile loaded into the seed cabinet (ADR-0001 decision 6) and serves three purposes:

1. Define cultivation parameters for the first deployment.
2. Provide a worked example for community contributors of what a profile looks like.
3. Validate the profile schema (specified in ADR-0009) against a real, non-trivial crop.

Profile schema, contribution workflow, registry mechanics, and versioning rules are out of scope here — they belong to ADR-0009.

## Decision drivers

- 2-week harvest cadence as the operational target (per ADR-0001).
- Day-neutral varieties required for cadence-driven production.
- Closed indoor environment with no natural pollinators.
- Apartment scale: small reservoirs, low noise tolerance, no exhaust to garden, no natural humidity sink.
- Parameters must be controllable by the available sensor/actuator subsystems and must not require ambient conditions the apartment cannot provide.
- A/B experiments will operate on *deviations* from these baselines; the baseline must be well-defined to make deviations measurable.
- The profile must be a worked example clear enough for community contributors to follow when authoring their own profiles.

## Decision

A deployment is **roots + leaves** — the IndustryGrow plant. **Roots** are the hardware/apparatus that realize cultivation (carrier and sensor modules per ADR-0002 / ADR-0014, power and metering per ADR-0018) — the *apparatus subspace* of ADR-0016. **Leaves** are the biological parameters the plant responds to — the *biological subspace* of ADR-0016. A cultivation profile **specifies the leaves and binds to — does not re-specify — the roots.**

The concrete leaf values for this profile live in `profiles/strawberry-day-neutral-v1.json`, the single source of truth for setpoints (ADR-0000 dec 2; ADR-0015). This ADR records the *structure* of a day-neutral strawberry profile and the *decision behind each section*; it does not restate the values. Profile schema and registry mechanics are deferred to ADR-0009; the JSON is the reference instance that anticipates that schema.

### Profile metadata

- **Crop:** *Fragaria × ananassa*, day-neutral varieties.
- **Operating mode:** continuous staggered cultivation, multi-slot rolling pipeline (slot count, offset, and cadence in the profile instance).
- **License:** CC-BY-SA 4.0 (open, community-contributable).
- **Status:** reference profile; expected to be empirically refined through the first 1–2 cycles of operation.

### Variety

1. **A day-neutral cultivar with documented controlled-environment behaviour.** Day-neutral is required because the operating mode is cadence-driven rather than seasonal; selection weighs flavour, fruit size, and disease resistance.
2. **Day-neutral fallback cultivars** for supply resilience, chosen for comparable controlled-environment data. (Cultivar names → profile instance.)

### Cycle structure

3. **A fixed per-plant cycle of vegetative → fruiting → decline.** (Phase durations → profile instance.)
4. **A staggered multi-slot rolling pipeline,** offset so one slot completes its cycle and is replanted each cadence interval; steady-state allocation favours fruiting. (Slot count, offset, cadence, allocation → profile instance.)
5. **New plants from internal propagation** — dedicated mother plants in a separate cooler zone produce runners (stolons), rooted before transplant.

### Climate (leaves)

6. **A day/night temperature differential, not a constant.** The diurnal drop is not cosmetic — without it, fruit accumulates less sugar. (Setpoints → profile instance.)
7. **Humidity regulated by VPD, not RH.** VPD is the physically meaningful moisture-driving pressure and the primary regulated parameter; RH is a derived consequence. The band suppresses *Botrytis cinerea* while supporting healthy transpiration. (VPD band → profile instance.)
8. **CO₂ runs at ambient** in the reference build; enrichment is a separate profile variant, not part of this baseline.

### Light (leaves)

9. **A long photoperiod.** Day-neutral varieties tolerate any photoperiod; the long day maximises daily light integral (DLI). (Hours → profile instance.)
10. **Ramped sunrise/sunset transitions** in intensity and spectrum at each photoperiod boundary. (Ramp duration → profile instance.)
11. **A phase-dependent spectrum.** Vegetative spectrum favours compact growth; flowering/fruiting adds a red boost (flowering and fruit set via phytochrome), a far-red trickle (fruit set), and a UV-A trace (flavonoid pathways → sugar and aroma). (Spectral bands → profile instance.)
12. **A DLI target at the canopy of fruiting plants.** (Target band → profile instance.)

### Pollination (leaves)

13. **Mechanical pollination by airflow** over the flowering zone, pulsed during the photoperiod — no insect introduction, no manual brushing, consistent with the system's autonomy goal. (Pulse timing → profile instance.)

### Nutrition (leaves; root-zone chemistry)

14. **Hydroponic delivery,** chosen over substrate for the EC/pH control signal it exposes. Specific topology (NFT, Dutch-bucket, hybrid) deferred to ADR-0006 once the cabinet mechanical decomposition is decided.
15. **Phase-dependent EC, pH, and formulation** — N-emphasis in vegetative, K-emphasis in flowering/fruiting. These are biological targets the plant responds to (leaves), not hardware. (Bands → profile instance.)
16. **Calcium prioritised** — deficiency causes tipburn and poor fruit quality; the flowering/fruiting formulation holds Ca:K above the typical leafy-greens hydroponic baseline.
17. **Dosing as a two-part A/B concentrate via peristaltic pumps** (the *roots* that deliver the leaf targets), switched per slot phase. Detailed mix specifications are deferred to the open recipe registry (ADR-0009).

## Relationship to other profiles

This profile is one entry in IndustryGrow's profile registry. The platform is crop-agnostic; profiles will be authored by the community for any crop the community chooses — fruiting crops (tomatoes, peppers, cucumbers, chillies), leafy greens (lettuce, basil, microgreens), ornamentals, medicinal herbs, mushrooms, or whatever else has a controllable cultivation regime — using the schema defined in ADR-0009. All community-contributed profiles are open under CC-BY-SA 4.0 by default; contributors may also license their profiles under other open licenses if compatible with the registry's distribution model.

*Tuned variants* of this profile — produced by commercial advanced control modules (ADR-0001 decision 4) through ML-driven A/B optimization — may be closed by their authors. The base reference profile (this document) remains open, and tuned variants are derived works marked as such.

## Alternatives considered

**A. June-bearing varieties** (one large flush per season). *Rejected:* incompatible with continuous cadence by definition.

**B. Constant temperature, no day/night differential.** *Rejected:* the diurnal drop is necessary for sugar accumulation; well-documented.

**C. RH-based humidity control instead of VPD.** *Rejected:* RH alone does not capture the actual moisture-driving pressure. VPD is the physically meaningful parameter.

**D. 12/12 photoperiod.** *Rejected:* day-neutral varieties don't require it; shorter photoperiod caps achievable DLI without corresponding benefit.

**E. Manual hand pollination with a brush.** *Rejected:* requires daily presence, fails on absences, contradicts the system's autonomy goals.

**F. Substrate (coco coir or peat) rather than hydroponic.** Considered as a quieter, lower-maintenance alternative. Trades off data richness — substrate doesn't give an EC/pH signal to control. For an experiment-first project, hydroponic data-richness wins. *Substrate variant remains a viable separate profile* if a community contributor chooses to author one.

**G. CO₂ enrichment to 800–1200 ppm from day one.** Higher yield potential. *Deferred:* adds CO₂ tank/regulator, monitoring, and leak-safety considerations not in scope for the initial build. Could become a separate profile variant.

## Consequences

### Positive

- Clear, testable parameter targets for every subsystem.
- A/B experiments operate on well-defined baselines.
- Worked example for community contributors authoring future profiles.
- Profile is open and contributable; corrections from operational experience flow back as updates.

### Negative

- Parameters are literature-derived, not empirically fitted to *this* cabinet's geometry, lighting, and airflow. Expect the first 1–2 cycles to be calibration; yield reduced during this learning phase.
- Hydroponic operation is data-rich but maintenance-active.
- VPD-first control requires both temperature and humidity sensors paired with a derived computation; the climate node firmware must implement this.
- Spectrum specification (decision 11) requires a multi-channel LED driver capable of independently controlling at least four channels.

## Deferred decisions

- Detailed nutrient salt-mix recipes — published as a separate artifact in the recipe registry rather than embedded here.
- Exact spectrum channel ratios (e.g., 70/20/8/2) — deferred until LED hardware is selected.
- VPD enforcement strategy when apartment ambient humidity is uncontrolled (e.g., 30% RH in winter) — climate module design concern.
- Mother plant runner-induction protocol — temperature schedule, daylength manipulation for stolon production.

## References

- `profiles/strawberry-day-neutral-v1.json`: the loadable profile instance — single source of truth for this profile's setpoints (leaves).
- ADR-0001: IndustryGrow framing and community-contributed content model.
- ADR-0002: Field bus (the actuators and sensors that enforce these parameters).
- ADR-0009: Cultivation profile schema, contribution workflow, registry design.
- Strawberry cultivation under controlled environments — published literature on Albion, Seascape, and other day-neutral cultivars (to be linked in the operational specification).
- VPD-based humidity management in indoor agriculture (general reference).
- [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
