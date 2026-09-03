<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M04-PLANT — module specification

- **Status:** Working specification. Pre-schematic: `E0005` has no schematic, no layout and no firmware
- **Date:** 2026-09-03
- **E-number:** `E0005` · module-ID strap `0b100`
- **Governing ADRs:** ADR-0014 (rev 6), ADR-0002 (rev 3), ADR-0003, ADR-0005 (rev 1, d11), ADR-0006, ADR-0016, ADR-0017 (rev 2)
- **Companions:** `M01-CLIMATE-specification.md`, `M02-LIGHT-specification.md`, `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.
Device values are from Melexis MLX90640 datasheet rev 12 (3901090640, 2019-12-03), cited as DS12.

## 1. Scope

Requirements for the M04-PLANT sensor module: sensor complement, measured quantities and their
derivation, electrical, optical, thermal, mechanical and firmware requirements, and their
verification.

Not specified here: carrier design (`store/E0001-000003-D-pinmap.md`), cultivation setpoints
(ADR-0003 and `profiles/strawberry-day-neutral-v1.json`), cabinet geometry and the mounting
structure (ADR-0006 defers dimensions to a mechanical specification), gateway-side handling of
published subjects (ADR-0014 d7), canopy segmentation and leaf VPD (deferred by ADR-0014 d4 and
rev 1).

## 2. Identification

| | Value |
|---|---|
| Module class | M04-PLANT |
| Module-ID strap | `0b100` — STRAP_0 low, STRAP_1 low, STRAP_2 high |
| E-number | `E0005` — assembly populated with the 110° × 75° imager (§6.5) |
| Bare design | One layout; each standard populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4). The two FOV options share one footprint and differ in ordering code, can height and aperture (§4.1) |
| Carrier | `E0001`, sensor-module header pair 2×12 + 2×8 (ADR-0014 d5) |

## 3. Function

M04 measures the radiometric surface-temperature field of the canopy, inside the growing
enclosure (ADR-0006 d2, growing volume), looking at the canopy plane.

Expected values below are for the reference cultivation profile
`profiles/strawberry-day-neutral-v1.json`, which is the source of truth for setpoints
(ADR-0000 d2): air temperature 20 °C day / 14 °C night, VPD band 0.8–1.2 kPa, photoperiod
16 h on / 8 h off. **The profile sets no canopy-surface temperature.** The expected band below is
M01's air band of 12…28 °C widened by a canopy-to-air differential of ±4 K, which no record
establishes. O-87.

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| Canopy temperature field, 32 × 24 = 768 pixels | U1 | −40…+300 °C object; Ta −40…+85 °C | 8…32 °C surface, 12…28 °C Ta | Frame ±1 °C; per-pixel = frame + non-uniformity, ±1.5 °C in zone 1 (§6.3), under the conditions §6.3 states |
| Frame statistics — mean, min, max, σ, gradient, hotspot mask (derived, §6.4) | U1 | — | Mean 8…32 °C; σ ≥ NETD 0.14 K | Mean inherits frame accuracy ±1 °C; the spread terms inherit non-uniformity, not frame accuracy |
| Die temperature Ta | U1 | −40…+85 °C | 20…36 °C — air +8 K (§8 T1) | ±0.5 °C |

Publication rate: **1 Hz** for the statistics record; **one full frame per 5 min or on event**,
served over `uavcan.file.Read` (ADR-0005 d11, §10.2). Frame cadence is 1 s at the specified
refresh rate (§6.1).

Location: above or beside the canopy, optical axis normal to the canopy plane, with the
obstacle-free cone of §9 M1 clear. One instance per canopy area at cabinet scale, one per growing
zone above it (ADR-0014 d4). The imager is board-mounted; no leads.

### 3.1 Environmental envelope of the populated module

| | U1 MLX90640 | **Module** |
|---|---|---|
| Operating temperature | −40…+85 °C (Ta) | **−40…+85 °C** |
| Storage temperature | −40…+85 °C | **−40…+85 °C** |
| Object temperature | −40…+300 °C | **−40…+300 °C** |
| Humidity | **Not specified by DS12** | **Unstated.** The TO-39 can is hermetic; the exposed optical surface is the window behind the external aperture, and contamination or condensation on it is an unspecified error (DS12 §14). O-90 |
| ESD | 4 kV, AEC-Q100-002 | **4 kV** |
| MSL / reflow profile | **Not stated in DS12** — a through-hole hermetic can, not a reflow part (§9 M6) | `verify`, O-93 |

### 3.2 Exclusions

The following are outside M04's scope and are specified by the module class named, or by the
layer named.

| Quantity | Owning class or layer |
|----------|-----------------------|
| Air temperature, RH, air VPD, CO₂, VOC at the canopy | M01-CLIMATE |
| Spectrum, PPFD, UV-A at the canopy | M02-LIGHT |
| Solution pH, EC, solution temperature | M03-ANALYTICS |
| Air velocity, filter Δp, envelope Δp | M06-VENTILATION |
| Ambient boundary conditions outside the enclosure | M07-AMBIENT |
| Bus voltage and current, door, leak | M05-SAFETY |
| **Canopy-to-air temperature differential** — needs M01's air temperature from a second node | **Gateway** (ADR-0016 d5, soft sensor) |
| **Leaf VPD, per-leaf temperature** | **Deferred** (ADR-0014 rev 1 and d4): requires a canopy-segmentation pipeline that does not exist |
| **Canopy segmentation** — which pixels are leaf and which are structure, medium or luminaire | **Not implemented.** §6.4's statistics are frame-wide, not canopy-only. O-88 |
| Visible-light camera | Gateway; not a Cyphal node (ADR-0014 d9) |

M04 carries no air, gas, pressure, humidity or light sensor. Ta is the imager's die temperature
and is **not** air temperature (§8 T1).

### 3.3 Partial populations

M04 has one sensor. A population without it has no function and is not a defined configuration,
so this module has no partial population in M01's and M02's sense.

The **FOV option is a populated-BOM variant** (ADR-0014 d2, third dimension): both ordering codes
share the TO-39 lead pattern of §4.1 and differ in can height, aperture and obstacle-free cone.
`E0005` names the `BAA` build (§6.5); a `BAB` build takes its own assembly E-number at design
commit (ADR-0017 rev 2 d4), as M07's second variant does. Firmware is identical across both
(§10) — the FOV is not readable from the device.

## 4. Sensor complement

| # | Device | Ordering part | Function | Rail | I²C address | Address type |
|---|--------|---------------|----------|------|-------------|--------------|
| U1 | Melexis MLX90640 | `MLX90640ESF-BAA-000-TU` | 32 × 24 IR array, 768 pixels, factory calibrated | 3.3 V | `0x33` default | EEPROM-programmable, `0x01`…`0xFF` (`0x00` forbidden), cell `0x240F` |

Named by ADR-0014 d4. `0x33` lies outside the `0x50`–`0x57` block ADR-0014 rev 4 d6 reserves for
the module-ID EEPROM; M04 identifies by strap (§5) and carries no EEPROM of its own. **The address
shall be left at its factory default** — a device programmed away from `0x33` is not
interchangeable with a spare, and the boot probe of §10 assumes the default.

### 4.1 Package

| Parameter | `BAA` — 110° × 75° | `BAB` — 55° × 35° |
|---|---|---|
| Package | TO-39, 4 leads, hermetic can | TO-39, 4 leads, hermetic can |
| Can diameter | Ø9.30 ±0.15 mm | Ø9.30 ±0.15 mm |
| Body height above the seating plane | **5.70 ±0.30 mm** | **11.25 ±0.30 mm** |
| Aperture | Ø2.60 ±0.10 mm, flush | Ø3.90 ±0.10 mm in an **M5 threaded** external aperture |
| Overall width across the index tab | 10.03 ±0.20 mm; tab 0.80 ±0.10 mm, at 45° to the lead pattern | Same |
| Lead circle | Ø5.84 ±0.18 mm, 4 leads at 90° | Same |
| Lead | Ø0.45 ±0.05 mm, 6 ±0.50 mm long | Same |
| Pin order, from below, from the tab | VDD, GND, SDA, SCL | VDD, GND, SDA, SCL |
| Obstacle-free zone | **140°**, referenced to the external aperture | **90°** |

Dimensions from DS12 Figures 28 and 29; the obstacle-free cones from DS12 Figure 25. The external
aperture is fitted before factory calibration and is part of the device.

The footprint shall be checked against the physical part with a 1:1 paper printout before ordering
(V5). The check shall include the index tab, which fixes pin 1 and does not sit on the lead circle.

### 4.2 Supporting parts

| # | Function | Value | Source |
|---|----------|-------|--------|
| C1 | Bulk decoupling at U1 VDD | 10 µF ceramic | DS12 §14 — 100 nF plus 10 µF close to the VDD and VSS pins |
| C2 | HF decoupling at U1 VDD | 100 nF ceramic | DS12 §14 |
| R1, R2 | I²C pull-ups, SCL and SDA | 4.7 kΩ to 3.3 V | §5.1 |

No regulator, no bus switch, no level translator: U1 runs from the header's 3.3 V and its digital
pins are 5 V tolerant (§5.1). M04 generates no module-local rail, so O-43 is not reopened here.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b100`: STRAP_0 low, STRAP_1 low, STRAP_2 high |
| Header B pin 1 / 2 — 3V3, GND | Strap reference |

