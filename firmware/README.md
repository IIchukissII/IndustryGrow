<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# IndustryGrow node firmware

Reference firmware for the IndustryGrow Cyphal/CAN sensor nodes. One codebase,
one carrier (`E0001`), one MCU family (STM32F405RGT6 on the WeAct STM32F4 64-Pin
Core Board, ADR-0002 rev 3); the sensor-module personality varies per node type
(ADR-0002 decision 5). Two images exist: **M05-SAFETY** (`E0006`) and
**M01-CLIMATE** (`E0002`).

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
> - **Carrier identity (ADR-0007):** the ATECC608 secure element on I²C2 is read
>   at boot for its 9-byte serial, which becomes the Cyphal node `unique_id`
>   (falling back to the STM32 factory UID when absent). Identity/provenance only
>   — no crypto, key generation, or provisioning (those are Production/Phase-2 per
>   ADR-0007 d6/d9); the node secure element is not a CAN-bus credential (d5).
> - **Verified on hardware (bare WeAct F405, ST-Link V3):** 168 MHz clock,
>   module-ID strap self-check (correctly flags the `0b000` no-carrier mismatch),
>   **bxCAN loopback self-test**, and the libcanard node coming up — all confirmed
>   over the USART1 debug log. The released image is `store/E0001-000001-F.hex`.
> **Status: M01-CLIMATE compiles; nothing bench-checked — `E0002` is ordered, not
> received.** Shares the whole common set with M05 and adds:
> - **Sensors:** SHT45 (`0x44`, primary T/RH), BME688 (`0x76`, VOC resistance +
>   barometric pressure + secondary T/RH), SCD41 (`0x62`, CO₂). Same boot probe
>   and 60 s re-probe (ADR-0014 d8) — a partial population needs no rebuild.
> - **Derived on-node:** air VPD from the SHT45 alone (M01 spec §6.1). The
>   BME688 and SCD41 T/RH are published on their own subjects and never enter it.
> - **CO₂ handling:** ASC disabled and `persist_settings` issued only when the
>   device is actually found with ASC on (2000-cycle EEPROM, spec §10); ambient
>   pressure fed from the BME688 each cycle, falling back to 1013 hPa when the
>   BME688 is unpopulated (closes spec O-48). Forced recalibration and the U3
>   temperature offset are deliberately **not** implemented — both are barred
>   until the board exists (spec §6.3.1, O-45).
> - **New wire types:** `industryflow.greenhouse.climate.{RelativeHumidity,
>   Co2Concentration,GasResistance}` — minted only where the standard set has no
>   type (ADR-0005 d2). Temperature rides `uavcan.si.sample.temperature.Scalar`
>   and both VPD and barometric pressure ride `uavcan.si.sample.pressure.Scalar`.
> - **Console:** unlike M05, M01 claims no header GPIO (spec §5), so USART1 stays
>   the debug console through boot **and** run.
>
> - **Next (needs the carrier PCB):** bus-level CAN **enumeration on the gateway**
>   (the bare WeAct has no transceiver) and live **sensor readings** — the I²C
>   sensors, reed, leak, S0, and the **ATECC608 identity read** are authored against
>   the datasheets but not yet bench-checked. Then wire the sensor subject-IDs to
>   `uavcan.pub.*.id` registers (ADR-0005 d7) and the gated leak excitation pin
>   once it is in the E0006 net/pin map.
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
- **Identity** — module *class* is read from the ID straps at boot (ADR-0014 d6/d8);
  the carrier's ATECC608 secure element supplies the *instance* identity — its
  serial becomes the Cyphal node `unique_id` (ADR-0007), with the STM32 factory
  UID as fallback. Role/zone are *not* in firmware, they are gateway-side tags
  (ADR-0014 d7).

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
pattern and adds its sensor personality, nothing more. **All personalities are
compiled into one image and the strap selects among them at boot** (ADR-0017
d16): the personality is a property of the board in the socket, not a build
variant. M02–M04 become sibling `nodes/` later, each reusing the same carrier
unit and adding one line to `nodes/registry.c`.

```
firmware/
├── README.md  CMakeLists.txt      ← one target: igrow.elf
├── cmake/      arm-none-eabi.cmake (toolchain) · dsdl.cmake (Nunavut codegen)
├── ldscripts/  STM32F405RGTx_FLASH.ld
├── common/                       ← runs identically whichever module is fitted
│   ├── node/       main.c node.h  ← boot → strap → personality dispatch; the seam
│   ├── carrier/    e0001.{h,c}    ← the E0001 carrier (PARENT): pins, LEDs, straps,
│   │                                ATECC608/identity seam (from E0001-000003-D-pinmap)
│   ├── platform/   clock.{h,c}    ← 168 MHz clock, SysTick, micros (SoC, below the carrier)
│   ├── drivers/    can i2c uart   ← bxCAN, I2C1, debug UART (register-level)
│   └── cyphal/     cyphal registers ← node skeleton: Heartbeat/GetInfo/register/ExecuteCommand
├── nodes/
│   ├── registry.c                ← module-ID → personality table (the ONLY file
│   │                                that knows every node type)
│   ├── m05_safety/               ← the M05 personality (CHILD of E0001)
│   │   ├── module_id.h           ← M05's module-ID class (0x05)
│   │   ├── sensors.{h,c}         ← m05_sensors_init/spin: probe + publish the M05 set
│   │   └── drivers/  ina226 tmp117 s0 leak
│   └── m01_climate/              ← the M01 personality (CHILD of E0001)
│       ├── module_id.h (0x01)  sensors.{h,c}  ← m01_sensors_init/spin
│       └── drivers/  sht4x bme68x scd4x · sensirion (shared CRC-8/word codec)
├── dsdl/industryflow/greenhouse/
│   ├── safety/    DoorStatus, LeakStatus       ← M05 (energy uses standard uavcan.si.sample.energy)
│   └── climate/   RelativeHumidity, Co2Concentration, GasResistance   ← M01
├── third_party/                  ← submodules: libcanard, o1heap, cmsis*, regulated types
└── tools/                        ← bootstrap.sh (fetch + pin submodules)
```

