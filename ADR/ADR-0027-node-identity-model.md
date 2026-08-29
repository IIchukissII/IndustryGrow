<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0027: Node identity model

- **ID:** ADR-0027
- **Status:** Accepted
- **Date:** 2026-08-25
- **Project:** IndustryGrow
- **Parent:** ADR-0002 (rev 3)
- **Companions:** ADR-0005, ADR-0007 (rev 1), ADR-0014 (rev 6), ADR-0017 (rev 2)
- **Realizes:** ADR-0005 decision 6's *"persistent register value set at provisioning"*, which names no medium and no procedure

## Revision history

- **Amendments** — decisions 8 and 9 (2026-08-25): the Cyphal `unique_id` source, which had no decision behind it, and the map of the five identifiers a node carries. The title widens with them, from *Node-ID provisioning and persistence*; decisions 1–7 are unchanged.
- **Amendments** — decision 10 (2026-08-25): `127` carries one meaning, not two; bounds decision 6.
- **Amendments** — decision 11 (2026-08-29): the store on the next carrier revision; bounds decision 2 to the carriers in service.

## Context and problem

ADR-0005 decision 6 fixes the rule: a node holds its Node-ID as a persistent register value set at provisioning, and the gateway's `(module_class, node_role, zone)` mapping keys off it. It names neither where the value is stored nor how it gets there. Nothing else does either, so the firmware supplies neither — `firmware/nodes/registry.c` carries `node_id = 96` for M05 and `97` for M01 as compile-time constants selected by the module-class strap, and `uavcan.node.id` is declared `persistent = false` and read before any write can reach it.

**One identifier is doing two jobs.** ADR-0014 decision 6 fixes an 8-bit **class** ID, programmed at module manufacture and explicitly not writable in the field. A Node-ID identifies an **instance** on one bus, is chosen per deployment, and must be writable at commissioning. The firmware derives the second from the first, which holds only while every class has exactly one instance.

That condition is already scheduled to fail, and the ADR that fails it says so: ADR-0006 records that two hydroponic loops mean two M03 instances — *"the first case where that count exceeds one. Both instances carry module-ID `0b011` and are distinguished by deployment identity, not by class."* That distinguishing mechanism is the subject of this ADR, and today there is none: deployment identity has no carrier, so both instances would present the same Node-ID. ADR-0014's own context sets the same pattern — *"design for instance multiplication (across zones), reject redundancy multiplication (within a zone)"* — with several classes at one per zone. Two nodes of one class would claim one Node-ID, which Cyphal forbids: the transport has no tie-break, so the result is corrupted transfers on a shared identifier rather than one node losing. That breaks the bus rather than degrading it, and it affects every subject on it, not only the colliding pair.

A medium must therefore be chosen. Three exist on or near the carrier, and a fourth store arrives with the next carrier revision. They differ in lifecycle, not in capacity — any of them can hold one byte.

That choice cannot be made in isolation, because the Node-ID is not the only identifier a node carries. Five exist, three of them governed elsewhere, one of them governed nowhere. Deciding the Node-ID's store without stating what the other stores are for is how the class ID came to be used as a Node-ID in the first place. Decision 9 is therefore the map; it is not a copy of the records it names.

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

8. **The Cyphal `unique_id` is the ATECC608 serial, left-justified and zero-padded; a node whose secure element does not answer falls back to the STM32 96-bit factory UID and reports its identity as unanchored** *(added 2026-08-25)*. This was firmware behaviour with no decision behind it — `read_unique_id()` cited ADR-0007 for a fallback ADR-0007 does not describe.

    The two sources are not equivalent and the decision is not to treat them as such. The ATECC serial is the logistics-to-cryptographic binding of ADR-0017 decision 8 and ADR-0007 decision 5, and it resolves to a `-PR` record. The MCU UID identifies one replaceable part inside the node, binds to no record, and survives no board swap. The fallback exists so that a bare core board without a carrier still enumerates — a node visible on the gateway can be diagnosed, one that is silent cannot — and a node running on it reports ADVISORY health, which is how the difference reaches an operator rather than being inferred from a byte pattern.

9. **A node carries five identifiers; none substitutes for another, and each has exactly one store** *(added 2026-08-25)*.

    | Identifier | Answers | Store | Lifecycle | Decided by |
    |---|---|---|---|---|
    | Module class ID | what kind of module this is | 3-bit strap, then serial EEPROM | fixed at module manufacture | ADR-0014 decision 6 |
    | Node-ID | which instance on this bus | MCU flash sector, on the carrier | provisioned at commissioning | decisions 1–6 above |
    | `unique_id` | which physical unit | ATECC608 serial, else MCU UID | factory, read-only | decision 8 |
    | Keypair and leaf certificate | that the unit is genuine | ATECC608 slot, on-chip | provisioned once, non-exportable | ADR-0007 decisions 1 and 5 |
    | Serial / E-number | which asset, in the records | ERP `-PR` record | assigned at manufacture | ADR-0017 decisions 8 and 12 |

    Each row's own decision lives in the record named. This ADR decides the two rows nobody owned and the separation itself; it restates none of the others, because a value mirrored into a second document is a value that goes stale in one of them (ADR-0000 decision 3).

    **Why the Node-ID gets a store of its own, given three already exist.** Each of the others is disqualified by its lifecycle, not by its size. The class-ID EEPROM is fixed at module manufacture and sits on the *module*, so it can hold neither a field-provisioned value nor one that must survive a module swap. The ATECC608's slots are earmarked for the on-chip keypair, and reaching a data slot means locking the configuration zone — a one-way operation on every node, spent ahead of the node-side provisioning design ADR-0007 has not yet written. The MCU UID is read-only silicon. The one writable, carrier-resident store that commits nothing else is MCU flash.

    **The next carrier revision does not reopen this.** When `E0001-000100` replaces the 3-bit strap with the 8-bit serial EEPROM (ADR-0014 decision 6), that part carries class identity and nothing else. It arrives manufacture-programmed and not field-writable, which is what class identity needs and what instance identity forbids; alternative A stands after the transport changes as it does before.


