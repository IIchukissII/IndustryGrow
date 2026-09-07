<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0031: Actuator node taxonomy

- **ID:** ADR-0031
- **Status:** Accepted
- **Date:** 2026-09-07
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0014 (rev 4), ADR-0015, ADR-0016, ADR-0018

## Context and problem

ADR-0014 decision 9 placed actuator modules out of scope and required their own taxonomy ADR.
Three records since have deferred content to it: ADR-0015 (which actuator nodes exist, what
commands they accept, the safe output per class), ADR-0018 decision 8 (switch types, isolation,
command semantics), and the draft environmental-actuator record (switching, monitoring,
protection, isolation).

The first actuator specification (`spec/A01-THERMAL-specification.md`, `E0011`) is written and
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

## Decision drivers

- **The class-ID range is assigned but the namespace has no form.** ADR-0014 d6 fixes `0x80`–`0xFE`; nothing states how a class is named or numbered within it.
- **ADR-0015 d18 defers the safe output per class to this record.** A node cannot implement its fail-safe without it.
- **ADR-0018 d10 protects the process, not the actuator.** A thermoelectric module reaches its solder limit while the grow volume is still cold (`spec/A01-THERMAL-specification.md` `T2`); the existing trip does not fire.
- **A demand's sign is per class.** A heater is unipolar; a thermoelectric module and a reversible pump are not. ADR-0015 d18 fixes the demand's form, not its range.
- **Sense and switch are separated for M05 (ADR-0018 d9); the mirror rule for actuators is unwritten.** Nothing states what an actuator node may publish.

## Decision

1. **Actuator classes are named `A0n` and numbered `0x7F + n`.** `A01` is `0x80`, `A02` is
   `0x81`, and so on. The class ID is the module's identity on the bus (ADR-0014 d6); the `A0n`
   label is its name in specifications and in `REGISTRY.md`. Sensor classes keep the `M0n` form,
   where the number and the ID coincide; for actuators they do not, and the ID is authoritative.

2. **Actuator taxonomy — the classes of the reference cabinet.** One class per regulated
   variable of the biological subspace (ADR-0016), not per device:

   | Class | ID | Regulated variable | Specification |
   |---|---|---|---|
   | `A01-THERMAL` | `0x80` | Grow-volume air temperature | `spec/A01-THERMAL-specification.md` |
   | `A02-VAPOUR` | `0x81` | Leaf VPD | none — physical design in the environmental-actuator record |
   | `A03-LIGHT` | `0x82` | PPFD, spectrum, photoperiod | none |
   | `A04-DOSING` | `0x83` | EC, pH | none |
   | `A05-IRRIGATION` | `0x84` | Root-zone delivery | none |
   | `A06-AIRFLOW` | `0x85` | Air movement at the canopy | none |
   | `A07-CO2` | `0x86` | CO₂ concentration — non-baseline (ADR-0003 d8) | none |

   A class without a specification is a reserved identifier, not a commitment to build. New
   classes continue from `0x87`; identifiers are assigned when a specification is committed, not
   reserved in advance (ADR-0017 d5).

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

6. **Safe output by class.** The value a node drives when a demand goes stale, when an interlock
   trips, or at boot before the first valid demand:

   | Class | Safe output | Ancillary safe output |
   |---|---|---|
   | `A01-THERMAL` | zero drive | heat rejection at full |
   | `A02-VAPOUR` | closed | — |
   | `A03-LIGHT` | zero output | — |
   | `A04-DOSING` | stopped | — |
   | `A05-IRRIGATION` | stopped | — |
   | `A06-AIRFLOW` | **full** | — |
   | `A07-CO2` | closed | — |

   Every safe output is de-energized except `A06-AIRFLOW`, which is full, and `A01-THERMAL`'s
   rejection path, which is full. Where a class's safe output is not de-energized, the element is
   wired so that loss of the node, of the bus, or of the switching element produces that output
   without the node acting.

