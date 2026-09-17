<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Universal carrier — module specification

- **Status:** `E0001-000003` fabricated and in service; `E0001-000100` specified, not laid out
- **Date:** 2026-09-17
- **E-number:** `E0001` · no class ID — the carrier is not a module
- **Governing ADRs:** ADR-0002 (rev 3), ADR-0007 (rev 1), ADR-0014 (rev 4), ADR-0017 (rev 2), ADR-0018, ADR-0027, ADR-0029, ADR-0031 (rev 1)
- **Companions:** every module specification in `spec/`; `store/E0001-000003-D-pinmap.md` for the fabricated revision
- **Supersedes:** nothing

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the universal carrier: what it hosts, the module-header contract it presents,
the pin assignment of each revision in service, power, firmware and verification. Every module
class mates with this contract and no module specification restates it.

Not specified here:

| Subject | Owner |
|---|---|
| What any module does with a header signal | that module's specification |
| Core-board design | `SP0005`, vendor document in `store/SP0005-D-coreboard-snapshot.zip` |
| Field-bus protocol, bit rate and topology | ADR-0002 rev 3 |
| Node identity, keys and certificates | ADR-0007 rev 1 |
| Cabinet power distribution upstream of the bus connector | ADR-0018 |
| Layout, fabrication outputs and the bill of materials | `store/E0001-VVVVVV-D-*`, `-L.csv` |

## 2. Identification

| | Value |
|---|---|
| E-number | `E0001` — one bare design per revision, one assembly (ADR-0017 d4) |
| Class ID | None. The carrier is the parent of a node and carries no module class |
| Node identity | ATECC608 on the carrier, not on the module (ADR-0007 rev 1) |
| Node-ID store | Flash record at `E0001-000003`; the carrier EEPROM U4 at `E0001-000100` (ADR-0027 d2, d11) |
| Serialized | Yes, per assembly |

### 2.1 Revisions in service

| Revision | Module identification | Header B | State |
|---|---|---|---|
| `E0001-000003` | 3-bit strap pattern on Header B positions 3–5 (ADR-0014 rev 4 d6) | 2×8 | Fabricated, in service |
| `E0001-000100` | 8-bit class ID read from the module's EEPROM over the header I²C, at an address inside `0x50`–`0x57` (ADR-0014 rev 4 d6) | 2×10 | Specified, not laid out (`O-100`) |

Neither supersedes the other. Each carries its own firmware image. An actuator module runs only
on `E0001-000100` (ADR-0031 d10).

## 3. Function

| Function | Realization |
|---|---|
| Host the processor | Two sockets receive the `SP0005` core board |
| Join the field bus | `SN65HVD230` on CAN1, daisy-chained through two identical connectors, termination fitted by a solder jumper |
| Hold the node identity | `ATECC608B` on a private I²C segment, reachable by no module |
| Derive the node rail | `TPS54302` buck, `+12 V` SELV input to 3.3 V |
| Present the module contract | Two sockets carrying the §5 signal set |
| Indicate state | Four indicators: input rail, node rail, bus activity, node status |

![The field bus and the twelve volt supply arrive together on one connector and leave on its twin, so nodes daisy-chain; the supply passes the input protection to the buck, whose output is the only rail the carrier derives and reaches the core board and both module headers; the bus pair reaches the transceiver with its termination jumper and on to the core board, the secure element sits on a second segment no module reaches, everything a module may use crosses the two header sockets, and at the later revision the carrier's own Node-ID store answers on the header bus beside the module's class-ID EEPROM](./figures/e0001-carrier-topology.svg)

### 3.1 Exclusions

| Function | Owner |
|---|---|
| Any measurement | The fitted module |
| Any load switching or actuator drive | The actuator module (ADR-0018 d1, d8) |
| Any rail other than 3.3 V | `SP0003` for `+12 V`, `SP0010` for `+24 V` (ADR-0018 d3) |
| Gateway, bus master, time source | `SP0004` |

## 4. Device complement

