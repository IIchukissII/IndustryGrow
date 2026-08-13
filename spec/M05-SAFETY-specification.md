<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M05-SAFETY — module specification

- **Status:** As-built. `E0006-000001` fabricated and bench-verified 2026-08-02
- **Date:** 2026-08-04 (measurements 2026-08-02; restructured to specification form 2026-08-04, no technical change)
- **E-number:** `E0006` · module-ID strap `0b101`
- **Governing ADRs:** ADR-0018, ADR-0014 (rev 3), ADR-0002 (rev 3), ADR-0017 (rev 2)
- **Companions:** `M01-CLIMATE-specification.md`, `M02-LIGHT-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements and as-built values for the M05-SAFETY cabinet distribution and monitoring board:
sensor complement, power budget, I²C bus, thermal, mechanical and firmware requirements, and
their verification.

Not specified here:

| Document | Owns |
|----------|------|
| `store/E0006-000001-D-pinmap.md` | Header usage, field connectors, I²C addresses, conditioning values, firmware-relevant notes |
| `store/E0006-000001-L.csv` | Parts |
| `firmware/nodes/m05_safety/` | Drivers, boot probe, publication |
| ADR-0018 | Separation principle, sense-only framing, trip location, leak-excitation gating |

## 2. Identification

| | Value |
|---|---|
| Module class | M05-SAFETY — cabinet distribution + monitoring board |
| Module-ID strap | `0b101` |
| E-number | `E0006`, released design `E0006-000001` |
| Carrier | `E0001`, sensor-module header pair (ADR-0014 d5) |
| Function class | Sense-only. No trip, no comparator, no relay-enable (ADR-0018 d10) |

## 3. Function

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| `+12 V` SELV bus voltage | U2 INA226 | Per part (`verify`) | 12.0–12.2 V; measured 12.12 V | `verify` |
| `+12 V` bus current | U2 INA226 across R1 = 0.1 Ω | 0…0.8192 A (±81.92 mV) | 20.9 mA per node; 5 nodes in Phase 1 | `verify` |
| Electrical-bay air temperature | U1 TMP117 | −55…+150 °C (`verify`) | 20…40 °C; measured ≈ 27 °C at bring-up | ±0.1 °C (`verify`) |
| Cabinet door state | Reed switch on a lead | Open / closed | — | — |
| Leak state | Leak strip, ADC + gated excitation | Dry / wet | — | — |
| Actuator energy | DIN kWh meter `SP0001`, S0 pulse input | Per meter | Per deployment | Meter class |

Reporting only: the bay temperature is not a trip element, and the door and leak channels
raise no hardware interlock (ADR-0018 d10, d11).

### 3.1 Exclusions

| Quantity | Owning class |
|----------|--------------|
| Grow-volume over-temperature trip | Heating actuator (ADR-0018 d10) |
| Per-load and per-actuator current | Not measured; actuator energy is captured by `SP0001` only (ADR-0018 d5) |
| Air state, air transport, boundary conditions | M01, M06, M07 |

## 4. Sensor complement

| # | Device | Function | Bus / channel |
|---|--------|----------|---------------|
| U1 | TI TMP117 | Electrical-bay air temperature, reported only | I²C1 |
| U2 | TI INA226 | `+12 V` bus voltage and current across shunt R1 | I²C1 |
| — | Reed switch | Cabinet door, report only | GPIO, on a lead |
| — | Leak strip | Reservoir / pump zone, gated excitation | ADC + GPIO, on a lead |
| — | DIN kWh meter (`SP0001`) | Actuator / high-power energy | S0 pulse input |

Neither U1 nor U2 sits at its part-default strap position. Addresses and conditioning values
are in `store/E0006-000001-D-pinmap.md`; firmware constants shall come from that document.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| I²C1 | U1, U2 |
| `GPIO_1` (PA9) | Leak-strip excitation. Claimed at sensor init; the carrier debug console on USART1 stops at that point (pin map notes 2, 6) |
| ADC | Leak-strip sense |
| GPIO | Door reed |
| Timer, external counter | S0 pulse input |
| J1–J5 | Input, fan-out + CAN, S0, leak lead, door lead |

### 5.1 I²C bus

| | Value |
|---|---|
| Devices | U1, U2 — both board-mounted on I²C1 |
| Configured speed | 100 kHz standard mode (`firmware/common/drivers/i2c.c`, PCLK1 42 MHz, `CCR = 210`) |
| Set by | The shared carrier driver. Both devices are rated 400 kHz or better (`verify`); M05 does not constrain the shared rate — M01 does |
| Segment capacitance | Two devices on short board traces; below the 400 pF limit |

## 6. Power budget

### 6.1 Measured, bench bring-up 2026-08-02

`E0006-000001` on an `E0001` carrier, one node on the bus:

| Quantity | Value |
|----------|-------|
| Bus voltage | 12.12 V |
| Bus current through R1 | 20.9 mA |
| Bus power | **254 mW ≈ 0.25 W** |

Whole node — carrier, WeAct core board, CAN transceiver, both sensors. Per-device figures for
U1 and U2 are `verify` and are microamp-class on 3.3 V. O-30.

0.25 W is the reference figure for a node on this platform until each module is measured. M01
and M06 exceed it (SCD41 bursts to 205 mA on 3.3 V); M02 does not.

### 6.2 Board's own continuous loads

| Load | Draw | Dissipation | Note |
|------|------|-------------|------|
| R11 + D6, `+12 V` present indicator | ≈ 2.1 mA (`verify`, LED V_f assumed 2 V) | ≈ 21 mW in R11, ≈ 4 mW in D6 | Continuous, tapped ahead of F1 (pin map note 6) |
| R1 at the measured 20.9 mA | — | ≈ 44 µW | — |
| R1 at INA226 full scale (0.8192 A) | — | ≈ 67 mW | 2512 package |
| R1 at the F1 rating (1 A) | — | ≈ 100 mW | Within the package |

### 6.3 Metering offsets and ceilings

| Item | Value | Consequence |
|------|-------|-------------|
| Indicator tap position | Upstream of F1, therefore upstream of R1 | Its ≈ 2.1 mA is never seen by U2. Reported bus current under-reports cabinet draw by ≈ 9 % at the Phase-1 load (20.9 mA reported against ≈ 23.0 mA drawn), ≈ 0.22 kWh/year. O-25 |
| INA226 full scale | 0.8192 A | Above it the current telemetry saturates while the rail is live; F1 has not opened. O-26 |
| F1 rating | 1 A | — |

### 6.4 Node count per distribution board

At 20.9 mA per node:

| Ceiling | Value | Nodes |
|---------|-------|-------|
| INA226 measurement full scale | 0.8192 A | ≈ **39** |
| F1 input fuse | 1 A | ≈ 48 |

Phase 1 requires five. ADR-0014 d1 states the same architecture at 50+ instances, so the board
stops metering before it stops conducting at that scale. O-31.

Figures assume nodes resembling this one. Actuator energy does not pass through R1; it is
metered by `SP0001` (ADR-0018 d5).

## 7. Thermal

U1 reports electrical-bay air temperature for bay health — cooling-fan failure, PSU or SSR
thermal buildup (ADR-0018 d11). Board self-heating is an error term on that reading.

| Heat source on the board | Magnitude | Status |
|--------------------------|-----------|--------|
| R11 + D6 indicator | ≈ 25 mW, continuous | Point source, permanently on |
| R1 shunt | 44 µW at Phase-1 load, 67 mW at full scale | Load-dependent; coupling would make bay temperature track bus current |

| ID | Requirement | Status |
|----|-------------|--------|
| T1 | U1 thermally separated from R1's copper pour and from D6 | Not established by the layout or any document; board already fabricated. O-27 |
| T2 | U1 offset against true bay air quantified at commissioning | Bring-up recorded ≈ 27 °C with no reference thermometer in the bay. O-28 |

## 8. Mechanical and enclosure

Enclosure released as `store/E0006-000001-D-case-src.zip`.

| ID | Requirement | Basis |
|----|-------------|-------|
| M1 | U1 has air exchange with bay air | It measures the bay, not the board |
| M2 | D6 visible without opening the cabinet | Only local power indicator |
| M3 | Cable entries for J1–J5 | Input, fan-out + CAN, S0, leak lead, door lead |
| M4 | F1 replacement is not a field operation | 0805 SMD fuse (`Fuse_0805_2012Metric`); clearing it needs rework. O-29 |

## 9. Firmware requirements

Implemented in `firmware/nodes/m05_safety/`.

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b101`; bit 1 = 0, unaffected by the unrouted `STRAP_1` on `E0001-000001` |
| Boot probe, publication, re-probe interval | ADR-0014 d8 |
| Leak excitation | Shares PA9 with the carrier debug console (pin map note 2) |
| Leak excitation state | Gated, pin resting low (pin map note 3) |
| Constants | From `store/E0006-000001-D-pinmap.md`, not from part defaults |