7. **Two interlock kinds, distinguished by what they protect.**

   | Kind | Protects | Sensor location | Required when |
   |---|---|---|---|
   | Process-protective | The grow volume and its contents | In the grow volume | The actuator can drive a process variable past a limit that harms the crop or the cabinet (ADR-0018 d10) |
   | Self-protective | The actuator itself | At the element | The element's own failure is faster than, or independent of, the process excursion the process-protective trip senses |

   Both are hardware: sensor → comparator → the switching element's enable, independent of the
   MCU, the gateway and the cloud. Firmware reads interlock state and can neither override nor
   re-arm it. An actuator may carry both; `A01-THERMAL` does.

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

## Alternatives considered

**A. Extend the `M0n` sensor namespace to actuators.** *Rejected:* the sensor number equals its class ID; continuing the sequence past `M07` would collide with the sensor range `0x01`–`0x7F`.

**B. Number actuator classes from `0x80` with no name form, using the hex ID alone.** *Rejected:* every other artifact class in the project carries a readable identifier (ADR-0017); a bare ID is not usable in a BOM or a registry row.

**C. One actuator class per device rather than per regulated variable.** *Rejected:* multiplies classes with each hardware change; the regulated variable is what the profile addresses and is stable across device choices.

**D. A single generic actuator class parameterized by EEPROM contents.** *Rejected:* the safe output, interlock set and demand range differ per class and are safety-relevant; one class would defer them all to runtime data.

**E. Interlocks in firmware with a hardware watchdog.** *Rejected:* ADR-0015 d11 requires the trip to be independent of the MCU.

**F. Publish process variables from the actuator that drives them.** *Rejected:* creates a second source for a quantity a sensor class already owns (ADR-0000 d3), and couples control feedback to the element being controlled.

**G. Pre-reserve the full actuator ID range against a planned class list.** *Rejected:* ADR-0017 d5 assigns identifiers at commit; reservation invites renumbering when the list changes.

## Consequences

### Positive

- `O-101` closes; `spec/A01-THERMAL-specification.md` has a governing record for its class name, demand range, safe outputs and both interlocks.
- The safe-output table gives every future actuator node its fail-safe without a per-class decision.
- Separating process- from self-protective interlocks makes the requirement derivable from the element rather than negotiated per design.
- Decision 8 keeps one source per quantity, so an actuator can be replaced without disturbing telemetry.

### Negative

- Decision 10 blocks every actuator module behind carrier `E0001-000100`, which does not exist. Phase 2 cannot start until that carrier is designed.
- Seven classes are named and six have no specification; the table will read as unfinished until they are written.
- Decision 4 costs command channels: an actuator with an ancillary element consumes two subject IDs rather than one.
- Decision 6's non-de-energized safe outputs (`A06-AIRFLOW`, `A01-THERMAL` rejection) require a wiring discipline that a de-energize-everything rule would not.

## Deferred decisions

- **DSDL types for actuator commands.** The signed/unsigned demand with a validity deadline has no wire type (`O-102`). ADR-0005 governs the vocabulary.
- **Per-class specifications** for `A02`–`A07`.
- **Commissioning of node-local conditioning constants** — current limits, dead bands, dwell, slew — sit with ADR-0028's custody model; the mechanism for writing them is not specified.
- **Actuator instance identity and zone tagging** at multi-instance scale, analogous to ADR-0014 d1 and d7.

## References

- ADR-0003: Strawberry day-neutral profile — regulated variables, CO₂ as a variant.
- ADR-0014 (rev 4): Sensor node taxonomy — decision 5 header contract, decision 6 class ID, decision 9 (this record's origin).
- ADR-0015: Gateway profile caching and local control loops — decision 11 interlock independence, decision 18 cascade and demand form.
- ADR-0016 (rev 1): Empirical survey and state-space modeling — biological subspace, decision 2.
- ADR-0018: Power distribution and rail monitoring — decisions 5, 7, 8, 9, 10.
- ADR-0028: Commissioning sequence and trim custody.
- [Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
