<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0018: Cabinet-level power distribution and consumption metering; separation of distribution, monitoring, and switching

- **ID:** ADR-0018
- **Status:** Proposed
- **Date:** 2026-06-03
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0014, ADR-0015
- **Relates to:** ADR-0017 (E-module identification), ADR-0007 (planned, PKI), actuator-taxonomy ADR (deferred per ADR-0014 decision 9)

## Context and problem

The project needs a defined place for cabinet-level power: where the rails enter from the power supplies, how power is fanned out to the field nodes, where consumption is measured, and where loads are switched. ADR-0002 rev 3 fixed only the per-node side (each carrier derives its own 3.3 V from a distributed rail via a local TPS54302 buck (3.3 V is the only on-carrier rail), bus carries signal, separate power rail). ADR-0014's M05-SAFETY monitors loads with INA226s and per-load terminal blocks **on the zone sensor module**. Neither describes a cabinet-level distribution point, nor the granularity at which power is measured.

An initial attempt to design a single universal board that both **distributes** the rails and **switches** the loads ran into a wall: actuator loads can be +12 V, +24 V, or 230 V mains, and a board that must switch all of them drags in galvanic isolation, optocoupler gate drive, isolated I²C for the power-side current monitor (ADuM2250-class), multiple ground domains, and mains creepage/clearance — all co-located with the STM32, the CAN transceiver, and analog current sensing. This is disproportionate and unsafe in topology: mains has no business on a board carrying logic and analog front-ends.

The resolution is to stop conflating three distinct concerns. **Distribution, monitoring, and switching are separate problems with separate natural locations.** Once switching is removed from the central board, the voltage-universality and isolation problems disappear from it entirely, and the board becomes trivial. This ADR fixes that separation and specifies the central distribution + monitoring board.

## Decision drivers

- **No over-engineering.** A single board must not try to switch 12 V, 24 V, and 230 V. The complexity that creates is a signal that the concern is in the wrong place.
- **Separation of concerns.** Distribution ≠ monitoring ≠ switching. Each belongs where its physics belongs.
- **Voltage-specific complexity belongs where the voltage is.** Isolation, gate drive, creepage, and switch sizing are properties of a load at a voltage — they belong at the actuator, not centralized.
- **Mains is physically segregated.** 230 V never shares a board with the STM32, CAN, or analog sensing. It is switched locally by an SSR at the load, controlled by an isolated low-voltage signal owned by the actuator.
- **Simple cabinet wiring.** Distribute the low-voltage rails on copper at one central board; switch loads locally near them, rather than routing every load into and out of a central switch board.
- **Power accounting as central aggregates.** Total consumption at the source, plus per-section aggregates (sensor section; DC-actuator section; AC-actuator section). Per-individual-load metering is over-engineering — the gateway already knows what it commanded, so aggregate + command state localizes anomalies in software.
- **Reuse, not new node classes.** The board is the physical baseboard of the existing M05 power/safety node, built from existing parts (INA226, carrier, Micro-Fit), not a new bus participant class and not a carrier variant.
- **Phase discipline.** Phase 1 is data collection with no actuators; switching and its isolation are therefore out of scope for the Phase 1 board and deferred to the actuator-taxonomy ADR.
- **Node liveness is already on the bus.** The Cyphal heartbeat reports node health, so per-node power metering would add no information.

## Decision

### Separation principle

1. **Three power concerns are kept distinct and located separately:**
   - **Distribution** — getting fused low-voltage rails to where they are needed — is performed by one central board.
   - **Monitoring** — measuring consumption — is performed as **central aggregates**: DC sections by INA226 on the central board, the AC-actuator section by a DIN kWh meter read over S0. Never per individual load.
   - **Switching** — energizing/de-energizing or modulating a load — is performed **at the actuator node**, sized to that actuator's voltage and current. It is never performed on the central distribution board.

### Central power distribution and monitoring board