| Ref | Device | Function | Rail |
|---|---|---|---|
| U1 | TI TPS54302, SOT-23-6 | 3.3 V buck from the `+12 V` bus, with L1 10 µH | `+12 V` |
| U2 | TI SN65HVD230, SOIC-8 | CAN transceiver on CAN1 | 3V3 |
| U3 | Microchip ATECC608B-SSHDA, SOIC-8 | Secure element on I²C2, node identity (ADR-0007 rev 1) | 3V3 |
| U4 | Serial EEPROM, 24Cxx class | Node-ID store on the header I²C, fitted at `E0001-000100` only (ADR-0027 d11) | 3V3 |
| F2 | Fuse, 0.5 A | Bus-input protection | `+12 V` |
| D4 | SK26AH Schottky | Reverse-polarity protection on the bus input | `+12 V` |
| D5 | SMBJ16A | Transient suppressor on the bus input | `+12 V` |
| FB1 | BLM21P ferrite | Bus-input filter | `+12 V` |
| D3 | PESD1CAN | Transient suppressor on CANH and CANL | — |
| R7, JP1 | 120 Ω across the bus pair, solder jumper | Bus termination, fitted only at the two ends of the run (ADR-0002 rev 3) | — |
| D6–D9 | Indicators: `+12 V`, 3V3, CAN activity, node status | §3 | — |
| J1, J2 | Molex Micro-Fit 3.0, 2×2, horizontal | Field bus and `+12 V`, in and out (ADR-0018 d13) | — |
| J3, J4 | 2×15 sockets, 2.54 mm | Core-board interface (§5.4) | — |
| J5 | 2×12 socket, 2.54 mm | Module Header A | — |
| J6 | 2×8 socket at `E0001-000003`, 2×10 at `E0001-000100` | Module Header B | — |

Every passive on the board belongs to a device above.

Deliberately absent:

| Not fitted | Reference |
|---|---|
| Any sensor. The carrier measures nothing | §3.1 |
| Any switching element. Load switching lives at the actuator | ADR-0018 d1, d8 |
| A 5 V rail. Header A position 2 is present and unconnected | `I5` |

## 5. Interfaces

### 5.1 Module header — the contract

Every module mates through **two** sockets carrying one signal set, whatever its class
(ADR-0014 d5). The sockets are different sizes (`I2`).

| Group | Count | Signals |
|---|---|---|
| Power | 3 | 3V3 at Header A position 1 and Header B position 1; 5 V at Header A position 2, unconnected |
| Ground | 14 at `E0001-000003`, 16 at `E0001-000100` | Interspersed; every analog input flanked |
| I²C | 2 | `I2C_SCL`, `I2C_SDA` — also the transport of the module class-ID EEPROM and, at `E0001-000100`, of U4 |
| SPI | 5 | `SPI_SCK`, `SPI_MISO`, `SPI_MOSI`, `SPI_CS1`, `SPI_CS2` |
| 1-Wire | 1 | `OW_DATA` |
| PWM | 4 | `PWM_1`–`PWM_4`, one timer, independent duty |
| GPIO | 4 at `E0001-000003`, 7 at `E0001-000100` | `GPIO_1`–`GPIO_4`; `GPIO_5`–`GPIO_7` replace the straps |
| ADC | 2 at `E0001-000003`, 4 at `E0001-000100` | `ADC_1`–`ADC_4`, 12-bit, from the processor |
| Module ID | 3 at `E0001-000003`, 0 at `E0001-000100` | `STRAP_0`–`STRAP_2`, tied on the module |
| Reserved | 2 | Header B positions 7 and 14, unconnected at both revisions |

### 5.2 Signal pin assignment

`E0001-000100` differs from `E0001-000003` in five positions and in nothing else. The **Core
board** column gives the position on the `SP0005` headers, P1 (2×12) or P2 (2×13).

