<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0006: Cabinet decomposition and hydroponic topology

- **ID:** ADR-0006
- **Status:** Accepted
- **Date:** 2026-08-23
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0003, ADR-0014 (rev 6), ADR-0015, ADR-0016, ADR-0017 (rev 2), ADR-0018
- **Discharges:** ADR-0001's `ADR-0006 (deferred)` reservation; ADR-0003 decision 14's deferred topology

## Context and problem

ADR-0001 reserved ADR-0006 for cabinet form factor, materials and mechanical decomposition.
ADR-0003 decision 14 committed to hydroponic delivery and deferred the topology — NFT,
Dutch-bucket, hybrid — to this record. Two dependent items have been waiting on it:
`spec/M03-ANALYTICS-specification.md`, which cannot state a sensor complement until the topology
is fixed, and ADR-0014 decision 4's M03 entry, which reserves space for dissolved oxygen without
a requirement that would justify it.

The record already fixes the cultivation shape the mechanics must serve:

| Quantity | Value | Source |
|---|---|---|
| Slots | 9 | `profiles/…-v1.json` `operating_mode.slots` |
| Slot offset / harvest cadence | 2 weeks | ADR-0003 d4; profile |
| Per-plant cycle | 19 weeks (6 pre-fruit, 12 fruiting, 1 decline) | ADR-0003 d3; profile |
| Steady-state allocation | 6 fruiting, 3 pre-fruit | profile `steady_state` |
| EC band | 1.2–1.4 mS/cm vegetative; 1.6–1.8 mS/cm flowering/fruiting | ADR-0003 d15; profile |
| pH band | 5.8–6.2, both phases | ADR-0003 d15; profile |
| Dosing | two-part A/B concentrate, peristaltic, switched per slot phase | ADR-0003 d17 |
| Propagation | 1–2 mother plants in a cooler zone | ADR-0003 d5; profile |

Those values contain a conflict that no mechanical layout can avoid. The pipeline runs pre-fruit
and fruiting slots **concurrently** — 3 and 6 at steady state — while decision 15 bands EC
differently for each phase and decision 17 switches formulation per slot phase. A single
reservoir carries one EC and one formulation. Either the loop count rises or decision 15 is not
deliverable.

## Decision drivers

- Apartment scale: small reservoirs, low noise tolerance, no exhaust to garden (ADR-0003 drivers).
- Aesthetics, modularity and replaceability are non-negotiable at apartment scale (ADR-0001 d33).
- One slot is emptied and replanted every cadence interval, indefinitely (ADR-0003 d4).
- Leak and pump response is software-mediated over Cyphal, not a hardware interlock
  (ADR-0015 d16, ADR-0018) — a minutes-to-hours response, not a seconds response.
- The same architecture must hold at greenhouse scale; only instance counts change (ADR-0001).
- ADRs carry rationale and alternatives; values belong in specifications (ADR-0000 d2, d3).

## Decision

### 1. This record decides shape, not dimensions

Form-factor class, material class, volume decomposition, hydroponic topology and loop count are
decided here. External dimensions, the cut list, materials by trade name and the slot interface
geometry belong to a mechanical specification and an E-number assigned at design commit
(ADR-0017 d3, d5). Placing millimetres here would make this record the source of truth for
values a specification owns.

### 2. Volume decomposition

The cabinet decomposes into four volumes, each with its own environmental envelope:

| Volume | Contents | Envelope |
|---|---|---|
| Growing | 9 slots, luminaire, canopy sensing (M01, M02, M04) | Profile-controlled climate |
| Propagation | mother plants, rooting runners (ADR-0003 d5) | Cooler than growing; separately controlled |
| Solution | reservoirs, dosing pumps, circulation pumps, M03 | Dark, bunded, drainable |
| Distribution | `E0007` — mains, MCB, `+12 V` SELV, gateway, M05 | Dry, mains-segregated (ADR-0018) |

Air handling (M06) crosses volumes and is not one of them. The distribution volume already
exists as `E0007` and is not re-decided here.

### 3. Form factor and material class

A furniture-grade freestanding cabinet with a closed envelope. Material requirements, stated as
properties rather than products:

- Every wetted surface is food-safe and non-reactive to nutrient solution at pH 5.5–7.0.
- Every solution-carrying part is opaque to visible and UV radiation. Light in a nutrient line
  is an algae culture.
- Structure and wetted parts are separable: a wetted part is replaceable without disassembling
  the structure, and the structure carries no solution.
- The solution volume is bunded, with its floor draining to a single point that the M05 leak
  lead monitors (ADR-0018).

### 4. Hydroponic topology — individual containers on a recirculating feed

