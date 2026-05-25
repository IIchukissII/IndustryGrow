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

### Profile metadata

- **Crop:** *Fragaria × ananassa*, day-neutral varieties.
- **Operating mode:** continuous staggered cultivation, 9-slot rolling pipeline, 2-week cadence.
- **Profile version:** 1.0
- **License:** CC-BY-SA 4.0 (Creative Commons, attribution + share-alike).
- **Status:** reference profile; expected to be empirically refined through the first 1–2 cycles of operation.

### Variety

1. **Day-neutral strawberry, primary cultivar: Albion.** Selected for flavour, fruit size, disease resistance, and well-documented behaviour under controlled environments.
2. **Fallback cultivars (if Albion unavailable):** Seascape, San Andreas, Monterey, Portola — all day-neutral, comparable controlled-environment data.

### Cycle structure

3. **Per-plant cycle: 19 weeks total** = 6 weeks pre-fruit (vegetative + flowering) + 12 weeks productive fruiting + 1 week decline/replacement.
4. **Slot rotation: 9 slots, offset by ~2 weeks each.** At steady state, 6 slots in fruiting, 3 in pre-fruit. Every 2 weeks, one slot completes its cycle and is replanted.
5. **Source of new plants:** Internal propagation. 1–2 dedicated mother plants in a separate cooler zone produce runners (stolons), rooted in the propagation area before transplant.

### Climate

6. **Air temperature setpoints:**
   - Day (photoperiod active): 20 °C
   - Night (photoperiod off): 14 °C
   - The day/night differential is not cosmetic — without it, fruit accumulates less sugar.
7. **Humidity:** Controlled via VPD, not RH directly.
   - **Target VPD: 0.8–1.2 kPa** for both photoperiods.
   - This range suppresses *Botrytis cinerea* while supporting healthy transpiration. VPD is the primary regulated parameter; RH is a derived consequence.
8. **CO₂: ambient.** No enrichment in the initial build. Reserved for a profile variant if yield experiments justify it.

### Light

9. **Photoperiod: 16h on / 8h off.** Day-neutral varieties tolerate any photoperiod; 16h maximises daily light integral (DLI).
10. **Sunrise/sunset emulation:** Intensity and spectrum ramp over 30 minutes at each photoperiod transition.
11. **Spectrum, by plant phase:**
    - *Vegetative:* warm white (~3500 K) + minor blue addition for compact growth.
    - *Flowering and fruiting:* warm white + 660 nm red boost (drives flowering and fruit set via phytochrome) + 730 nm far-red trickle (improves fruit set) + UV-A 365–385 nm trace (stimulates flavonoid pathways → increases sugar and aroma).
12. **DLI target: 17–20 mol·m⁻²·day⁻¹** at the canopy of fruiting plants.

### Pollination

13. **Mechanical pollination via dedicated low-speed fan** over the flowering zone. 60-second pulses every hour during photoperiod. No insect introduction. No manual brushing.

### Nutrition

14. **Method: hydroponic.** Specific topology (NFT, Dutch-bucket, hybrid) deferred to ADR-0006 once the cabinet mechanical decomposition is decided.
15. **EC and pH targets by phase:**
    - *Vegetative:* EC 1.2–1.4 mS/cm, pH 5.8–6.2, N-emphasised formulation.
    - *Flowering and fruiting:* EC 1.6–1.8 mS/cm, pH 5.8–6.2, K-emphasised formulation with deliberate calcium loading.
16. **Calcium prioritised** — deficiency causes tipburn and poor fruit quality. The flowering/fruiting recipe explicitly maintains Ca:K ratio above the typical leafy-greens hydroponic baseline.
17. **Dosing approach:** Two-part A/B nutrient concentrate, peristaltic pumps, recipe switched per slot phase. Detailed mix specifications are a separate operational artifact (planned for the open recipe registry, see ADR-0009).

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

- ADR-0001: IndustryGrow framing and community-contributed content model.
- ADR-0002: Field bus (the actuators and sensors that enforce these parameters).
- ADR-0009: Cultivation profile schema, contribution workflow, registry design.
- Strawberry cultivation under controlled environments — published literature on Albion, Seascape, and other day-neutral cultivars (to be linked in the operational specification).
- VPD-based humidity management in indoor agriculture (general reference).
- [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
