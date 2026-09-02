<!-- SPDX-FileCopyrightText: 2026 The IndustryGrow contributors -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Bench bring-up

Order matters: each step's failure is diagnosable only if the one before it passed.

## Boards

| | R1 | R2 |
|---|---|---|
| Board | STM32H753I-EVAL | STM32H757I-EVAL |
| MCU | H753XI, single core | H747XI, dual core, CM7 image only |
| Panel | MB1063, 640x480, LTDC parallel RGB | MB1166, 800x480, DSI, OTM8009A |
| Touch | EXC7200 at `0x08`, works | FT6x06 at `0x70`, answers; glass dead (`O-85`) |
| Core supply | `PWR_LDO_SUPPLY` | `PWR_DIRECT_SMPS_SUPPLY` |
| Console | COM5 | COM4 |
| Probe volume | `EVA_H753XI` | `EVA_H757XI` |
| Build | `tools\build.ps1` into `build\` | `tools\build.ps1 -Board H757` into `build-h757\` |

Both are the MB1246 motherboard: connectors, SDRAM, QSPI, MFX, joystick, codec,
PHY and CAN are identical. The STM32H753 has no DSI peripheral, so MB1166 runs
only on R2.

`Core/Inc/eval_board.h` turns `-DIGROW_BOARD=` into the BSP, the device header,
the panel size and the supply option. `tools\flash.ps1 -Board` selects the probe
by serial number; all three ST-LINKs here are `VID_0483&PID_374E`.

## Core supply

**The option is not interchangeable and the wrong one locks the board until a
power cycle.** CR3 bit 2 is `SCUEN` on the H753 and `SMPSEN` on the H747.

| Board | Option | Wrong option |
|---|---|---|
| R1 | `PWR_LDO_SUPPLY` | — |
| R2 | `PWR_DIRECT_SMPS_SUPPLY` | LDO switches off the SMPS feeding the core; `NRST` does not recover it and SWD reports `No STM32 target found` |

`PWR_EXTERNAL_SOURCE_SUPPLY` is never correct on either.

## 0. Before power

The CAN transceiver is **not connected to the MCU as shipped**: `PA11`/`PA12` are
shared with USB OTG FS and the board steers them at USB.

| MB1246 | Required for CAN |
|---|---|
| CAN connector | **CN3** |
| JP1, JP2 | **fitted** |
| SB59, SB60 | **open** |
| SB50 | **closed** |

| Check | |
|---|---|
| `CAN_H` / `CAN_L` on the Sub-D by continuity to the transceiver | The pinout is not in the BSP and is unverified |
| The jumpers and bridges above | A board left in the USB configuration is indistinguishable from wrong pins: idle RX, no frames, no errors |
| **A termination is fitted on the tool; M01's was removed to compensate** | `O-84`. I5 fails until it moves back |
| No third terminator | Drops the differential load to ~40 Ω, degrading margin while still appearing to work |
| Drop under ~30 cm | Stub length at 500 kbit/s |

Cable — Molex Micro-Fit 3.0 4-pin, three wires:

| Pin | Signal | To the tool |
|---|---|---|
| 1 | +12 V | **not connected** |
| 2 | GND | GND — the node transceivers are not isolated |
| 3 | `CAN_H` | `CAN_H` |
| 4 | `CAN_L` | `CAN_L` |

## 1. Flash and console

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Board H757
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757
powershell -ExecutionPolicy Bypass -File tools\mon.ps1  -Port COM4 -Seconds 30
```

Each build produces three images; `-Image igrow-min` and `-Image igrow-check`
flash the other two. **Open the console before resetting** — the banner prints
once.