2. **One central power distribution + monitoring board per machine (cabinet)**, located in the electrical/distribution cabinet immediately downstream of the power supplies. It is the cabinet-level power entry and fan-out point.

3. **Phase 1: a single `+12 V` SELV input.** There is no `+24 V` rail on the board in Phase 1 — the `+24 V` power section is added together with the actuators in a later phase. Common ground throughout; the inter-domain isolation question only arises once a switching/power domain exists, and even then it lives at the actuator (decision 8), not here.

4. **Input fusing sized to the actual load.** In Phase 1 the `+12 V` input is fused to its real (small) sensor-side load. The `+24 V` power section, when added, gets its own input fusing. Per-load/per-branch protection is **not** placed centrally — it lives at the actuator (see decision 8).

5. **Consumption is metered as central aggregates, never per individual load.**
   - **DC domain — INA226 on the common logic ground.** Phase 1: `total` (board input) and `sensor-section` feed; the difference isolates the node's own draw. When the DC power section is added: a third INA226 on the `DC-actuator-aggregate` feed (fans, dosing/irrigation pumps — all of which have standard 12/24 V DC variants). Each INA reports bus voltage + current → true power, source-side.
   - **AC domain — COTS DIN kWh meter, read over S0.** Mains-fed actuators (a 230 V fan, pump, or heater) are metered by a standard DIN-rail kWh meter on the common mains-actuator feed, read through its **S0 pulse output** (DIN EN 62053-31): the node counts pulses on a spare GPIO, accumulates energy, and publishes it on Cyphal. The S0 output is opto-isolated, so mains never reaches the node. This is a **current-phase** capability, not a deferred commercial-tier one — mains fans/pumps are ordinary cabinet actuators.
   - **The architectural commitment is the S0-counting contract, not a specific meter.** S0 is the universal, vendor-independent meter interface — present on essentially every DIN energy meter, single- and three-phase, direct-connect and CT-based, from commodity to industrial. The node firmware (count Wh pulses → publish energy) is **identical at every scale**; only the COTS meter behind it changes — a single-phase meter for an apartment, a *single* three-phase meter for a three-phase site (one device, not three single-phase meters). This is ADR-0014's all-scales principle applied to metering, and it is the opposite of a niche commitment.
   - **No M-Bus, no Modbus on the metering path by default.** S0 carries no protocol — just edge counting — keeping the node on its single application bus (Cyphal) and avoiding the polling meter protocols ADR-0002 rejected (alternative H). A three-phase Modbus meter (per-phase, power-factor) is an **additive opt-in** on the same node where that data is genuinely required, not a redesign.
   - Per-individual-load (per-actuator) metering is deliberately **not** done (decision 8, alternative C).

6. **No switching elements on this board.** No MOSFETs, no relays, no SSRs, no load switching of any kind.

7. **No inter-domain galvanic isolation on this board.** Because the board switches nothing, the isolation drivers (switching noise, inductive kickback, fault isolation between a logic domain and a switching domain) do not apply here. There are no optocouplers and no I²C isolator on this board; the INA226s sit on the common logic ground. When the `+24 V` power section is later added, it can be measured high-side with its INA226 still on the logic ground (common-mode range covers 24 V), so even then its I²C crosses no barrier; isolation, if ever needed, lives at the actuator.

### Switching at the actuator (deferred detail)

8. **Load switching and per-branch protection live at the actuator and the branch; per-load monitoring does not.** Each actuator node owns a switching element sized to its load: a MOSFET for low-voltage DC, an SSR or relay for mains. Per-branch overcurrent protection (fuse/PTC) lives at the branch. **Mains (e.g. 230 V) is switched by a local SSR at the load**, fed directly from mains, controlled by an isolated low-voltage signal; it does not enter any board carrying logic or analog sensing. Consumption, however, is **not** metered per actuator — it is metered centrally as aggregates (decision 5): DC actuators by the board's DC-actuator INA226, AC actuators by the DIN kWh meter on the mains feed. The gateway already knows which actuators it commanded (ADR-0015), so aggregate power plus command state attributes anomalies to specific actuators in software, without per-node metering hardware. The full actuator design (switch types, isolation, command semantics) is owned by the deferred actuator-taxonomy ADR (ADR-0014 decision 9); control flows as Cyphal commands from the gateway per ADR-0015.