## 10. Verification

| ID | Verifies | Method | Status |
|----|----------|--------|--------|
| V1 | §6.1 | Bus voltage and current measured at the node, one node on the bus | **Executed 2026-08-02** |
| V2 | T2 | U1 against a reference thermometer in the bay | Not executed. O-28 |
| V3 | T1 | U1 reading against stepped bus current, looking for correlation with R1 dissipation | Not defined. O-27 |
| V4 | §6.3 | Reported current against an external meter at the cabinet input | Not executed. O-25 |

## 11. Open items

Shared `O-` namespace. O-3, O-5, O-7, O-9, O-32 to O-41 and O-43 to O-51 are M01's; O-2, O-4,
O-6, O-8, O-10, O-11, O-42 are M06's; O-12 to O-24 are M07's; O-52 to O-62 are M02's.

| ID | Item | Blocks |
|----|------|--------|
| O-25 | `+12 V` indicator draws ≈ 2.1 mA upstream of the shunt; reported bus current under-reports cabinet draw by ≈ 9 % at Phase-1 load. Compensate in firmware, move the tap to the load side at the next revision, or document and leave | Metering accuracy, ADR-0018 d5 anomaly models |
| O-26 | INA226 full scale (0.8192 A) below the F1 rating (1 A): telemetry saturates while the rail is live. Reduce the shunt, lower the fuse, or publish a saturation flag | Telemetry validity, next `E0006` revision |
| O-27 | Thermal coupling of U1 to R1's pour and to D6 uncharacterized (T1) | Validity of the bay-health reading |
| O-28 | U1 offset against true bay air unquantified; no reference thermometer at bring-up (T2) | Commissioning method |
| O-29 | F1 is an 0805 SMD fuse; a blown input fuse is a rework job, not a field swap | Serviceability, next `E0006` revision |
| O-30 | Per-device power figures for U1 and U2 unconfirmed against datasheets | `L` release completeness |
| O-31 | One distribution board meters ≈ 39 nodes and conducts ≈ 48 against ADR-0014 d1's "50+ instances"; whether ADR-0018 d2's "one central board per machine" means per cabinet or per deployment is unstated | Scaling, ADR-0018 d2 reading |

Recorded elsewhere and not duplicated: the `STRAP_0`/`STRAP_1` schematic naming defect, carried
by pin map note 1 for correction at the next `E0006` revision.

## 12. Maturity

**As-built.** The board exists, §6.1 is measured, and the open items are defects and unknowns of
a fabricated board.

| Rung | Content |
|------|---------|
| Pre-schematic | Complement and requirements fixed; values estimated or `verify` |
| Schematic-frozen | `verify` resolved, values computed, footprints checked; `L` releases here |
| **As-built** ← here | Estimates replaced by measured values; verification §10 executed where possible |
