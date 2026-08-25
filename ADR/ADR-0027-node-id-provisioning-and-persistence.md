<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0027: Node-ID provisioning and persistence

- **ID:** ADR-0027
- **Status:** Proposed
- **Date:** 2026-08-25
- **Project:** IndustryGrow
- **Parent:** ADR-0002 (rev 3)
- **Companions:** ADR-0005, ADR-0007 (rev 1), ADR-0014 (rev 6), ADR-0017 (rev 2)
- **Realizes:** ADR-0005 decision 6's *"persistent register value set at provisioning"*, which names no medium and no procedure

## Context and problem

ADR-0005 decision 6 fixes the rule: a node holds its Node-ID as a persistent register value set at provisioning, and the gateway's `(module_class, node_role, zone)` mapping keys off it. It names neither where the value is stored nor how it gets there. Nothing else does either, so the firmware supplies neither — `firmware/nodes/registry.c` carries `node_id = 96` for M05 and `97` for M01 as compile-time constants selected by the module-class strap, and `uavcan.node.id` is declared `persistent = false` and read before any write can reach it.

**One identifier is doing two jobs.** ADR-0014 decision 6 fixes an 8-bit **class** ID, programmed at module manufacture and explicitly not writable in the field. A Node-ID identifies an **instance** on one bus, is chosen per deployment, and must be writable at commissioning. The firmware derives the second from the first, which holds only while every class has exactly one instance.

That condition is already scheduled to fail, and the ADR that fails it says so: ADR-0006 records that two hydroponic loops mean two M03 instances — *"the first case where that count exceeds one. Both instances carry module-ID `0b011` and are distinguished by deployment identity, not by class."* That distinguishing mechanism is the subject of this ADR, and today there is none: deployment identity has no carrier, so both instances would present the same Node-ID. ADR-0014's own context sets the same pattern — *"design for instance multiplication (across zones), reject redundancy multiplication (within a zone)"* — with several classes at one per zone. Two nodes of one class would claim one Node-ID, which Cyphal forbids: the transport has no tie-break, so the result is corrupted transfers on a shared identifier rather than one node losing. That breaks the bus rather than degrading it, and it affects every subject on it, not only the colliding pair.

A medium must therefore be chosen. Three exist on or near the carrier. They differ in lifecycle, not in capacity — any of them can hold one byte.

## Decision drivers

- **Boards already in service must be fixable.** `E0002` and `E0006` are built and bench-verified on `E0001-000002`, whose only identity transport is the 3-bit strap. A remedy requiring `E0001-000100` leaves the collision unfixed on every existing board and puts M03 behind a carrier iteration.
- **The Node-ID must survive a module swap.** Replacing a failed sensor module is routine service. The gateway's role and zone mapping keys off a stable Node-ID (ADR-0014 decision 7), so renumbering a node because its module changed would silently re-point that mapping.
- **Class identity stays manufacture-fixed.** ADR-0014 decision 6 makes the class ID unwritable in the field deliberately. Instance identity has the opposite requirement, so it cannot inherit that store's policy.
- **The secure element holds secrets.** ADR-0007 scopes the ATECC608 to identity and key material; a non-secret configuration byte does not need it.
- **An unprovisioned node must be diagnosable.** A node visible on the gateway can be diagnosed; one that stays silent cannot.

## Decision

1. **The Node-ID is an instance identifier, distinct from the class ID, and neither is derived from the other.** `registry.c` stops carrying a Node-ID per personality. The class ID continues to select the personality (ADR-0017 decision 16); it no longer selects the identity on the bus.

2. **The Node-ID is stored in a dedicated sector of the MCU's internal flash, on the carrier.** The STM32F405 is on every carrier revision in service, so this reaches `E0001-000002` boards with no hardware iteration. The sector holds a small record — magic, version, Node-ID, CRC — read once at boot. Its address and layout are downstream values (ADR-0000 decision 2), not fixed here.

3. **The store is on the carrier, so the Node-ID follows the carrier rather than the module.** A swapped sensor module leaves the node's identity, and the gateway's mapping, intact. A swapped carrier is a new node and is re-provisioned.

4. **The firmware update path must not erase the sector.** It is excluded from the region an update writes. A reflashed node keeps its identity; losing it on every update would make the store worse than the constant it replaces.

5. **Provisioning is a write through the standard register interface.** `uavcan.node.id` becomes `mutable = true, persistent = true`. A write validates the range and commits the sector, and takes effect at the next restart rather than immediately — changing a Node-ID under a live transport invalidates in-flight transfer state. While a committed value differs from the running one, the node reports the difference on its diagnostic channel.