No other header signal is claimed. SPI, both ADC channels, all four PWM channels, 1-Wire and all
four GPIO stay free; `GPIO_1`/`GPIO_2` (PA9/PA10) are not used, so USART1 remains available as the
carrier debug console throughout boot and run (pin map note 6).

**M04's strap pattern has bit 2 = 1**, which is readable on every carrier revision: only `STRAP_1`
(PA6) was unrouted before `E0001-000003`, so a board carrying that defect still reads M04
correctly.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, one segment, one device, board-mounted. No switch, no second segment |
| Speed | 100 kHz standard mode, the platform default set by the shared carrier driver. U1 supports FM+ to 1 MHz, but **EEPROM operations are limited to 400 kHz** (DS12 Table 5 note 5), which includes the calibration read of §6.2 |
| Levels | SDA and SCL are 5 V tolerant and referenced to the pull-up rail, not to VDD. V_IH 0.7 · VDD, V_IL 0.3 · VDD, V_OL 0.4 V at 3 mA sink |
| Pull-ups | R1, R2, 4.7 kΩ to 3.3 V, on the module; the carrier carries none (O-51). Sink 0.70 mA against the 3 mA V_OL condition, and against the **10 mA** ceiling DS12 Table 5 note 4 places on the SDA driver for thermal reasons (absolute maximum 40 mA) |
| Sizing check | Rise time at 100 kHz admits ≈ 250 pF on the segment, against board traces and U1's 10 pF pin capacitance. DS12 Figure 26 uses 1 kΩ; 4.7 kΩ is retained at 100 kHz and **shall be re-sized to ≈ 1 kΩ if the bus is raised to 400 kHz or above** (§5.2) |
| Address | `0x33`. No device on this module occupies `0x50`–`0x57` |

