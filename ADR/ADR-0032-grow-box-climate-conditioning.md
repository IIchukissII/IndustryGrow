<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0032: Grow-box climate conditioning

- **ID:** ADR-0032
- **Status:** Accepted
- **Date:** 2026-09-08
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0003, ADR-0015, ADR-0016 (rev 1), ADR-0018, ADR-0031 (rev 1)

## Context and problem

`spec/E0011-R-specification.md` fixes which machine moves heat, which way the humidity
actuator acts, and where condensation forms. No record holds the rationale (`O-115`). ADR-0031
owns the actuator classes and the command form, ADR-0018 the rails, ADR-0015 the loop. The
conditioning method has no owner. The specification was written before this record, inverting
ADR-0000 d1.

Fixed elsewhere, not restated here:

| Concern | Owner |
|---|---|
| Actuator class per medium, element as commanded unit, safe output per element | ADR-0031 rev 1 d2, d4, d6 |
| Signed demand, dead band, dwell at zero, validity deadline | ADR-0031 d5, ADR-0015 d18 |
| Interlocks in hardware, independent of the MCU | ADR-0015 d11, ADR-0031 d7 |
| Actuator publishes no process variable | ADR-0031 d8 |
| Setpoints, VPD band, photoperiod, CO₂ as a variant | ADR-0003, `profiles/` |
| Loads, exchangers, parts, thresholds | `spec/E0011-R-specification.md` |

## Decision drivers

- Series production: a per-unit charge, evacuation or leak test is a production station and a
  service obligation.
- Both directions required: night setpoint below room temperature, day load luminaire-dominated.
- A cycling actuator injects its own period into the volume, which ADR-0016 identification then
  carries as a disturbance rather than an input.
- Below dew point, the coldest surface in the air path holds the vapour pressure at
  `e_s(T_surface)`.
- ADR-0016 places condensation in the apparatus subspace: bounded, not regulated. Free water off
  the collector is unmeasured mass out of the moisture balance.
- Thermoelectric efficiency is a function of the element's own ΔT.
- Envelope load scales as surface area, luminaire load as floor area. The dominant term changes
  with size.

## Decision

1. **Scope is a grow box: canopy ≤ 1 m², enclosed volume ≤ 3 m³, day cooling load ≤ 150 W.**
   The load is the binding parameter; area and volume are proxies.

   | Floor | Volume | Envelope area | `U·A` | Luminaire | Day load | Conditioning |
   |---|---|---|---|---|---|---|
   | 0.27 m² | 0.44 m³ | 4.8 m² | 2.9 W/K | 36 W | 42 W | this record |
   | 1 m² | 1.7 m³ | 8.8 m² | 5.4 W/K | 135 W | 146 W | this record, at its edge |
   | 4 m² | 6.8 m³ | 21.6 m² | 13.3 W/K | 540 W | 567 W | compressor chiller |
   | 10 m² | 17 m³ | 41.5 m² | 25.5 W/K | 1350 W | 1400 W | chiller and air-handling unit |

   Common assumptions across the rows: DLI 18, luminaire inside the volume, 1.7 m height, 30 mm
   PIR. The reference cabinet's own loads are in `spec/E0011-R-specification.md` §2.2, which
   places the luminaire outside the volume.

   Outside the envelope the following change, each invalidating a decision below:

   | Quantity | At 0.27 m² | At 10 m² | Consequence |
   |---|---|---|---|
   | Thermoelectric lift | 42 W | 1400 W | Compressor cycle wins above 100–150 W, ≈ 1 m² canopy |
   | Latent load | 6–18 W against 42 W | 280–850 W against 1400 W | Dehumidification becomes a duty of its own, with recuperation |
   | Air exchange for uniformity | 56 m³/h | 2200 m³/h | Ducted ventilation; fan dissipation in the hundreds of watts |
   | CO₂ | ambient replenishes | photosynthesis outruns ambient | Enrichment becomes a control loop |
   | Time constant | minutes | tens of minutes | Predictive or photoperiod-scheduled control |
   | Envelope share of day load | 15 % | 4 % | The task becomes luminaire heat removal |

2. **Heat is pumped thermoelectrically, not by a vapour-compression cycle.** The element is
   bidirectional by current sign, with no reversing valve; it is continuously modulable from
   zero; it carries no refrigerant charge, compressor, oil return or minimum run time. Its
   coefficient of performance is below a compressor cycle's — bounded by decision 3, operating
   point by decision 6.

3. **The hot side rejects into a liquid loop, and that loop is the interface to the cold
   source.** The loop holds the hot-face temperature near the coolant temperature independently
   of the element's dissipation, which bounds module ΔT. Cabinet-side apparatus, driver and
   control law are unchanged whether the heat behind the loop is taken by room air, a dry cooler,
   a building loop or a compressor chiller. Coolant temperature is published as actuator state
   and derates every thermoelectric demand.

4. **Humidity is set by subcooling and reheating a side stream, not by injection.** A side stream
   is cooled to a commanded coil temperature, which fixes humidity ratio at saturation, then
   reheated at constant humidity ratio. Humidity ratio and air temperature become independent
   commands on one air stream (ADR-0031 d4). A vapour source cannot hold a band against a surface
   below dew point, which pins vapour pressure for as long as the cooler runs.

   The setpoint on the bus for this element is a **coil temperature**, not a humidity. The
   sensor class's humidity reading stays an independent check, not an input (ADR-0031 d8).

   The humidity actuator acts in one direction, removal. A closed volume with a transpiring crop
   and a cooling-dominant load accumulates vapour. A deficit occurs only at the dry edge of a
   cold band and is resolved by the profile (`O-108`).

