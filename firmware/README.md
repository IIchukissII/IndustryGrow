<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow node firmware

Reference firmware for the IndustryGrow Cyphal/CAN sensor nodes. One codebase,
one carrier (`E0001`), one MCU family (STM32F405RGT6 on the WeAct STM32F4 64-Pin
Core Board, ADR-0002 rev 3); the sensor-module personality varies per node type
(ADR-0002 decision 5). The first node brought up is **M05-SAFETY** (`E0006`).

> **Status: M05 firmware compiles, flashes, and passes layer-1 bring-up on a bare
> WeAct F405.** Built incrementally:
> - **Skeleton:** clock (168 MHz), debug UART, module-ID strap self-check, bxCAN
>   500 kbit/s; libcanard (v3) + o1heap + Nunavut; the full ADR-0005 d5 node
>   skeleton — `Heartbeat`, `GetInfo`, `register` Access/List, `ExecuteCommand`
>   — so the node is built to **enumerate and be configurable on the gateway**.
> - **M05 personality:** INA226 (bus V/I/P), TMP117 (cabinet temp), reed (door),
>   leak (ADC, gated-excitation), S0 pulse → joule energy; I²C presence-probing
>   with 60 s re-probe (ADR-0014 d8); published on the standard SI sample types
>   and the project `industryflow.greenhouse.safety` types (ADR-0005).
> - **Verified on hardware (bare WeAct F405, ST-Link V3):** 168 MHz clock,
>   module-ID strap self-check (correctly flags the `0b000` no-carrier mismatch),
>   **bxCAN loopback self-test**, and the libcanard node coming up — all confirmed
>   over the USART1 debug log. The released image is `store/E0006-000001-F.hex`.
> - **Next (needs the carrier PCB):** bus-level CAN **enumeration on the gateway**
>   (the bare WeAct has no transceiver) and live **sensor readings** — the I²C
>   sensors, reed, leak, and S0 are authored against the datasheets but not yet
>   bench-checked. Then wire the sensor subject-IDs to `uavcan.pub.*.id` registers
>   (ADR-0005 d7) and the gated leak excitation pin once it is in the E0006 net/pin map.
>
> Firmware **sources** are `AGPL-3.0-or-later` (ADR-0002 decision 5); this
> document is `CC-BY-SA-4.0`. First build needs `nnvg` (Nunavut) and the submodules
> from `tools/bootstrap.sh`; cross-build with `arm-none-eabi-gcc` + CMake/Ninja, or
> import the CMake project into STM32CubeIDE.

## What the firmware is

A Cyphal/CAN node. Application protocol and wire vocabulary are fixed elsewhere:

- **Bus** — classic CAN, **500 kbit/s**, linear, Node-ID static/provisioned (ADR-0002 d8; ADR-0005 d6).
- **Vocabulary** — DSDL: the OpenCyphal standard `uavcan.*` set plus the project
  `industryflow.greenhouse.*` types, per **ADR-0005**. Physical quantities ride
  `uavcan.si.sample.*` (SI units); accumulated S0 energy is **joule** (ADR-0005 rev 1 d3);
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
| MCU peripherals | CMSIS device headers + **register-level init** (the LL layer, hand-written — keeps the bring-up self-contained and free of a vendored HAL tree; LL/HAL drivers can be layered in later if a peripheral warrants it) |
| Compiler / build | `arm-none-eabi-gcc` + **CMake** (no IDE lock-in, CI-friendly) |
| Dependency vendoring | **git submodules**, pinned to tags (`firmware/tools/bootstrap.sh`) |

## Layout

One codebase, one carrier, personality per node type (ADR-0002 d5). The **carrier
E0001 is the parent**: `common/carrier/` owns the bus, LEDs, MCU socket and node
identity (module-ID straps + the ATECC608 secure element) shared by every node.
A **node `nodes/<type>/` is a child** of E0001 — it asserts a module-ID strap
pattern and adds its sensor personality, nothing more. This image is M05-SAFETY;
M01–M04 become sibling `nodes/` later, each reusing the same carrier unit.

```
firmware/
├── README.md  CMakeLists.txt
├── cmake/      arm-none-eabi.cmake (toolchain) · dsdl.cmake (Nunavut codegen)
├── ldscripts/  STM32F405RGTx_FLASH.ld
├── common/                       ← shared across all node types
│   ├── carrier/    e0001.{h,c}    ← the E0001 carrier (PARENT): pins, LEDs, straps,
│   │                                ATECC608/identity seam (from E0001-000001-D-pinmap)
│   ├── platform/   clock.{h,c}    ← 168 MHz clock, SysTick, micros (SoC, below the carrier)
│   ├── drivers/    can i2c uart   ← bxCAN, I2C1, debug UART (register-level)
│   └── cyphal/     cyphal registers ← node skeleton: Heartbeat/GetInfo/register/ExecuteCommand
├── nodes/
│   └── m05_safety/               ← the M05 personality (CHILD of E0001)
│       ├── main.c                ← clock → strap self-check → CAN test → node + sensors
│       ├── module_id.h           ← M05's own module-ID strap pattern (0b101)
│       ├── sensors.{h,c}         ← presence-probe + publish the M05 set
│       └── drivers/  ina226 tmp117 s0 leak
├── dsdl/industryflow/greenhouse/safety/   ← DoorStatus, LeakStatus (Apache-2.0; energy uses standard uavcan.si.sample.energy)
├── third_party/                  ← submodules: libcanard, o1heap, cmsis*, regulated types
└── tools/                        ← bootstrap.sh (fetch + pin submodules)
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

## Build & flash

```sh
# host / vcan build for protocol bring-up without hardware
cmake -S firmware -B firmware/build-host -DIGROW_TARGET=host
cmake --build firmware/build-host

# cross build for the WeAct STM32F405 board
cmake -S firmware -B firmware/build -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
cmake --build firmware/build
# flash: ST-Link (openocd / st-flash) or WeAct USB DFU (dfu-util)
```

## Release artifacts (`store/`)

`firmware/tools/release.sh` builds the image and publishes it into `store/` under
the ADR-0017 **`F` (Firmware)** document layer (see `REGISTRY.md`):

```
store/E0006-000001-F.hex       # built image
store/E0006-000001-F-src.zip   # source snapshot of firmware/ at HEAD
```

Filed under **`E0006`** (the M05 module defines the node personality), licensed
AGPL-3.0-or-later (annotated in `REUSE.toml`). The local `firmware/build*/` tree
stays git-ignored; only these released artifacts are committed.

## Flash tool

Documented for both ST-Link (`openocd` / `st-flash`) and WeAct USB DFU
(`dfu-util`) — CAN1 was kept off the USB pins so DFU stays available.

## References

- ADR-0002 rev 3 — field bus (Cyphal/CAN, MCU, carrier, 500 kbit/s).
- ADR-0005 (rev 1) — DSDL foundation (vocabulary, joule energy, node skeleton, port-IDs).
- ADR-0014 — sensor-node taxonomy (module straps, presence-probing, gateway tagging).
- ADR-0018 — M05 sense-only; door/leak report-only; S0 energy.
- `store/E0001-000001-D-pinmap.md` — carrier pin map (the BSP source of truth).