### 5.2 Bus occupancy

One frame read is the whole RAM image, 834 words = **1668 B**, at 9 clocks per byte:

| Bus clock | One frame read | Highest refresh rate whose subpage interval exceeds it |
|---|---|---|
| 100 kHz | **150 ms** | 4 Hz (250 ms) |
| 400 kHz | 37.5 ms | 16 Hz (62.5 ms) |
| 1 MHz, FM+ | 15.0 ms | 32 Hz (31.25 ms) |

At the 2 Hz refresh rate of §6.1 the module performs **two subpage reads per second** and holds the
bus **300 ms of every second — 30 %** at 100 kHz. The carrier's I²C driver is blocking, so that
figure is also main-loop occupancy; §10 requires the read to be chunked. O-91.

M04 is the only device on its segment, so no other module's traffic is displaced.

## 6. Measurement requirements

### 6.1 Acquisition

| Item | Requirement |
|------|-------------|
| Reading pattern | **Chess** (`0x800D` bit 12 = 1, factory default). The device is calibrated in chess pattern; interleaved (TV) mode shall not be used |
| Refresh rate | **2 Hz** (`0x800D` bits 9:7 = `010`, the device default) — one subpage each 500 ms, one **complete frame each 1 s**, matching the 1 Hz statistics cadence of ADR-0014 d4 |
| ADC resolution | 18 bit (`0x800D` bits 11:10 = `10`, default) |
| Subpage mode | Enabled, alternating (`0x800D` bit 0 = 1, bit 3 = 0). **Both subpages are required**: the Ta computation combines them, so Ta refreshes at half the subpage rate |
| Frame handshake | Poll bit 3 of the status register `0x8000` (new data available), read the subpage, clear the bit. Bits 2:0 of the same register name the subpage measured |
| Data hold | Overwrite enabled (`0x800D` bit 2 = 0, default): a missed read is overwritten by the next frame rather than stalling acquisition |
| First data after power-on | 80 ms plus the interval of the selected refresh rate (DS12 §12.2.1). Subpage 0 is always measured first |
| Settling | **Up to 4 min of thermal stabilization** before the accuracy of §6.3 applies (DS12 §12.2.2). Frames published inside that window carry `stabilized = false` (§10.1) |

### 6.2 Compensation chain

