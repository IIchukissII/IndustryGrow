<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0031 (rev 1): Actuator node taxonomy

- **ID:** ADR-0031 (rev 1)
- **Status:** Accepted
- **Date:** 2026-09-07 (rev 1: 2026-09-07)
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0014 (rev 4), ADR-0015, ADR-0016, ADR-0018
- **Supersedes:** ADR-0031 (initial, 2026-09-07)

## Revision history

- **rev 1 (2026-09-07)** — Repartitions decision 2. Actuator classes are grouped by the medium they act on, mirroring ADR-0014's sensor partition, instead of one class per regulated variable. Seven classes become three; `A02-VAPOUR` and `A07-CO2` fold into `A01-CLIMATE`, and `A04-DOSING` and `A05-IRRIGATION` fold into `A03-ROOTZONE`. Decision 6 moves from safe output per class to safe output per element, which decision 4 had already made the addressable unit. Decision 11 is added for the module-local drive expansion the coarser classes require. Withdraws the unnumbered environmental-actuator draft and moves its CO₂ content to `A01-CLIMATE`. Records alternatives H, I and J. Decisions 1, 3, 4, 5, 7, 8, 9 and 10 are unchanged.

## Context and problem

ADR-0014 decision 9 placed actuator modules out of scope and required their own taxonomy ADR.
Three records since have deferred content to it: ADR-0015 (which actuator nodes exist, what
commands they accept, the safe output per class), ADR-0018 decision 8 (switch types, isolation,
command semantics), and an unnumbered environmental-actuator draft (switching, monitoring,
protection, isolation).

The first actuator specification (`spec/A01-CLIMATE-specification.md`, `E0011`) is written and
carries `O-101` against every requirement that has no governing decision.

What is already fixed elsewhere and is not restated here:

| Concern | Owner |
|---|---|
| Carrier + module pattern, shared header, 4× PWM reserved for actuator modules | ADR-0014 d5 |
| 8-bit class ID, actuator range `0x80`–`0xFE`, EEPROM transport at `0x50`–`0x57` | ADR-0014 d6 |
| Multi-instance and partial-BOM scaling | ADR-0014 d1, d2 |
| Supervisory loop on the gateway; conditioning, deadline and fail-safe on the node | ADR-0015 d18 |
| Switching element at the actuator; per-branch protection at the branch; mains by local SSR | ADR-0018 d8 |
| No per-actuator energy metering; all actuator energy via the DIN meter over S0 | ADR-0018 d5 |
| Grow-volume over-temperature trip at the heating actuator | ADR-0018 d10 |

**rev 1.** The initial record grouped actuator classes by regulated variable. ADR-0014 groups
sensor classes by the medium a module serves: M01-CLIMATE carries air temperature, humidity, VPD
and CO₂ in one class, M05-SAFETY carries rail, door, leak and energy in another. Applying a
different rule to actuators produced three classes — `A01-THERMAL`, `A02-VAPOUR`, `A07-CO2` —
acting on one volume of air through one rejection loop, and reserved six identifiers against
specifications that do not exist, which decision 2's own closing paragraph and ADR-0017 decision
5 forbid. It also fails ADR-0014's requirement of the same architecture from cabinet to
deployment scale: at that scale the climate apparatus is one air-handling unit whose coil,
reheat, fan and injection point are commanded together, and a per-variable partition splits one
machine across three nodes.

The environmental-actuator draft is withdrawn with this revision. Its humidification decisions
assumed vapour injection into a volume whose coldest surface sits below the dew point, where the
surface pins the vapour pressure at `e_s(T_surface)` and the injector opposes the cooler
continuously. Its CO₂ content — non-baseline status, pulse dosing, minimum-dose overshoot, and
CO₂ accumulation in an occupied room — moves to `A01-CLIMATE`'s unpopulated CO₂ branch.

## Decision drivers

- **The class-ID range is assigned but the namespace has no form.** ADR-0014 d6 fixes `0x80`–`0xFE`; nothing states how a class is named or numbered within it.
- **ADR-0015 d18 defers the safe output per class to this record.** A node cannot implement its fail-safe without it.
- **ADR-0018 d10 protects the process, not the actuator.** A thermoelectric module reaches its solder limit while the grow volume is still cold (`spec/A01-CLIMATE-specification.md` `T2`); the existing trip does not fire.
- **A demand's sign is per class.** A heater is unipolar; a thermoelectric module and a reversible pump are not. ADR-0015 d18 fixes the demand's form, not its range.
- **Sense and switch are separated for M05 (ADR-0018 d9); the mirror rule for actuators is unwritten.** Nothing states what an actuator node may publish.

## Decision

1. **Actuator classes are named `A0n` and numbered `0x7F + n`.** `A01` is `0x80`, `A02` is
   `0x81`, and so on. The class ID is the module's identity on the bus (ADR-0014 d6); the `A0n`
   label is its name in specifications and in `REGISTRY.md`. Sensor classes keep the `M0n` form,
   where the number and the ID coincide; for actuators they do not, and the ID is authoritative.

