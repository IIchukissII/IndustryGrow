<!-- SPDX-FileCopyrightText: 2026 The IndustryGrow contributors -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Bench bring-up

Order matters: each step's failure is diagnosable only if the one before it
passed.

## The two boards

Both are the MB1246 motherboard, so the connectors, SDRAM, QSPI, MFX, joystick,
codec, PHY and CAN are identical. The MCU and the LCD daughterboard differ, and
the pairing is forced: the STM32H753 has no DSI peripheral, so MB1166 can only
run on the H757.

| | STM32H753I-EVAL | STM32H757I-EVAL |
|---|---|---|
| Build | `tools\build.ps1` into `build\` | `tools\build.ps1 -Board H757` into `build-h757\` |
| MCU | H753XI, single core | H747XI, dual core, CM7 image only |
| Panel | MB1063, 640x480, LTDC RGB, EXC7200 at 0x08 | MB1166, 800x480, DSI, OTM8009A, FT6x06 at 0x54 or 0x70 |
| Core supply | `PWR_LDO_SUPPLY` | `PWR_DIRECT_SMPS_SUPPLY` |
| Console, probe volume | COM5, `EVA_H753XI` | COM4, `EVA_H757XI` |

`Core/Inc/eval_board.h` turns `-DIGROW_BOARD=` into the BSP, the device header,
the panel size and the supply option. `tools\flash.ps1 -Board` picks the right
probe by serial number. All 13 `igrow-check` tests pass on both boards.

**THE SUPPLY OPTION IS NOT INTERCHANGEABLE AND THE WRONG ONE LOCKS THE BOARD.**
CR3 bit 2 is `SCUEN` on the H753 and `SMPSEN` on the H747. The H757's core is
fed by the SMPS, so requesting the LDO there switches off the supply running
the CPU. The configuration is write-once until a power-on reset, so `NRST` does
not recover it and SWD reports `No STM32 target found`.

**BOTH BOARDS WORK ON A REAL BUS.** The capture below is an **H757** result — it
predates the H753 entirely and was taken while the H757 carried MB1063 — so read
every CAN section here as H757 unless it says otherwise.

| | H757 | H753 |
|---|---|---|
| Above the transceiver | proven | proven, internal loopback |
| On a real bus | **proven**, captured below | **proven**, captured 2026-09-02: 10 min listen-only, 26 358 frames, 11 499 transfers, 0 unknown, 0 dropped, TEC/REC 0, 17 subjects live (spec V10) |

Steps 0 to 4 are **done on the H757**. The pair is **FDCAN1 PA11/PA12**, reached
with JP1 and JP2 fitted, and it is now candidate 0 so a normal boot does not
sweep. Ten minutes on the cabinet bus: 22 830 frames, 9 929 transfers, 0 dropped,
0 unknown, `TEC` and `REC` 0, all 17 subjects of both nodes live with learned
periods.

The same wiring applies to either board: JP1/JP2 fitted, CN3, stub under ~30 cm,
Micro-Fit pins 3/4/2 (`CAN_H`/`CAN_L`/GND — **never pin 1, it is +12 V**).

Before the jumpers went on, every candidate read as idle pins -- no frames, no
errors, `LEC` 7. That signature means *not connected*, never a wrong bit rate,
which produces error frames and a climbing `REC` instead.

## 0. Before power

The CAN transceiver on this motherboard is **not connected to the MCU as
shipped**: `PA11`/`PA12` are shared with USB OTG FS and the board steers them at
USB. ST's own FDCAN example for the MB1246 motherboard (`STM32H743I-EVAL`, the
same motherboard this board uses) states the setup:

| MB1246 | Required for CAN |
|---|---|
| CAN connector | **CN3** |
| JP1, JP2 | **fitted** |
| SB59, SB60 | **open** |
| SB50 | **closed** |

| Check | Why |
|---|---|
| Find `CAN_H` and `CAN_L` on the Sub-D by continuity to the transceiver, not by assumption | The pinout is not in the BSP and has not been verified |
| Confirm the jumpers and solder bridges above | A board left in the USB configuration looks exactly like wrong pins: idle RX, no frames, no errors |
| **A termination IS fitted on this board — remove it, and put M01's back** | Confirmed 2026-09-01. The bench currently terminates on the tool instead of M01, so unplugging the tool leaves the bus with one terminated end. The tool is removable by definition; the cabinet must not depend on it. Spec `O-84`, and I5 fails until it is moved |
| A third terminator, if both ends are already terminated | Drops the differential load to ~40 Ω and degrades margin while still appearing to work |
| Keep the drop under ~30 cm | Stub length at 500 kbit/s |

Cable to the bus — Molex Micro-Fit 3.0 4-pin, three wires only:

| Micro-Fit pin | Signal | To the panel |
|---|---|---|
| 1 | +12 V | **not connected** |
| 2 | GND | GND — required; the node transceivers are not isolated |
| 3 | `CAN_H` | `CAN_H` |
| 4 | `CAN_L` | `CAN_L` |

## 1. Flash and console

```powershell
powershell -ExecutionPolicy Bypass -File tools\build.ps1 -Board H757
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757
```

Each build produces three images. `tools\flash.ps1 -Image igrow-min` flashes the
bare-metal one (see *The bare-metal image* below) and `-Image igrow-check` the
headless self-test.

**Open the serial terminal before resetting the board** — the boot banner is
printed once and says which of SDRAM, display, touch and joystick came up.
`tools\mon.ps1 -Port COM4 -Seconds 30` does it. All three ST-LINKs on this
machine are `VID_0483&PID_374E`; tell them apart by the mass-storage volume.

Observed banner on the H757, **no CAN cable attached** — the honest negative from
the sweep below, not a regression against the bus run above:

```
boot
SUPPLY  : ConfigSupply(direct SMPS) HAL_OK, CR3 bit 2 was 1
VOS     : D3CR 00006000 -> 0000C000 -> 0000E000  CSR1 0000E000  waited 0 ms
CLOCK   : SYSCLK 400000000 Hz, HCLK 200000000 Hz