| Signal | MCU pin | Function (AF) | Core board | Header | Revision |
|---|---|---|---|---|---|
| CAN_RX | PB8 | CAN1_RX (AF9) | P2.4 | — | both |
| CAN_TX | PB9 | CAN1_TX (AF9) | P2.1 | — | both |
| I2C_SCL | PB6 | I2C1_SCL (AF4) | P2.6 | A.4 | both |
| I2C_SDA | PB7 | I2C1_SDA (AF4) | P2.3 | A.5 | both |
| I2C2_SCL | PB10 | I2C2_SCL (AF4) | P1.24 | — | both, ATECC608 |
| I2C2_SDA | PB11 | I2C2_SDA (AF4) | P1.23 | — | both, ATECC608 |
| SPI_SCK | PB13 | SPI2_SCK (AF5) | P2.26 | A.9 | both |
| SPI_MISO | PB14 | SPI2_MISO (AF5) | P2.23 | A.10 | both |
| SPI_MOSI | PB15 | SPI2_MOSI (AF5) | P2.24 | A.11 | both |
| SPI_CS1 | PA3 | GPIO output | P1.14 | A.13 | both |
| SPI_CS2 | PA4 | GPIO output | P1.13 | A.14 | both |
| GPIO_1 | PA9 | GPIO, also USART1_TX | P2.18 | A.15 | both |
| GPIO_2 | PA10 | GPIO, also USART1_RX | P2.15 | A.16 | both |
| GPIO_3 | PA15 | GPIO, JTDI at reset | P2.14 | A.17 | both |
| GPIO_4 | PB12 | GPIO | P2.25 | A.18 | both |
| PWM_1 | PC6 | TIM3_CH1 (AF2) | P2.21 | A.20 | both |
| PWM_2 | PC7 | TIM3_CH2 (AF2) | P2.22 | A.21 | both |
| PWM_3 | PB0 | TIM3_CH3 (AF2) | P1.19 | A.22 | both |
| PWM_4 | PB1 | TIM3_CH4 (AF2) | P1.22 | A.23 | both |
| OW_DATA | PA0 | GPIO open-drain | P1.9 | A.7 | both |
| LED_STATUS | PA1 | GPIO output | P1.12 | — | both |
| LED_CAN | PA2 | GPIO output | P1.11 | — | both |
| ADC_1 | PC4 | ADC12_IN14 | P1.17 | B.10 | both |
| ADC_2 | PC5 | ADC12_IN15 | P1.20 | B.12 | both |
| STRAP_0 | PA5 | GPIO input | P1.16 | B.3 | `E0001-000003` |
| STRAP_1 | PA6 | GPIO input | P1.15 | B.4 | `E0001-000003` |
| STRAP_2 | PA7 | GPIO input | P1.18 | B.5 | `E0001-000003` |
| GPIO_5 | PA5 | GPIO | P1.16 | B.3 | `E0001-000100` |
| GPIO_6 | PA6 | GPIO | P1.15 | B.4 | `E0001-000100` |
| GPIO_7 | PA7 | GPIO | P1.18 | B.5 | `E0001-000100` |
| ADC_3 | PC0 | ADC123_IN10 | P1.5 | B.17 | `E0001-000100` |
| ADC_4 | PC1 | ADC123_IN11 | P1.8 | B.19 | `E0001-000100` |
| SWDIO | PA13 | SWDIO (AF0) | — | — | both, core-board debug header |
| SWCLK | PA14 | SWCLK (AF0) | — | — | both, core-board debug header |

Unassigned processor pins:

| Pin | Core board | Reserve for |
|---|---|---|
| PC2 | P1.7 | A further analog channel |
| PC3 | P1.10 | A further analog channel |
| PB3 | — | Any signal; also JTDO / SWO |
| PB4 | — | Any signal |
| PB5 | — | Any signal |

### 5.3 Header pinout

