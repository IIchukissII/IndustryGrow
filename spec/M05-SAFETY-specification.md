<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M05-SAFETY — as-built module specification

- **Status:** As-built. `E0006-000001` fabricated and bench-verified 2026-08-02
- **Date:** 2026-08-03
- **E-number:** `E0006` · module-ID strap `0b101`
- **Governing ADRs:** ADR-0018, ADR-0014 (rev 2), ADR-0002 (rev 3), ADR-0017 (rev 1)
- **Companions:** `M01-M06-air-nodes-specification.md`, `M07-AMBIENT-specification.md`

Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. What this document adds, and what it does not repeat

M05 is the one module class that is **built**, so this specification is written backwards
from hardware rather than forwards into it. It is the same document class as the M06/M07
specifications at a later maturity: the sensor complement, power budget, bus, thermal and
mechanical sections persist across a module's life, and what changes is that `verify`
values resolve and open items close.

Four existing documents already own most of M05 and are **not** restated here:

| Document | Owns |
|----------|------|
| ADR-0018 (d5, d9, d10, d11) | *Why* — the separation principle, sense-only framing, why the trip lives at the heating actuator, why leak excitation is gated |
| `store/E0006-000001-D-pinmap.md` | Header usage, field connectors, I²C addresses, every conditioning value, and seven notes firmware depends on |
| `store/E0006-000001-L.csv` | The parts |
| `firmware/nodes/m05_safety/` | Drivers, boot probe, publication |

What none of them covers, and what this document is for: the **power budget**, the **I²C
bus**, **thermal separation**, and **mechanical/enclosure requirements** — the four sections
the M06 and M07 specifications carry and M05 has never had.

## 2. Sensor complement

| # | Device | Function | Bus / channel |
|---|--------|----------|---------------|
| U1 | TI TMP117 | Electrical-bay air temperature — **reported only**, not a trip element (ADR-0018 d10, d11) | I²C1 |
| U2 | TI INA226 | `+12 V` SELV sensor-bus voltage and current, across shunt R1 | I²C1 |
| — | Reed switch | Cabinet door, report/alert only | GPIO, on a lead |
| — | Leak strip | Reservoir/pump zone, report/alert only, gated excitation | ADC + GPIO, on a lead |
| — | DIN kWh meter (`SP0001`) | All actuator / high-power energy | S0 pulse input |

Addresses and conditioning: pin map. Neither sensor sits at its part-default strap position,
so firmware constants must come from that document and nowhere else.

## 3. Power budget

### 3.1 Measured, bench bring-up 2026-08-02

**One M05 node, module plus carrier, draws 0.25 W.** That is the figure to carry forward;
everything else in this section is derived from it.

`E0006-000001` on an `E0001` carrier, one node on the bus:

| Quantity | Value |
|----------|-------|
| Bus voltage | 12.12 V |
| Bus current (through R1) | 20.9 mA |
| Bus power | **254 mW ≈ 0.25 W** |

This is the whole node — carrier, WeAct core board, CAN transceiver and both sensors — not
the sensor complement alone. Per-device figures are `verify`; U1 and U2 are microamp-class
on 3.3 V and are not the term that matters. A sensor node's draw is dominated by the
carrier, which is common to every module class, so **0.25 W is a reasonable first estimate
for any node on this platform** until each module is measured — M01 and M06 will exceed it
(SCD41 bursts to 205 mA on 3.3 V), M02 will not.

### 3.2 The board's own continuous loads

| Load | Draw | Dissipation | Note |
|------|------|-------------|------|
| R11 + D6, `+12 V` present indicator | ≈ 2.1 mA (`verify` — LED V_f assumed 2 V) | ≈ 21 mW in R11, ≈ 4 mW in D6 | **Continuous, and tapped ahead of F1** (pin map note 6) |
| R1 shunt at the measured 20.9 mA | — | ≈ 44 µW | Negligible |
| R1 shunt at INA226 full scale (0.8192 A) | — | ≈ 67 mW | 2512 package, ample margin |
| R1 shunt at the F1 rating (1 A) | — | ≈ 100 mW | Still within the package |

### 3.3 The indicator sits upstream of the shunt

R11 taps the input net directly, ahead of F1, and therefore **ahead of R1**. Its ≈ 2.1 mA
never flows through the shunt and is never seen by the INA226.

The board's stated job includes metering the sensor-bus consumption (ADR-0018 d5). It
under-reports the cabinet's actual `+12 V` draw by that indicator current — at the measured
Phase-1 load, **≈ 9 %** (20.9 mA reported against ≈ 23.0 mA drawn). Energetically this is
nothing, ≈ 0.22 kWh/year. As a *systematic offset in a metered quantity* it is worth
knowing about, because ADR-0018 d5 feeds this series to offline anomaly models: a constant
offset is harmless to them only as long as nobody later mistakes it for a real load. O-25.

### 3.4 Measurement ceiling below the protection threshold

INA226 full-scale current is **0.8192 A** (pin map note 4: ±81.92 mV across 0.1 Ω); F1 is
rated **1 A**. Between those two figures lies a band in which the current telemetry
saturates while the fuse has not yet opened — the rail is live and over the measurable
range, and the reported current is a clipped constant rather than an obvious fault. O-26.

### 3.5 What 0.25 W per node bounds

At ≈ 20.9 mA per node, the two ceilings above convert directly into a node count for one
distribution board:

| Ceiling | Value | Nodes |
|---------|-------|-------|
| INA226 measurement full scale | 0.8192 A | ≈ **39** |
| F1 input fuse | 1 A | ≈ 48 |

