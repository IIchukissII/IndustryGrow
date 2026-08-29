<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="img/industrygrow-logo-mono-dark.svg" />
  <img src="img/industrygrow-logo-mono-light.svg" alt="IndustryGrow" width="280" />
</picture>

# IndustryGrow

[![REUSE status](https://github.com/IIchukissII/IndustryGrow/actions/workflows/reuse.yml/badge.svg)](https://github.com/IIchukissII/IndustryGrow/actions/workflows/reuse.yml)

Open-core field hardware, firmware and edge software for instrumented, profile-driven crop
cultivation. One architecture, from a cabinet to a several-hundred-square-metre facility. The
companion platform **IndustryFlow** stores history, runs analytics and distributes profiles.

Scope and rationale: [`MOTIVATION.md`](MOTIVATION.md), [ADR-0001](ADR/ADR-0001-industrygrow-framing.md).

## Architecture constraints

| Constraint | Record |
|---|---|
| Scaling multiplies node *instances* across zones. It adds no node classes. | ADR-0014 d2 |
| A signed, versioned JSON cultivation profile is the only channel that changes deployment behaviour. There is no remote-command API and no real-time tuning channel. | ADR-0015 d1, ADR-0025 |
| The gateway runs control loops locally against a cached profile. The cloud supplies profiles and observes; it does not command in real time. | ADR-0015 d8, ADR-0004 rev 1 |
| The over-temperature trip is analog, MCU- and bus-independent, and sits at the heating actuator. Profiles set operating parameters; hardware sets survival parameters. | ADR-0018 d10 |
| Sensor density is a deployment phase, not a fixture: dense during the empirical survey, reduced for the operating phase. The profile carries the identified model with the setpoints. | ADR-0016 |

## Technology

| Layer | Choice |
|---|---|
| Field bus | Cyphal over classic CAN @ 500 kbit/s, linear topology |
| Smart-node MCU | WeAct STM32F4 64-pin core board (STM32F405RGT6; F412/F446 drop-in) |
| Node carrier PCB | Custom integration board: CAN transceiver, ATECC608 secure element, two-header sensor-module interface |
| Node firmware | Embedded C with libcanard. One application holds every module personality; the module-ID strap selects at boot |
| Node flash layout | Bootloader at the reset vector plus the application in one of two slots; build output is `igrowboot.hex`, `igrow-a.hex`, `igrow-b.hex` |
| Firmware update | Signed image served over the bus, `uavcan.file.Read` to the slot that is not running, trial boot with fallback |
| Gateway | Raspberry Pi (3B+ minimum, 4/5 for higher traffic) + isolated 2-channel CAN HAT |
| Gateway software | Python / asyncio, SocketCAN + Pycyphal + Nunavut-generated DSDL bindings |
| Cloud link | mTLS to IndustryFlow; stateless edge in steady state, with a bounded local store for buffering and survey |
| Identity | Module class from the ID strap, `unique_id` from the ATECC608, Node-ID from a flash store; 127 = unprovisioned |
| Security | Operator CA, signed firmware, signed profiles |

Governing records: ADR-0002 rev 3 (bus, carrier, gateway), ADR-0005 (DSDL), ADR-0020
(persistence), ADR-0027 (identity), ADR-0029 (update and bootloader), ADR-0024 / ADR-0025 (CA,
profile signing).

> PCBs are authored in **KiCad 10** and will not open in earlier versions; cabinet distribution
> schematics (`E0007`) use **QElectroTech**. Design files live in `store/` under the ADR-0017
> scheme, packaged per document layer as `Exxxx-VVVVVV-S-src.zip` / `-D-src.zip` (d19).
> [`CONTRIBUTING.md`](CONTRIBUTING.md) has the unpack/repack procedure.

## Hardware

Seven sensor-module designs, one functional subsystem each, on a universal carrier. Instances are
specialized by populated BOM, never by new designs. E-numbers, sensor complement, module-ID straps
and variant rules: [`REGISTRY.md`](REGISTRY.md).

| Design | E-number | State |
|---|---|---|
| Universal carrier | `E0001` | v0.0.3 released; in service on the bench |
| M01-CLIMATE | `E0002` | Fabricated; bench-verified 2026-08-24, ten subjects publishing |
| M02-LIGHT | `E0003` | Laid out, DRC/ERC/parity clean; not fabricated |
| M03-ANALYTICS | `E0004` | Topology fixed (ADR-0006); front-end ADR open |
| M04-PLANT | `E0005` | Blocked on a bulk-transport decision |
| M05-SAFETY | `E0006` | Fabricated; bench-verified, seven subjects publishing |
| M06-VENTILATION | `E0008` | Specified; not laid out |
| M07-AMBIENT | `E0009` | Specified; not laid out |
| Distribution case | `E0007` | Schematic `E0007-000001-S` (QElectroTech) |

## Status

| Phase | Scope | State |
|---|---|---|
| Phase 1 | Hardware and firmware bring-up; M01–M05 plus gateway; standalone, no cloud | In progress |
| Phase 2 | Cloud integration: mTLS ingestion, profile sync, audit trail | Blocked on external IndustryFlow work (ROADMAP stage 11) |
| Phase 3 | Community profile registry, predictive ML, multi-cabinet coordination | Planned |

Stage-level sequencing: [`project/ROADMAP.md`](project/ROADMAP.md), 14 dependency-ordered stages.

## Repository

| Path | Contents | Records |
|---|---|---|
| [`ADR/`](ADR/) | Architecture decision records — the source of truth — plus [`GLOSSARY.md`](ADR/GLOSSARY.md) | ADR-0000 |
| [`REGISTRY.md`](REGISTRY.md) | E-number / SP-number identifier map, machine-readable | ADR-0017, ADR-0019, ADR-0023 |
| [`store/`](store/) | Hardware design sources, fab packages and firmware release artifacts, flat, keyed by identifier | ADR-0017 |
| [`spec/`](spec/) | Module specifications — M01, M02, M05, M06, M07. M03 and M04 not yet written | ADR-0014 |
| [`firmware/`](firmware/) | Node firmware (C, libcanard) and the `industryflow.greenhouse` DSDL vocabulary | ADR-0005, ADR-0029 |
| [`gateway/`](gateway/) | Gateway provisioning, hardening, identity and profile-pull client | ADR-0004, ADR-0015 |
| [`erp/`](erp/) | Instance-and-integration ERP — the pre-cloud system of record | ADR-0021, ADR-0022 |
| [`pki/`](pki/) | Operator CA bootstrap and root-key ceremony | ADR-0024 |
| [`signing/`](signing/) | Profile signing tool and runbook | ADR-0025 |
| [`profiles/`](profiles/) | Cultivation profile instances | ADR-0003 |
| [`project/`](project/) | `ROADMAP.md` (14 stages), `RESEARCH.md` (open directions L1–L7) | — |
| [`MOTIVATION.md`](MOTIVATION.md) | The gap this project closes | — |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Contribution licensing, DCO sign-off, working with the store | — |

## Build and run

Firmware — three images, ARM cross-toolchain and Ninja required:

```sh
firmware/tools/bootstrap.sh            # pin submodules; needs nnvg (Nunavut)
cmake -S firmware -B firmware/build -G Ninja \
      -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/firmware/cmake/arm-none-eabi.cmake"
cmake --build firmware/build
```

ERP — application plus operator console:

```sh
cd erp && docker compose up --build    # app on 127.0.0.1:8021
```

Gateway — runs on the Pi, post-first-boot, idempotent:

```sh
sudo ./provision.sh                    # from the staged gateway/ bundle
```

Per-component detail, flashing procedure and signing keys are in each directory's `README.md`.

## Licensing

Open-core, licensed per part of the repository. [`LICENSE.md`](LICENSE.md) is the authoritative
mapping — hardware designs, documentation, application code and the DSDL protocol layer carry four
different licences, and directory-level assumptions do not hold. Full texts are in
[`LICENSES/`](LICENSES/).

Third-party content keeps its own terms, annotated in [`REUSE.toml`](REUSE.toml): the WeAct
core-board snapshot is published with **no licence stated**, so it is not covered by this project's
licences. Compliance is CI-enforced — run `reuse lint` before opening a pull request.
