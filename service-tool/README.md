<!-- SPDX-FileCopyrightText: 2026 The IndustryGrow contributors -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Service tool firmware — `E0010`

Reads the cabinet CAN bus and shows it, executes a carried `-M-` protocol step by
step, and emits the record it does not keep. Requirements, interfaces and
verification are in [`spec/SERVICE-TOOL-specification.md`](../spec/SERVICE-TOOL-specification.md);
the role and its bounds are in `ADR/ADR-0030`. Neither is restated here.

Hardware bring-up order, the traps, and what has been measured on each board are
in [`BENCH.md`](BENCH.md).

## What this directory is

Self-contained. Nothing elsewhere in the repository refers to it and the node
firmware build does not build it. The one thing it reaches for is the
vocabulary: `cmake/dsdl.cmake` runs Nunavut over `firmware/dsdl` and
`firmware/third_party/public_regulated_data_types` at build time, so the tool
decodes the same types the nodes publish rather than a second copy that can
drift. `app/subjects.c` holds only the subject-ID to type binding, mirroring
`gateway/files/app/igrow_subjects.py`.

## Targets

One codebase, one `F` root, two build variants (ADR-0017 d16). `-DIGROW_BOARD=`
selects the MCU, the startup file, the BSP, the panel size and the core supply;
`Core/Inc/eval_board.h` is the only file that knows which.

| | `H753` (default) | `H757` |
|---|---|---|
| Board | STM32H753I-EVAL | STM32H757I-EVAL |
| Panel | MB1063, 640x480, LTDC parallel RGB | MB1166, 800x480, DSI |
| Build tree | `build/` | `build-h757/` |

| Image | What |
|---|---|
| `igrow-panel` | the tool: FreeRTOS + LVGL + Cyphal |
| `igrow-min` | bare metal, console first, every wait bounded — for when the panel goes silent |
| `igrow-check` | headless board self-test, no display needed |

## Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1                        # H753, all three
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Board H757            # H757, all three
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Board H757 -Target igrow-check.elf

powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757 -Image igrow-check
powershell -ExecutionPolicy Bypass -File tools\mon.ps1 -Port COM4 -Seconds 30
```

`flash.ps1 -Board` selects the probe by serial number: both eval boards present
an ST-LINK-V3E as `VID_0483&PID_374E`, and an image flashed into the wrong MCU is
not a diagnosis.

### Dependencies

| CMake variable | What | Vendored here |
|---|---|---|
| `CUBE_DIR` | STM32CubeH7 — HAL, CMSIS, both EVAL BSPs, components | no |
| `LVGL_DIR` | LVGL 9.2 | no |
| `IGROW_REPO` | this repository — libcanard, o1heap, the DSDL | n/a, defaults to `..` |

STM32CubeH7 and LVGL are large upstream trees and are not vendored. Clone them
and pass `-DCUBE_DIR=` and `-DLVGL_DIR=`, or place them under `deps/`
(gitignored), which is where CMake looks by default. `tools\build.ps1` passes
this bench's own location; it is one line at the top of that file.

`arm-none-eabi-gcc` 12.3 and `STM32_Programmer_CLI` both come from the
STM32CubeIDE 1.16.0 installation; `nnvg` is Nunavut 2.3.1 from the user Python
install. Nothing extra needs installing.

Files retained from STM32CubeH7 and LVGL keep their own terms — see the
third-party section of [`LICENSE.md`](../LICENSE.md).

## Layout

```
app/     can_port      FDCAN, the pin sweep, statistics
         cyphal_rx     libcanard, subscriptions, the two request types
         subjects      subject-ID -> type -> display unit, and the decoders
         model         what has been seen: nodes, signals, 240-sample histories
         protocol      the carried -M- procedure, its executor and the emitted record
         console       USART1 text output; the whole UI when there is no display
         panel_mem     the hand-partitioned SDRAM map
ui/      lvgl_port     display, touch, joystick, LVGL binding
         ui_main       the six tabs and joystick navigation
diag/    min_main      the bare-metal image
         board_check   the headless self-test
Core/    ST template files, both startup files, the linker script, lv_conf.h
```

## Interface

Six tabs — Bus, Liveness, Nodes, Plot, Values, Protocol.

Driven by the MB1246 five-way alone: arrows move, a click on SEL enters, a hold
on SEL goes back. The two push-buttons blank and restore the display and take no
part in navigation. Touch is used where the fitted panel has it.

Ground and accents come from `img/industrygrow-logo.svg`: navy `#0A1428` with
the mark's cyan/green family. The four status colours are **not** brand colours
and are never reused as a series colour; they map one to one onto
`uavcan.node.Health`, and every use pairs the colour with a word, so nothing
means anything by colour alone. One signal is plotted at a time, so there is no
categorical palette and no legend. Every mark clears 3:1 against the navy and
every text colour 4.5:1.

If the display does not come up the firmware carries on and reports the same
material as text on the ST-LINK virtual COM port at 115200 8N1.

## Bus parameters

Fixed by ADR-0002 d8 and not configurable here.

| Parameter | Value |
|---|---|
| Format | Classic CAN, `FDCAN_FRAME_CLASSIC`; the tool never emits an FD frame |
| Bit rate | 500 kbit/s |
| Kernel clock | HSE, 25 MHz — not a PLL output, so display retuning cannot move the bit rate |
| Bit timing | Prescaler 2, TSEG1 21, TSEG2 3, SJW 3 → 25 tq, sample point 88 % |
| Node-ID | 10 by default. 1 is the gateway time master, 96 and 97 are the nodes |

The tool joins in **bus monitoring mode** and stays there until somebody presses
a button; in that mode the peripheral does not drive the bus at all, not even the
ACK bit. It publishes no heartbeat.

## Memory

| Region | Address | Contents |
|---|---|---|
| Flash | `0x08000000` | 362 KB of 1024 KB |
| DTCM | `0x20000000` | `.data`, `.bss`, stack |
| AXI SRAM | `0x24000000` | `.axi_bss` — the 48 KB libcanard arena |
| SDRAM | `0xD0000000` | LTDC framebuffer, 4 MB reserved: holds either panel at RGB565 |
| | `0xD0400000` | Two LVGL draw buffers, 120 lines each |
| | `0xD0480000` | LVGL heap, 4 MB |
| | `0xD0900000` | `.sdram_bss` — the model |

The low 9 MB of SDRAM are assigned by address in `app/panel_mem.h` because the
LTDC and LVGL are handed addresses at init, not linker symbols. Static asserts
catch the framebuffer and the heap running into what follows them.

The framebuffer is written by the CPU and read by the LTDC, which does not see
the D-cache, so the MPU maps SDRAM write-through.
