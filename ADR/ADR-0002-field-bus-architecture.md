<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0002 — Field bus architecture: Cyphal/CAN, core board on a carrier, Raspberry Pi gateway

- **Status:** Accepted
- **Date:** 2026-05-16; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0004](ADR-0004-gateway-host-hardening.md) (gateway host hardening behind decision 7), [ADR-0005](ADR-0005-dsdl-foundation.md) (the DSDL vocabulary of decision 1), [ADR-0007](ADR-0007-pki-and-secure-element-identity.md) (the PKI the secure element of decision 3 serves), [ADR-0014](ADR-0014-sensor-node-taxonomy.md) (sensor modules and the module-class strap behind decision 5), [ADR-0016](ADR-0016-empirical-survey-and-state-space-modeling.md) (the state estimator decision 11 serves), [ADR-0017](ADR-0017-component-document-and-instance-identification.md) (firmware rooted on the carrier, decision 5), [ADR-0018](ADR-0018-power-distribution-and-rail-monitoring.md) (power distribution upstream of decisions 3 and 8), [ADR-0019](ADR-0019-purchased-part-%28SP%29-identification.md) (purchased-part identity of the core board), [ADR-0020](ADR-0020-gateway-persistence-model.md) (gateway persistence, decision 6)

## Context

A cabinet holds 15–20 sensors and actuators across climate, lighting, irrigation, plant monitoring, pollination and power. At silicon level they are heterogeneous (I²C, SPI, analog, PWM, GPIO); to the platform they must present one interface. Every deployment, from community self-build to commercial managed, uses the same MCU, firmware, protocol and carrier: uniformity simplifies development, service, inventory and training (ADR-0001 dec 8).

The bus must be deterministic, because some loops are safety-relevant and must not be starved by telemetry; fault-containing, because a shorted or stuck node must not silence it; and modular, because replacing a sensor must not reconfigure the bus, the gateway or the platform. Installation is furniture-grade: one twisted pair through the cabinet, not a star. The MCU must be mainstream with predictable multi-distributor supply, and the design surface the project owns is kept to integration concerns where open modules exist.

## Decision

1. **Cyphal is the application-layer protocol.** It is open under Apache-2.0 and typed by DSDL; the vocabulary is ADR-0005's.
2. **The smart node is an STM32F4 core board on a project carrier.** The core board supplies MCU, crystals, power conditioning, USB DFU and reset/boot controls; pin-compatible MCUs of the same family fit the same socket as a supply hedge. The selected part is held as a purchased part (ADR-0019).
3. **The carrier is an integration-only board.** It hosts the core-board sockets, a secure element on every board (ADR-0007), the CAN transceiver, termination fitted only at the bus ends, the sensor-module header (ADR-0014), paired bus connectors in and out that also carry the field rail, a local buck to the single on-carrier 3.3 V rail, a CAN activity indicator and bus ESD protection. Its design is published under CERN-OHL-S; part values are in the carrier specification.
4. **The hardware reference repository holds the carrier design and a snapshot of the upstream core-board documentation.** The carrier schematic, layout and BOM are under CERN-OHL-S. The snapshot keeps the core board buildable against upstream loss, with provenance; its content and licence are decision 10's.
5. **One firmware image runs every node type; the personality is selected at boot by the sensor module's class identification.** Drivers conform to a common interface, and the image is versioned on the carrier (ADR-0017 dec 16, ADR-0014 dec 6). Reference firmware is AGPL-3.0-or-later.
6. **The gateway is a Raspberry Pi with an isolated two-channel CAN HAT, running an asyncio Python service on Pycyphal with Nunavut-generated bindings.** The model scales with deployment headroom, from the smallest supported model for one cabinet to industrial SBCs on the same SocketCAN stack; the second CAN channel is reserved for safety-bus separation or debug. What the gateway stores locally, and on what medium, is ADR-0020's.
7. **The security boundary is the gateway.** The CAN domain inside the cabinet is a trusted zone with no per-node authentication on the bus. PKI is ADR-0007's; host hardening is ADR-0004's.
8. **Physical layer: a linear bus at a fixed 500 kbit/s classic CAN.**
   - One rate for every node and the gateway, no run-time negotiation; it leaves ample headroom for a cabinet's nodes and a bus length far beyond any cabinet or zone run.
   - Termination at the two physical ends only; no stars.
   - Shielded twisted pair, shield grounded at one point near the gateway.
   - The field rail (ADR-0018) runs on separate conductors beside the pair and is regulated per node; the pair carries signal only.
9. **Domain mapping happens at the gateway.** Each node belongs to exactly one IndustryFlow `module` entity of the cabinet `machine`: Subject-IDs identify the data, the Node-ID identifies the physical node, and the gateway resolves Node-ID to `module` by configuration. Slot tagging is applied at the gateway from a node-to-slot mapping, never encoded in the frame.
10. **The upstream core board publishes no gerbers and no licence.** The snapshot is what upstream publishes — schematic, board outline, 3D model — filed under the core board's purchased-part identity with upstream copyright and an unstated-licence marker, not the CERN-OHL-S default. Decision 2's board selection and decision 4's retention stand.
11. **The gateway is the bus time-synchronization master.** It is the only master, publishes the standard synchronization message at high priority strictly inside the type's maximum publication period, and every node fills its telemetry timestamps from that time base.
    - The gateway holds the lowest Node-ID on the bus, so master election is deterministic.
    - A node applies master time as an offset where a timestamp is written, never as a step of its monotonic clock, which carries transport deadlines and transfer-ID timeouts.
    - On loss of the master a node reports the type's "unknown" value, not a frozen offset, which a consumer could not detect as stale.
    - Accuracy is milliseconds, bounded by the SPI-attached gateway controller and by polled reception on the node; that suffices for a shared sample origin across nodes and for no finer claim.