### Relationship to the M05 power/safety node

9. **This board is the M05 sensor module itself, not a separate baseboard.** It satisfies the standard sensor-module header contract (ADR-0014 decision 5): module-ID straps `0b101`, the INA226s and any TMP117 on the header I²C, reed/leak on header GPIO/ADC, the S0 pulse on a header GPIO. Because it is the larger, heavier board (fuses, connectors, field-lead terminals, the DIN-meter S0 input), the small carrier (WeAct STM32F405) mounts **onto** it as a mezzanine — the size-driven inversion of the usual module-on-carrier stack — with the board itself fixed in the cabinet (DIN/screws) and serving as its own support. One integration detail: the sensor-module header carries 3V3 *from carrier to module*, **not** raw 12 V, so the carrier's 12 V is fed board-to-board from this board's `+12 V` rail to the carrier's power input, separate from the signal header. This is why the `total` INA226 (decision 5) includes the node's own draw and the `sensor-section` INA the external feed. The safety interlocks (TMP117 over-temperature, reed door switch, leak detection) connect on leads to the field per ADR-0014 decision 3. The board performs no switching and exposes no command surface.

10. **The hardware over-temperature safety trip remains hardware-independent and field-located.** Per ADR-0015 decision 11, the over-temperature cutoff must trip independently of the MCU, the gateway, and the cloud, and the sensor must sit in the grow volume. For the **trip** path, prefer an analog sensor (thermistor/PT1000) plus a comparator driving the relay-enable, rather than an I²C TMP117, because the trip must not depend on software and I²C does not span a long lead to the canopy. A TMP117 may still provide the **reported** temperature; it is not the trip element.

11. **M05 sensor complement carried on this board.** Being the M05 node, the board carries M05's full sensor set (ADR-0014 M05), with the field-located sensors on leads per ADR-0014 decision 3:
    - **Power** — 2–3× INA226 on the board I²C (total, sensor-section, and — with the power section — DC-actuator aggregate), plus the AC-actuator DIN kWh meter read over S0 (decision 5).
    - **Over-temperature** — the analog trip sensor (thermistor/PT1000 + comparator → relay-enable) for the hardware-independent cutoff, and optionally a TMP117 for the *reported* cabinet temperature (decision 10), on a lead to the grow volume.
    - **Door** — reed switch on a GPIO, wired to the cabinet door.
    - **Leak** — leak-detection strip on an ADC channel, under the reservoir/pumps.
    Module-ID straps identify the board as M05 (`0b101`, ADR-0014 decision 6); firmware activates the sensors that respond via sensor-presence probing (ADR-0014 decision 8).

### Topology

12. **The board does not replace per-node regulation.** It sits upstream of the per-node buck regulators (ADR-0002 rev 3): each carrier still derives its own 3.3 V from the distributed rail. The board provides clean, fused, metered rails to the bus.

13. **CAN remains a linear bus through the board; no star.** Node liveness/health is already observable via the Cyphal heartbeat, so per-node power metering is unnecessary and star power (which would conflict with the linear bus) is avoided. Power and CAN may share the Micro-Fit run from the board to the first node.

### Identification

14. **The board is a distinct electrical-discipline E-module under ADR-0017** (one bare design, one assembly E-number), not a variant of the universal carrier. The universal-carrier no-variant principle (ADR-0017 decision 4) is preserved.

## Alternatives considered

