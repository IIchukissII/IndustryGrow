<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow node firmware

Reference firmware for the IndustryGrow Cyphal/CAN sensor nodes. One codebase,
one carrier (`E0001`), one MCU family (STM32F405RGT6 on the WeAct STM32F4 64-Pin
Core Board, ADR-0002 rev 3); the sensor-module personality varies per node type
(ADR-0002 decision 5). The first node brought up is **M05-SAFETY** (`E0006`).

> **Status: scaffold.** This tree is being built incrementally. The build system
> and STM32 sources land in steps so each is reviewable and compilable on real
> hardware; the bring-up milestone (below) comes first. Firmware **sources** are
> `AGPL-3.0-or-later` (ADR-0002 decision 5); this document is `CC-BY-SA-4.0`.

## What the firmware is

A Cyphal/CAN node. Application protocol and wire vocabulary are fixed elsewhere:

- **Bus** — classic CAN, **500 kbit/s**, linear, Node-ID static/provisioned (ADR-0002 d8; ADR-0005 d6).
- **Vocabulary** — DSDL: the OpenCyphal standard `uavcan.*` set plus the project
  `industryflow.greenhouse.*` types, per **ADR-0005**. Physical quantities ride
  `uavcan.si.sample.*` (SI units); accumulated S0 energy is **Wh** (ADR-0005 d3);
  door/leak are minimal `safety` status types with no command field (M05 is
  sense-only, ADR-0018 d9).
- **Identity** — module class is read from the ID straps at boot (ADR-0014 d6/d8);
  role/zone are *not* in firmware, they are gateway-side tags (ADR-0014 d7).

## Toolchain (decided)

| Concern | Choice |
|---|---|
| Cyphal transport | [libcanard](https://github.com/OpenCyphal/libcanard) (MIT) |
| Allocator | [o1heap](https://github.com/pavel-kirienko/o1heap) (MIT) |
| DSDL → C | [Nunavut](https://github.com/OpenCyphal/nunavut) (pinned via pip), run at build time; generated code **not** vendored (ADR-0005 d10) |
| Standard types | [public_regulated_data_types](https://github.com/OpenCyphal/public_regulated_data_types), pinned |
| MCU peripherals | STM32 **LL/HAL** (CubeMX-class init, hand-trimmed) |
| Compiler / build | `arm-none-eabi-gcc` + **CMake** (no IDE lock-in, CI-friendly) |
| Dependency vendoring | **git submodules**, pinned to tags (proposed — see *Open setup choices*) |

## Planned layout

```
firmware/
├── README.md                 ← this file
├── CMakeLists.txt            ← top-level build (host + cross)
├── cmake/
│   ├── arm-none-eabi.cmake   ← cross toolchain file
│   └── dsdl.cmake            ← Nunavut codegen integration
├── ldscripts/STM32F405RG.ld  ← linker script (1 MB flash / 192 KB RAM)
├── bsp/                       ← board support: pin map from E0001-000001-D-pinmap
│   ├── board.h               ← pin defines (CAN1 PB8/PB9, I2C1 PB6/7, straps PA5/6/7, …)
│   └── board.c
├── src/
│   ├── main.c                ← bring-up app
│   ├── cyphal/               ← libcanard glue: heartbeat, GetInfo, register, port allocation
│   ├── drivers/              ← bxCAN, I2C, ADC, GPIO, timer (S0), UART (debug) via LL
│   └── platform/             ← clock, SysTick, fault handlers, retarget
├── dsdl/industryflow/greenhouse/
│   └── safety/               ← project types: DoorStatus, LeakStatus, EnergyWh (ADR-0005)
├── third_party/              ← submodules: libcanard, o1heap, public_regulated_data_types
└── tools/                    ← flash + vcan helpers
```

## Bring-up milestone (roadmap stage 1)

**Goal: the node enumerates on the gateway console.** Scope kept minimal — only the
standard node skeleton (ADR-0005 d5), no sensor publications yet:

1. Clock to 168 MHz (HSE 8 MHz × PLL), SysTick 1 kHz.
2. `bxCAN1` on **PB8 (RX) / PB9 (TX)**, AF9, 500 kbit/s; CAN-activity LED on **PA2**.
3. libcanard + o1heap up; publish **`uavcan.node.Heartbeat.1`** at 1 Hz; answer
   **`uavcan.node.GetInfo.1`** and the **register** interface (`Access`/`List`).
4. Module-ID strap self-check: read **STRAP_0 = PA5**, **STRAP_2 = PA7**; expect
   `0b1·1` for M05. **STRAP_1 (PA6) is unrouted to the MCU on `E0001-000001`**
   (see the pin-map note / the tracked carrier fix), so bit 1 is read as a
   pulled-down `0`, which matches M05's `0b101` — flagged in code, not silently
   assumed. Status LED on **PA1** signals identity OK / mismatch.
5. Optional debug log over **USART1 (PA9/PA10)**.

**Verification path:** build for host first and run libcanard over **SocketCAN
`vcan0`** (mirrors the gateway self-test already in `gateway/`), then cross-compile
and flash the WeAct board; confirm the node appears via the gateway (`yakut`/
`pycyphal`). USB DFU stays available for flashing because CAN1 was placed on
PB8/PB9, off the USB pins (pin-map note 5).

## Build & flash (once sources land)

```sh
# host / vcan build for protocol bring-up without hardware
cmake -S firmware -B firmware/build-host -DIGROW_TARGET=host
cmake --build firmware/build-host

# cross build for the WeAct STM32F405 board
cmake -S firmware -B firmware/build -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
cmake --build firmware/build
# flash: ST-Link (openocd / st-flash) or WeAct USB DFU (dfu-util)
```

## Open setup choices (will proceed on the defaults unless redirected)

- **Dependency vendoring:** git submodules pinned to tags (vs. CMake `FetchContent`).
  Submodules keep the exact upstream commit auditable in-tree; default = submodules.
- **Flash tool:** documented for both ST-Link and USB DFU; no single one is forced.

## References

- ADR-0002 rev 3 — field bus (Cyphal/CAN, MCU, carrier, 500 kbit/s).
- ADR-0005 — DSDL foundation (vocabulary, Wh energy, node skeleton, port-IDs).
- ADR-0014 — sensor-node taxonomy (module straps, presence-probing, gateway tagging).
- ADR-0018 — M05 sense-only; door/leak report-only; S0 energy.
- `store/E0001-000001-D-pinmap.md` — carrier pin map (the BSP source of truth).