A strap value that no personality claims is **unidentified**, never a guessed
class (ADR-0014 rev 4 d6): the node still brings up the Cyphal skeleton and
enumerates as `org.industrygrow.node.unidentified` at Node-ID 127, publishing no
subjects. A node that is visible can be diagnosed; a silent one cannot.

### Default subject-ID map

Baked defaults in the unregulated range; ADR-0005 d7 makes these
`uavcan.pub.<name>.id` register entries, which neither image has yet.
M05 holds 4096–4102 (`store/E0006-000001-M-bringup-protocol.md`); M01 starts at
4112 so the M05 block can grow.

| ID | M01 subject | Source | Type |
|----|-------------|--------|------|
| 4112 | air temperature | U1 SHT45 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4113 | air relative humidity | U1 SHT45 | `…climate.RelativeHumidity` (ratio) |
| 4114 | air VPD | derived from U1 | `uavcan.si.sample.pressure.Scalar` (Pa) |
| 4115 | CO₂ | U3 SCD41 | `…climate.Co2Concentration` (mole fraction) |
| 4116 | barometric pressure | U2 BME688 | `uavcan.si.sample.pressure.Scalar` (Pa) |
| 4117 | gas resistance (VOC trend) | U2 BME688 | `…climate.GasResistance` (Ω) |
| 4118 | secondary temperature | U2 BME688 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4119 | secondary humidity | U2 BME688 | `…climate.RelativeHumidity` (ratio) |
| 4120 | secondary temperature | U3 SCD41 | `uavcan.si.sample.temperature.Scalar` (K) |
| 4121 | secondary humidity | U3 SCD41 | `…climate.RelativeHumidity` (ratio) |

Only 4112–4114 are admissible for VPD and for the climate control loop; 4118–4121
are the secondary sources of M01 spec §4, and 4120/4121 additionally carry an
uncalibrated offset until spec V7 runs (O-45).

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

**Verification path:** cross-compile, flash the WeAct board over its ST-Link
debug header (`openocd` / `st-flash`), and watch the **USART1 layer-1 bring-up
log** — the 168 MHz clock, the module-ID strap self-check, and the bxCAN
internal-loopback self-test all report there (all confirmed on a bare WeAct
F405). Bus-level **enumeration on the gateway** (`yakut` / `pycyphal`) follows
once the carrier PCB provides a CAN transceiver — the bare WeAct has none. USB
DFU stays available for flashing because CAN1 was placed on PB8/PB9, off the USB
pins (pin-map note 5).

## Build & flash

```sh
# cross build for the WeAct STM32F405 board
cmake -S firmware -B firmware/build -DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake
cmake --build firmware/build
# flash: ST-Link (openocd / st-flash) or WeAct USB DFU (dfu-util)
```

Output is `igrow.hex` / `igrow.bin` — the same image for every node. What the
node becomes is decided by the module plugged into the carrier, not by which
file was flashed. The image *is* built for one carrier revision, because the
identification transport differs between them (ADR-0014 rev 4 d6).

## Release artifacts (`store/`)

`firmware/tools/release.sh` builds the image and publishes it into `store/` under
the ADR-0017 (rev 1) **`F` (Firmware)** document layer (see `REGISTRY.md`):

```
store/E0001-000001-F.hex       # built image
store/E0001-000001-F-src.zip   # source snapshot of firmware/ at HEAD
```

Filed under **`E0001`** (the carrier whose one codebase every node runs; the
personality is strap-selected at runtime, so firmware identity follows the
carrier, not the module — ADR-0017 rev 1 d16), licensed AGPL-3.0-or-later
(annotated in `REUSE.toml`). The local `firmware/build*/` tree stays git-ignored;
only these released artifacts are committed.

One image holds every personality, so one `F` object per firmware version covers
every node type — which is what roots `F` on the carrier in the first place
(ADR-0017 rev 1 d16). Refreshed in place at the same `F` version while the
firmware is unverified against hardware; `F` bumps on a released change.

## Flash tool

Documented for both ST-Link (`openocd` / `st-flash`) and WeAct USB DFU
(`dfu-util`) — CAN1 was kept off the USB pins so DFU stays available.

## References

- ADR-0002 rev 3 — field bus (Cyphal/CAN, MCU, carrier, 500 kbit/s).
- ADR-0005 (rev 1) — DSDL foundation (vocabulary, joule energy, node skeleton, port-IDs).
- ADR-0014 — sensor-node taxonomy (module straps, presence-probing, gateway tagging).
- ADR-0007 — PKI, hardware identity, provisioning (ATECC608 as the identity anchor).
- ADR-0018 — M05 sense-only; door/leak report-only; S0 energy.
- `spec/M01-CLIMATE-specification.md` — M01 sensor complement, accuracies, VPD,
  CO₂ handling and firmware requirements (§10).
- `spec/M05-SAFETY-specification.md` — the as-built form of that document class.
- `store/E0001-000003-D-pinmap.md` — carrier pin map (the BSP source of truth).
