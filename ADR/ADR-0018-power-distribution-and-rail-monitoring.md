<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0018 — Cabinet power distribution and consumption metering

- **Status:** Proposed
- **Date:** 2026-06-03; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0002](ADR-0002-field-bus-architecture.md) (per-node regulation and linear bus this board feeds), [ADR-0014](ADR-0014-sensor-node-taxonomy.md) (M05-SAFETY, the module class this board realises), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (actuator commands and the hardware interlock rule), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (E-module identification of the board), [ADR-0031](ADR-0031-actuator-node-taxonomy.md) (actuator switching, isolation and interlocks)

## Context

ADR-0002 fixes only the per-node side of power: each carrier derives its own logic rail from a distributed supply rail. Nothing fixes where the rails enter the cabinet, how they fan out to the nodes, where consumption is measured, or where loads are switched.

Actuator loads run at 12 V, 24 V or mains. A single board that both distributes rails and switches every such load needs galvanic isolation, isolated gate drive, an isolated bus to its current monitors, several ground domains and mains creepage, all beside the MCU, the CAN transceiver and analog sensing. Isolation, gate drive and creepage are properties of a load at its voltage. The gateway knows what it commanded, and the Cyphal heartbeat already reports node health.

## Decision

1. **Distribution, monitoring and switching are separate concerns with separate locations.** Distribution of fused low-voltage rails is done by one central board; monitoring by one sensor-bus current monitor on that board plus a DIN kWh meter for all actuator energy; switching at the actuator node, sized to its load. Switching is never performed on the central board.
2. **One central distribution and monitoring board per machine,** immediately downstream of the power supplies. It is the cabinet's power entry and fan-out point.
3. **The board takes a single `+12 V` SELV input on a common ground.** The `+24 V` actuator section is added with the actuators, on its own supply; the inter-domain isolation question arises only with a switching domain, and is answered at the actuator (decision 8).
4. **Input fusing is sized to the real load of each input.** Per-load and per-branch protection is not central; it lives at the branch (decision 8).
5. **Consumption is metered as sensor-bus power plus an actuator energy total, never per load and never per section.**
   - One INA226 on the `+12 V` SELV feed reports source-side power for the whole sensor side, the board's own carrier included; the bus carries no actuator, so a total/section split would measure the same thing twice. It is a dedicated channel because sensor-bus anomalies are invisible in a whole-machine meter.
   - All actuator and other high-power energy, DC and AC, is taken from a COTS DIN kWh meter's opto-isolated S0 pulse output (DIN EN 62053-31), counted by the node and published on Cyphal; mains never reaches the node. There is no actuator-side current monitor.
   - The energy stream feeds offline anomaly models, not control; closed-loop regulation runs through the environmental sensors (ADR-0015).
   - The commitment is the S0 counting contract, not a meter: node firmware is identical at every scale and only the meter, single- or three-phase, changes per deployment.
   - S0 carries no protocol, keeping Cyphal the single application bus; a Modbus meter for per-phase or power-factor data is an additive opt-in on the same node.
6. **The board carries no switching element.** No MOSFET, relay, SSR or load switch of any kind.
7. **The board carries no inter-domain galvanic isolation.** It switches nothing, so the drivers for isolation (switching noise, inductive kickback, fault isolation between domains) do not apply. Isolation, where needed, lives at the actuator (ADR-0031 d9).
8. **Load switching and per-branch protection live at the actuator and the branch; consumption is not metered there.** Each actuator owns a switching element sized to its load, and mains is switched by a local SSR at the load under an isolated low-voltage signal, never entering a board with logic or analog sensing. The gateway attributes energy anomalies to actuators from aggregate energy plus its own command state (ADR-0015). Switch types, isolation and command semantics are ADR-0031's.
9. **M05 is an ordinary node, carrier plus M05 module, whose module board carries the cabinet power hardware.** It meets the standard header contract (ADR-0014 d5), identifies as M05 (ADR-0014 d6), and adds the input fuse, fan-out, field-lead terminals and S0 input to the module board. It is sense-only: it reports, never switches, hosts no interlock and exposes no command surface. Which board mounts on which is a layout detail with no architectural weight.
10. **The over-temperature safety trip is hardware-independent and lives at the heating actuator, not on M05.** ADR-0015 d11 requires a trip independent of MCU, gateway and cloud with its sensor in the grow volume; the trip is a switching function, so it belongs with the element it cuts. It is an analog thermistor or PT1000 on a lead in the grow volume, a comparator and the actuator's relay enable.
11. **M05's sensor complement is carried on this board.**
    - Power: the sensor-bus INA226 and the S0 meter input (decision 5).
    - Cabinet temperature: a TMP117 sensing the electrical-bay air, reported for electronics health and never a trip element; grow-volume temperature is M01-CLIMATE's.
    - Door: a reed switch, report and alert only, triggering no cutoff.
    - Leak: a strip on a lead to the reservoir and pump zone, report and alert only, with a software response (the gateway commands the pump off). Its electrodes are excited only during a sample, so a sustained wet event does not electrolyse them. A nutrient leak is slow and non-fire, so it takes no hardware interlock.