=== IndustryGrow panel (STM32H757I-EVAL + MB1166) ===
SDRAM   : ok
display : ok (800x480)
touch   : ok (controller answered)
joystick: ok - UP/DOWN move focus, LEFT/RIGHT act, SEL selects
controls: PA0 or PC13 (B2) blanks the display
CAN     : classic, 500 kbit/s, LISTEN-ONLY on FDCAN1 PA11/PA12
hunting for the CAN pins (listen-only, 3 s per candidate)
no CAN frames on any candidate pin pair - back on FDCAN1 PA11/PA12 (listen-only)
```

The last line is the sweep reporting an honest negative. It also puts the
interface back on candidate 0; without that it used to end silently, sitting on
whichever pair it tried last.

`touch : ok` means the controller answered on I2C during `BSP_LCD_InitEx`, which
is not the same as the glass registering a finger. `c` on the console prints the
press count for both inputs; that is what settles it.

## Input: the joystick, and what "touch ok" is worth

**The MB1166 fitted here has an FT6x06 that answers but a panel that registers
nothing.** Measured: 68 joystick presses and **0 touch presses** in the same
session. `BSP_TS_Init` returning `BSP_ERROR_NONE` only proves the controller
acknowledged its I2C address and returned a plausible chip ID; it says nothing
about the glass. The press counters exist so that distinction costs one boot
instead of an afternoon.

The five-way is on the **MB1246 motherboard**, not on the daughterboard, so it
works with either panel and with none. It hangs off the MFX IO expander on the
same I2C1 the touch controller uses.

**Everything is reachable from the stick alone.** Neither push-button navigates;
both still blank and restore the display.

Three levels. Which one holds the navigation is shown, not remembered: the tab
row is outlined on the first, the tab page on the second, and neither once a
widget carries its own focus ring.

| | Tab row | Landed in a tab | Widget selected |
|---|---|---|---|
| LEFT / RIGHT | previous / next tab, wrapping | select the entry widget | nearest widget that way |
| DOWN | go into the tab | select the entry widget | nearest widget that way |
| UP | — | back out to the tab row | nearest widget that way; off the top returns to landed |
| SEL, clicked | go into the tab | **nothing** | activate the selected widget |
| SEL, held 500 ms | — | back out to the tab row | back out to the tab row |

**ENTERING A TAB MUST NOT LAND ON A CONTROL.** With two levels it did, and on the
Bus tab the first widget in reading order is `Go normal` — so the push that
entered the tab left the cursor on the control that changes the CAN mode, and the
next push fired it. The landed level is a state with nothing selected: a
direction chooses a widget, and only then does SEL act on anything.

Landing is enforced by the group, not by a flag. **The LVGL group holds the
selected widget and nothing else**, so an empty group is a state in which a key
cannot reach any object. Keeping every widget in the group and unfocusing
instead does not work: `lv_group_focus_obj(NULL)` returns immediately without
clearing anything, so the last focused widget stays live — including while the
navigation is on the tab row. Movement is resolved from the tool's own
coordinate list, so the group is needed only for the focus ring and key
delivery.

The entry widget is topmost then leftmost. The direction that leaves the landed
level chooses to go further in, not which control: reading order does that.

BACK is a **held SEL** because a five-way has no sixth direction and navigation
without a way out traps the operator. It fires the moment the hold time is
reached, so the screen changes while the finger is still down and the length of
the press never has to be guessed.

**Direction is resolved against the LAYOUT, not the group order.** LVGL's keypad
navigation walks the group in creation order, which on a real screen is not the
order things are arranged in — DOWN lands somewhere up and to the left, and the
control stops being predictable. `nav_nearest()` scores candidates as travel
along the axis plus twice the drift across it, and rejects anything more
sideways than forwards.

Three things had to be got right, and each looked like broken hardware first:

- **`BSP_JOY_GetState()` reports SEL held down forever when polled.** It tests
  `(pins | SEL_PIN) == mask` first, and with nothing pressed every pin is at its
  pull-up, so `pins` is already the whole mask and that test passes. ST's
  examples drive it from an EXTI callback, where a state that never changes
  never fires. Read the pins raw instead, sample the idle pattern once at init,
  and treat a press as a DEVIATION from it — no assumption about polarity, the
  same rule as `BSP_PB_GetState`.
- **`LV_KEY_LEFT`/`RIGHT` go to the focused widget, which mostly ignores them.**
  Sent as keys they read as a dead joystick even though all five directions were
  arriving on the console. The stick raises `panel_nav_t` events instead; what
  the UI declines is passed on as the matching `LV_KEY`, which is what lets an
  **open dropdown** keep its own up/down/enter.
- **Contacts bounce, and here a bounce is a tab change.** Both the stick and the
  push-buttons need two agreeing samples on both edges (`JOY_DEBOUNCE`,
  `BTN_DEBOUNCE`); debouncing only the press edge lets a dropout mid-push read
  as release-then-press and turn one press into two moves.

The focus set is rebuilt from the visible tab on every tab change: LVGL's default
group collects every interactive widget as it is created, and a tabview's
inactive pages are scrolled aside rather than hidden, so one flat group walks the
focus off-screen. Focused widgets get a cyan outline — without a focus ring,
every push looks like it did nothing.

If `display` ever says FAILED the firmware keeps running and prints the node list
and values every five seconds instead, and follows the banner with the LCD return
code and an I2C scan -- see step 4.

## 2. The CAN pin sweep

The firmware starts a sweep at boot: three candidate pin pairs, three seconds
each, in bus monitoring mode throughout. It stops at the first pair that
receives a frame and prints it.

| Candidate | |
|---|---|
| 0 | **FDCAN1 PA11/PA12** — the pair on this board |
| 1 | FDCAN1 PB8/PB9 |
| 2 | FDCAN2 PB12/PB13 |

Three pairs the H747 also offers are **deliberately excluded and must stay so**:
`PD0/PD1` and `PH13/PH14` are FMC lines carrying the SDRAM the framebuffer lives
in, and `PB6` is `I2C1_SCL`, the bus the touch controller sits on. Driving any of
them as FDCAN takes out the display.

`CAN frames on <pair>` means the physical layer is right and the bit timing is
right — a wrong bit rate produces error frames, not accepted ones.

`no CAN frames on any candidate pin pair` means one of: JP1/JP2 are off (step 0),
the wiring is wrong, or the bus is not live. Check the bus is live from the
gateway first — `ssh igrow@gbox-dev.fritz.box "timeout 6 candump -n 15 can0"` —
and do not permute wiring against a dead bus. The sweep puts the interface back
on candidate 0 when it ends.

## 3. Reading the bus

With frames arriving, the **Bus** tab shows the frame counter climbing and
`transfers N ok`. If frames climb but transfers stay at zero, the physical layer
is right and something above it is wrong — wrong bit rate producing corrupt
frames, or a bus that is not Cyphal.

The **Nodes** tab should list 96 and 97 within a second or two (heartbeat is
1 Hz), plus node 1, the gateway time master. The **Values** tab fills as each
subject publishes. Names are blank until GetInfo is asked for, which needs
normal mode.

## 4. The display — resolved

**Done.** The fitted daughterboard is **MB1063**: AMPIRE 640 × 480 over LTDC
parallel RGB, with an **EXC7200** touch controller. The build uses the
STM32H743I-EVAL BSP, which targets the same MB1246 motherboard and drives exactly
that combination.

How it was settled, for the next time a board's daughterboard is in doubt:

| Evidence | Reading |
|---|---|
| `BSP_LCD_InitEx` returned **-7** (`BSP_ERROR_UNKNOWN_COMPONENT`) | LTDC/DSI came up; the *panel controller* did not answer. Not a peripheral fault |
| I2C scan found `0x08`, `0x34`, `0x84` | `0x08` is `TS_EXC7200_I2C_ADDRESS`. `0x84` is the MFX IO expander and `0x34` the audio codec, both motherboard |
| No device at `0x54` | No FT6x06, so not MB1166 |

The return code and the scan both print automatically whenever the display fails,
so the same diagnosis costs one boot.

## 5. Leaving listen-only

Only after steps 2 and 3 pass. The **Go normal** button on the Bus tab takes the
peripheral out of monitoring mode, at which point the panel starts ACKing. Watch
that TEC and REC stay at zero and no bus-off appears.

Nothing is transmitted until a button is pressed even in normal mode: the panel
publishes no heartbeat and no diagnostics, by design.

## 6. Manual actions

| Action | Requires | Effect |
|---|---|---|
| Ask GetInfo | Normal mode | Fills the node's name column |
| Restart node | Normal mode, two presses within 5 s | `uavcan.node.ExecuteCommand` 65535 to the selected node |

`ExecuteCommand` is sent at **version 1.0**, which is what the node firmware
implements. Version 1.1 would not deserialize there.

The restart arming lapses after five seconds. It is deliberately two presses:
the node leaves the bus.

## Voltage Scale 1 -- resolved

Both boards now reach Scale 1 and run at **SYSCLK 400 MHz / HCLK 200**. What was
recorded here for months as "the regulator will not leave Scale 3" was never a
regulator fault, and two mistakes kept it that way:

| Mistake | Consequence |
|---|---|
| `HAL_PWREx_ConfigSupply` was never called | It is the only HAL call that completes the supply handshake and waits for `ACTVOSRDY`. Without it that flag stays 0 out of reset and every Scale 1 request is silently ignored |
| The wait polled `D3CR.VOSRDY` | Already set for Scale 3 at reset and slow to clear, so a read just after the write returns a stale 1 and reports a transition that never happened |

Call `ConfigSupply` with the board's own option before
`__HAL_PWR_VOLTAGESCALING_CONFIG`, then poll **`CSR1.ACTVOS`/`ACTVOSRDY`** -- the
scale actually in force, which cannot report a transition that has not happened.
Scale 1 then arrives via Scale 2 in about 834 000 cycles.

The supply configuration is applied **once per power-on**. After a programmer
reset or `NRST` the call is refused or does nothing and the board legitimately
runs at the reduced set until the next power cycle, so a 200 MHz boot right
after flashing is expected, not a fault.

The ST template's unbounded `while (!VOSRDY)` would hang the board before
`console_init`, with a dark screen and a silent console: indistinguishable from
a dead board. `SystemClock_Config` bounds that wait at 100 ms and falls back:

| | PLL1 N | SYSCLK | HCLK | APB | Requires |
|---|---|---|---|---|---|
| full | 160 | 400 MHz | 200 MHz | 100 MHz | Scale 1 |
| reduced | 80 | 200 MHz | 100 MHz | 50 MHz | legal at Scale 3 |

Two consequences worth knowing:

- **The SDRAM refresh counter follows the clock.** The BSP constant is for a
  100 MHz SDRAM clock (HCLK/2); on the reduced set the SDRAM clock is 50 MHz and
  the same constant refreshes every row half as often as the part requires --
  a slow corruption of the framebuffer, not a failure to initialise.
  `panel_sdram_init()` rescales it from `HAL_RCC_GetHCLKFreq()`.
- **The CAN bit rate does not move.** `can_port.c` puts FDCAN on HSE directly
  (`RCC_FDCANCLKSOURCE_HSE`), so neither the bit rate nor the 88 % sample point
  depends on which set came up.

Everything on the board -- SDRAM, LTDC, touch, console -- is healthy at either
set. **400 MHz does not reduce CPU load**: idle is 99 % under the GUI at both
clocks, because the busy part is fixed-duration I/O rather than computation.
What doubles is memory-bound work, and that is the margin that keeps a repaint
from starving the 64-deep FDCAN FIFO.

## The bare-metal image

`diag/min_main.c` builds `igrow-min.hex`: same HAL and BSP, no FreeRTOS, no
LVGL, no CAN. It brings the console up **first**, on the reset clock, then
reports every step and bounds every wait, so a board that hangs before its
banner still talks. It is what separated "regulator flag" from "dead board".

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757 -Image igrow-min
```

