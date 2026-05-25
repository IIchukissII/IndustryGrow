# ADR-0002 (rev 3): Field bus architecture — Cyphal/CAN over WeAct STM32F4 core board on custom carrier PCB with stateless-edge Raspberry Pi gateway

- **ID:** ADR-0002 (rev 3)
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Supersedes:** ADR-0002 (rev 2, same date)

## Context and problem

ADR-0001 committed IndustryGrow to an open-core model serving deployments from apartment cabinets to commercial facilities. This ADR decides *how* the field-level instrumentation works: which bus, which protocol, which hardware, which gateway, and where the security boundary sits.

A cabinet contains ~15–20 sensors and actuators across functional subsystems (climate, lighting, irrigation, plant monitoring, pollination, power/safety). They are heterogeneous at the silicon level — I²C, SPI, analog, PWM, GPIO — but must present a uniform interface to the platform. **Every deployment — community self-build, hobbyist, commercial managed — uses the same MCU, same firmware, same protocol, same carrier PCB.** Uniformity is a load-bearing project value.

This revision changes only the gateway side. Rev 2 specified Raspberry Pi 5 + Waveshare 2-CH CAN HAT and "local buffering via SQLite for resilience against transient network loss". A subsequent review (captured in ADR-0004 rev 1) reframed the gateway as a **stateless edge** — persistent local state is minimized to identity and configuration, runtime state lives in RAM, audit-trail moves to the IndustryFlow platform side. This has two practical consequences for ADR-0002:

1. **Local SQLite buffer becomes an in-memory ring buffer.** Persistent crash-safe local storage is no longer required.
2. **Gateway hardware minimum drops.** Without a write-heavy local audit log, a Raspberry Pi 3B+ with 1 GB RAM is sufficient for apartment-scale deployments. Pi 4 and Pi 5 remain valid choices for higher-traffic or commercial deployments where extra headroom matters.

The smart-node side (carrier PCB, WeAct core board, F405/F412/F446 drop-in, ATECC608, sensor module header) is unchanged from rev 2.

## Decision drivers

- **Uniformity across all deployments.** Same MCU, same protocol, same firmware, same parts. Simplifies development, service, debugging, inventory, and operator training.
- **Minimize design surface area we own.** Where open-hardware modules with the right characteristics exist, use them and concentrate our design effort on integration concerns rather than re-implementing well-known reference designs.
- **Stateless gateway.** Persistent local state on the gateway is a liability (write-amplification, lost-on-replacement, dual-storage forensic surface). Anything that doesn't have to live on the gateway lives in IndustryFlow.
- **Modularity at field level.** Replacing a sensor must not require reconfiguring the bus, the gateway, or the platform.
- **Determinism.** Some loops are safety-relevant. Their messages must not be starved by telemetry.
- **Fault containment.** A short or stuck node must not silence the bus.
- **Mainstream MCU and predictable supply.** Deep ecosystem support, many distributors, no single-vendor lock-in. Pin-compatible drop-in options provide an additional hedge.
- **Furniture-grade installation.** Wiring must be clean. One twisted pair through the cabinet, not a star of cables to a hub.
- **Existing competence.** STM32, embedded C, real-time control are tools already in use.

## Decision

1. **Protocol: Cyphal as application-layer.** Open under Apache 2.0; the DSDL types live in `industryflow.greenhouse.*` (ADR-0005).

2. **Smart-node hardware: WeAct STM32F4 64-Pin Core Board on carrier PCB.** Primary MCU: **STM32F405RGT6** (168 MHz Cortex-M4F, 192 KB RAM, 1 MB Flash, 2× bxCAN). Drop-in fallback MCUs in the same WeAct socket: **STM32F412RET6** and **STM32F446RET6**. The core board provides MCU + HSE/LSE crystals + power conditioning + USB DFU programming + reset/boot controls. Hardware design is open under upstream's repository license. The project keeps a versioned snapshot of WeAct's schematic and gerbers in its own hardware reference repo for resilience against upstream loss.

3. **Carrier PCB design — integration-only board.** The carrier hosts:
   - Socket(s) matching the WeAct STM32F4 64-Pin Core Board pinout
   - Microchip ATECC608B secure element populated on every board, for hardware identity (PKI per ADR-0007)
   - CAN transceiver (NXP TJA1051T/3 or TI SN65HVD230, 3.3 V tolerant)
   - Termination jumper (120 Ω, populated only on the two physical bus ends)
   - Sensor-module header (pinout per ADR-0014)
   - CAN bus connectors (Molex Micro-Fit 3.0 4-pin, IN and OUT)
   - Power input (12 V or 24 V via barrel jack; local LDO supplies 3.3 V to ATECC608, bus pull-ups, and the WeAct core board)
   - Status LED for CAN activity
   - ESD protection on the CAN bus

   Likely 2-layer PCB, ~60–70 × 40–50 mm. Schematics, gerbers, and BOM published under CERN-OHL-S.

