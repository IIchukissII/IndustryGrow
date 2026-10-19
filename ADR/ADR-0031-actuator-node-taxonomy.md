<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0031 — Actuator node taxonomy

- **Status:** Accepted
- **Date:** accepted 2026-09-07; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0014 (sensor partition mirrored here; header contract and class-ID space), ADR-0015 (cascaded control, demand form, interlock independence), ADR-0016 (identification needs unconfounded inputs), ADR-0017 (identifiers assigned at commit), ADR-0018 (switching at the actuator, energy metering, the over-temperature trip, M05 sense-only), ADR-0005 (wire types for demand and actuator state), ADR-0028 (custody of node-local constants), ADR-0032 (how A01-CLIMATE conditions the air)

## Context

ADR-0014 d9 leaves actuator modules to their own taxonomy. ADR-0015, ADR-0018 d8 and the first actuator specification leave to it: which actuator classes exist, how a class is named within the actuator ID range, what a demand's range is, what each actuator drives when it fails safe, which interlocks it carries, and what it may publish.

Already fixed elsewhere: the carrier and module pattern and the header contract (ADR-0014 d5); the 8-bit class ID and actuator range (ADR-0014 d6); instance scaling (ADR-0014 d1, d2); supervisory loop on the gateway with conditioning, deadline and fail-safe on the node (ADR-0015 d18); switching, branch protection and mains handling at the actuator (ADR-0018 d8); no per-actuator metering (ADR-0018 d5); the grow-volume over-temperature trip at the heating actuator (ADR-0018 d10).

ADR-0014 groups sensor classes by the medium a module serves. At deployment scale a climate apparatus is one air-handling unit whose coil, reheat, fan and injection point are commanded together. The grow-volume trip protects the process, not the actuator: a thermoelectric module reaches its solder limit while the grow volume is still cold.

## Decision

1. **Actuator classes are named `A0n` and numbered `0x7F + n`.** The class ID is the module's identity on the bus (ADR-0014 d6) and is authoritative; the `A0n` label is its name in specifications and in the registry. Sensor classes keep `M0n`, where number and ID coincide.
2. **One actuator class per medium acted on, carrying every regulated variable of that medium.** This mirrors ADR-0014 d4.
   - `A01-CLIMATE` acts on grow-volume air: air temperature, humidity and CO₂ (non-baseline, ADR-0003 d8); class ID `0x80`.
   - `A02-LIGHT` acts on radiation at the canopy: PPFD, spectrum, photoperiod.
   - `A03-ROOTZONE` acts on the nutrient solution: EC, pH, root-zone delivery.
   - The plant, the cabinet utility (sense-only under ADR-0018 d9) and the boundary have no actuator class.
   - Air transport is not a class: an air mover is an element of the apparatus whose medium it serves, and the pollination pulse of ADR-0003 d13 is a command mode of `A01-CLIMATE`'s air mover.
   - A class ID is assigned when the class's specification is committed (ADR-0017 d5); new classes continue the sequence.
3. **Ancillary elements are not classes.** An element that exists only to make one actuator work, such as a heat-exchanger fan, a loop circulation pump or a purge valve, belongs to that actuator's module. It may still be commanded separately (decision 4).
4. **An actuator accepts one command per independently drivable element.** Each command is a demand plus a validity deadline (ADR-0015 d18). Independently drivable elements are commanded independently, ancillary ones included, so no two effects are confounded in identification (ADR-0016 d2).
5. **The demand is signed where the element is bidirectional, unsigned where it is not.** The range is `−1.000 … +1.000` signed, `0.000 … 1.000` unsigned, with full scale the element's commissioned limit, not its device maximum. A bidirectional element declares a dead band and a minimum dwell at zero before the sign may change.
6. **Safe output is a property of the element, not of the class.** The element is the commanded unit, so it is the unit that fails safe; each specification declares per element the value driven on a stale demand, on an interlock trip, and at boot before the first valid demand. Every safe output is de-energised except heat-rejection and circulation fans and pumps, which run at full, and such an element is wired so that loss of node, bus or switching element produces that output without the node acting.
   - Bidirectional heat pump, resistive heater: zero drive. Dosing or metering pump: stopped. Valve on a stored fluid or gas: closed. Light source: zero output. Heat-rejection fan or pump, circulation or transport fan: full.