| Item | Requirement |
|------|-------------|
| Calibration source | The device's own EEPROM, `0x2400`–`0x273F`, 832 words = 1664 B. Read **once** after power-on at ≤ 400 kHz and restored into RAM per DS12 §11.1; never re-read per frame |
| Per-pixel computation | DS12 §11.2 in full — supply-voltage and Ta computation, gain, offset, Ta and VDD compensation, emissivity compensation, gradient compensation, normalization to sensitivity, and the range-dependent sensitivity correction |
| Thermal gradient compensation | **TGC is disabled and cannot be changed** on both ordering codes: the `xAx` option fixes TGC = 0 (DS12 §4, §15.3). It is not a tuning parameter of this module |
| Emissivity ε | **Deployment constant**, not compiled in (§10). Applied per DS12 §11.2.2.5.4, which assumes ε = 1 unless a coefficient is supplied. O-87 |
| Reflected temperature Tr | **Deployment constant.** Where none is supplied, Tr = Ta − 8 K (DS12 §11.2.2, §12.1.2 note) |
| Defective pixels | Up to 4 per device, identified in the device EEPROM (DS12 §9). Firmware shall read the list, exclude those pixels from every statistic of §6.4, replace them by interpolation of their neighbours in the published frame, and publish the count |
| Published with every sample | ε, Tr and the hotspot constants in force (§6.4). Statistics without them are not a measurement |

### 6.3 Accuracy

All figures below hold **under settled isothermal conditions and only where the object fills the
pixel's field of view** (DS12 §12.1.1). Neither condition is guaranteed by a canopy: leaf edges,
gaps and background make mixed pixels the normal case (§6.5, O-88).

| Ta band | Object band | Frame accuracy | Non-uniformity, `BAA` | Non-uniformity, `BAB` |
|---|---|---|---|---|
| 0…50 °C | 0…100 °C | **±1 °C** | zone 1 ±0.5 °C · zone 2 ±1 °C · zone 3 ±2 °C, plus 2 % · \|To−Ta\| | zone 1 ±1 °C · zone 2 ±2 °C |
| −40…0 °C and 50…85 °C | 0…100 °C | ±2 °C | zone 1 ±1 °C · zone 2 ±2 °C · zone 3 ±3 °C, each plus 2 % · \|To−Ta\| | Not specified (TBD in DS12) |
| 0…50 °C | −40…0 °C | ±5 °C | 2 % · \|To−Ta\| | Not specified |

- **Pixel absolute accuracy = frame accuracy + non-uniformity** (DS12 §12.1.1). In zone 1 at the
  operating point of §3 that is **±1.5 °C** for `BAA` and ±2 °C for `BAB`.
- **The zones are defined graphically** (DS12 Figure 18): a central zone 1, a surrounding zone 2,
  and corner patches of zone 3 on `BAA` only. DS12 gives no pixel-index boundaries. O-92.
- **NETD at 1 Hz, all pixels:** `BAA` 0.14 K average (0.1 K minimum, σ 0.05 K); `BAB` 0.25 K
  average (0.2 K minimum, σ 0.05 K). Corner pixels are noisier than central ones, and noise rises
  at lower object temperature (DS12 §12.3).
- **Long-term drift: an additional ±3 °C for objects around room temperature over years**
  (DS12 §12.1.1 note 2). That is the operating point of §3, and the term dominates every other
  line of the budget. O-89.
- Ta channel: ±0.5 °C.

### 6.4 Frame statistics

Computed on the node from the compensated field of §6.2, over the 768 pixels less the defective
list, once per complete frame, published at 1 Hz (§10.1).

| Statistic | Definition |
|---|---|
| `t_mean` | Arithmetic mean of the valid pixels, K |
| `t_min`, `t_max` | Extremes over the valid pixels, K |
| `t_sigma` | Population standard deviation of the valid pixels, K. Floor set by NETD (§6.3) |
| `gradient_x`, `gradient_y` | Slopes of the least-squares plane fitted to the field, reported as **K across the full frame width and height**, so the value does not depend on pixel count |
| `hotspot_mask` | 768 bits, one per pixel, row-major from pixel 1. Bit set where `T > t_mean + max(k · t_sigma, delta_min)` |
| `k`, `delta_min` | Deployment constants (§10). Defaults `k` = 3 and `delta_min` = 1.0 K; the floor is ≈ 7 × the `BAA` NETD and keeps a uniform canopy from masking on noise alone |

The statistics describe the **whole frame**, not the canopy: no segmentation separates leaf pixels
from structure, growing medium or luminaire (§3.2). O-88.

### 6.5 Geometry and FOV selection