It prints the PWR registers at boot and after the VOS request, brings up SDRAM,
verifies 8192 words of it, initialises the LCD and draws a cyan frame on navy
with a green block. Console keys: `p` PWR dump, `v` retry Scale 1, `d` redraw,
`o`/`O` display off/on, `r` reset, plus the supply configurations the part
actually has: `3` LDO only on either board, `1` and `2` for the SMPS options on
the H757.

**On the H757, `3` (LDO only) browns the core out** and needs a power cycle --
see the warning at the top of this file.

## The second core

This project flashes no CM4 image, but the H757 is **not** running only ours.

| Read off the board | |
|---|---|
| `BCM4` option byte | `0x1`, CM4 boot enabled |
| `BOOT_CM4_ADD0` | `0x8100000`, bank 2 |
| First words at `0x08100000` | `10048000 081127E1`, a valid stack pointer and reset vector |

So the board arrived carrying someone's earlier CANopen project, its CM4 image
is still in bank 2 — flashing the panel erases only the CM7 sectors in bank 1 —
and **the CM4 boots into it at every reset**. Nothing in our firmware releases
it, and nothing needs to: the option byte does that. Whether it then does
anything is a property of that image, not of ours; the ST dual-core templates
have the CM4 enter STOP until the CM7 signals an HSEM, which ours never does.

