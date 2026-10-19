<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0027 — Node identity model

- **Status:** Accepted
- **Date:** accepted 2026-08-25; changed 2026-10-19
- **Parent:** ADR-0002
- **Related:** ADR-0005 (decision 6: the persistent Node-ID rule this realizes), ADR-0006 (two hydroponic loops: two instances of one class), ADR-0007 (the secure element's scope and the `unique_id` anchor), ADR-0014 (decision 6: the class ID and its transports; decision 7: gateway mapping keyed off the Node-ID), ADR-0017 (serial and `-PR` record; personality selection), ADR-0028 (commissioning that provisions the Node-ID), ADR-0029 (the update path that preserves the store)

## Context

ADR-0005 decision 6 requires a node to hold its Node-ID as a persistent register value set at provisioning; it names no medium and no procedure. The class ID of ADR-0014 decision 6 says what kind of module is fitted, is fixed at module manufacture and is not field-writable. A Node-ID says which instance is on one bus, is chosen per deployment, and must be writable at commissioning. Deriving the second from the first holds only while every class has one instance, and ADR-0006's two hydroponic loops put two instances of one class on a bus. Two nodes on one Node-ID corrupt transfers for every subject on the bus; Cyphal has no tie-break.

The store must reach the carriers in service, whose only class-identity transport is the strap. The gateway's role and zone mapping keys off the Node-ID (ADR-0014 decision 7), so a module swap must not renumber the node. The secure element holds identity and key material (ADR-0007). An unprovisioned node must stay visible, because a node on the bus can be diagnosed and a silent one cannot. A node carries five identifiers; three are governed elsewhere, and the store for each follows from its lifecycle, not its size.

## Decision

1. **The Node-ID is an instance identifier, distinct from the class ID; neither is derived from the other.** The class ID selects the personality (ADR-0017 decision 16) and not the bus identity.
2. **On carriers with no Node-ID EEPROM, the Node-ID is stored in a dedicated sector of the MCU's internal flash.** The MCU is on every carrier revision in service, so the store needs no hardware change. The sector holds one record — magic, version, Node-ID, CRC — read once at boot; its address and layout are owned by the firmware (ADR-0000 decision 2).
3. **The store is on the carrier, so the Node-ID follows the carrier, not the module.** A swapped module keeps the node's identity and the gateway's mapping; a swapped carrier is a new node and is re-provisioned.
4. **The firmware update path does not write the store.** A reflashed node keeps its identity.
5. **Provisioning is a write to the Node-ID register of the standard register interface, which is mutable and persistent.** A write validates the range, commits the store, and takes effect at the next restart, because changing a Node-ID under a live transport invalidates in-flight transfer state. While the committed value differs from the running one, the node reports the difference on its diagnostic channel.
6. **An unprovisioned node runs at the reserved Node-ID `127` and publishes no telemetry subjects.** It publishes heartbeat, answers node-info and register requests, and reports ADVISORY health naming the missing provisioning. Two unprovisioned nodes collide with each other, a commissioning-time condition with an operator present.
7. **The gateway does not allocate Node-IDs.** Plug-and-play allocation stays deferred under ADR-0005 decision 6; provisioning is an operator action recorded against the instance, so a cabinet's Node-ID map is chosen by a human and auditable.
8. **The Cyphal `unique_id` is the secure element's serial, left-justified and zero-padded; a node whose secure element does not answer uses the MCU's factory UID and reports its identity as unanchored with ADVISORY health.** The secure-element serial resolves to a `-PR` record (ADR-0017 decisions 8, 12; ADR-0007 decision 5); the MCU UID identifies one replaceable part and binds to no record. The fallback lets a bare core board enumerate for diagnosis.
9. **A node carries five identifiers; none substitutes for another, and each has one store.** The row decisions live in the records named.

   | Identifier | Answers | Store | Lifecycle | Decided by |
   |---|---|---|---|---|
   | Module class ID | what kind of module | strap, or module serial EEPROM | fixed at module manufacture | ADR-0014 decision 6 |
   | Node-ID | which instance on this bus | carrier store | provisioned at commissioning | decisions 1–6, 11 |
   | `unique_id` | which physical unit | secure-element serial, else MCU UID | factory, read-only | decision 8 |
   | Keypair and leaf certificate | that the unit is genuine | secure element, on-chip | provisioned once, non-exportable | ADR-0007 decisions 1, 5 |
   | Serial / E-number | which asset in the records | ERP `-PR` record | assigned at manufacture | ADR-0017 decisions 8, 12 |

10. **Node-ID `127` means only that no Node-ID is provisioned.** A provisioned node whose class ID resolves to no personality keeps its Node-ID, publishes no subjects, and reports the unresolved personality through node-info and health. An operator reading `127` off the bus learns one fact without a further request.
11. **On carriers from `E0001-000100`, the Node-ID store is a field-writable serial EEPROM on the carrier, holding decision 2's record.** A fixture's mass erase clears a flash sector and de-provisions the board, and identity outside program flash lets every unit take byte-identical, CRC-verifiable images. Its address lies in the block ADR-0014 decision 6 reserves, distinct from the module class-ID EEPROM's, and is fixed in the carrier specification; earlier carriers keep decision 2's store.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **C.** Keep deriving the Node-ID from the class ID and forbid two instances of a class | Contradicts ADR-0006 and ADR-0014's instance multiplication; turns a firmware limitation into an architectural constraint |
| 1 | **D.** Widen the derivation: class ID plus an instance strap | Strap transport is transitional and exhausted (ADR-0014 decision 6); fixes instance identity at manufacture |
| 2 | **A.** The module's class-ID serial EEPROM | Absent from carriers in service; manufacture-programmed and not field-writable; on the module, so a swap carries the identity away |
| 2 | **B.** A data slot of the secure element | Needs an irreversible configuration-zone lock on every node ahead of node-side provisioning; slots are reserved for the keypair; widens what a compromise exposes |
| 5 | Apply a Node-ID write immediately | Changing a Node-ID under a live transport invalidates in-flight transfer state |
| 6 | An unprovisioned node stays silent | A silent node cannot be diagnosed |
| 7 | **E.** Cyphal plug-and-play allocation | Adds an allocator role and non-determinism a closed single-gateway bus does not need; the map would be chosen by no human |
| 8 | Treat the secure-element serial and the MCU UID as equivalent | The MCU UID binds to no record and survives no board swap |
| 10 | **F.** Reserve a second value: `126` unprovisioned, `127` unidentified | Spends a reserved identifier to encode personality state in the field that means only "which instance"; fixes no collision |
| 11 | Keep the flash sector as the store on series-built carriers | Mass erase at programming de-provisions the board, and per-unit identity in program flash prevents identical images |

## Consequences

**Positive**

- Two instances of one module class share a bus.
- Carriers in service are fixed in firmware alone.
- ADR-0005 decision 6 has an implementation, and the register interface reports a value it honours.
- Class identity and instance identity no longer share one number, so a manufacture-fixed policy and a field-writable one do not conflict.

**Negative**

- Node firmware has a flash-write path; a partly written record is detected by its CRC and yields an unprovisioned node, not a corrupt one.
- The update path carries a constraint it must preserve; ADR-0029 places the store outside both application slots.
- A mass erase of a flash-store carrier de-provisions it.
- A node is not usable until provisioned, and each node in service is provisioned once when it takes this firmware, or it comes up at `127`.
- Carrier revisions differ in store, and firmware selects the store by carrier revision.

## Deferred

| Open question | Settled by |
|---|---|
| Which ERP record holds a cabinet's Node-ID map against the instance | ERP record design under ADR-0021 |

## References

- ADR-0005, ADR-0014, ADR-0007, ADR-0029.
- `spec/E0001-R-specification.md` — the store per carrier revision and its I²C address.
- Node firmware carrier identity module — the record layout and sector.