| | `BAA` | `BAB` |
|---|---|---|
| FOV, X × Y (typ) | 110° × 75° | 55° × 35° |
| Per-pixel IFOV, nominal | 3.44° × 3.13° | 1.72° × 1.46° |
| Footprint width × height at distance *d* | 2.856 · *d* × 1.535 · *d* | 1.041 · *d* × 0.631 · *d* |
| Central pointing from normal, max | 5° | 3° |

Nominal pinhole model. DS12 characterises the FOV only as the 50 % sensitivity width in the wider
(32-pixel) direction and gives no per-pixel angular map and no distortion figure. O-92.

| Mounting distance | `BAA` footprint · per pixel | `BAB` footprint · per pixel |
|---|---|---|
| 0.3 m | 857 × 460 mm · 27 × 19 mm | 312 × 189 mm · 10 × 8 mm |
| 0.5 m | 1428 × 767 mm · 45 × 32 mm | 520 × 315 mm · 16 × 13 mm |
| 0.8 m | 2285 × 1228 mm · 71 × 51 mm | 833 × 505 mm · 26 × 21 mm |

**Selection rule: the populated variant is the one whose footprint at the installed height covers
the canopy area that instance is responsible for** (ADR-0014 d4 — one instance per canopy area at
cabinet scale, one per growing zone above it). At the 0.3…0.6 m a cabinet luminaire plane allows,
that is `BAA`, which also carries the lower NETD and the tighter zone-1 non-uniformity (§6.3).
`BAB` is the variant for a mount far enough back to keep coverage, where its 2.6 × finer sampling
reduces mixed pixels. The installed height is fixed by no record — ADR-0006 defers cabinet
dimensions to a mechanical specification — so the rule is stated and its input is open. O-86.

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | **Not measured. Estimated 360 mW.** O-94 |
| Basis of the estimate | M01's measured quiescent 263 mW on `+12 V` (M01 §7.1) plus U1's 82.5 mW referred through the carrier converter at η = 0.85 → 97 mW |
| Module contribution | **82.5 mW** — 25 mA maximum at 3.3 V, all on the header rail |
| Burst reflected to `+12 V` | **None.** U1 draws its current continuously; there is no burst load of the SCD41 class |

U1 is the largest single sensor load in the catalog: 25 mA against M01's 15 mA average and M02's
0.57 mA total.

### 7.2 Device current

| # | Device | Operating | Rail |
|---|--------|-----------|------|
| U1 | MLX90640 | **25 mA max** (DS12 Table 5); "less than 23 mA" on DS12 §1. No sleep or standby state is offered — measurement runs continuously while the device is powered | 3.3 V |

### 7.3 3.3 V rail

Supplied by the carrier's TPS54302 buck (400 kHz fixed, 5 ms internal soft start), shared with the
MCU and CAN transceiver. No rail redesign, and no module-local rail (§4.2).

| Item | Requirement |
|------|-------------|
| Supply range | 3.0…3.3…3.6 V |
| Accuracy recommendation | **3.3 V ±0.05 V for best performance** (DS12 Table 5 note 1, §12.1.1 note 1). It is a recommendation, not an operating limit: the compensation chain measures the actual supply through the VDD sensor pixel and corrects against it (§6.2, DS12 §11.2.2.2) |
| Rail deviation at the module | Unmeasured against the ±0.05 V recommendation, DC and ripple. V11, O-95 |
| Decoupling | C1 10 µF and C2 100 nF, both as close to the **VDD and VSS pins** as the layout allows; DS12 §14 requires the VSS return trace to be as short as the VDD trace |
| 5 V | Header pin A.2 is not claimed. The carrier has no 5 V rail in Phase 1 and U1 needs none |

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | **Ta is the die temperature; the air around the device is ≈ 8 K below it** (DS12 §12.1.2 note). Ta shall not be published as air temperature and shall not be substituted for M01's measurement in any derived quantity | §3.2, §10.1 |
| T2 | 82.5 mW is dissipated inside the can (§7.2). DS12 §14 states that package dissipation both heats the ambient-sensing element above true ambient and creates gradients across the cap. No further heat source shall sit on this module, and the can shall stand clear of the board's other copper | §7.2, V4 |
| T3 | **The device is calibrated for settled conditions and is explicitly not to be subjected to transient thermal conditions** (DS12 §14). The can shall not sit in the luminaire's direct beam, in a fan discharge, or against a surface that swings with the photoperiod. The board sits under the luminaire at the 65…76 W/m² PAR of M02 §3; steady-state rise above canopy air shall be recorded at bring-up, not assumed | §9 M5, V4 |
| T4 | The accuracy of §6.3 applies only after **4 min** of thermal stabilization from power-on (§6.1). Every frame inside that window is published with `stabilized = false` and is not a measurement of record | §6.1, V4 |