Each slot is an individual container fed from a common recirculating line, drained back to the
reservoir. Dutch-bucket class. Not NFT (alternative A), not deep-water culture (alternative C).

Three grounds, in order of weight:

- **Turnover.** One slot is emptied and replanted every 2 weeks against a 19-week root mass. An
  individual container makes a slot a unit that leaves and returns without touching its
  neighbours. In a shared channel, roots mat along the channel and single-slot turnover
  disturbs the plants either side of it.
- **Failure tolerance.** A container holds a root-zone water reserve; a film does not. With a
  stopped pump, a container buys hours and a film costs the crop in minutes. The response this
  system has is software-mediated over Cyphal (ADR-0015 d16) and therefore not a minutes
  response — the topology must survive the response time the architecture actually provides.
- **Loop stability.** Root volume per plant buffers EC and pH between dosing events, which is
  what makes a two-part peristaltic loop stable at apartment reservoir sizes.

### 5. Two solution loops

The cabinet carries a **vegetative loop** and a **fruiting loop**, each with its own reservoir,
dosing set, circulation and return. A plant transfers between loops at the pre-fruit → fruiting
boundary.

This follows from decision 15 of ADR-0003 and the staggered pipeline: two EC bands and two
formulations are required simultaneously, and one reservoir delivers one of each. The
alternative — a single loop at a compromise EC — is alternative D, and it makes decision 15
undeliverable rather than merely approximate.

**Two loops means two M03 instances.** ADR-0014 decision 4 places one M03 per hydroponic loop
regardless of deployment scale; this decision is the first case where that count exceeds one.
Both instances carry module-ID `0b011` and are distinguished by deployment identity, not by
class (ADR-0014 d7).

### 6. Propagation shares the vegetative loop

The propagation volume is thermally separate (decision 2) and chemically identical: runners and
pre-fruit plants take the same N-emphasised formulation and the same EC band. It takes feed from
the vegetative loop. There is no third loop and no third M03.

### 7. Reservoir level and circulation confirmation are required quantities

The topology creates two quantities the record does not currently assign to any module:

- **Reservoir level.** Dosing is a mass per volume. Without volume, a dose is unbounded, and a
  fall in EC cannot be attributed to uptake rather than to a top-up.
- **Circulation confirmation.** A running pump is not a flowing loop: dry run, air lock and
  blocked line all present as normal pump current. M05's single INA226 on the `+12 V` bus
  (ADR-0018) observes the pump drawing current and nothing about the solution moving.

Both sit in or at the reservoir and therefore fall to M03 under ADR-0014 decision 4's
co-location rule. Adding them to M03's complement is ADR-0014's change to make, not this
record's.

### 8. Dissolved oxygen is not required in this topology

Roots are not submerged: the container drains and the return aerates. DO becomes a requirement
only under a deep-water topology (alternative C). This discharges the DO item ADR-0014
decision 4 holds under "reserved space for future ion-selective electrodes"; ORP, Ca²⁺ and NO₃⁻
are unaffected and remain reserved without a requirement.

### 9. Solution temperature is an apparatus quantity here

It is required as the compensation input for both the EC and the pH chains, and that requirement
is unconditional. Whether it also carries a biological band is a profile question:
`profiles/strawberry-day-neutral-v1.json` has no `root_zone` temperature entry, and this record
does not add one.

## Alternatives considered

**A. NFT channels.** Continuous thin film over bare roots in a shared channel. *Rejected:* it
holds no root-zone reserve, so pump loss is a minutes-scale crop loss against a response path
that is software-mediated and therefore not minutes-scale (ADR-0015 d16). Shared channels also
mat roots between slots, which conflicts with a 2-week turnover against a 19-week root mass.
Lower water volume and lower cost per slot are real advantages and do not outweigh either.

**B. Substrate beds.** *Rejected in ADR-0003 alternative F* — substrate exposes no EC/pH control
signal, which is the ground on which decision 14 chose hydroponics at all. Recorded here only so
that this record's topology comparison is complete.

**C. Deep-water culture.** Roots submerged in an aerated reservoir. *Rejected:* it makes
dissolved oxygen a first-order controlled variable, adding a DO chain to the module ADR-0014
already calls the most complex in the set, and an aeration pump running continuously in an
apartment conflicts with the low-noise driver. Its thermal mass also makes solution temperature
a control problem rather than a measurement.

**D. One shared loop at a compromise EC.** *Rejected:* it does not deliver ADR-0003 decision 15.
Vegetative plants would sit at fruiting EC or fruiting plants at vegetative EC for the whole
pipeline, since at steady state both phases are always present. The saving is one reservoir, one
dosing set and one M03; the cost is that a banded setpoint in the profile becomes a value the
apparatus cannot hold, which is the failure mode ADR-0016's roots/leaves split exists to prevent.