6. **An unprovisioned node claims the reserved unidentified Node-ID and publishes no telemetry subjects.** The firmware already reserves `127` for a node whose personality is unresolved; an unprovisioned node joins it there. It publishes heartbeat, answers `GetInfo` and the register interface, and reports ADVISORY health with a diagnostic naming the missing provisioning — visible and addressable, claiming nothing a provisioned peer might be using. Two unprovisioned nodes still collide with each other; that is a commissioning-time condition an operator is present for, not a silent fault in a running cabinet.

7. **The gateway does not allocate Node-IDs.** Cyphal plug-and-play stays deferred where ADR-0005 decision 6 left it. Provisioning is an operator action recorded against the instance (ADR-0017), so a cabinet's identifier map stays something a human decided and can audit.

## Alternatives considered

**A. Serial EEPROM at the reserved `0x50`–`0x57` addresses.** ADR-0014 decision 6 already fixes this transport for the class ID on `E0001-000100` and later. *Rejected:* it is absent from every carrier in service, so it cannot fix the collision on the boards that have it, and it puts M03 behind a carrier iteration. Its stated policy is also the inverse of what is needed — programmed at module manufacture, not writable in the field — and it sits on the *module*, so a module swap would carry the node's identity away with it. Reusing the part while inverting both its lifecycle and its writability would make one store mean two incompatible things.

**B. The ATECC608.** Fitted on every carrier, has data zones, already the node's identity anchor (ADR-0007). *Rejected:* it is scoped to secrets and identity binding, and a Node-ID is neither — it is a non-secret operational value an operator changes at will. Mixing writable configuration into the element whose worth is that its contents are constrained widens what a compromise of it means, for a byte the MCU can already store.

**C. Keep deriving the Node-ID from the class ID, and forbid two instances of a class.** *Rejected:* it contradicts ADR-0006's two-loop case and ADR-0014's instance-multiplication pattern, and converts a firmware limitation into an architectural constraint on how cabinets may be built.

**D. Widen the derivation — class ID plus an instance strap.** *Rejected:* ADR-0014 rev 4 already declared the strap transport transitional and rejected a fourth strap pin on exhaustion grounds that apply here too. It would also fix instance identity at manufacture, when it is a deployment-time choice.

**E. Cyphal plug-and-play allocation.** *Rejected on the grounds ADR-0005 decision 6 already gave:* it adds an allocator role and a non-determinism the closed single-gateway bus does not need, and it would make the identifier map something no human chose.

## Consequences

### Positive

- Two instances of one module class can share a bus. ADR-0006 already assumes this works; today it does not.
- The remedy reaches `E0001-000002` boards, so `E0002` and `E0006` are fixable in firmware alone.
- ADR-0005 decision 6 gains an implementation; the register interface stops reporting a value it cannot honour.
- Class identity and instance identity stop sharing one number, so ADR-0014 decision 6's manufacture-fixed policy and this ADR's field-writable one no longer contradict each other.

### Negative

- A flash-write path in node firmware is new, and a partially written sector must be detectable — hence the CRC in decision 2. Its failure mode is an unprovisioned node (decision 6), not a corrupt one.
- The update path gains a constraint it did not have. Decision 4 makes the sector's exclusion a property that mechanism must preserve, and the firmware-update ADR does not exist yet.
- Commissioning gains a step: a node is not usable until provisioned, where today it self-identifies — wrongly, but silently.
- Nodes in service must each be provisioned once when they take this firmware, or they come up as `127`. This is deliberate: a visible unprovisioned node is preferred to one that quietly keeps a colliding identity.

## Deferred decisions

- **The sector address and record layout.** Downstream values (ADR-0000 decision 2); they belong with the linker script and the firmware.
- **The provisioning tool.** Whether an operator writes the register over the bus, over SWD at manufacture, or through a gateway command is an operational specification.
- **How the update path excludes the sector.** Belongs with the firmware-update ADR that ADR-0004 decisions 12–16 anticipate and which is not yet written.
- **Where the assignment is recorded against the instance.** Which ERP record holds a cabinet's Node-ID map, under ADR-0017 and ADR-0021.

## References

- ADR-0002 (rev 3): Field bus architecture — the carrier, the MCU, the single-gateway bus.
- ADR-0005: DSDL foundation — decision 6 fixes the Node-ID rule this ADR implements.
- ADR-0006: Cabinet decomposition and hydroponic topology — two loops mean two M03 instances, distinguished by deployment identity.
- ADR-0007 (rev 1): PKI and secure element identity — the ATECC608's scope.
- ADR-0014 (rev 6): Sensor node taxonomy — decision 6 fixes the class ID and its transports.
- ADR-0017 (rev 2): Component, document and instance identification — decision 16 selects the personality.