| Position | Header A, 2×12 | Header B, `E0001-000003`, 2×8 | Header B, `E0001-000100`, 2×10 |
|---|---|---|---|
| 1 | 3V3 | 3V3 | 3V3 |
| 2 | 5V, unconnected | GND | GND |
| 3 | GND | `STRAP_0` | `GPIO_5` |
| 4 | `I2C_SCL` | `STRAP_1` | `GPIO_6` |
| 5 | `I2C_SDA` | `STRAP_2` | `GPIO_7` |
| 6 | GND | GND | GND |
| 7 | `OW_DATA` | RESERVED | RESERVED |
| 8 | GND | GND | GND |
| 9 | `SPI_SCK` | GND | GND |
| 10 | `SPI_MISO` | `ADC_1` | `ADC_1` |
| 11 | `SPI_MOSI` | GND | GND |
| 12 | GND | `ADC_2` | `ADC_2` |
| 13 | `SPI_CS1` | GND | GND |
| 14 | `SPI_CS2` | RESERVED | RESERVED |
| 15 | `GPIO_1` | GND | GND |
| 16 | `GPIO_2` | GND | GND |
| 17 | `GPIO_3` | — | `ADC_3` |
| 18 | `GPIO_4` | — | GND |
| 19 | GND | — | `ADC_4` |
| 20 | `PWM_1` | — | GND |
| 21 | `PWM_2` | — | — |
| 22 | `PWM_3` | — | — |
| 23 | `PWM_4` | — | — |
| 24 | GND | — | — |

### 5.4 Core-board interface

| Socket | Receives | Spans |
|---|---|---|
| J3, 2×15 | `SP0005` P1 and the 3V3 power header below it | 12 signal rungs plus 3 power rungs |
| J4, 2×15 | `SP0005` P2 and the 5 V power header below it | 13 signal rungs plus 2 power rungs |

### 5.5 Interface requirements

| ID | Requirement | Reference |
|---|---|---|
| `I1` | Both module sockets carry the §5.1 signal set at every revision. A revision adds positions; it never moves or repurposes one already assigned | ADR-0014 d5 |
| `I2` | The two module sockets differ in size at every revision, so a module cannot be seated rotated | ADR-0014 d5 |
| `I3` | A module built to an earlier Header B seats on positions 1–16 of the wider socket and does not reach beyond | §5.3 |
| `I4` | Header B positions 7 and 14 stay unconnected until a revision assigns them | §5.1 |
| `I5` | Header A position 2 is present and unconnected; no revision fits a 5 V rail without its own specification change | §4 |
| `I6` | Every analog input on Header B is flanked by ground, and no switching signal shares Header B | ADR-0014 d5 |
| `I7` | Bus termination is fitted by `JP1` only at the two ends of the run | ADR-0002 rev 3 |
| `I8` | SWD reaches the processor on the core board's own debug header; the carrier routes neither line | §5.2 |
| `I9` | U4 and a fitted module's class-ID EEPROM share the header I²C, both take an address inside `0x50`–`0x57`, and the two addresses are distinct | ADR-0027 d11, `O-133` |

## 6. Power

| ID | Requirement | Reference |
|---|---|---|
| `P1` | Input is the `+12 V` SELV bus, fused at 0.5 A, reverse-protected, transient-suppressed and filtered before the buck | ADR-0018 d3 |
| `P2` | The carrier derives 3.3 V locally and is the only source of the module rail. No module derives its own | ADR-0002 rev 3 |
| `P3` | 3.3 V reaches the core board through the power rungs of J3, and reaches each module at both header position 1 | §5.4 |
| `P4` | The core board is powered from the carrier **or** from its own USB, never both | `SP0005` |
| `P5` | Carrier and module current on the 3.3 V rail is bounded by the buck's rating (`verify` against the fitted inductor and thermal design) | `O-132` |
| `P6` | No actuator load is taken from the `+12 V` bus or the 3.3 V rail | ADR-0018 d3 |

## 7. Firmware requirements