2. **Actuator taxonomy — one class per medium acted on** *(rev 1)*. An actuator class is the
   apparatus acting on one medium and carries every regulated variable of that medium, mirroring
   the sensor partition of ADR-0014 decision 4.

   | Class | Medium | Regulated variables | ID | Specification |
   |---|---|---|---|---|
   | `A01-CLIMATE` | Grow-volume air | Air temperature, humidity, CO₂ (ADR-0003 d8, non-baseline) | `0x80` | `spec/A01-CLIMATE-specification.md` |
   | `A02-LIGHT` | Radiation at the canopy | PPFD, spectrum, photoperiod | on commit | none |
   | `A03-ROOTZONE` | Nutrient solution | EC, pH, root-zone delivery | on commit | none |

   Media with no actuator class: the plant, which M04-PLANT observes; the cabinet utility, which
   is sense-only under ADR-0018 decision 9; and the boundary, which M07-AMBIENT observes.

   **Air transport is not a class** — an air mover is an element of the apparatus whose medium it
   serves. This takes a position on ADR-0014's deferred *ventilation / pollination subsystem
   boundary* and discharges it for actuator classification only; which `node_role` M06 takes is a
   sensor-side question and stays with ADR-0014. Pollination follows: ADR-0003 decision 13
   pollinates mechanically by pulsed airflow over the flowering zone, so the pollination pulse is
   a command mode of the grow-volume air mover — an element of `A01-CLIMATE` — and not a class.

   Names follow decision 1's sequence. A class ID is assigned when that class's specification is
   committed, not here (ADR-0017 d5). New classes continue the sequence.

3. **Ancillary elements are not classes.** An element that exists only to make one actuator work
   — a heat-exchanger fan, a circulation pump inside a rejection loop, a purge valve — is part of
   that actuator's module and carries no class of its own. It may still be separately commanded
   (decision 4).

4. **An actuator accepts one command per independently drivable element.** Each command is a
   demand plus a validity deadline (ADR-0015 d18). Elements that can be driven independently are
   commanded independently, including ancillary elements, so that no two effects are confounded
   in identification (ADR-0016 decision 2).

5. **The demand is signed where the element is bidirectional, unsigned where it is not.** Range
   is `−1.000 … +1.000` signed, `0.000 … 1.000` unsigned; full scale is the element's
   commissioned limit, not its device maximum. A bidirectional element declares a dead band and
   a minimum dwell at zero before the sign may change.

6. **Safe output is a property of the element, not of the class** *(rev 1)*. Decision 4 makes the
   element the commanded unit, so the element is also the unit that fails safe. Each
   specification declares, per element, the value driven when a demand goes stale, when an
   interlock trips, and at boot before the first valid demand.

   | Element kind | Safe output |
   |---|---|
   | Bidirectional heat pump | zero drive |
   | Resistive heater | zero drive |
   | Heat-rejection fan or pump | **full** |
   | Circulation or transport fan | **full** |
   | Dosing or metering pump | stopped |
   | Valve on a stored working fluid or gas | closed |
   | Light source | zero output |

   Every safe output is de-energized except the two that move a fluid to carry heat away. Where a
   safe output is not de-energized, the element is wired so that loss of the node, of the bus, or
   of the switching element produces that output without the node acting.

7. **Two interlock kinds, distinguished by what they protect.**

   | Kind | Protects | Sensor location | Required when |
   |---|---|---|---|
   | Process-protective | The grow volume and its contents | In the grow volume | The actuator can drive a process variable past a limit that harms the crop or the cabinet (ADR-0018 d10) |
   | Self-protective | The actuator itself | At the element | The element's own failure is faster than, or independent of, the process excursion the process-protective trip senses |

   Both are hardware: sensor → comparator → the switching element's enable, independent of the
   MCU, the gateway and the cloud. Firmware reads interlock state and can neither override nor
   re-arm it. An actuator may carry both; `A01-CLIMATE` does.

8. **An actuator node publishes actuator state, never a process variable.** Element state,
   applied demand, interlock state, and the actuator's own internal temperatures are actuator
   state. Grow-volume air temperature, humidity, PPFD, EC and pH are published by the sensor
   class that owns them (ADR-0014 d4). An interlock's sensor is a trip element and is not a
   published quantity. This is the mirror of ADR-0018 d9's sense-only rule for M05: a sensor
   module switches nothing, an actuator module measures nothing the cabinet regulates.

9. **Galvanic isolation of the command path is required when the switched return carries load
   current shared with the logic or analog ground, and always for mains.** Otherwise the common
   ground of ADR-0018 d3 is sufficient. Isolation lives at the actuator (ADR-0018 d7), never on
   the distribution board.

10. **Actuator classes cannot identify by strap.** `0x80` and above need eight bits, so the
    EEPROM transport, so carrier `E0001-000100` or later (ADR-0014 d6). No actuator module is
    fabricated against `E0001-000002` or `E0001-000003`.