## 9. Optical and mechanical requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | **The obstacle-free cone shall be clear** — 140° for `BAA`, 90° for `BAB`, referenced to the external aperture (DS12 Figure 25). No enclosure part, cable, standoff, connector or neighbouring component may enter it | §4.1, V2 |
| M2 | **No window shall stand in the optical path** unless its transmission is measured in the device's band. DS12 states no spectral passband for the filter, and common enclosure materials are opaque in the thermal infrared. An unqualified window makes the module measure the window | §3.2, O-96 |
| M3 | No conformal coating on the can, the external aperture or the window. Coating everywhere else: the growing volume may condense on excursions | §3.1 |
| M4 | Contamination of the optical surface causes unspecified error (DS12 §14). Inspection and cleaning belong to the maintenance procedure; the module offers no fault indication for it | §3.1, O-90 |
| M5 | The optical axis shall be normal to the canopy plane within the device's central-pointing tolerance (5° `BAA`, 3° `BAB`) plus the mounting error, and the can shall be oriented by its **index tab**, not by the can outline | §4.1, §6.5 |
| M6 | U1 is a **four-lead through-hole hermetic can** with 6 ±0.50 mm of lead, on a board otherwise SMT, and is not placed in the reflow pass. The assembly route — hand solder, selective solder, or consignment — shall be fixed before the fab package is ordered | §4.1, O-93 |
| M7 | Mounting height and orientation are fixed at deployment and recorded as deployment metadata. A height change changes the footprint and the per-pixel area (§6.5) and invalidates commissioning done against the previous geometry | §6.5, ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8. **Mounting holes shall be provided and standoffs populated**: the pin map names M04 among the heavier modules, and the `BAB` can stands 11.25 mm above the seating plane on 6 mm leads | Pin map, header section |
| M9 | The face carrying U1 carries nothing that enters the cone of M1. ADR-0014 d4's reserved space for future leaf-level sensors is **board space only**; this specification defines no footprint for it | ADR-0014 d4 |

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b100` — STRAP_0 low, STRAP_1 low, STRAP_2 high. The personality is selected at runtime from the strap in the one carrier image (ADR-0017 d16); no separate build |
| Power-on delay | No transaction before 80 ms plus the refresh interval have elapsed (§6.1) |
| Boot probe | `0x33`, backed by a device-specific read: the three device-ID words at EEPROM **`0x2407`, `0x2408`, `0x2409`**, which shall be non-zero and shall not change between reads. An ACK is not identification (M01 §10 precedent). The 48-bit ID is per-part and is the instance evidence a bring-up record cites |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; an absent U1 means an absent subject and no served file, not an error (ADR-0005 d8) |
| Calibration restore | Read the 832-word EEPROM once after power-on at ≤ 400 kHz and restore per DS12 §11.1 (§6.2). A failed or implausible restore shall disable publication rather than publish uncompensated data |
| Configuration at start-up | Refresh rate, resolution, reading pattern and subpage mode per §6.1, written to `0x800D` explicitly rather than inherited from the device's EEPROM defaults |
| Frame read | Chunked, with the main loop serviced between chunks: one read is 150 ms of blocking I²C at 100 kHz, twice a second, and the node owes a 1 Hz heartbeat and the file service throughout (§5.2). O-91 |
| Statistics | Per §6.4, over the valid-pixel set of §6.2 |
| Not derived | Canopy-to-air differential, leaf VPD, DLI — gateway or deferred (§3.2) |
| Deployment constants | ε, Tr, `k` and `delta_min` read from the deployment profile, carried as the `uavcan.register` entries `industryflow.greenhouse.plant.emissivity`, `.reflected_temperature`, `.hotspot_k` and `.hotspot_delta_min`. Volatile: no register but `uavcan.node.id` has a store (ADR-0005 d7), so a commissioned node re-takes them at every restart. The uncommissioned state is ε = 1 and Tr = Ta − 8 K, and the record publishes the values in force |
| Memory budget | ≈ 9.8 KB persistent — restored parameters 4.7 KB (α 1536 B, offset 1536 B, Kta 768 B, Kv 768 B, scalars ≈ 100 B), frame buffer 1668 B, compensated field 3072 B, published frame image 1536 B — plus a 1664 B transient for the EEPROM image at start-up. Against 192 KB on the STM32F405 |
| Message timestamps | `uavcan.time.SynchronizedTimestamp` from the gateway time base, as M01, M02 and M05 |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m04_plant/` when written. **No firmware exists for this class**; neither stream of ADR-0005 d11 is implemented |
| Node-ID | Not a property of the module class: provisioned per instance into carrier flash (ADR-0027), and distinct across the bus. Bring-up assignment for the first instance is **99** |
| Publication rate | 1 s for the statistics record; 5 min or on event for the frame (§10.2) |