**A. One universal central board that distributes and switches all actuator voltages (12/24/230 V), with per-branch MOSFET/relay and the necessary isolation.** *Rejected:* forces a single board to span SELV and mains, dragging in creepage/clearance, isolated gate drive, isolated I²C for the power-side INA, and multiple ground domains, and co-locates mains with the STM32 and analog sensing. Switching belongs at the actuator.

**B. Two galvanically isolated domains on the distribution board** (logic vs. `+24 V` power), with optocouplers on gate/relay drive and an ADuM2250-isolated I²C on the power-side INA. *Rejected for this board:* the isolation drivers exist only if the board switches loads; since it switches nothing, common-ground rail metering is sufficient. Isolation returns locally, at actuator nodes, when a specific load warrants it.

**C. Per-load metering (an INA at every sensor and actuator node).** *Rejected:* for sensor nodes, liveness/health is already on the bus via the Cyphal heartbeat, so per-node power adds nothing and would force star power against the linear CAN bus. For actuators, aggregate metering plus the gateway's command knowledge (decision 8) localizes anomalies in software, so per-actuator hardware metering is over-engineering. Central aggregates (decision 5) plus the heartbeat and command state are sufficient.

**D. Keep M05 as in ADR-0014** (INA226 + per-load terminal blocks on the per-zone sensor module). *Rejected/refined:* co-locating load monitoring with the zone module requires kelvin wiring to scattered loads and per-load terminal blocks on a small board. Centralizing the metering as source-side aggregates (decision 5) matches where power actually originates (the cabinet), while switching moves to where the load actually is (the actuator).

**E. No central board — just a fused `+12 V` rail daisy-chained to the nodes.** *Rejected:* loses central power accounting and clean cabinet distribution. A small distribution + metering board is cheap and provides both.

**F. AC actuator energy via a metering IC designed onto our own board** (ADE-class + CT + mains front-end). *Rejected:* puts a mains-referenced measurement domain, creepage/clearance, and isolation onto our PCB — the same category error as switching mains centrally. A COTS DIN kWh meter does true-RMS/PF metering internally and hands out an isolated S0 pulse; we count pulses and stay in the LV domain.

**G. AC metering over Modbus or M-Bus.** *Rejected as default:* Modbus reintroduces RS-485 and the polling protocol ADR-0002 rejected (alternative H); M-Bus adds a meter-specific bus and master. S0 (decision 5) is a no-protocol, vendor-independent pulse counted on a GPIO, keeping Cyphal as the single application bus. Modbus is retained as an opt-in only where per-phase / power-factor data is required at larger scale.

## Consequences

### Positive

- The "what voltage?" problem disappears from the central board — it only ever carries low-voltage SELV rails on a common ground.
- Mains is physically segregated from logic and analog sensing by construction.
- The Phase 1 board is trivial: rails in, one fuse per rail, one INA per rail, fan-out. No switching, no isolation, no optocouplers, no I²C isolator.
- Cabinet wiring is cleaner: loads are switched locally near them, not routed through a central switch board.
- Power accounting is central and uniform: DC sections by INA226, AC actuators by a COTS DIN meter over S0. The node-side contract (count pulses / read I²C → publish on Cyphal) is identical from a single-phase apartment to a three-phase site; only the COTS meter swaps. AC actuator energy is handled **now**, with a cheap commodity meter, not deferred.
- Reuses INA226, the carrier, and Micro-Fit; the board *is* the M05 sensor module (standard header contract), not a new node class and not a carrier variant.
- Forward-compatible: actuator-specific switching and isolation are designed per actuator when the actuator taxonomy lands, with no redesign of the central board (including a future mains variant).

### Negative