```
boot
SUPPLY  : ConfigSupply(direct SMPS) HAL_OK, CR3 bit 2 was 1
VOS     : D3CR 00006000 -> 0000C000 -> 0000E000  CSR1 0000E000  waited 0 ms
CLOCK   : SYSCLK 400000000 Hz, HCLK 200000000 Hz

=== IndustryGrow panel (STM32H757I-EVAL + MB1166) ===
SDRAM   : ok
display : ok (800x480)
touch   : ok (controller answered)
joystick: ok - arrows move, click SEL to enter, HOLD SEL to go back
controls: PA0 or PC13 (B2) blanks the display
CAN     : classic, 500 kbit/s, LISTEN-ONLY on FDCAN1 PA11/PA12
hunting for the CAN pins (listen-only, 3 s per candidate)
no CAN frames on any candidate pin pair - back on FDCAN1 PA11/PA12 (listen-only)
```

| Banner line | Reads as |
|---|---|
| `SUPPLY ... HAL_ERROR` | Warm boot: the configuration was applied by the boot after the last power cycle. Not a fault |
| `CLOCK : SYSCLK 200000000` | Same cause — Scale 1 needs a power cycle |
| `touch : ok` | The controller answered on I2C during `BSP_LCD_InitEx`. Says nothing about the glass; `c` prints press counts for both inputs |
| `no CAN frames on any candidate` | No cable, or step 0 not done. The sweep returns the interface to candidate 0 |
| `display : FAILED` | The firmware keeps running, prints the LCD return code and an I2C scan, and reports nodes and values as text every 5 s |

## 2. The CAN pin sweep

Three candidates, 3 s each, bus monitoring throughout; stops at the first pair
that receives a frame.

| Candidate | |
|---|---|
| 0 | **FDCAN1 PA11/PA12** — the pair on both boards |
| 1 | FDCAN1 PB8/PB9 |
| 2 | FDCAN2 PB12/PB13 |

`PD0/PD1` and `PH13/PH14` are FMC lines carrying the SDRAM the framebuffer lives
in; `PB6` is `I2C1_SCL`. **They must stay out of the sweep** — driving any as
FDCAN takes out the display.

| Outcome | Means |
|---|---|
| `CAN frames on <pair>` | Physical layer and bit timing both right |
| No frames, no errors, `LEC` 7 | **Not connected.** Never a wrong bit rate — that produces error frames and a climbing `REC` |

Confirm the bus is live before permuting wiring:
`ssh igrow@gbox-dev.fritz.box "timeout 6 candump -n 15 can0"`.

## 3. Reading the bus

| Observation | Means |
|---|---|
| Frames climb, transfers climb | Working |
| Frames climb, transfers 0 | Physical layer right, layer above wrong: corrupt frames or a non-Cyphal bus |

**Nodes** lists 96 and 97 within a second or two (1 Hz heartbeat) plus node 1,
the gateway time master. Names stay blank until GetInfo, which needs normal mode.

## 4. The display

| Board | Daughterboard | Interface | Touch |
|---|---|---|---|
| R1 | MB1063, AMPIRE 640x480 | LTDC parallel RGB | EXC7200 at `0x08` |
| R2 | MB1166, 800x480 | DSI, OTM8009A | FT6x06 at `0x54`, or `0x70` on revision A02 |

Identifying an unknown daughterboard:

| Evidence | Reading |
|---|---|
| `BSP_LCD_InitEx` returns **-7** (`BSP_ERROR_UNKNOWN_COMPONENT`) | LTDC/DSI came up; the panel controller did not answer. Not a peripheral fault |
| `BSP_LCD_InitEx` returns **-5** | The touch controller did not answer on I2C — a bus diagnosis, not a display one |
| I2C scan finds `0x08` | EXC7200, so MB1063 |
| I2C scan finds `0x54` or `0x70` | FT6x06, so MB1166 |
| `0x84`, `0x34` | MFX IO expander and audio codec, both motherboard |

The return code and the scan print automatically on any display failure.

## 5. Leaving listen-only

Only after steps 2 and 3 pass. **Go normal** on the Bus tab leaves monitoring
mode and the tool starts ACKing. Watch TEC and REC stay at 0 with no bus-off.