11. **An actuator module expands its own drive lines on-module** *(rev 1)*. A class covering a
    whole medium commands more elements than the ADR-0014 decision 5 header carries in PWM, GPIO
    and ADC. The module generates the additional drive and setpoint lines, and reads the
    additional states, over the I²C the header already provides; the header contract is
    unchanged. Three groups stay on dedicated header lines: the interlock states of decision 7,
    the driver fault outputs, and the 1-Wire bus. The fast current or duty loop stays in the
    driver silicon and the interlocks stay in hardware, so a bus-rate setpoint bounds no
    protective function.

## Alternatives considered

**A. Extend the `M0n` sensor namespace to actuators.** *Rejected:* the sensor number equals its class ID; continuing the sequence past `M07` would collide with the sensor range `0x01`–`0x7F`.

**B. Number actuator classes from `0x80` with no name form, using the hex ID alone.** *Rejected:* every other artifact class in the project carries a readable identifier (ADR-0017); a bare ID is not usable in a BOM or a registry row.

**C. One actuator class per device rather than per medium.** *Rejected:* multiplies classes with each hardware change; the medium an apparatus acts on is stable across device choices.

**D. A single generic actuator class parameterized by EEPROM contents.** *Rejected:* the safe output, interlock set and demand range differ per class and are safety-relevant; one class would defer them all to runtime data.

**E. Interlocks in firmware with a hardware watchdog.** *Rejected:* ADR-0015 d11 requires the trip to be independent of the MCU.

**F. Publish process variables from the actuator that drives them.** *Rejected:* creates a second source for a quantity a sensor class already owns (ADR-0000 d3), and couples control feedback to the element being controlled.

**G. Pre-reserve the full actuator ID range against a planned class list.** *Rejected:* ADR-0017 d5 assigns identifiers at commit; reservation invites renumbering when the list changes.

**H. Partition actuator classes by regulated variable — one class each for temperature, vapour and CO₂.** *(rev 1) Rejected:* it applies a different grouping rule to actuators than ADR-0014 applies to sensors; it gives one 0.43 m³ air volume three classes sharing one rejection loop, so a loop failure takes all three under any partition; and at deployment scale it splits one air-handling unit across three nodes.

**I. Widen the ADR-0014 decision 5 header to carry every actuator line directly.** *(rev 1) Rejected:* the header is common to every module class, so widening it for one class changes every module's connector and every carrier revision — for lines that need no better than bus rate.

**J. Put an MCU on the actuator module and command it as a smart peripheral.** *(rev 1) Rejected:* a second firmware image, toolchain and update path in answer to a pin count; decision 11 closes the same gap with one I²C part.

## Consequences

### Positive

- `O-101` closes; `spec/A01-CLIMATE-specification.md` has a governing record for its class name, demand range, safe outputs and both interlocks.
- The safe-output table gives every future actuator element its fail-safe without a per-class decision.
- Separating process- from self-protective interlocks makes the requirement derivable from the element rather than negotiated per design.
- Decision 8 keeps one source per quantity, and decision 2 pairs each actuator class with the sensor class observing the same medium, so the pairing states where a quantity comes from.
- Three actuator classes carry the reference cabinet, one of them specified and none reserved.

### Negative

- Decision 10 blocks every actuator module behind carrier `E0001-000100`, which does not exist. Phase 2 cannot start until that carrier is designed.
- A class spanning a medium commands many elements, so decision 4 costs subject IDs: `A01-CLIMATE` consumes one per element rather than one per node.
- Loss of one climate node leaves air temperature, humidity and CO₂ uncommanded together.
- Decision 6's non-de-energized safe outputs require a wiring discipline that a de-energize-everything rule would not.
- Decision 11 puts an I²C part in the drive path of every actuator module, and its failure mode joins each specification's verification.

## Deferred decisions

- **DSDL types for actuator commands.** The signed/unsigned demand with a validity deadline has no wire type (`O-102`). ADR-0005 governs the vocabulary.
- **Per-class specifications** for `A02-LIGHT` and `A03-ROOTZONE`, and their class IDs with them.
- **Commissioning of node-local conditioning constants** — current limits, dead bands, dwell, slew — sit with ADR-0028's custody model; the mechanism for writing them is not specified.
- **Actuator instance identity and zone tagging** at multi-instance scale, analogous to ADR-0014 d1 and d7.

## References

- ADR-0003: Strawberry day-neutral profile — regulated variables, CO₂ as a variant.
- ADR-0014 (rev 4): Sensor node taxonomy — decision 5 header contract, decision 6 class ID, decision 9 (this record's origin), deferred ventilation / pollination subsystem boundary.
- ADR-0015: Gateway profile caching and local control loops — decision 11 interlock independence, decision 18 cascade and demand form.
- ADR-0016 (rev 1): Empirical survey and state-space modeling — biological subspace, decision 2.
- ADR-0018: Power distribution and rail monitoring — decisions 5, 7, 8, 9, 10.
- ADR-0028: Commissioning sequence and trim custody.
- [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
