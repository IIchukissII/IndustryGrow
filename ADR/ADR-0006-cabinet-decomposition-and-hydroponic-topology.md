<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0006 — Cabinet decomposition and hydroponic topology

- **Status:** Accepted
- **Date:** accepted 2026-08-23; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0003 (decisions 14–17 drive the topology and loop count; decision 14's topology is settled here), ADR-0014 (decision 4's M03 entry changes with decisions 5, 7 and 8), ADR-0015 (software-mediated leak and pump response, decision 16), ADR-0016 (solution temperature in the apparatus subspace), ADR-0017 (E-number at design commit), ADR-0018 (the distribution volume and the M05 leak lead)

## Context

The cabinet's mechanics must serve the cultivation shape the profile fixes: a staggered pipeline of slots at a two-week offset, a per-plant cycle many times the offset, a steady state holding pre-fruit and fruiting slots together, phase-dependent EC bands and formulations, two-part peristaltic dosing switched per slot phase, and mother plants in a cooler zone (ADR-0003 dec 3–5, 15, 17; values in the profile instance). The M03 specification cannot state a sensor complement until the topology is fixed.

Those values contain a conflict no layout avoids: pre-fruit and fruiting slots run concurrently, ADR-0003 dec 15 bands EC differently for each phase, and one reservoir carries one EC and one formulation. Further constraints: apartment scale (small reservoirs, low noise, no exhaust); aesthetics, modularity and replaceability at that scale (ADR-0001); one slot emptied and replanted every cadence interval indefinitely; leak and pump response that is software-mediated over Cyphal and so takes minutes to hours, not seconds (ADR-0015 dec 16); and the same architecture at greenhouse scale with only instance counts changing.

## Decision

1. **This record decides shape, not dimensions.** Form-factor class, material class, volume decomposition, topology and loop count are decided here; dimensions, cut list, materials by trade name and slot interface geometry belong to a mechanical specification under an E-number assigned at design commit (ADR-0017 dec 3, 5). Millimetres here would make this record the home of values a specification owns.
2. **The cabinet decomposes into four volumes, each with its own environmental envelope.** Air handling (M06) crosses volumes and is not one of them.
   - Growing: slots, luminaire, canopy sensing (M01, M02, M04); profile-controlled climate.
   - Propagation: mother plants and rooting runners; cooler than growing, separately controlled.
   - Solution: reservoirs, dosing and circulation pumps, M03; dark, bunded, drainable.
   - Distribution: the ADR-0018 distribution assembly (mains entry, protection, SELV supply, gateway, M05); dry and mains-segregated, not re-decided here.
3. **A furniture-grade freestanding cabinet with a closed envelope; materials are specified as properties.**
   - Every wetted surface is food-safe and non-reactive to nutrient solution at pH 5.5–7.0.
   - Every solution-carrying part is opaque to visible and UV light; light in a nutrient line grows algae.
   - Structure and wetted parts are separable: a wetted part is replaced without disassembling the structure, and the structure carries no solution.
   - The solution volume is bunded, its floor draining to one point the M05 leak lead monitors (ADR-0018).