All 13 self-tests pass with it present, so it is not measurably interfering.
It is still a foreign program with access to D2. To remove it, either erase
bank 2 explicitly or clear `BCM4`; read the option bytes first rather than
trusting a bit name from memory:

```powershell
powershell -ExecutionPolicy Bypass -File tools\flash.ps1 -Board H757 -ShowOptionBytes
```

## Known-untested

Verified: SDRAM, LTDC at 640 × 480, EXC7200 touch init, the LVGL UI, the boot
diagnostics, the sweep's negative path, and -- through the FDCAN internal
loopback self-test -- decode, reassembly, the model, liveness and the tables.

Not yet exercised, worth doubting in this order:

- **Everything downstream of a CAN frame that came off a bus.** No external
  frame has ever reached this firmware; the loopback proves only what sits above
  the transceiver.
- The FDCAN message RAM layout (64 Rx elements, 16 Tx). `HAL_FDCAN_Init` accepts
  it, and loopback traffic moves through it, but no external frame has.
- The 88 % sample point against whatever the SN65HVD230 nodes actually use.
- The touch *orientation*. Init succeeds and LVGL reads it, but whether a press
  lands where the finger is has not been checked — if it is mirrored,
  `TS_SWAP_NONE` in `panel_touch_init()` is the constant to change.
- LVGL's partial-render flush against the write-through SDRAM framebuffer —
  tearing would show as horizontal bands.