### 10.1 Published subjects

Baked defaults in the unregulated range. ADR-0005 d7 makes these `uavcan.pub.<name>.id` register
entries. M05 holds 4096–4102; M01 holds 4112–4121; M02 holds 4128–4131.

| ID | Quantity | Source | Type |
|----|----------|--------|------|
| 4144 | Canopy thermal summary | U1, derived §6.4 | `industryflow.greenhouse.plant.CanopyThermalSummary` |

**One subject, one minted type** (ADR-0005 d11): the statistics describe one frame and, split
across separate SI subjects, would describe no frame in particular. The `plant` sub-namespace is
named by ADR-0005 d1.

Record fields, kelvin wherever a temperature:

| Field | Type | Content |
|---|---|---|
| `timestamp` | `uavcan.time.SynchronizedTimestamp` | Frame completion |
| `t_mean`, `t_min`, `t_max`, `t_sigma` | `float32` | §6.4 |
| `gradient_x`, `gradient_y` | `float32` | §6.4, K across the frame |
| `hotspot_mask` | `uint8[96]` | 768 bits, §6.4 |
| `ta` | `float32` | Die temperature; **not air temperature** (§8 T1) |
| `emissivity`, `reflected_temperature`, `hotspot_k`, `hotspot_delta_min` | `float32` | Constants in force (§6.2, §6.4) |
| `defective_pixels` | `uint8` | Count excluded, 0…4 (§6.2) |
| `stabilized` | `bool` | False for the first 4 min after power-on (§6.1, T4) |
| `frame_seq` | `uint32` | Sequence of the frame these statistics describe |
| `frame_available` | `bool` | A full frame for this sequence is served at §10.2 (ADR-0005 d11) |

Record size ≈ 140 B, ≈ 20 Cyphal/CAN frames at 1 Hz.

### 10.2 Full frame

| Item | Requirement |
|---|---|
| Mechanism | `uavcan.file.Read` — the node serves, the gateway reads (ADR-0005 d11). No type is minted |
| Trigger | The gateway reads on `frame_available` changing, not by polling. The node offers a frame every 5 min, and on event or alarm |
| Path | `/plant/frame.bin`, fixed |
| Content | A 32 B header — magic `IGP1`, `frame_seq` `uint32`, `timestamp` `uint64` µs, `emissivity` `float32`, `reflected_temperature` `float32`, `ta` `float32`, `defective_pixels` `uint8`, remainder reserved — followed by **768 × `uint16` centikelvin**, row-major from pixel 1 (row 1, column 1), little-endian. **1568 B total** |
| Resolution | 0.01 K over 0…655.35 K, which spans the device's whole −40…+300 °C object range |
| Content is compensated, not raw | The raw 1668 B RAM image carries no meaning without that device's own calibration constants, which would have to be transferred and bound per instance. The node computes the compensated field for §6.4 already and serves that. ADR-0005 d11 defers the byte layout to this document and costs 1668 B; 1568 B is inside it |
| Hold policy | The served frame is replaced only at the next offer. A read spanning a replacement is detected by the gateway from the header's `frame_seq` and re-read |
| Cost | ADR-0005 d11 at 500 kbit/s: 239 frames, ≈ 72 ms, once per 5 min — 0.024 % mean occupancy |

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.3 | Frame accuracy against a reference target of known emissivity filling the field, at three temperatures spanning 10…30 °C, after the 4 min settling of T4, with the reference read by a contact thermometer. Frame mean within ±1 °C of the reference; per-pixel spread against the zone figures |
| V2 | §9 M1, §6.5 | Installed footprint measured against the §6.5 table at the installed height, with a warm target moved to each edge of the field; nothing in the frame that is not the intended scene |
| V3 | §5.2, §10 | Frame read time measured at 100 kHz against 150 ms, and bus occupancy logged over ≥ 1 h at the 2 Hz refresh rate. Heartbeat continuity and file-service responsiveness confirmed **during** frame reads — the failure the chunking requirement exists to prevent |
| V4 | §6.1, T2, T3, T4 | Cold power-on logged for ≥ 30 min: Ta and frame mean against a fixed reference target, drift across the first 4 min recorded, and the `stabilized` flag observed to clear. Ta compared against canopy air from an M01 instance in the same volume, against T1's 8 K offset |
| V5 | §4.1 | 1:1 paper printout against the physical part, including the index tab and the lead circle |
| V6 | §5 | Module-ID readback of `0b100` on the carrier in use |
| V7 | §7.1 | Node draw measured on `+12 V` against the 360 mW estimate, and U1's own current against 25 mA |
| V8 | §10.2, §6.4 | A full frame read by the gateway, and `t_mean`, `t_min`, `t_max` recomputed from the file to match the record carrying the same `frame_seq` |
| V9 | §6.2 | Defective-pixel list read from the device EEPROM, the count published, and the excluded pixels confirmed absent from the statistics and interpolated in the served frame |
| V10 | §6.2, §6.5 | ε and Tr identified at commissioning against a reference target at canopy distance; the identified values recorded as deployment metadata together with the mounting height |
| V11 | §7.3 | Rail measured at U1's VDD pin under load — DC value against 3.3 V ±0.05 V, ripple against the buck's 400 kHz fundamental |
| V12 | §10 | U1 identified by the three device-ID words at `0x2407`–`0x2409`, non-zero and stable, not by address ACK alone |