Phase 1 needs five. But ADR-0014 decision 1 promises the same architecture at
"50+ instances" in a large greenhouse, and at that count **a single M05 board stops
metering before it stops conducting** — the INA226 saturates around 39 nodes while the fuse
holds to about 48. The architecture's answer is presumably one distribution board per zone
rather than one per deployment, which is consistent with ADR-0018 decision 2 ("one central
board per machine") once *machine* is read per-cabinet rather than per-site. That reading is
not stated anywhere, and the arithmetic above is the first thing that forces the question.
O-31.

These figures assume every node resembles this one. Nodes with burst loads (M01, M06) draw
more, and actuator energy does not pass through this shunt at all — it is metered separately
by the DIN kWh meter (ADR-0018 d5).

## 4. I²C bus

| | Value |
|---|---|
| Devices | U1 TMP117, U2 INA226 — both on I²C1, both board-mounted |
| Configured speed | **100 kHz** standard mode (`firmware/common/drivers/i2c.c`, PCLK1 42 MHz, `CCR = 210`) |
| Set by | **The shared carrier driver, not by either M05 device.** TMP117 and INA226 are both good for 400 kHz or better (`verify`) |
| Segment capacitance | Two devices on short board traces; far below the 400 pF limit, not a binding constraint |

M05 is therefore **not** the module that constrains the common I²C rate. M01 is — SCD41 and
FS3000 are 100 kHz parts. Recorded here so that a future attempt to raise the shared bus
speed does not mistake M05 for the limiting node.

## 5. Thermal

U1 reports the electrical-bay air temperature for electronics-bay health — cooling-fan
failure, PSU or SSR thermal buildup (ADR-0018 d11). Its accuracy claim is therefore about
the **bay air**, and any self-heating of the board it sits on is an error term.

| Heat source on the same board | Magnitude | Assessment |
|-------------------------------|-----------|------------|
| R11 + D6 indicator | ≈ 25 mW, continuous | Small, but permanently on and physically a point source |
| R1 shunt | 44 µW at Phase-1 load; 67 mW at full scale | Load-dependent, so any coupling appears as a temperature reading that tracks bus current — the most misleading possible failure mode for a bay-health sensor |

Neither the layout nor any document establishes whether U1 is thermally separated from R1's
copper pour and from D6. The M01 specification treats exactly this question as a design
constraint for SHT45 against the BME688 hotplate and SCD41; M05 has never had the equivalent
check, and its board is already fabricated. O-27.

Bring-up recorded ≈ 27 °C with **no reference thermometer in the bay**, so the offset between
what U1 reports and the true bay air is currently unquantified. O-28.

## 6. Mechanical and enclosure

The enclosure exists — `store/E0006-000001-D-case-src.zip` — but its requirements were never
written down. Recorded here from the board:

| Requirement | Basis |
|-------------|-------|
| U1 needs air exchange with bay air | It measures the bay, not the board. A sealed pocket around it makes the reading the board's temperature instead |
| D6 must be visible without opening the cabinet | It is the only local power indicator |
| J1–J5 need cable entries | Input, fan-out + CAN, S0, leak lead, door lead — five field connectors |
| F1 replacement is **not** a field operation | F1 is an 0805 SMD fuse (`Fuse_0805_2012Metric`). Clearing it needs rework, not a spare. O-29 |

## 7. Firmware

Boot probe, publication and the re-probe interval follow ADR-0014 d8 and are implemented in
`firmware/nodes/m05_safety/`. Two constraints from the pin map that no other document states,
repeated **only** as pointers: the leak excitation shares PA9 with the carrier's debug console
(note 2), and the excitation must be gated with the pin resting low (note 3).

## 8. Open items

Continues the `O-` namespace shared with the M06/M07 specifications.

| ID | Item | Blocks |
|----|------|--------|
| O-25 | The `+12 V` indicator draws ≈ 2.1 mA upstream of the shunt, so reported bus current under-reports actual cabinet draw by ≈ 10 % at Phase-1 load. Compensate in firmware, move the tap to the load side at the next revision, or document and leave it | Metering accuracy, ADR-0018 d5 anomaly models |
| O-26 | INA226 full scale (0.8192 A) sits below the F1 rating (1 A): a band where current telemetry saturates while the rail is still live. Reduce the shunt, lower the fuse, or publish a saturation flag | Telemetry validity, next E0006 revision |
| O-27 | Thermal coupling of U1 to R1's pour and to D6 is uncharacterized. Load-dependent coupling would make bay temperature track bus current | Validity of the bay-health reading |
| O-28 | No reference thermometer at bring-up, so U1's offset against true bay air is unquantified | Commissioning method |
| O-29 | F1 is an 0805 SMD fuse — a blown input fuse is a rework job, not a field swap, on a cabinet distribution board | Serviceability, next E0006 revision |
| O-30 | Per-device power figures for U1 and U2 unconfirmed against datasheets | `L` release completeness |
| O-31 | At 0.25 W per node, one distribution board meters ≈ 39 nodes and conducts ≈ 48, while ADR-0014 d1 promises "50+ instances" at greenhouse scale. Whether ADR-0018 d2's "one central board per machine" means per cabinet or per deployment is unstated, and this is the arithmetic that forces the question | Scaling story, ADR-0018 d2 reading |

Already recorded elsewhere and **not** duplicated here: the `STRAP_0`/`STRAP_1` schematic
naming defect, carried by pin map note 1 for correction at the next E0006 revision.

## 9. Maturity

This document is at **as-built** maturity: the board exists, the numbers in §3.1 are measured
rather than estimated, and the open items are defects and unknowns of a real board rather than
choices still to be made. The M06 and M07 specifications sit at **pre-schematic** maturity and
will arrive here by the same route — `verify` values resolving against datasheets, then against
hardware.
