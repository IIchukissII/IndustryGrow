<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0005: DSDL foundation — the `industryflow.greenhouse` type vocabulary, standard-type reuse, and port-ID allocation

- **ID:** ADR-0005
- **Status:** Accepted
- **Date:** 2026-06-17
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0014, ADR-0015, ADR-0018

## Context and problem

ADR-0002 (rev 3) fixed the field bus as **Cyphal over classic CAN** and named, in passing, the place the wire vocabulary would live: "the DSDL types live in `industryflow.greenhouse.*` (ADR-0005)." That ADR was never written. Every artifact that depends on it has been blocked or stubbed in consequence: the gateway ships only a *placeholder* self-test that proves SocketCAN and the sandbox work but explicitly defers the real Pycyphal application "pending the DSDL vocabulary (ADR-0005, planned)"; the roadmap makes "DSDL foundation" stage 2, the prerequisite for the first sensor node (stage 3) and everything after it; and no sensor-node firmware can finalize its publication interface without agreed types.

DSDL (Data Structure Description Language) is Cyphal's interface definition language: every subject (publish/subscribe topic) and service (request/response) on the bus is typed by a versioned DSDL definition, from which **Nunavut** transpiles serialization code for each language (C for the libcanard-based nodes, Python for the Pycyphal gateway). Two sides that do not share the *same* DSDL definitions cannot communicate; the type set is therefore a single-source-of-truth artifact (ADR-0000) in the strongest sense — it is compiled into both the firmware and the gateway.

What this ADR must decide is not *whether* to use DSDL (ADR-0002 settled that) but the **shape of the vocabulary**: the root namespace, how much is reused from the OpenCyphal standard set versus minted by the project, how data types map onto the sensors ADR-0014 enumerates, how nodes and subjects acquire their numeric identifiers on the bus, which standard services every node implements, and how the definitions are versioned, located, and regenerated. These are the decisions that constrain firmware and gateway code, so per ADR-0000 they belong in an ADR before that code is written.