4. **Hardware reference repository policy.** The project's hardware reference repository contains:
   - Our carrier PCB schematic + gerbers + BOM under CERN-OHL-S.
   - A snapshot of the WeAct STM32F4 64-Pin Core Board schematic + gerbers at a known-good version, retained under upstream's open-hardware license with provenance attribution.

5. **Firmware: static personality with modular driver structure.** Each node type ships its own firmware binary that knows its sensor module. Internally, sensor and actuator drivers conform to a common interface so that migration to dynamic personality (one firmware, type read via sensor-module strap pins) requires only an initialization-stage addition, not an architectural rewrite. Reference firmware under AGPL-3.0-or-later.

6. **Gateway: Raspberry Pi with isolated 2-channel CAN HAT (reference).** Any Raspberry Pi model with SPI-accessible 40-pin GPIO header and supported SocketCAN MCP2515 driver works:
   - **Minimum (apartment-scale): Raspberry Pi 3B+** with 1 GB RAM. Adequate for headless Pycyphal-asyncio gateway servicing one cabinet's 5–10 nodes at classic-CAN rates. Headless Pi OS Lite, no GUI, no local storage-heavy services.
   - **Recommended (commercial / higher-traffic): Raspberry Pi 4 (2 GB+) or Pi 5 (4 GB+)** for headroom and NVMe-attached storage. Pi 5's PCIe NVMe slot is useful for production gateways that may host additional edge services in the future.
   - **Industrial enclosures (commercial managed deployments):** RevPi, Compulab, or equivalent — same SocketCAN + Pycyphal software stack, different mechanical/thermal envelope.

   Reference HAT: 2-channel isolated CAN HAT (MCP2515 controllers + SN65HVD230 transceivers, with galvanic isolation). Two channels — one for the cabinet bus, one reserved for future safety-bus separation or debug. Note: the same `SN65HVD230` transceiver is used on the carrier PCB per decision 3 — consistent CAN-PHY across gateway and nodes.

   Gateway service in Python with asyncio, using Pycyphal for protocol handling and Nunavut-generated bindings for DSDL types. **In-memory ring buffer (bounded, default 100 MB) for resilience against transient network loss** — *not* persistent across reboots; reboot-during-outage data loss is accepted as the operational cost of stateless-edge architecture (see ADR-0004 rev 1 for full rationale).

7. **Security boundary at the gateway.** The CAN domain inside the cabinet is explicitly a trusted zone. No per-node authentication on the bus. The boundary between CAN and the outside network is enforced at the gateway. PKI specifics in ADR-0007; host hardening and stateless-edge operational disciplines in ADR-0004 rev 1.

8. **Physical layer:**
   - Linear bus topology, no stars.
   - 120 Ω termination at the two physical endpoints of the bus only.
   - Shielded twisted pair (CAT6 STP is acceptable substrate). Shield grounded at one point near the gateway.
   - Separate power rail (12 V or 24 V), per-node LDO. Bus pairs carry signal only, no power.

9. **Domain mapping.** Each Cyphal node belongs to exactly one IndustryFlow `module` within the cabinet `machine`. Subject-IDs identify *what data*; Node-ID identifies *which physical node*; gateway resolves Node-ID → module assignment via configuration. Tagging by `production_unit_id` (slot) is applied at the gateway based on node-to-slot mapping, not encoded in the CAN frame.

## Alternatives considered

**A. Two-tier hardware — BluePill prototype + custom STM32G4 production (initial ADR-0002 draft).** *Rejected.* Price gap small, two carriers introduced engineering and documentation cost, mixed-grade buses introduced lowest-common-denominator caveats, dependency on BluePill clone supply was a long-tail risk, contradicted uniformity principle.

**B. STM32G431CBU6 on WeAct BlackPill-style breakout, on carrier PCB.** *Rejected* — niche breakout, vendor dependency without published open hardware.

**C. STM32F411 BlackPill on carrier PCB.** *Rejected immediately:* STM32F411 has no bxCAN peripheral.

**D. MCU directly on carrier PCB (ADR-0002 rev 1).** *Rejected* — open-hardware breakout (WeAct STM32F4 64-Pin Core Board) matches rev 1's principles at lower EE cost, faster bring-up, and stronger supply-chain hedging.

**E. CANopen.** *Rejected:* dated tooling, EDS workflow archaic, DSDL is a closer cultural match.

**F. CAN FD throughout, from day one (STM32G0/G4 family).** *Rejected:* STM32F4 with classic CAN is more mainstream, deeper community support; 8-byte payload constraint is solvable via aggregation on node and Cyphal multi-frame transfers.

**G. MQTT over WiFi per node.** *Rejected:* not deterministic, no fault containment, fake modularity.

**H. Modbus RTU over RS-485.** *Rejected:* polling-based, no typed payloads, weaker tooling.

**I. USB sensor hub.** *Rejected:* anti-modular, hub becomes single point of failure.

**J. Pi 5 only as gateway with persistent SQLite buffer (rev 2).** *Rejected on revision:* rev 2 specified persistent local buffering and assumed Pi 5 for headroom. ADR-0004 rev 1 reframes gateway as stateless edge with audit-trail on IndustryFlow, dropping the persistent buffer requirement. With write-heavy local storage removed, Pi 3B+ minimum becomes adequate, and gateway hardware scales with operational headroom rather than persistence load. *Rev 2 superseded for the gateway side; smart-node side unchanged.*