Nothing is transmitted until a button is pressed even in normal mode: no
heartbeat, no diagnostics.

## 6. Manual actions

| Action | Requires | Effect |
|---|---|---|
| Ask GetInfo | Normal mode | Fills the node's name column |
| Restart node | Normal mode, two presses within 5 s | `uavcan.node.ExecuteCommand` 65535 to the selected node |

`ExecuteCommand` is sent at **1.0**; 1.1 would not deserialize on the nodes. The
restart arming lapses after 5 s and takes two presses — the node leaves the bus.

## Input

Everything is reachable from the five-way alone. It is on the MB1246
motherboard, so it works with either panel or none, and hangs off the MFX on the
same I2C1 as the touch controller. Neither push-button navigates; both blank and
restore the display.

Three levels, shown not remembered: the tab row is outlined on the first, the tab
page on the second, and neither once a widget carries its focus ring.

| | Tab row | Landed in a tab | Widget selected |
|---|---|---|---|
| LEFT / RIGHT | previous / next tab, wrapping | select the entry widget | nearest widget that way |
| DOWN | go into the tab | select the entry widget | nearest widget that way |
| UP | — | back out to the tab row | nearest that way; off the top returns to landed |
| SEL, clicked | go into the tab | **nothing** | activate the selected widget |
| SEL, held 500 ms | — | back out to the tab row | back out to the tab row |

The entry widget is topmost then leftmost. Focus is rebuilt from the visible tab
on every tab change. `nav_nearest()` scores candidates as travel along the axis
plus twice the drift across it, rejecting anything more sideways than forwards.

## Voltage Scale 1

Both boards reach Scale 1 and run at **SYSCLK 400 MHz / HCLK 200**. Scale 1
arrives via Scale 2 in about 834 000 cycles.

Call `HAL_PWREx_ConfigSupply` with the board's option **before**
`__HAL_PWR_VOLTAGESCALING_CONFIG`, then poll `CSR1.ACTVOS` / `ACTVOSRDY`.

| Trap | |
|---|---|
| `HAL_PWREx_ConfigSupply` not called | The only HAL call that completes the supply handshake and waits for `ACTVOSRDY`. Without it the flag stays 0 and every Scale 1 request is ignored |
| Polling `D3CR.VOSRDY` | Already set for Scale 3 at reset and slow to clear: a read just after the write returns a stale 1 and reports a transition that never happened |
| ST's unbounded `while (!VOSRDY)` | Hangs before `console_init` — dark screen, silent console, indistinguishable from a dead board. `SystemClock_Config` bounds it at 100 ms |

The supply configuration applies **once per power-on**. After a programmer reset
or `NRST` the call is refused or does nothing, so a 200 MHz boot right after
flashing is expected.

| | PLL1 N | SYSCLK | HCLK | APB | Requires |
|---|---|---|---|---|---|
| full | 160 | 400 MHz | 200 MHz | 100 MHz | Scale 1 |
| reduced | 80 | 200 MHz | 100 MHz | 50 MHz | legal at Scale 3 |

| Consequence of the reduced set | |
|---|---|
| SDRAM refresh | The BSP constant assumes a 100 MHz SDRAM clock (HCLK/2). At 50 MHz it refreshes every row half as often as the part requires — slow framebuffer corruption, not a failed init. `panel_sdram_init()` rescales from `HAL_RCC_GetHCLKFreq()` |
| CAN bit rate | Unchanged. `can_port.c` puts FDCAN on HSE directly (`RCC_FDCANCLKSOURCE_HSE`) |
| CPU load | Unchanged: 99 % idle under the GUI at both clocks. Memory-bound work doubles |

## The bare-metal image