7. **Two interlock kinds, distinguished by what they protect.** A process-protective interlock senses in the grow volume and is required where the actuator can drive a process variable past a limit harming crop or cabinet (ADR-0018 d10). A self-protective interlock senses at the element and is required where the element's own failure is faster than, or independent of, the process excursion. Both are hardware (sensor, comparator, switching-element enable) independent of MCU, gateway and cloud; firmware reads their state and can neither override nor re-arm them.
8. **An actuator node publishes actuator state, never a process variable.** Element state, applied demand, interlock state and the actuator's internal temperatures are actuator state; grow-volume air temperature, humidity, PPFD, EC and pH are published by the sensor class owning them (ADR-0014 d4). This mirrors M05's sense-only rule (ADR-0018 d9): a sensor module switches nothing, an actuator module measures nothing the cabinet regulates.
9. **The command path is galvanically isolated where the switched return carries load current shared with logic or analog ground, and always for mains.** Otherwise the common ground of ADR-0018 d3 suffices. Isolation lives at the actuator (ADR-0018 d7).
10. **Actuator classes cannot identify by strap.** Their IDs need eight bits, so the EEPROM transport (ADR-0014 d6); no actuator module is built against a strap-only carrier revision.
11. **An actuator module expands its own drive lines on-module over the header I²C.** A class covering a medium commands more elements than the header's PWM, GPIO and ADC lines carry; the header contract is unchanged. Interlock states, driver fault outputs and the 1-Wire bus stay on dedicated header lines, and the fast current or duty loop stays in driver silicon, so a bus-rate setpoint bounds no protective function.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **A.** Extend the `M0n` sensor namespace to actuators | The sensor number equals its ID; continuing past M07 collides with the sensor range |
| 1 | **B.** Hex class ID alone, no name form | Every artifact class carries a readable identifier (ADR-0017); a bare ID is unusable in a BOM or registry row |
| 2 | **C.** One class per device rather than per medium | Classes multiply with each hardware change; the medium is stable across device choices |
| 2 | **H.** One class per regulated variable (temperature, vapour, CO₂, dosing, irrigation) | A different rule from the sensor partition; three classes on one air volume share one rejection loop; splits one air-handling unit across nodes at scale |
| 2 | **D.** A single generic class parameterised by EEPROM contents | Safe output, interlock set and demand range are safety-relevant and would become runtime data |
| 2 | **G.** Pre-reserve the actuator ID range against a planned class list | ADR-0017 d5 assigns at commit; reservation invites renumbering |
| 2 | A separate environmental-actuator record with humidification by vapour injection | Injection opposes a cooler surface below dew point (ADR-0032 d4); its CO₂ content belongs to `A01-CLIMATE` |
| 6 | Safe output per class | The element is the commanded unit, and elements in one class fail safe differently |
| 7 | **E.** Interlocks in firmware with a hardware watchdog | ADR-0015 d11 requires MCU independence |
| 8 | **F.** Publish process variables from the actuator that drives them | A second source for a sensor class's quantity (ADR-0000 d3), coupling feedback to the controlled element |
| 11 | **I.** Widen the header to carry every actuator line directly | The header is common to every class and carrier revision, for lines that need only bus rate |
| 11 | **J.** An MCU on the actuator module as a smart peripheral | A second firmware image, toolchain and update path in answer to a pin count |

## Consequences

**Positive**

- The first actuator specification has a governing record for class name, demand range, safe outputs and interlocks.
- Every future actuator element takes its fail-safe from the element-kind rule without a per-class decision.
- The interlock requirement is derived from the element, not negotiated per design.
- Each actuator class pairs with the sensor class observing the same medium, so every quantity has one source.
- The demand and actuator state have wire types in the project DSDL namespace (ADR-0005).

**Negative**

- Every actuator module waits on the EEPROM-identifying carrier revision.
- A class spanning a medium consumes one subject per element rather than one per node.
- Loss of one climate node leaves air temperature, humidity and CO₂ uncommanded together.
- Non-de-energised safe outputs need a wiring discipline a de-energise-everything rule would not.
- An I²C part sits in every actuator module's drive path, and its failure mode joins each specification's verification.

## Deferred

| Open question | Settled by |
|---|---|
| Specifications and class IDs for `A02-LIGHT` and `A03-ROOTZONE` | Commit of each class's specification |
| Mechanism for writing node-local conditioning constants (limits, dead bands, dwell, slew) | ADR-0028 custody model, extended |
| Actuator instance identity and zone tagging at multi-instance scale | First multi-instance actuator deployment |

## References

- `spec/E0011-R-specification.md` — `A01-CLIMATE`.
- `firmware/dsdl/industryflow/greenhouse/actuator/` — `Demand`, `ElementState`, `InterlockState`.
- `REGISTRY.md` — `E0011`, `E0012`.
