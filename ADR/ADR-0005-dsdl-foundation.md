<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0005 — DSDL foundation: type vocabulary, standard-type reuse, port-ID allocation

- **Status:** Accepted
- **Date:** 2026-06-17; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0002](ADR-0002-field-bus-architecture.md) (the Cyphal bus and gateway-side mapping this vocabulary serves), [ADR-0014](ADR-0014-sensor-node-taxonomy.md) (module classes, gateway role/zone tagging, presence probing), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (control over the normal command path; profiles are JSON, not DSDL), [ADR-0018](ADR-0018-power-distribution-and-rail-monitoring.md) (M05 sense-only; door and leak are report/alert), [ADR-0020](ADR-0020-gateway-persistence-model.md) (gateway-side cost of decision 12), [ADR-0027](ADR-0027-node-identity-model.md) (implements decision 6), [ADR-0029](ADR-0029-node-firmware-update-and-bootloader.md) (firmware-update service binding by reuse), [ADR-0031](ADR-0031-actuator-node-taxonomy.md) (actuator types minted under decision 2)

## Context

DSDL is Cyphal's interface definition language: every subject and service on the bus is typed by a versioned definition, from which Nunavut generates serialization code for the C nodes and the Python gateway. Two sides that do not share the same definitions cannot communicate, so the type set is compiled into both and is a single source of truth in the strongest sense (ADR-0000 dec 3).

The record decides the shape of the vocabulary: root namespace, reuse versus minting, the mapping onto measured quantities, identifier allocation, the standard services every node implements, and versioning and location. The forces: the project's owned design surface stays small (ADR-0002); one vocabulary serves every scale, scale being instances and gateway-side tagging (ADR-0014); the node publishes what it measures and the gateway resolves role and zone (ADR-0014 dec 7, ADR-0002 dec 9); a partially populated board is a normal condition (ADR-0014 dec 8); the bus is closed, trusted and single-gateway (ADR-0002 dec 7); and safety signals carry report semantics without an actuation contract (ADR-0018). The profile document is gateway-to-cloud JSON owned by ADR-0009, not DSDL.

## Decision