12. **The copper-layer count of a board is set by its specification.** Each board takes the layers its layout needs; the count is not an architectural decision.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | CANopen | Dated tooling, archaic EDS workflow; DSDL is the closer match |
| 1 | MQTT over WiFi per node | Not deterministic, no fault containment, modularity in name only |
| 1 | Modbus RTU over RS-485 | Polling-based, no typed payloads, weaker tooling |
| 1 | USB sensor hub | Anti-modular; the hub is a single point of failure |
| 2 | Two hardware tiers: prototype clone board, production STM32G4 board | Small price gap, two carriers to engineer and document, lowest-common-denominator caveats on mixed buses, long-tail clone-supply risk; contradicts uniformity |
| 2 | STM32G431 on a third-party breakout | Niche breakout, vendor dependency without published hardware |
| 2 | STM32F411 breakout | The STM32F411 has no CAN peripheral |
| 2 | MCU placed directly on the carrier | An open core board meets the same requirements at lower EE cost, faster bring-up and better supply hedging |
| 3 | Per-carrier barrel-jack power input, separate from the bus wiring | Power and bus share one daisy-chained connector run from the distribution board (ADR-0018 dec 13) |
| 5 | Static personality: one firmware binary per node type | A change to shared code would multiply releases per type; the personality is a hardware attribute read at boot (ADR-0017 dec 16) |
| 6 | Raspberry Pi 5 only, with a persistent SQLite buffer | Gateway model is a headroom choice, not a fixed requirement; local persistence is decided by ADR-0020, not here |
| 6 | RAM-only ring buffer, reboot-during-outage loss accepted | No cloud sink exists before IndustryFlow arrives, so the gateway must hold a durable record (ADR-0020) |
| 6 | Allwinner T527-class SBC with native CAN and NPU | Mainline Linux support immature; revisit for commercial gateways needing on-edge ML or an extended temperature range |
| 8 | CAN FD throughout (STM32G0/G4) | Classic-CAN STM32F4 is more mainstream; the 8-byte payload is handled by node aggregation and multi-frame transfers |
| 8 | 1 Mbit/s | Shortens maximum bus length and tightens transceiver and stub margins for no throughput need |
| 8 | A bit rate below 500 kbit/s | Wastes headroom with no length benefit inside a cabinet |
| 11 | Publish exactly at the maximum publication period | Ordinary scheduling jitter pushes about half the intervals past the limit, and slaves discard those pairs |
| 11 | Step the node clock to master time | Corrupts transport deadlines and transfer-ID timeouts carried by the monotonic clock |
| 11 | Hold the last offset when the master is lost | A stale timestamp presented as truth is undetectable to a consumer |

## Consequences

**Positive**

- One MCU family, one firmware codebase, one carrier and one core board serve every deployment.
- EE effort goes into integration, not into re-implementing MCU minimum-system designs.
- Pin-compatible MCUs in one socket hedge supply within one family.
- Bus load measured by exact bit accounting is about 1 % on a two-node bus and a few percent for the full sensor fleet, so capacity does not limit module count; the loads that matter are gateway-paced bursts below telemetry priority.
- A failed node is unplugged and replaced; CAN error confinement contains faults; arbitration gives safety loops deterministic access.
- A secure element on every carrier makes hardware identity universal without a hardware variant.
- The gateway scales from an entry-level Raspberry Pi to industrial SBCs on one software stack.

**Negative**

- Classic CAN's 8-byte payload applies everywhere: multi-value sensors pack or use multi-frame transfers, and thermal frames need aggregation or file transfer (ADR-0005 dec 11).
- The node is a three-board stack (core board, carrier, sensor module); a soldered-down core board is the fallback for field-reliability concerns.
- Ready-built core boards remain a dependency on one vendor; the snapshot (decisions 4, 10) mitigates but holds no fabrication data.
- Each sensor module needs its own driver work before it yields data.
- pH and EC analog front-ends sit on the sensor module, concentrating analog design in M03 (ADR-0014).
- Forensic capability lives on the IndustryFlow side, which must provide it (ADR-0004).

## Deferred

| Open question | Settled by |
|---|---|
| Operational policy for commercial managed deployments: QA, supply-chain control, lifecycle, warranty | ADR-0010 |
| IndustryFlow-side audit-trail schema | IndustryFlow platform roadmap |
| Native-CAN industrial SBC as gateway | Maintainers, when mainline Linux support for that SoC class matures |
| Sub-millisecond time synchronization | A use that needs it; requires hardware receive timestamping on the node |

## References

- `spec/E0001-R-specification.md` — carrier parts, pin assignment, layer count.
- `REGISTRY.md` — the core-board purchased part and its snapshot.
- `gateway/README.md`, `gateway/busload.py` — gateway runbook, bus-load accounting.
- WeAct STM32F4 64-pin core board: https://github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard
- Cyphal specification; libcanard, Pycyphal, Nunavut (OpenCyphal).
- STM32F405 reference manual (RM0090); ATECC608, MCP2515, SN65HVD230 datasheets.