10. **Node-ID `127` means one thing: no Node-ID is provisioned** *(added 2026-08-25)*. Decision 6 sends an unprovisioned node there. Before this ADR the value also meant *personality unresolved*, because the personality selected the Node-ID — the coupling decision 1 removes. With that coupling gone the second meaning has no mechanism behind it, and it is not restored here.

    The two conditions are independent, and each is reported on its own channel. A node whose class ID resolves to no personality but which *has* been provisioned uses its provisioned Node-ID; it publishes no subjects, because it does not know what it would publish, and it reports the unresolved personality through the `uavcan.node.GetInfo` name and its health. A node that is provisioned and identified publishes normally. A node that is neither sits at `127` and reports both.

    This bounds decision 6 rather than changing it. Unprovisioned still means `127`; what changes is that a provisioned node is no longer sent there for an unrelated reason, so an operator reading `127` off the bus learns exactly one thing and does not need a `GetInfo` round-trip to find out which fault it is. Two unprovisioned nodes still collide at `127` — decision 6 already says so, and no reserved value fixes it.

    The firmware constant is `IGROW_NODE_ID_UNPROVISIONED`.

11. **On `E0001-000100` and later the Node-ID store is a carrier-resident serial EEPROM** *(added 2026-08-29)*. The record is decision 2's — magic, version, Node-ID, CRC — in a field-writable part on the carrier. Its I²C address is allocated inside the `0x50`–`0x57` block ADR-0014 decision 6 reserves, distinct from the module class-ID EEPROM's; both parts sit on one bus and never share an address.

    | Property | Module EEPROM (ADR-0014 d6) | Carrier EEPROM (here) |
    |---|---|---|
    | Board | module | carrier |
    | Holds | class ID | Node-ID |
    | Written | at module manufacture | in the field, per decision 5 |

    Alternative A rejects the module part on those three properties. This is not that part, and that rejection stands.

    Two properties decision 2 cannot give a series build: an SWD mass erase clears the flash sector, so a fixture that erases before programming de-provisions the board — decision 4's exclusion binds the bus update path only; and with identity outside program flash every unit takes byte-identical images, verifiable by CRC at test.

    Bounded: `E0001-000002` and `E0001-000003` keep decision 2's flash store. Which store a build uses follows the carrier revision; how firmware selects between them is a firmware concern, not fixed here.

## Alternatives considered

**A. Serial EEPROM at the reserved `0x50`–`0x57` addresses.** ADR-0014 decision 6 already fixes this transport for the class ID on `E0001-000100` and later. *Rejected:* it is absent from every carrier in service, so it cannot fix the collision on the boards that have it, and it puts M03 behind a carrier iteration. Its stated policy is also the inverse of what is needed — programmed at module manufacture, not writable in the field — and it sits on the *module*, so a module swap would carry the node's identity away with it. Reusing the part while inverting both its lifecycle and its writability would make one store mean two incompatible things.

**B. The ATECC608.** Fitted on every carrier, has data zones, and is the node's identity anchor (ADR-0007). *Rejected:* reaching a data slot requires writing and locking the configuration zone, and that lock is irreversible. It would commit every node's secure element permanently in order to store one byte, ahead of the node-side provisioning design ADR-0007 decision 5 anticipates and has not yet written. The slots are also already spoken for — decision 1 of ADR-0007 puts an on-chip non-exportable keypair in them — so the part is not the empty store it looks like from the firmware, which today reads only its factory serial. Secondary: a Node-ID is a non-secret operational value an operator changes at will, and widening what the secure element holds widens what a compromise of it means.


**F. Reserve a second value, so `126` means unprovisioned and `127` unidentified.** The obvious way to separate two meanings. *Rejected:* it spends a second reserved identifier to encode, in the Node-ID, a fact that is not about the node's identity. Decision 1 already separates the two axes; encoding personality state back into the Node-ID re-couples them in the one field that must mean only "which instance". It also fixes nothing about collisions — two unprovisioned nodes collide at `126` exactly as they would at `127`.

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
- **The I²C address allocation inside `0x50`–`0x57`** between the carrier store of decision 11 and the module class-ID EEPROM of ADR-0014 decision 6. Fixed by the `E0001-000100` pin map before layout.
- **Where the assignment is recorded against the instance.** Which ERP record holds a cabinet's Node-ID map, under ADR-0017 and ADR-0021.

## References

- ADR-0002 (rev 3): Field bus architecture — the carrier, the MCU, the single-gateway bus.
- ADR-0005: DSDL foundation — decision 6 fixes the Node-ID rule this ADR implements.
- ADR-0006: Cabinet decomposition and hydroponic topology — two loops mean two M03 instances, distinguished by deployment identity.
- ADR-0007 (rev 1): PKI and secure element identity — the ATECC608's scope.
- ADR-0014 (rev 6): Sensor node taxonomy — decision 6 fixes the class ID and its transports.
- ADR-0017 (rev 2): Component, document and instance identification — decision 16 selects the personality.