`diag/min_main.c` → `igrow-min.hex`: same HAL and BSP, no FreeRTOS, no LVGL, no
CAN. Console first, on the reset clock; every wait bounded.

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757 -Image igrow-min
```

Prints the PWR registers at boot and after the VOS request, brings up SDRAM,
verifies 8192 words, initialises the LCD, draws a cyan frame on navy with a green
block.

| Key | |
|---|---|
| `p` | PWR dump |
| `v` | retry Scale 1 |
| `1`, `2` | SMPS supply options, H757 only |
| `3` | LDO only — **browns out the H757**, needs a power cycle |
| `d` | redraw |
| `o` / `O` | display off / on |
| `r` | reset |

## The second core

R2 is not running only our image.

| Read off the board | |
|---|---|
| `BCM4` option byte | `0x1`, CM4 boot enabled |
| `BOOT_CM4_ADD0` | `0x8100000`, bank 2 |
| First words at `0x08100000` | `10048000 081127E1` — valid stack pointer and reset vector |

The board arrived carrying a previous CANopen project; flashing the panel erases
only the CM7 sectors in bank 1, and the option byte boots the CM4 into bank 2 at
every reset. All 13 self-tests pass with it present. To remove it, erase bank 2
or clear `BCM4`; read the option bytes first rather than trusting a bit name:

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757 -ShowOptionBytes
```

## Measured

| | R1 | R2 |
|---|---|---|
| `igrow-check`, 13 tests | pass | pass |
| SDRAM, 32 MB sweep | pass | pass, 297 ms at 400 MHz |
| Display, touch, LVGL UI | pass | display pass; touch glass dead (`O-85`) |
| FDCAN internal loopback | 32/32, 0 corrupt | 32/32, 0 corrupt |
| Live bus, 10 min listen-only | **26 358 frames, 11 499 transfers, 0 unknown, 0 dropped, TEC/REC 0, 17 subjects live** (2026-09-02, spec V10) | **22 830 frames, 9 929 transfers, 0 dropped, TEC/REC 0** (earlier, while R2 carried MB1063) |

A third 10-minute run — 22 078 frames, 2026-09-01, spec V10 — names no board and
nothing in the repository records which it was.

## Not exercised

- The 88 % sample point against whatever the SN65HVD230 nodes actually use.
- Touch **orientation** on R1: init succeeds and LVGL reads it, but whether a
  press lands under the finger is unchecked. `TS_SWAP_NONE` in
  `panel_touch_init()` is the constant to change.
- LVGL's partial-render flush against the write-through SDRAM framebuffer;
  tearing would show as horizontal bands.
- Normal mode on a live bus (step 5) — every bus run so far has been
  listen-only.

## Traps

| | |
|---|---|
| `BSP_I2C1_DeInit()` | **Never call it.** It decrements its reference counter twice, underflowing 0 to `0xFFFFFFFE`, after which I2C1 can never be initialised again. Took the display from 3 boots in 5 to 0 in 8 |
| `BSP_JOY_GetState()` polled | Reports SEL held forever: it tests `(pins \| SEL_PIN) == mask` first, and idle pins are already the whole mask. Read the pins raw, sample the idle pattern at init, treat a press as a deviation |
| `lv_group_focus_obj(NULL)` | Returns immediately and clears nothing. Anything built on "unfocus everything" is a silent no-op |
| `LV_KEY_LEFT` / `RIGHT` | Delivered to the focused widget, which mostly ignores them. The stick raises `panel_nav_t` events; what the UI declines is passed on as the matching `LV_KEY`, which keeps an open dropdown's own up/down/enter |
| Contact bounce | Two agreeing samples on **both** edges (`JOY_DEBOUNCE`, `BTN_DEBOUNCE`). Press-edge-only debouncing lets a mid-push dropout read as release-then-press |
| `MX_LTDC_Init` | `__weak`, and ignores the width and height passed to `BSP_LCD_InitEx` — it hardcodes the AMPIRE timings. A different panel means overriding the function |
| `BSP_PB_GetState` | Returns the raw pin level and normalises no polarity. A level alone never means "pressed"; only a change does |