## 12. Open items

Continues the `O-` namespace shared with the M01, M02, M05, M06, M07 and service-tool
specifications. O-2, O-4, O-6, O-8, O-10, O-11 and O-42 are M06's; O-12 to O-24 are M07's;
O-25 to O-31 are M05's; O-3, O-5, O-7, O-9, O-32 to O-51, O-73 and O-75 are M01's; O-52 to O-72
are M02's; O-74 is M05's; O-76 to O-85 are the service tool's.

| ID | Item | Blocks |
|----|------|--------|
| O-86 | Mounting height and the canopy area one instance covers are not fixed: ADR-0006 defers cabinet dimensions to a mechanical specification, so the selection rule of §6.5 has no input | FOV variant confirmation, M5, M7, V2 |
| O-87 | Leaf emissivity, reflected apparent temperature and the canopy-to-air differential band of §3 are not established. ε = 1 and Tr = Ta − 8 K are placeholders, not values | Absolute canopy temperature, expected range, V10 |
| O-88 | No canopy segmentation exists, so §6.4's statistics are frame-wide and include structure, medium and luminaire pixels. ADR-0014 d4 defers the pipeline | Meaning of every statistic, leaf VPD, hotspot interpretation |
| O-89 | DS12 states an additional ±3 °C over years for objects around room temperature — the operating point of §3 — with no re-referencing scheme. Whether the module is periodically referenced against a known target, against M01's air temperature at a settled night point, or replaced on an interval | Absolute accuracy over service life, calibration protocol |
| O-90 | The device carries no humidity rating in DS12 and its optical surface is exposed in a volume that may condense. Post-condensation validity, recovery and M4's inspection interval are undefined | Excursion handling, data validity, maintenance procedure |
| O-91 | One frame read blocks the I²C driver for 150 ms and the module reads twice a second. §10's chunking requirement is stated but unimplemented and unverified, and the carrier driver's blocking I²C is its precondition | Heartbeat continuity, file service, V3 |
| O-92 | DS12 defines the accuracy zones graphically with no pixel-index boundaries, and gives no per-pixel angular map or lens-distortion figure. Neither per-zone weighting of the statistics nor an exact pixel-to-position mapping can be stated | §6.3 zone application, §6.5 geometry |
| O-93 | The assembly route for a four-lead through-hole hermetic can on an otherwise SMT board is not fixed, and DS12 states no MSL and no soldering profile | Fab package, assembly quote, M6 |
| O-94 | Node power unmeasured | Distribution-board sizing, O-31 |
| O-95 | Rail deviation and ripple at the module unmeasured against §7.3's ±0.05 V recommendation | V11, absolute accuracy |
| O-96 | No IR-transmissive window is specified, and DS12 states no spectral passband against which one could be qualified. Whether the module looks into the growing volume unglazed, or an enclosure window is qualified by measurement | Enclosure design, M2 |

## 13. Maturity

**Pre-schematic.** The complement is fixed by ADR-0014 d4 and every device value here is stated
from DS12. `E0005` has no schematic, no layout, no fab package and no firmware, and the `plant`
DSDL types do not exist.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | ADR-0014 d4 fixes the part ✔; DS12 values transcribed ✔ |
| **Schematic captured** | Parts fixed to ordering part numbers; schematic exists; component values determined | FOV variant confirmed against O-86; `E0005-000001` schematic exists |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-93 and O-96 closed; V5 executed |
| **As-built** | Estimates replaced by measured values; §11 executed | `E0005` fabricated and bench-verified; O-94 and O-95 measured |