4. **Each slot is an individual container on a common recirculating feed, drained back to the reservoir (Dutch-bucket class).** A container lets a slot leave and return without disturbing its neighbours; it holds a root-zone reserve that outlasts the software-mediated pump response by hours, where a film fails in minutes; and root volume buffers EC and pH between dosing events, which keeps a two-part loop stable at apartment reservoir sizes.
5. **The cabinet carries two solution loops, vegetative and fruiting, each with its own reservoir, dosing set, circulation and return.** A plant transfers between loops at the pre-fruit to fruiting boundary. Two EC bands and two formulations are needed at once and one reservoir delivers one of each; two loops means two M03 instances of the same class, distinguished by deployment identity (ADR-0014 dec 4, 7).
6. **Propagation takes feed from the vegetative loop.** Runners and pre-fruit plants take the same formulation and EC band; the propagation volume is thermally separate and chemically identical.
7. **Reservoir level and circulation confirmation are required quantities.** Dosing is mass per volume, so without volume a dose is unbounded and an EC fall cannot be told from a top-up; a running pump is not a flowing loop, since dry run, air lock and a blocked line all draw normal current. Both sit at the reservoir and so fall to M03 under ADR-0014 dec 4's co-location rule; adding them is ADR-0014's change.
8. **Dissolved oxygen is not required in this topology.** Roots are not submerged: the container drains and the return aerates. ORP, Ca²⁺ and NO₃⁻ remain reserved on M03 without a requirement.
9. **Solution temperature is an apparatus quantity.** It is the compensation input for both the EC and the pH chains, unconditionally. Whether it also carries a biological band is a profile question; the profile instance carries none and this record adds none.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **G.** Dimensions and trade-name materials in this record | Values belong to the specification (ADR-0000 dec 2, 3); a cut list here would be restated or read from an ADR by a builder |
| 3 | **F.** Rack or industrial form factor | The first deployment is an apartment, where aesthetics are required; not excluded at commercial scale, and nothing in decisions 4–9 depends on decision 3 |
| 4 | **A.** NFT channels | No root-zone reserve, so pump loss is a minutes-scale crop loss against a slower response path; shared channels mat roots between slots against single-slot turnover. Lower water volume and cost per slot do not outweigh either |
| 4 | **B.** Substrate beds | Rejected by ADR-0003 dec 14: no EC/pH control signal |
| 4 | **C.** Deep-water culture | Makes dissolved oxygen a controlled variable on the most complex module, needs a continuously running aeration pump in an apartment, and turns solution temperature into a control problem |
| 5 | **D.** One shared loop at a compromise EC | ADR-0003 dec 15 becomes undeliverable, since both phases are always present; saves one reservoir, dosing set and M03 at the cost of a profile band the apparatus cannot hold |
| 5 | **E.** Nine independent loops, one per slot | Nine reservoirs, dosing sets and M03 instances at apartment scale, and nine individually calibrated consumable pH electrodes |
| 6 | A third loop and third M03 for propagation | Propagation needs the vegetative formulation and EC band, which the vegetative loop already delivers |

## Consequences

**Positive**

- The M03 dependency is cleared: topology, loop count, dissolved oxygen and solution temperature are fixed.
- ADR-0003 dec 15 becomes deliverable.
- The topology's failure timescale matches the response timescale the architecture provides.
- Scale-out adds loops and M03 instances of the same class, without redesign.

**Negative**

- The solution side doubles: two reservoirs, dosing sets, circulation pumps, M03 boards, and pH electrodes on independent calibration schedules.
- A manual plant transfer between loops enters the procedure every cadence interval, disturbing roots as the plant enters its most demanding phase.
- Container volume raises the water inventory, and the bund must contain the larger reservoir.
- M03's complement grows by level and circulation sensing on the module ADR-0014 already identifies as the most complex.
- Neither EC nor pH observes the Ca:K ratio (ADR-0003 dec 16) or the N/K emphasis (dec 15): dosing is closed-loop on concentration and open-loop on composition.

## Deferred

| Open question | Settled by |
|---|---|
| Mechanical specification: dimensions, materials, slot container and interface, transfer mechanics of decision 5 | Mechanical specification and E-number at design commit (ADR-0017 dec 5) |
| Nutrient recipe | Recipe registry (ADR-0003 dec 17, ADR-0009) |
| Formulation observability: a Ca²⁺ or NO₃⁻ ion-selective electrode, or open-loop composition with scheduled reservoir replacement | M03 front-end record |
| Pipeline arithmetic: the slot offsets span less than the cycle and the steady state allocates no slot to the decline phase, so decline must complete inside a changeover interval | ADR-0003 and the profile instance |

## References

- `profiles/strawberry-day-neutral-v1.json` — slot count, cadence, cycle phases, EC and pH bands.
- ADR-0014 dec 4 — the M03 entry this record changes.
- ADR-0018 — distribution assembly and the M05 leak lead.