A note on scope. This ADR is the **foundation and the worked first example (M05-SAFETY)**, not the complete catalog. The full type set for M01–M04 extends the vocabulary as each module is built (decision 9 of ADR-0014's "designs few" pattern applies to types as it does to boards). The profile *document* schema is a different artifact entirely — it is gateway-to-cloud JSON (ADR-0015 decision 2) and is owned by ADR-0009, not by DSDL; it is out of scope here.

## Decision drivers

- **One vocabulary, compiled into both sides.** Firmware (libcanard, C) and gateway (Pycyphal, Python) must be generated from the *same* DSDL source. Divergence is not a style issue; it is a wire-incompatibility bug.
- **Minimize the design surface we own (ADR-0002).** The same principle that put the MCU on an open core board applies to types: where the OpenCyphal standard namespace already defines the right type with the right semantics, reuse it rather than re-mint it. Custom types are a maintenance and interoperability cost and are justified only by a real gap.
- **One architecture, all scales (ADR-0014).** The type vocabulary, like the PCB designs and firmware, must be identical from an apartment cabinet to a commercial greenhouse. Scale is expressed by instance multiplication and gateway-side tagging, never by new types.
- **The node publishes *what it measures*; the gateway resolves *where and what role* (ADR-0014 decision 7, ADR-0002 decision 9).** Role and zone are not in the DSDL or the frame; Node-ID → `(module_class, node_role, zone)` is a gateway-configuration mapping. The types must therefore be class-generic, carrying physical quantity, not deployment context.
- **Partial-BOM and presence-probing must round-trip (ADR-0014 decision 8).** A node publishes only the subjects for sensors that actually responded; the vocabulary and the port-ID mechanism must make "this subject is simply absent on this instance" a normal, non-error condition.
- **Closed, trusted, single-gateway bus (ADR-0002 decision 7).** The CAN domain is one cabinet, one gateway, a handful of nodes, no per-node authentication. The identifier-allocation scheme can assume a small, gateway-coordinated network rather than an open multi-master federation.
- **Standard tooling, no bespoke codegen.** Nunavut and the public regulated data types are the OpenCyphal-supported path; staying on it keeps both the C and Python toolchains mainstream.
- **Safety semantics survive the wire (ADR-0018, ADR-0015).** M05 is sense-only and its discrete safety signals (door, leak) are *report/alert*, not interlocks (ADR-0018 decisions 10/11). The types must carry enough to drive an alert without implying an actuation contract the architecture forbids.

## Decision

### Namespace and reuse

1. **Root namespace: `industryflow.greenhouse`.** All project-defined DSDL types live under this namespace (e.g. `industryflow.greenhouse.safety.LeakStatus`), as named by ADR-0002. Sub-namespaces group by functional subsystem mirroring the module taxonomy (`safety`, `climate`, `light`, `analytics`, `plant`, plus a shared `node` for cross-class records), so the type tree reads the same way the module catalog does.

2. **Reuse the OpenCyphal standard namespace first; mint custom types only for a real gap.** The `uavcan.*` regulated namespace (consumed from the pinned `public_regulated_data_types` repository) is the primary vocabulary. The project mints a type under `industryflow.greenhouse.*` only where no standard type carries the needed semantics. This is the "minimize design surface" driver applied to the wire vocabulary.

3. **Physical quantities use the standard SI sample types.** Continuous measurements are published as `uavcan.si.sample.*` types — each carries a synchronized timestamp and an SI-unit float — never as bespoke per-sensor records. In particular the M05 set maps as:
   - bus voltage → `uavcan.si.sample.voltage.Scalar` (volt)
   - bus current → `uavcan.si.sample.electric_current.Scalar` (ampere)
   - bus power → `uavcan.si.sample.power.Scalar` (watt)
   - cabinet/enclosure temperature → `uavcan.si.sample.temperature.Scalar` (kelvin)
   - accumulated actuator energy (S0 pulse integral) → a project type in **watt-hours (Wh)** under `industryflow.greenhouse` (see below), *not* the standard joule sample

   Instantaneous/rate quantities stay in SI sample types (volt, ampere, watt, kelvin): unit conversion for human display is a gateway/platform concern, not a node concern, and keeping these in SI removes a class of unit-mismatch bugs. **Accumulated energy is the one deliberate exception — it is published in watt-hours, not joules.** The S0 meter contract (ADR-0018 decision 5) is defined in imp/kWh, so the node's pulse integral is naturally and losslessly a count of Wh (or a meter-defined fraction of a kWh); re-expressing it in joules would impose a 3600× scaling that buys nothing for a quantity that meters, tariffs, and operators all read in Wh/kWh. Because the standard `uavcan.si.sample.energy.Scalar` is joule-denominated by definition, Wh cannot honestly ride it — so accumulated energy is a small project type (a genuine gap under decision 2), carrying the Wh value plus a timestamp.

4. **Discrete safety signals are project types under `industryflow.greenhouse.safety`, not raw bits.** Door (reed) and leak (strip) are *report/alert* signals (ADR-0018 decisions 10/11), and a bare `uavcan.primitive.scalar.Bit` would strip the semantics an alerting gateway needs (which signal, asserted/clear, and a self-test/validity flag distinguishing "dry" from "sensor not excited this cycle"). The project therefore mints minimal status types here. Their concrete fields and their numeric port-IDs are the *what* and live in the DSDL files and the subject-ID map (ADR-0000 decision 2); this ADR fixes only *that* they are small project types carrying state + validity + timestamp, and *why* (semantics a raw bit loses), not their byte layout. Crucially these types carry **no command or actuation field** — M05 switches nothing (ADR-0018 decision 9), and the response to a leak is a gateway-issued pump command over the normal control path (ADR-0015), not a field on this message.

### Node and port-ID allocation

5. **Every node implements the standard node skeleton.** Independent of its sensor set, each node implements: `uavcan.node.Heartbeat.1` (published at 1 Hz), `uavcan.node.GetInfo.1` (responder), the register interface `uavcan.register.Access.1` + `uavcan.register.List.1`, and `uavcan.node.ExecuteCommand.1` (responder; at minimum `RESTART`, with firmware-update commands reserved to the future update ADR). `uavcan.node.port.List.1` is published for bus introspection. Heartbeat + GetInfo are the minimum that lets a node **enumerate on the gateway console** — the roadmap stage-1 bring-up criterion — and are therefore the first thing the M05 firmware must answer (and the only node-side types its bring-up milestone needs).

6. **Node-ID is static and provisioned per node, persisted through the register interface; plug-and-play allocation is deferred.** Each node holds its Node-ID as a persistent register value set at provisioning; the gateway's `(module_class, node_role, zone)` mapping (ADR-0014 decision 7) keys off a stable Node-ID. Cyphal plug-and-play allocation (`uavcan.pnp`) is a recognized convenience for field replacement but is **deferred** — it adds an allocator role and a non-determinism the closed single-gateway bus does not yet need. Revisit when field-replacement ergonomics justify it.

7. **Subject-IDs are register-configurable with firmware-baked defaults, not hard-wired.** Each publication/subscription subject-ID is held in a standard `uavcan.register` entry (`uavcan.pub.<port_name>.id` / `uavcan.sub.…`, per the Cyphal register conventions) with a sensible default compiled into the firmware for the closed cabinet bus. Port-IDs are thus reconfigurable by the gateway without reflashing, and the default map is a downstream artifact (a subject-ID map document), not a constant frozen into this ADR. Project subjects take their defaults from the **unregulated** subject-ID range; we register no fixed port-IDs with OpenCyphal. This keeps the firmware decoupled from any single numeric assignment and lets the gateway own the live map, consistent with the gateway-resolves-mapping model (ADR-0002 decision 9).

8. **A non-responding sensor yields an absent subject, not an error.** Per ADR-0014 decision 8, firmware probes its sensors at boot and publishes only the subjects whose sensors responded (re-probing periodically). An absent subject is the normal partial-BOM/failure condition; the gateway treats "subject never seen" as "sensor not populated/responding," not as a fault. The vocabulary supports this by construction — there is no node-level "I have all my sensors" assertion that a partial population would violate.

### Versioning, tooling, and location

9. **Types are versioned per the Cyphal `major.minor` rule.** A minor bump is a bit-compatible extension; a major bump is a breaking change. Standard `uavcan` types are consumed at a pinned version of `public_regulated_data_types`; project types carry their own `major.minor` and follow the same compatibility discipline. The pin and the version are the single source of truth for what is on the wire.

10. **One DSDL source tree; generated code is regenerated, not vendored.** The DSDL definitions are the single source of truth (ADR-0000). Nunavut regenerates the C bindings for firmware and the Python bindings for the gateway from that one tree as a build step; generated sources are **not** committed, so there is no second copy of the vocabulary to drift. Project DSDL lives under a versioned namespace directory (`dsdl/industryflow/greenhouse/…`); the standard `uavcan` namespace is consumed from the pinned upstream repository, not copied into ours.

## Alternatives considered

**A. An all-custom `industryflow.greenhouse` vocabulary, ignoring the standard namespace.** *Rejected:* re-mints types (temperature, voltage, heartbeat-like status) that OpenCyphal already defines with settled semantics and ecosystem tooling, maximizing exactly the design surface ADR-0002 set out to minimize, and forfeiting interoperability with standard Cyphal tools (Yakut, the DSDL registry, monitors).

**B. Hard-wired fixed subject-IDs compiled into firmware, no register interface.** *Rejected:* brittle and reflash-coupled. It collides with multi-instance deployments and with the gateway-owned Node-ID→role/zone mapping, and it abandons the standard register-configuration idiom that makes port-IDs reassignable in the field. Fixed *defaults* are kept (decision 7); fixed *bindings* are not.

**C. Encode `node_role` / `zone` in the DSDL or the CAN frame.** *Rejected:* contradicts ADR-0014 decision 7 and ADR-0002 decision 9, which place that mapping at the gateway precisely so one firmware image and one type set serve every role at every scale. Putting context in the type would force per-deployment type or frame variants — the anti-pattern the taxonomy exists to avoid.

**D. Bespoke per-sensor message records that bundle several quantities into one type** (e.g. one `M05Telemetry` struct with voltage+current+power+temp+door+leak). *Rejected as the default:* it couples unrelated measurements into one subject, breaks the partial-BOM/presence-probing model (a struct cannot be partly absent), and duplicates semantics the SI sample types already carry. Aggregate records are reserved for cases where atomicity or multi-frame framing genuinely requires them (e.g. the M04 thermal frame, ADR-0014) — not for routine multi-sensor nodes.

**E. Plug-and-play Node-ID allocation from day one.** *Rejected for now (deferred, not refused):* PnP introduces an allocator and boot-time non-determinism that a closed, single-gateway, handful-of-nodes bus does not need yet. Static provisioned Node-IDs are simpler and match the stable-Node-ID assumption of the gateway mapping. PnP is revisited when field replacement makes manual provisioning a real burden.

**F. A second protocol/IDL (DroneCAN, raw CANopen PDOs, ad-hoc structs).** *Rejected:* ADR-0002 already chose Cyphal/DSDL over CANopen (alternative E there) and the rest; this ADR does not reopen that. Listed only to record that the IDL question is closed upstream.

**G. Vendor the Nunavut-generated sources into the repository.** *Rejected:* a committed copy of generated code is a second, drift-prone copy of the vocabulary (ADR-0000). Regenerating from the single DSDL tree at build time keeps one source of truth; reproducibility is handled by pinning Nunavut and the upstream type-set version, not by checking in their output.

## Consequences

### Positive

- The roadmap stage-2 blocker is cleared: firmware and gateway have an agreed, compilable vocabulary, and the gateway placeholder can become the real Pycyphal application.
- Reuse-first keeps the project's owned type surface small — for M05, essentially the standard SI samples plus two tiny `safety` status types and one Wh energy type — minimizing maintenance and maximizing tool interoperability.
- One DSDL tree compiled into both sides makes wire compatibility a build-time property, not a runtime hope; pinning the upstream set and Nunavut makes it reproducible.
- Register-configured port-IDs with baked defaults give a node that "just works" on the closed bus while remaining reconfigurable by the gateway without reflashing — the same image serves every instance.
- Class-generic types plus gateway-side role/zone tagging preserve the one-architecture-all-scales promise: scale is instances and configuration, never new types.
- The partial-BOM/presence-probing model round-trips cleanly: absent subjects are normal, so the same firmware and type set cover any populated subset of a board.

### Negative

- A live external dependency on `public_regulated_data_types` and on Nunavut enters the build; both must be pinned and periodically updated, and a build now requires a codegen step rather than compiling checked-in sources.
- Register-configurable port-IDs add per-node configuration state (the register store) and a small persistence requirement on the node, and they mean "what subject-ID is this, really?" is answered by the live register set, not by reading a constant — indirection that tooling must surface.
- Deferring PnP means Node-ID provisioning is a manual step in the node bring-up/replacement procedure until that decision is revisited.
- This ADR fixes the foundation and only the M05 type mapping in full; M01–M04 type sets remain to be defined as those modules are built, so the vocabulary is intentionally incomplete at acceptance.
- Minting even small `safety` status types is project-owned surface that must be versioned and kept bit-compatible across firmware/gateway releases under the `major.minor` discipline.

## Deferred decisions

- **The concrete subject-ID default map and register names** — a downstream artifact (subject-ID map document), owned where pin-maps and BOM values are owned (ADR-0000 decision 2), not frozen here.
- **The exact field layout of the `industryflow.greenhouse.safety` status types** (and any other minted types) — lives in the `.dsdl` files; this ADR fixes their existence, intent, and the no-command constraint, not their bytes.
- **M01–M04 (and future-module) type sets** — extend this vocabulary as each module reaches firmware; the reuse-first rule and SI-sample mapping apply.
- **Firmware-update / OTA service types** (e.g. `uavcan.file`, the `ExecuteCommand` update verbs) — belong with the node firmware-update decision (ADR-0004 covers CAN-node update at the policy level; the DSDL service binding is deferred to that work).
- **Plug-and-play Node-ID allocation (`uavcan.pnp`)** — deferred per decision 6; revisit on field-replacement ergonomics.
- **Profile document schema (ADR-0009)** — explicitly *not* DSDL; gateway-to-cloud JSON, separate ADR.
- **Aggregate/multi-frame record types** (e.g. the M04 thermal frame) — defined when that module is built; this ADR only records that aggregation is the exception, not the default (alternative D).

## References

- ADR-0001: IndustryGrow framing — machine → modules data model; one-architecture-all-scales premise.
- ADR-0002 (rev 3): Field bus architecture — Cyphal over classic CAN; names `industryflow.greenhouse.*` and defers this ADR; Node-ID → module mapping at the gateway (decision 9); closed trusted bus (decision 7); libcanard/Pycyphal/Nunavut toolchain.
- ADR-0014: Sensor node taxonomy — module classes and per-module sensor sets; node-publishes-what-it-measures, gateway tags role/zone (decision 7); presence-probing and partial BOM (decision 8).
- ADR-0015: Gateway profile and control loops — control via gateway-issued Cyphal commands; profile is JSON (decision 2), not DSDL.
- ADR-0018: Power distribution and rail monitoring — M05 sense-only; door/leak are report/alert, not interlocks (decisions 9–11); the single +12 V INA226 and the S0 energy stream.
- ADR-0000: Single source of truth — ADR owns *why*, downstream owns *what*; one authoritative copy (drives the no-vendored-codegen and downstream-subject-ID-map decisions).
- [Cyphal Specification](https://opencyphal.org/specification) and the DSDL language reference.
- [OpenCyphal public_regulated_data_types](https://github.com/OpenCyphal/public_regulated_data_types) — the standard `uavcan.*` namespace (Heartbeat, GetInfo, register, SI sample types).
- [Nunavut](https://github.com/OpenCyphal/nunavut) — DSDL transpiler (C and Python back-ends).
- [libcanard](https://github.com/OpenCyphal/libcanard) and [Pycyphal](https://github.com/OpenCyphal/pycyphal) — the node-side and gateway-side runtimes the generated code targets.