**K. Allwinner T527-based SBCs (Radxa Cubie A5E, EBYTE A527/T527 SBC).** Interesting native-CAN industrial SBC option with on-chip 2-TOPS NPU. *Currently deferred:* mainline Linux support is in active development (mid-2025 status: BSP kernel, USB3 and 2nd GbE WIP in mainline). Recommend monitoring through 2026; revisit when Allwinner T527/T537/A733 mainline support matures, particularly for commercial-tier gateways where on-edge ML and -40~+85 °C operating range become relevant.

## Consequences

### Positive

- One MCU family, one firmware codebase, one carrier design, one core board design. Uniform across every deployment.
- EE effort concentrated on integration, not on re-implementing well-known MCU minimum-system designs. Carrier schematic capture ~2–3 days; carrier layout ~1–2 days. 2-layer PCB.
- Triple-MCU drop-in compatibility (F405/F412/F446 in WeAct socket) provides a supply-chain hedge inside one MCU family.
- Open-hardware core board with published schematic + gerbers means vendor dependency is recoverable.
- Mainstream STM32F4 with long production history and deep community support.
- Modularity is real: failed nodes are unplugged and replaced.
- Deterministic real-time properties usable for safety loops.
- Fault containment via CAN error confinement.
- ATECC608 populated on every carrier — hardware identity universally available without per-deployment hardware variant.
- **Gateway hardware scales down to Raspberry Pi 3B+ for apartment-scale deployments.** Significant cost savings (€40 vs €100+ in gateway hardware) for the entry-level case. Pi 4 / Pi 5 / industrial SBCs remain valid upgrades when operational headroom warrants.
- **Gateway is truly replaceable** — no accumulated persistent state, drop-in replacement is a cable-change plus identity re-provisioning.
- **SD-card write load drops dramatically** — consumer SD cards are adequate for service life, industrial-grade is *nice-to-have*, not *required*.

### Negative

- Classic CAN's 8-byte payload limit applies to all nodes. Multi-value sensors require packing or multi-frame Cyphal transfers. Thermal frames (MLX90640, 1.5 KB) require aggregation on node or multi-frame transfers. Accepted as the cost of uniformity and mainstream-MCU choice.
- Stacked-board mechanical on the node side: core board + carrier + sensor module. Higher stack height; acceptable for cabinet operation, soldered-down core board is the fallback for field-reliability concerns.
- Residual WeAct dependency for ready-built core boards; mitigated by hardware-repo snapshot (decision 4).
- Firmware development per sensor module is still required before that module produces useful data.
- Custom analog front-ends (pH, EC) remain on the sensor module side, requiring analog design effort concentrated in M03-ANALYTICS (ADR-0014).
- **Gateway in-memory buffer is lost on reboot.** Network outage at the moment of gateway reboot loses bounded in-flight data. Mitigation: IndustryFlow records the gap explicitly via per-gateway sequence numbers (ADR-0004 rev 1).
- **IndustryFlow-side audit-trail infrastructure must exist.** Forensic capability that was nominally local in rev 2 is now platform-side. This is a platform roadmap item, not a hardware item — appropriate for a centralized industrial-IoT platform.

## Deferred decisions

- **ADR-0004 (rev 1)** — Gateway host hardening, stateless-edge operation, firmware signing.
- **ADR-0007** — PKI for gateway-to-IndustryFlow authentication, ATECC608 binding policy and provisioning workflow.
- **ADR-0010** — Operational policy for commercial managed deployments: QA pipeline, supply chain control, lifecycle, warranty.
- **ADR-0014** — Sensor node taxonomy (exists; references unchanged from rev 2).
- **Carrier PCB schematic + gerbers + BOM** — published in hardware reference repository under CERN-OHL-S.
- **WeAct core board snapshot** — schematic + gerbers retained at known-good version in hardware reference repository.
- **IndustryFlow-side audit-trail schema** — touches platform roadmap, not this ADR.

## References

- ADR-0001: IndustryGrow framing.
- ADR-0004 (rev 1): Gateway host hardening + stateless-edge operation.
- ADR-0014: Sensor node taxonomy and module decomposition.
- WeAct STM32F4 64-Pin Core Board: [github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard](https://github.com/WeActStudio/WeActStudio.STM32F4_64Pin_CoreBoard)
- [Cyphal Specification](https://opencyphal.org/specification)
- [libcanard (embedded C)](https://github.com/OpenCyphal/libcanard)
- [Pycyphal (Python)](https://github.com/OpenCyphal/pycyphal)
- [Nunavut (DSDL transpiler)](https://github.com/OpenCyphal/nunavut)
- STM32F405 reference manual (RM0090), bxCAN section.
- Microchip ATECC608 datasheet.
- Microchip MCP2515 datasheet — CAN controller (gateway HAT side).
- Texas Instruments SN65HVD230 datasheet — 3.3V CAN transceiver (used on both carrier PCB and gateway HAT).
