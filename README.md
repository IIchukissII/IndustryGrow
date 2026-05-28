# IndustryGrow

**An open-core platform for instrumented, profile-driven crop cultivation that scales — on one architecture — from an apartment-sized cabinet to a several-hundred-square-metre commercial facility.**

---

<img src="img/industrygrow-logo-preview.png" alt="IndustryGrow" width="1024" />

---
## Purpose

IndustryGrow turns a growing space into a measured, controllable system. It provides the
field hardware, firmware, and edge software that sense a cultivation environment and run its
control loops. Its companion platform, **IndustryFlow**, stores history, runs analytics, and
distributes cultivation profiles.

The project is **open-core**: the same architecture serves community self-builders and
commercial managed deployments. Hardware designs and reference firmware are open; the
defensible value is not the sensors (commodity) but the expertise to *identify* a deployment's
dynamics and operate it efficiently afterward.

## Core concept

- **One architecture, all scales.** Apartment cabinet and a 200 m² greenhouse use identical
  PCB designs, firmware, and data types. Scaling means *multiplying node instances across
  zones* — never introducing new node classes.
- **Profile as the single mutation channel.** A *cultivation profile* (signed, versioned JSON)
  is the only way to change how a deployment behaves. Human edits, ML-generated
  optimizations, and community contributions all flow through profile versioning — one audit
  trail, one rollback path.
- **Autonomous edge.** The gateway runs control loops *locally* against a cached profile.
  The cloud is an observer and profile source, never a real-time commander — plants keep
  growing through network outages.
- **Safety is hardware, separate from control.** Hardware interlocks (over-temperature, leak)
  cut power independently of any software. The profile defines *operating* parameters;
  hardware defines *survival* parameters. The two never overlap.
- **Sensor density is temporal.** Dense coverage during an empirical *survey*, a reduced-order
  state-space model is *identified*, then most sensors return to inventory for the lean
  *operating* phase. Profiles carry the model alongside the setpoints.

## Technology

| Layer | Choice |
|-------|--------|
| Field bus | Cyphal application protocol over classic CAN, linear topology |
| Smart-node MCU | WeAct STM32F4 64-pin Core Board (STM32F405RGT6; F412/F446 drop-in) |
| Node carrier PCB | Custom integration board: CAN transceiver, ATECC608 secure element, sensor-module header |
| Gateway | Raspberry Pi (3B+ minimum, Pi 4/5 for higher traffic) + isolated 2-channel CAN HAT |
| Gateway software | Python / asyncio, SocketCAN + Pycyphal + Nunavut-generated DSDL bindings |
| Cloud link | mTLS to IndustryFlow; gateway is a **stateless edge** (state in RAM, audit trail on platform) |
| Node firmware | Embedded C with libcanard |
| Identity & security | Per-node ATECC608 hardware identity; signed firmware; trusted in-cabinet CAN domain |

> Hardware design files in `carrier/` are authored in **KiCad 10** and will not open in earlier versions.

### Sensor module catalog

Five reusable PCB designs, one functional subsystem each. Instances are specialized by
populated-BOM, not by new designs.

| Module | Subsystem | Key sensing |
|--------|-----------|-------------|
| M01-CLIMATE | Air environment | Temp/RH, gas/VOC, CO₂, airflow |
| M02-LIGHT | Photic environment | 11-channel spectral, UV-A |
| M03-ANALYTICS | Hydroponic solution | pH, EC, solution temperature |
| M04-PLANT | Plant-level | Canopy thermal imaging |
| M05-SAFETY | Power & interlocks | Load current, over-temp cutoff, door, leak |

## Project status

| Phase | Scope | State |
|-------|-------|-------|
| Phase 1 | Hardware + firmware bring-up; 5 sensor nodes + gateway; standalone, no cloud | In progress |
| Phase 2 | Cloud integration: mTLS ingestion, profile sync, audit trail | Blocked on IndustryFlow prerequisites |
| Phase 3 | Community profile registry, predictive ML, multi-cabinet coordination | Planned |

## Architecture decision records

ADRs are the source of truth for the design. Present in this repository:

- **ADR-0001** — Project framing: open-core cultivation platform on IndustryFlow
- **ADR-0002** (rev 3) — Field bus architecture
- **ADR-0003** — Strawberry day-neutral cultivation profile (reference profile)
- **ADR-0004** (rev 1) — Gateway host hardening & stateless-edge operation
- **ADR-0014** — Sensor node taxonomy and module decomposition
- **ADR-0015** — Gateway profile caching and local control loops
- **ADR-0016** — Empirical survey and state-space modeling

Planned / not yet written: ADR-0005 (DSDL types), ADR-0006 (mechanical/hydroponic),
ADR-0007 (PKI), ADR-0008 (deployment topology), ADR-0009 (profile schema),
ADR-0010 (commercial operations), ADR-IF-0001 (IndustryFlow `production_unit`).

## Licensing

- Hardware designs (carrier + sensor modules): **CERN-OHL-S**
- Reference firmware: **AGPL-3.0-or-later**
- WeAct core board snapshot retained under its upstream open-hardware license