**E. Nine independent loops, one per slot.** Per-slot EC, pH and formulation. *Rejected:* nine
reservoirs, nine dosing sets and nine M03 instances at apartment scale. It also multiplies
calibration: each pH electrode is an individually calibrated, consumable instrument.

**F. Rack or industrial form factor.** *Rejected:* ADR-0001 places the first deployment in an
apartment and makes aesthetics non-negotiable there. The rack form is not excluded at commercial
scale — decision 3 is a form-factor class for this deployment, not an architectural constraint,
and nothing in decisions 4 to 9 depends on it.

**G. Decide dimensions and materials by trade name in this record.** *Rejected:* ADR-0000
decisions 2 and 3 put values in the specification and rationale in the ADR. A cut list here
would have to be restated in the mechanical specification or read from an ADR by a builder,
which is the duplication that discipline exists to prevent.

## Consequences

### Positive

- M03's blocking dependency is discharged: the topology, the loop count, the DO question and the
  status of solution temperature are all fixed, which is what the specification was waiting for.
- ADR-0003 decision 15 becomes deliverable rather than aspirational.
- The topology's failure timescale matches the response timescale the architecture provides.
- Scale-out is instance multiplication, not redesign: a greenhouse adds loops and M03 instances
  of the same class, which is ADR-0001's promise applied to the fluidic subsystem.

### Negative

- **Two of everything on the solution side.** Two reservoirs, two dosing sets, two circulation
  pumps, two M03 boards, two pH electrodes on independent calibration schedules. This is the
  direct cost of decision 5 and the largest cost in this record.
- **A transfer step enters the operating procedure.** A plant moves between loops at the
  pre-fruit → fruiting boundary. That is a manual operation every cadence interval, and it is
  root disturbance at the moment the plant enters its most demanding phase.
- **Container volume raises the water inventory** in an apartment, and the bund of decision 3
  must contain the larger of the two reservoirs.
- **M03's complement grows** by level and circulation sensing (decision 7), on the module
  ADR-0014 already identifies as the most complex in the set.
- **Formulation stays unobservable.** Two loops give the correct EC per phase; neither EC nor pH
  observes the Ca:K ratio decision 16 of ADR-0003 prioritises, or the N/K emphasis of decision
  15. Dosing remains closed-loop on concentration and open-loop on composition.

## Relationship to other ADRs

- **ADR-0001** — discharges the ADR-0006 reservation.
- **ADR-0003** — discharges decision 14's deferred topology; decisions 15, 16 and 17 are the
  drivers for decision 5 here. No decision of ADR-0003 is changed.
- **ADR-0014** — decision 4's M03 entry gains a second instance (decision 5), loses the DO
  reservation (decision 8) and gains level and circulation quantities (decision 7). Each is a
  change to ADR-0014, made there.
- **ADR-0015** — decision 16's software-mediated leak/pump response is a driver for decision 4.
- **ADR-0016** — decision 9 keeps solution temperature in the apparatus subspace.
- **ADR-0018** — the distribution volume is `E0007`; the bund drain point of decision 3 is
  monitored by the M05 leak lead.

## Deferred decisions

- **Mechanical specification and E-number.** Dimensions, materials, the slot container and its
  interface, and the transfer mechanics of decision 5. Assigned at design commit (ADR-0017 d5).
- **Nutrient recipe.** Deferred by ADR-0003 decision 17 to the recipe registry (ADR-0009).
- **Formulation observability.** Whether a Ca²⁺ or NO₃⁻ ion-selective electrode is added to close
  the gap in the last negative consequence, or whether composition control stays open-loop with a
  scheduled reservoir replacement as its mitigation. Belongs to the M03 front-end record.
- **Pipeline arithmetic.** 9 slots at a 2-week offset span 18 weeks against a 19-week cycle, and
  `steady_state` allocates 6 + 3 = 9 slots with none for the 1-week decline phase. The decline
  week therefore has to complete inside a changeover interval. This is an ADR-0003 / profile
  question, recorded here because the mechanics inherit it.

## References

- ADR-0001 — project framing; ADR-0006 reservation; first deployment, ~9 slots, 2-week cadence.
- ADR-0003 — strawberry day-neutral profile; decisions 3, 4, 5, 14, 15, 16, 17; alternative F.
- ADR-0014 (rev 6) — sensor node taxonomy; decision 4's M03-ANALYTICS entry; decision 7.
- ADR-0015 — gateway control loops; decision 16, leak and pump response.
- ADR-0018 — power distribution; `E0007`; M05 leak lead.
- `profiles/strawberry-day-neutral-v1.json` — slot count, cadence, cycle phases, EC and pH bands.