| ID | Requirement | Reference |
|---|---|---|
| `F1` | Each revision runs its own image; an image identifies the revision it was built for | §2.1 |
| `F2` | On `E0001-000100`, probe the module EEPROM at the reserved I²C address first. A module that answers is identified by byte 0 and its `GPIO_5`–`GPIO_7` are never read as an identity | ADR-0014 rev 4 d6 |
| `F3` | `0x00` and `0xFF` are *unidentified*, never a class | ADR-0014 rev 4 d6 |
| `F4` | On `E0001-000100`, a module that answers at no reserved address may be identified by the legacy strap pattern on `GPIO_5`–`GPIO_7` | §2.1 |
| `F5` | `GPIO_5`–`GPIO_7` are configured as inputs at reset and driven only after identification, because a module may hold them against a rail | `F4` |
| `F6` | The node publishes its heartbeat and health over the field bus; the carrier publishes no measured quantity | §3.1 |
| `F7` | `GPIO_1` and `GPIO_2` carry USART1. A bench console is available only while no fitted module claims them, and the module wins | §5.2 |
| `F8` | At `E0001-000100` the Node-ID is the record U4 holds — magic, version, Node-ID, CRC — and the class ID is never read as an instance identity. At `E0001-000003` the same record is held in flash | ADR-0027 d2, d9, d11 |

## 8. Verification

| ID | Verifies | Method |
|---|---|---|
| `V1` | `I1`, `I2`, `I4`, `I5`, `I6` | Continuity-check every header position against §5.3 on a bare board; confirm the two sockets differ in size, and that Header B positions 7 and 14 and Header A position 2 reach no net |
| `V2` | `I3` | Seat a module built to the earlier Header B on the wider socket; confirm every signal it uses reads correctly |
| `V3` | `I7` | Measure across the bus pair with `JP1` open and closed; confirm 120 Ω only when closed |
| `V4` | `I8` | Attach a debugger to the core-board header with the carrier mounted; confirm it halts the processor |
| `V5` | `P1`, `P2`, `P4`, `P6` | Apply reversed input at the bus connector and confirm no current passes. Measure the 3.3 V rail at both header position 1. Power the core board from the carrier and from USB in turn, never both, and confirm the rail in each case |
| `V6` | `P3`, `P5` | Load the 3.3 V rail to the `P5` bound for 1 h; record the buck case temperature and the rail's droop |
| `V7` | `F1`, `F2`, `F3`, `F4` | Present in turn a module with a programmed EEPROM, one reading `0xFF`, and one with only a strap pattern; confirm the identity each yields, and that the image reports the revision it was built for |
| `V8` | `F5`, `F6`, `F7` | Hold `GPIO_5`–`GPIO_7` against both rails through a module and confirm no carrier output opposes them at reset. Enumerate every subject the node publishes and confirm no measured quantity is among them. Open a console on USART1 with no module fitted, then seat one that claims `GPIO_1`, and confirm the console stops |
| `V9` | `I9`, `F8` | Address-scan the header I²C with a module seated; confirm U4 and the module EEPROM answer at distinct addresses. Write a Node-ID through the provisioning path, power-cycle, and confirm the node claims the written identifier and rejects a record whose CRC fails |

## 9. Open items

- `O-100` — `E0001-000100` is specified here but not laid out and not fabricated. Blocks every actuator module (ADR-0031 d10).
- `O-133` — The `0x50`–`0x57` address allocation between U4 and a module's class-ID EEPROM is unassigned (ADR-0027 deferred decisions). Blocks `I9` and the layout of `E0001-000100`.
- `O-132` — The 3.3 V rail's current bound is not established: the buck's rating, the fitted inductor and the carrier's thermal design have not been read together, and no module specification states its own draw against it. Blocks `P5`.

## 10. Maturity

| Rung | State |
|---|---|
| Requirements-fixed | Reached for both revisions |
| **`E0001-000003` as-built** | **Current.** Fabricated, in service, its layout and fabrication outputs released |
| `E0001-000100` schematic-frozen | Not reached — not laid out |

`E0001-000100` reaches the next rung when its schematic and layout exist and `O-132` is closed.
