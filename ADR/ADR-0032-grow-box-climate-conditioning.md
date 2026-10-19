<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0032 — Grow-box climate conditioning

- **Status:** Accepted
- **Date:** accepted 2026-09-08; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0003 (setpoints, VPD band, CO₂ as a variant), ADR-0015 (cascaded control and demand form, hardware interlocks), ADR-0016 (condensation in the apparatus subspace; actuator cycling as disturbance), ADR-0017 (identification at commit), ADR-0018 (rails and sections), ADR-0019 (single-parent parts on the parent's root), ADR-0031 (`A01-CLIMATE` class, element commands, signed demand, safe output, publication rule)

## Context

The `A01-CLIMATE` specification fixes which machine moves heat, which way the humidity actuator acts and where condensation forms; this record holds why. ADR-0031 owns the class, the command form and the safe outputs; the specification owns loads, exchangers, parts and thresholds.

The product is built in series, so a per-unit refrigerant charge, evacuation or leak test is a production station and a service obligation. Both directions are needed: the night setpoint is below room temperature and the day load is luminaire-dominated. A cycling actuator injects its own period into the volume, which identification (ADR-0016) then carries as a disturbance. Below dew point, the coldest surface in the air path holds vapour pressure at its own saturation value. Condensation is an apparatus quantity, bounded rather than regulated, and free water off the collector is unmeasured mass leaving the moisture balance. Thermoelectric efficiency is a function of the element's own ΔT. Envelope load scales with surface area and luminaire load with floor area, so the dominant term changes with size.

## Decision

1. **Scope is a grow box: canopy ≤ 1 m², enclosed volume ≤ 3 m³, day cooling load ≤ 150 W.** The load is the binding parameter; area and volume are proxies. Beyond it, under the assumptions below (DLI 18, luminaire inside, 1.7 m height, 30 mm PIR), each decision here is invalidated:

   | Floor | Day load | Conditioning |
   |---|---|---|
   | 0.27 m² | 42 W | this record |
   | 1 m² | 146 W | this record, at its edge |
   | 4 m² | 567 W | compressor chiller |
   | 10 m² | 1400 W | chiller and air-handling unit |

   At 10 m² against 0.27 m²: a compressor cycle out-lifts thermoelectrics above about 1 m²; latent load becomes a duty of its own with recuperation; uniformity needs ducted ventilation with fan dissipation in the hundreds of watts; photosynthesis outruns ambient CO₂ replenishment; the time constant grows to tens of minutes; and the task becomes luminaire heat removal.
2. **Heat is pumped thermoelectrically, not by a vapour-compression cycle.** The element is bidirectional by current sign without a reversing valve, continuously modulable from zero, and carries no refrigerant, compressor, oil return or minimum run time. Its lower coefficient of performance is bounded by decision 3 and its operating point by decision 6.
3. **The hot side rejects into a liquid loop, and the loop is the interface to the cold source.** The loop holds hot-face temperature near coolant temperature regardless of dissipation, bounding module ΔT; cabinet apparatus, driver and control law are the same whether room air, a dry cooler, a building loop or a chiller takes the heat. Coolant temperature is published as actuator state and derates every thermoelectric demand.
4. **Humidity is set by subcooling and reheating a side stream, not by injection.** The stream is cooled to a commanded coil temperature, fixing humidity ratio at saturation, then reheated at constant humidity ratio, so humidity ratio and air temperature are independent commands (ADR-0031 d4); a vapour source cannot hold a band against a surface below dew point. The setpoint on the bus is a coil temperature, and the sensor class's humidity reading stays an independent check (ADR-0031 d8). The actuator acts in one direction, removal, because a closed volume with a transpiring crop and a cooling-dominant load accumulates vapour.
5. **No surface bounding the grow volume is driven below the grow-volume dew point.** Condensation is admitted at the decision 4 coil alone, where it is collected and drained; every other conditioned surface carries a floor, and cooling demand is derated to hold it, since a breached floor makes water nothing collects and an uncommanded humidity actuator. The floor depends on setpoint and band, so it arrives with the demand (ADR-0015 d18), and the node also holds a commissioned absolute minimum no demand lowers.
6. **The thermoelectric operating point is chosen per element against its ΔT, far below `Imax`.** Pumped heat rises with current and Joule dissipation with its square, so at small ΔT more modules at low current win; conduction leak scales with module count and competes with the lift at large ΔT, where the element takes fewer modules. Counts, currents and limits are specification values.
7. **The humidity tract's reheat is pumped from the subcooler's rejection plate through a commandable thermoelectric link, not generated resistively.** The plate rejects several times the reheat duty a few kelvin from the reheat temperature, the regime where a thermoelectric element is most efficient and a resistor least defensible. Unpowered the link conducts, so reheat is free whenever the plate is above target, and powered it lifts the remainder.
   - The specification sizes two bounds: pumping capacity at the largest plate-to-coil lift, and off-state conductance times the largest reverse lift.
   - Zero drive is not zero heat: the link is sized so uncommanded reheat stays inside the band the tract would command anyway. The ADR-0031 d6 safe output is a drive value and does not imply zero transfer, which the element's specification states.
8. **The tract air path is commodity tube sections and printed flanged parts on one interface bore, set by the exchanger face, not the air mover.** Both tracts share bore and flange, so any section, bend, flange or termination fits either; the duct meets the fin stack, the fan's throat cost is already inside the exchanger's rated resistance, and duct friction at fixed flow scales as the fifth power of the inverse bore, which removes run length and bend count from the fan's operating point. Bore, flange pattern and fan opening are specification values.
   - The bore is constant from termination to termination; the narrowest point of the path is the air mover's own opening.
   - The discharge termination's open area, not the bore, sets the velocity entering the grow volume.
   - Sections, flanges and terminations take no identifier while the combination iterates; identification follows at commit (ADR-0017 d5), a single-parent part on that parent's root (ADR-0019 d9).

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 2 | **A.** Vapour-compression cycle inside the cabinet | Minimum capacity above the load, so it only cycles; per-unit charge and leak testing; not bidirectional without a reversing valve. Admitted behind the loop at deployment scale |
| 2 | **B.** Resistive heating with passive or ambient cooling | Both air setpoints are below room temperature |
| 3 | **C.** Hot side rejected directly into room air at the module | Hot-face temperature rises with dissipation, ΔT grows, COP falls; fixes the cold source to the room |
| 4 | **D.** Humidification by injection (atomising, ultrasonic, evaporative) | Runs against the pinned surface for as long as the cooler runs; needs feed-water treatment and a wetted surface; ultrasonic aerosolises dissolved solids |
| 4 | **E.** Humidity or VPD setpoint commanded to the node | A sensor-class quantity in the actuator's loop (ADR-0031 d8); binds accuracy to a drifting hygrometric element |
| 4 | **F.** Desiccant dehumidification | Regeneration heat returns to the apparatus, its cycle is a disturbance, and it adds a consumable |
| 5 | **G.** One air tract for both duties | One coil has one temperature; the humidity duty needs a surface below dew point, forbidden on the surface exchanging with the whole volume |
| 6 | **H.** Thermoelectric elements near `Imax` to minimise module count | The Joule term dominates at the operating ΔT |
| 7 | **I.** Resistive reheater | Makes heat the plate already holds a few kelvin away at COP 1, and charges its draw twice, to the supply and to the rejection duty |
| 7 | **J.** Coolant branch through the reheat exchanger | Cheapest thermodynamically, but a second wet branch and leak path inside the enclosure, and its reheat follows the loop rather than a command |
| 8 | **K.** Tract bore matched to the air mover's throat | Contracts and re-accelerates air at the fin-stack exit, raises friction so run and bends reach the fan, and moves the expansion to the discharge where a sudden step costs most |
| 8 | **L.** One housing drawn per tract, with no interface | Each change of run, bend or termination redraws the part, and the tracts share nothing |

## Consequences

**Positive**

- The specification's conditioning requirements have a governing record.
- The record is bounded by load, so a larger enclosure is out of scope by test.
- The product carries no refrigerant, pressure vessel or charging step.
- The cold source is a deployment choice, and the derate variable has one source.
- Humidity and air temperature are independent commands on one stream, with the humidity setpoint on a thermometer.
- Condensate is confined to one collected surface, keeping condensation in ADR-0016's apparatus subspace.
- Reheat costs a fraction of its delivered heat, and is free whenever the plate is above target.
- Run, bend count and terminations change without touching an element, a requirement or a number, and both tracts share one set of parts.

**Negative**

- COP is below a compressor cycle's, so rejection load is a multiple of the load removed and supply and loop are sized for it.
- The rejection loop adds a pump, a coolant circuit, a flow interlock and a leak path inside the enclosure.
- Subcool-and-reheat costs a fan, an exchanger pair and a thermal break, and cannot reach the dry edge of a cold band.
- The cooling limit is setpoint-dependent and rides with the demand.
- Efficiency is bought with module count, clamping hardware, interface material and board area.
- An unpowered reheat link still transfers heat, a bound the element must be sized against.
- An enclosure past decision 1's envelope has no conditioning record; ADR-0031 still holds there.
- Every duct joint is a leak path and a fastener count the assembled measurement carries.
- A bore set by the main tract is wide for the humidity tract, whose low flow wants a restriction.

## Deferred

| Open question | Settled by |
|---|---|
| Thermoelectric module selection and the element values that follow | `O-103` |
| The rejection loop as a purchased assembly, and which parts take SP numbers | Rejection-loop specification |
| The cold source behind the loop at deployment scale | First deployment-scale build |
| CO₂ enrichment: source, regulator, dose against a small volume, accumulation in an occupied room | `O-110` |
| Humidity at the dry edge of a cold band | Profile (`O-108`) |
| Humidity tract as a branch off the main tract instead of its own fan | `O-129`; amends decision 4 if adopted |
| Outdoor variant, where neither the rejection assumption nor the dew-point floor holds as stated | `O-106` |

## References

- `spec/E0011-R-specification.md` — `A01-CLIMATE` actuator module, loads, open items.
- `spec/E0012-R-specification.md` — tempering unit and the duct bore.
- Incropera, DeWitt, Bergman & Lavine — *Fundamentals of Heat and Mass Transfer*, 7th ed.
- ISO 6946 — thermal resistance and transmittance of building components.
- Alduchov & Eskridge (1996), *J. Appl. Meteorol.* 35(4) 601–609 — Magnus saturation vapour pressure.
- ASHRAE Handbook — Fundamentals, ch. 1, psychrometrics.
- Goldsmid — *Introduction to Thermoelectricity*; Rowe (ed.) — *CRC Handbook of Thermoelectrics*.
- Monteith & Unsworth — *Principles of Environmental Physics*, 4th ed.; Kozai, Niu & Takagaki (eds.) — *Plant Factory*.