5. **No surface bounding the grow volume is driven below the grow-volume dew point.**
   Condensation is admitted at the decision 4 coil alone, where it is collected and drained.
   Every other conditioned surface carries a floor, and cooling demand is derated to hold it. A
   breached floor produces water where nothing collects it, and makes the surface an uncommanded
   humidity actuator by decision 4's mechanism.

   The floor is a function of air setpoint and humidity band, so it arrives with the demand
   (ADR-0015 d18). The node additionally holds a commissioned absolute minimum that no demand
   lowers.

6. **Thermoelectric operating point is chosen per element against that element's ΔT, and is far
   below `Imax`.** Pumped heat rises with current, Joule dissipation with its square: at small ΔT
   more modules at low current beat fewer at high current. Conduction leak scales with module
   count and competes with the lift at large ΔT: there the element takes fewer modules. Counts,
   currents and limits are specification values.

## Alternatives considered

**A. Vapour-compression cycle inside the cabinet.** *Rejected:* minimum capacity above the load,
so it can only cycle; refrigerant charge, leak testing and regulated handling per unit; not
bidirectional without a reversing valve and a second exchanger. Decision 3 admits it behind the
loop at deployment scale.

**B. Resistive heating with passive or ambient cooling.** *Rejected:* both air setpoints are below
room temperature; no passive path reaches them.

**C. Hot side rejected directly into room air at the module.** *Rejected:* hot-face temperature
rises with the element's own dissipation, module ΔT grows, COP falls. Fixes the cold source to
the room.

**D. Humidification by injection — atomizing nozzle, ultrasonic, evaporative.** *Rejected:* runs
against the pinned surface of decision 4 for as long as the cooler runs; adds feed-water
treatment and a wetted surface in the volume; the ultrasonic route aerosolizes dissolved solids.

**E. Humidity or VPD setpoint commanded to the node.** *Rejected:* puts a sensor-class quantity
in the actuator's loop against ADR-0031 d8, and binds actuator accuracy to a hygrometric element
that drifts with exposure.

**F. Desiccant dehumidification.** *Rejected:* regeneration heat returns to the same apparatus,
the regeneration cycle is the disturbance decision 2 avoids, and it adds a consumable.

**G. One air tract for both duties.** *Rejected:* one coil has one temperature. The humidity duty
requires a surface below dew point, which decision 5 forbids on the surface exchanging with the
whole volume.

**H. Thermoelectric elements near `Imax` to minimize module count.** *Rejected:* the Joule term
dominates at the ΔT the apparatus operates at (decision 6).

## Consequences

### Positive

- `O-115` closes; the specification's conditioning requirements have a governing record.
- Decision 1 bounds the record by load, so a larger enclosure is out of scope by test, not by
  judgement.
- No refrigerant, pressure vessel or charging step in the product.
- Decision 3 makes the cold source a deployment choice and gives the derate variable one source.
- Decision 4 gives humidity and air temperature independent commands on one stream, with the
  humidity setpoint on a thermometer.
- Decision 5 confines condensate to one collected surface, keeping condensation in ADR-0016's
  apparatus subspace.

### Negative

- COP below a compressor cycle's: rejection load is a multiple of the load removed, and supply
  and loop are sized for it.
- Decision 3 adds a pump, a coolant circuit and a flow interlock, and a leak path inside the
  enclosure.
- Decision 4 costs a fan, an exchanger pair and a thermal break, and cannot reach the dry edge of
  a cold band.
- Decision 5 makes the cooling limit setpoint-dependent, so it rides with the demand — a
  requirement on `O-102`.
- Decision 6 buys efficiency with module count, clamping hardware, interface material and board
  area.
- An enclosure past decision 1's envelope has no conditioning record. ADR-0031 holds at that
  size; nothing else here does.

## Deferred decisions

- **Thermoelectric module selection** and the element values following from it (`O-103`).
- **The rejection loop as a purchased assembly** — pump, coolant, exchanger, flow switch — and
  which parts are SP-identified.
- **The cold source behind the loop at deployment scale**, made replaceable by decision 3.
- **CO₂ enrichment**, non-baseline under ADR-0003 d8: source, regulator, dose against a small
  volume, accumulation in an occupied room (`O-110`).
- **The outdoor variant** (`O-106`): ambient is the weather, and neither the rejection assumption
  nor the dew-point floor holds as stated.

## References

- ADR-0003: Strawberry day-neutral profile — d7 (VPD-first), d8 (CO₂ as a variant).
- ADR-0015: Gateway profile caching and local control loops — d11, d18.
- ADR-0016 (rev 1): Empirical survey and state-space modeling — apparatus subspace.
- ADR-0018: Power distribution and rail monitoring.
- ADR-0031 (rev 1): Actuator node taxonomy — d2, d4, d5, d6, d7, d8.
- Incropera, DeWitt, Bergman & Lavine — *Fundamentals of Heat and Mass Transfer*, 7th ed.
- ISO 6946 — Thermal resistance and thermal transmittance of building components.
- Alduchov & Eskridge (1996) — Improved Magnus form approximation of saturation vapor pressure,
  *J. Appl. Meteorol.* 35(4) 601–609.
- ASHRAE Handbook — Fundamentals, ch. 1, psychrometrics.
- Goldsmid — *Introduction to Thermoelectricity*.
- Rowe (ed.) — *CRC Handbook of Thermoelectrics*.
- Monteith & Unsworth — *Principles of Environmental Physics*, 4th ed.
- Kozai, Niu & Takagaki (eds.) — *Plant Factory*.
- [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