12. **The board does not replace per-node regulation.** It sits upstream of each carrier's own regulator (ADR-0002) and supplies fused, metered rails.
13. **CAN stays a linear bus through the board; power is not starred.** Node health is on the heartbeat, so per-node power metering adds nothing. Power and CAN may share one cable from the board to the first node.
14. **The board is a distinct electrical E-module under ADR-0017,** one bare design and one assembly, not a variant of the universal carrier. The carrier's no-variant principle (ADR-0017 d4) holds.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | One universal central board distributing and switching every actuator voltage, with isolation | Spans SELV and mains on one board, with creepage, isolated gate drive and multiple ground domains beside the MCU and analog sensing |
| 2 | No central board; a fused `+12 V` rail daisy-chained to the nodes | Loses central power accounting and cabinet distribution, which a small board provides cheaply |
| 5 | Three-way DC current split: total, sensor section, DC actuator aggregate | The `+12 V` board carries sensors only, so total and sensor section are one measurement |
| 5 | An INA226 on the DC actuator aggregate | The DIN meter captures all actuator energy; a second monitor adds hardware for no information |
| 5 | Actuator AC energy via a metering IC on the project's own board | Puts a mains-referenced domain, creepage and isolation on project PCB; a COTS meter meters internally and outputs an isolated pulse |
| 5 | Actuator metering over Modbus or M-Bus by default | Modbus reintroduces RS-485 and the polling protocol ADR-0002 rejects; M-Bus adds a bus and master. Kept as opt-in |
| 7 | Two galvanically isolated domains on the board, with optocouplers and an isolated I²C | Isolation drivers exist only if the board switches; it switches nothing |
| 8 | Per-load metering at every sensor and actuator node | Heartbeat covers sensor-node health and per-node power forces star power; aggregate energy plus command state localises actuator anomalies |
| 9 | M05 as the per-zone module with per-load INA226s and terminal blocks | Needs Kelvin wiring to scattered loads on a small board; power originates at the cabinet |
| 9 | M05 framed as an inverted mezzanine with architectural significance | Board stacking is a layout detail; M05 is an ordinary carrier-plus-module node |
| 10 | The over-temperature trip on M05 | The trip is a switching function and belongs with the element it cuts; M05 is sense-only |

## Consequences

**Positive**

- The central board carries only low-voltage SELV rails on a common ground, with no switching, isolation or actuator-side monitor.
- Mains is segregated from logic and analog sensing by construction.
- Loads are switched near themselves rather than routed through a central switch board.
- Power accounting is uniform from a single-phase apartment to a three-phase site; only the COTS meter changes.
- The board reuses the carrier, the INA226 and the bus connector, and is the M05 module rather than a new node class.
- Actuator switching and isolation are designed per actuator with no change to the central board.

**Negative**

- Which actuator misbehaved is inferred in software from aggregate energy and command state, not read from a per-actuator monitor.
- The DIN meter is an extra system component, and S0 time resolution is coarse for low-power loads unless the pulse constant is chosen for it.
- The board is one more E-module to design and maintain.

## Deferred

| Open question | Settled by |
|---|---|
| Distribution hierarchy (main and sub-distribution) at greenhouse scale | First multi-zone deployment |
| Whether the `+24 V` actuator section passes through the central board or enters each actuator direct from its supply | Next central-board revision |

## References

- `spec/E0006-R-specification.md` — the board: fuse, connectors, S0 conditioning, leak excitation.
- `REGISTRY.md` — `E0006`, `E0007` (distribution case), `SP0001` (DIN meter), `SP0003` / `SP0010` (SELV supplies).
- DIN EN 62053-31 — S0 pulse output for electricity meters.
- TI INA226 datasheet.