- **M05's realization diverges from ADR-0014 and the Phase 1 BOMs** (which place INA226 + per-load terminal blocks on the per-zone module). Those documents must be reconciled to this ADR; tracked as a gap (ADR remains the target, divergence is tracked, per project discipline).
- Aggregate-only metering does not directly resolve which actuator misbehaved; localization relies on correlating aggregate power with the gateway's command state in software rather than reading it off a per-actuator monitor.
- The AC path adds a COTS DIN kWh meter as a system component (procurement, cabinet space, S0 wiring), and S0 time-resolution is coarse for low-power loads — mitigated by choosing a meter with a high enough pulse constant (imp/kWh).
- One more board class / E-module to design and maintain, though it is simple.
- The board is per-machine; at greenhouse scale a distribution hierarchy (main + sub-distribution) may be needed. Out of scope here, noted.

## Relationship to other ADRs

- **ADR-0002 (rev 3)** — supplements the power physical layer. Per-node local regulation is unchanged; the distribution board sits upstream of the per-node buck regulators and provides fused, metered rails. CAN remains linear; no star.
- **ADR-0014** — refines M05-SAFETY. Power/current monitoring moves from per-load INA226s on the per-zone module to source-side INA226 aggregates on the central board (total / sensor / DC-actuator), plus a DIN kWh meter over S0 for AC actuators; per-individual-load monitoring is dropped, not relocated. The board *is* the M05 module (carrier mounted on it; straps `0b101`; standard header contract). Safety interlocks (TMP117/reed/leak) remain on leads per decision 3. The M05 description is to be reconciled.
- **ADR-0015** — switching is performed by actuator nodes on Cyphal command from the gateway; the central board performs no switching and exposes no command surface, consistent with the single-mutation-channel model and the "no actuator command API" constraint. Hardware safety interlocks remain hardware-independent (decision 10).
- **ADR-0017** — the board is a distinct electrical-discipline E-module, not a carrier variant.
- **Actuator-taxonomy ADR (deferred, ADR-0014 decision 9)** — owns load switching, per-actuator monitoring, per-actuator protection, and per-actuator isolation.

## Deferred decisions

- Phase 1 `+12 V` input fuse value (sized to the real, small sensor-side load). The `+24 V` power-section input fusing and per-actuator-branch protection (cartridge vs. PTC vs. eFuse; the ~6 A figure) move to the design of the power section / actuator nodes.
- Reconciliation edits to ADR-0014 (M05) and the Phase 1 BOMs (`sensor-modules-phase1-bom.md` M05, `procurement-phase1-data-collection.md` M05).
- Board connector set (rail inputs, CAN, fan-out), field-lead terminals for TMP117/reed/leak, and the S0 input conditioning (pull-up, debounce) for the DIN meter.
- Specific COTS kWh meter (single- vs three-phase) per deployment and its pulse constant (imp/kWh) chosen for adequate S0 time resolution at the expected load power — hardware procurement, not architecture.
- The over-temperature trip implementation (thermistor/PT1000 + comparator → relay-enable) and where the relay-enable acts in the (deferred) actuator path.
- Greenhouse-scale distribution hierarchy (main + sub-distribution) for multi-zone deployments.

## References

- ADR-0001: IndustryGrow framing — machine/module data model, power/safety subsystem.
- ADR-0002 (rev 3): Field bus architecture — per-node regulation, power rail, linear CAN bus.
- ADR-0014: Sensor node taxonomy — M05-SAFETY, short-lead extension (decision 3), partial-BOM (decision 2), deferred actuator taxonomy (decision 9).
- ADR-0015: Gateway profile and control loops — actuator commands via Cyphal, hardware safety interlocks (decision 11).
- ADR-0017: Component/document identification — E-module scheme, universal-carrier no-variant principle.
- `procurement-phase1-data-collection.md`, `sensor-modules-phase1-bom.md`.
- Texas Instruments INA226 datasheet.
- DIN EN 62053-31 — S0 pulse output for electricity meters.
- Example COTS DIN kWh meters with S0 pulse output: Eastron SDM120 / SDM230 (single-phase), Eastron SDM630 (three-phase); commodity equivalents DDS238-series (single-phase), DTS238-series (three-phase). Meter is a swappable line item; the S0-counting contract is the commitment.