1. **Project types live under the root namespace `industryflow.greenhouse`.** Sub-namespaces group types by functional subsystem, mirroring the sensor and actuator taxonomies (ADR-0014, ADR-0031), so the type tree reads like the module catalogue.
2. **The OpenCyphal standard namespace is reused first; a project type is minted only for a real gap.** The pinned regulated type set is the primary vocabulary. A minted type is a maintenance and interoperability cost justified only by semantics no standard type carries.
3. **Physical quantities are published as the standard SI sample types in SI base units.** Each carries a synchronized timestamp and an SI float; unit conversion for display is a gateway or platform concern. Accumulated S0 energy is published in joule: the node scales a dimensionless pulse count by the meter constant exactly, whatever the target unit.
4. **Discrete safety signals (door, leak) are minimal project types carrying state, validity and timestamp, with no command or actuation field.** A bare standard bit loses which signal, asserted or clear, and whether the sensor was excited this cycle. M05 switches nothing (ADR-0018 dec 9); the response to a leak is a gateway-issued command over the normal control path (ADR-0015).
5. **Every node implements the standard node skeleton.** It publishes heartbeat at 1 Hz and its port list, and answers node info, register access and list, and execute-command with at least restart; the update commands are ADR-0029's. Heartbeat and node info are what lets a node enumerate on the gateway.
6. **The Node-ID is static, provisioned per node and persisted through the register interface.** The gateway's role and zone mapping keys off a stable Node-ID (ADR-0014 dec 7); medium and procedure are ADR-0027's. Plug-and-play allocation is deferred: it adds an allocator role and a non-determinism the closed single-gateway bus does not need.
7. **Subject-IDs are register-configurable, with firmware-baked defaults in the unregulated range.** The gateway can reassign a port without reflashing, and no fixed port-ID is registered with OpenCyphal. The default map is a downstream value, not a constant of this record.
8. **A non-responding sensor yields an absent subject, not an error.** Firmware probes its sensors at boot and periodically and publishes only the subjects that responded. No node-level "all sensors present" assertion exists for a partial population to violate.
9. **Types are versioned by the Cyphal major.minor rule.** Minor is a bit-compatible extension, major is breaking; project types follow the same discipline as the pinned standard set. The pin and the versions define what is on the wire.
10. **One DSDL source tree; generated code is regenerated at build time and never committed.** Nunavut generates the C and Python bindings from that tree, and the standard namespace is consumed from the pinned upstream repository, not copied. Reproducibility comes from pinning Nunavut and the upstream set.
11. **M04's two telemetry streams take two mechanisms, and only one mints a type.** The summary statistics of a frame are one minted aggregate on a published subject, because split across SI subjects they would describe no frame in particular. The frame itself is pixel data with no semantics a type would add, so it is served by the node over the standard file-read service; the summary record flags availability and the gateway reads on change.
12. **The frame's cadence, payload and layout are module-specification values, bounded to a node-preprocessed product, never the raw device image.** The default is one interval product per minute or on event. The bound is what keeps the reuse ground of decision 11 and the features-not-raw-frames rule of ADR-0020.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| — | A second protocol or IDL (DroneCAN, CANopen PDOs, ad-hoc structs) | Settled by ADR-0002 dec 1; not reopened |
| 1 | Role or zone encoded in the type or the frame | Contradicts ADR-0014 dec 7 and ADR-0002 dec 9; forces per-deployment type or frame variants |
| 2 | All-custom vocabulary ignoring the standard namespace | Re-mints settled types, maximises owned surface, forfeits standard Cyphal tooling |
| 3 | Accumulated energy in watt-hours as a project type | Wh is cosmetic: the scaling is a constant either way and float32 precision is unit-independent; it costs a minted type where a standard one fits |
| 4 | Door and leak as the standard single-bit primitive | Strips signal identity and the validity flag an alerting gateway needs |
| 6 | Plug-and-play Node-ID allocation from the start | Adds an allocator and boot-time non-determinism a closed handful-of-nodes bus does not need |
| 7 | Hard-wired subject-IDs, no register interface | Reflash-coupled, collides with multi-instance deployments and the gateway-owned mapping; defaults are kept, fixed bindings are not |
| 7 | Fixed port-IDs registered with OpenCyphal | Freezes one numeric assignment into firmware; the gateway owns the live map |
| 8 | Bundled per-node telemetry records (all quantities in one type) | A struct cannot be partly absent, couples unrelated measurements, duplicates SI semantics; aggregates are reserved for atomicity (decision 11) |
| 10 | Commit the generated sources | A second, drift-prone copy of the vocabulary |
| 11 | Summary statistics as separate SI subjects | They describe one frame; split, they describe no frame in particular |
| 11 | A minted type for the full frame | Pixel data carries no semantics a type would add |
| 11 | Gateway polls for frame availability | Availability is a field of the summary record; reading on change costs no poll traffic |
| 12 | Full device frame every 5 minutes as the default payload | Cadence and payload are module-specification values; the specification sets a per-minute interval product |
| 12 | Raw device image as payload | Loses the node-preprocessing bound on which the downstream cadence rests (ADR-0020) |

## Consequences

**Positive**

- Firmware and gateway share one compilable vocabulary, and wire compatibility is a build-time property.
- For M05 the owned surface is the standard SI samples plus two small safety types.
- One image with register-configured ports serves every instance, reconfigurable without reflashing.
- Class-generic types with gateway-side tagging keep one vocabulary at every scale.
- A populated subset of a board needs no other firmware or type set.
- The firmware-update binding reuses standard services and mints no type (ADR-0029 dec 5).
- An interval frame each minute costs no additional sensor-bus traffic, since the node reads every frame for the summary.

**Negative**

- The build depends on the pinned regulated set and on Nunavut, both to be updated deliberately, and requires a codegen step.
- Register-configured ports add per-node configuration state, and the live subject-ID is read from the register set rather than a constant.
- Without plug-and-play, Node-ID provisioning is a manual step at bring-up and replacement.
- Each minted type is owned surface that must stay bit-compatible across releases.
- The interval frame raises M04's bus occupancy and the gateway's stored volume (ADR-0020).

## Deferred

| Open question | Settled by |
|---|---|
| Plug-and-play Node-ID allocation | Maintainers, when field replacement makes manual provisioning a burden (ADR-0027 dec 7) |
| M03 type set | M03 firmware, under decisions 2 and 3 |

## References

- `firmware/dsdl/` — project DSDL tree; `firmware/third_party/public_regulated_data_types` — pinned standard set.
- `firmware/cmake/dsdl.cmake` — build-time generation.
- `spec/E0005-R-specification.md` — M04 cadence, payload and frame layout.
- Cyphal specification and DSDL reference; Nunavut; libcanard; Pycyphal.
