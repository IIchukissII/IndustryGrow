<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M04-PLANT — module specification

- **Status:** Working specification. Pre-schematic: `E0005` has no schematic, no layout and no firmware
- **Date:** 2026-09-03
- **E-number:** `E0005` · module-ID strap `0b100`
- **Governing ADRs:** ADR-0014 (rev 6), ADR-0002 (rev 3), ADR-0003, ADR-0005 (rev 1, d11 and d12), ADR-0006, ADR-0016, ADR-0017 (rev 2), ADR-0020, ADR-0027, ADR-0028 (d10)
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
| E-number | `E0005` — assembly populated with the 110° × 75° imager (§6.6) |
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
| Frame statistics — mean, min, max, σ, gradient, hotspot mask (derived, §6.4) | U1 | — | Mean 8…32 °C; σ ≥ 0.18 K, the noise of the 4-frame mean it is computed on | Mean inherits frame accuracy ±1 °C; the spread terms inherit non-uniformity, not frame accuracy |
| Die temperature Ta | U1 | −40…+85 °C | 20…36 °C — air +8 K (§8 T1) | ±0.5 °C |

Publication rate: **1 Hz** for the statistics record; **one interval frame per minute, or on
event**, served over `uavcan.file.Read` (ADR-0005 d11 and d12, §6.5, §10.2). The device delivers a
complete frame every 250 ms (§6.1), so the 1 Hz record is computed on four frames and one interval
frame is the product of 240.

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
so this module has no partial population in M01's and M02's sense. U2 is not optional either: it
holds the trim (§6.7), and a module without it declares none (ADR-0028 d5, d10).

The **FOV option is a populated-BOM variant** (ADR-0014 d2, third dimension): both ordering codes
share the TO-39 lead pattern of §4.1 and differ in can height, aperture and obstacle-free cone.
`E0005` names the `BAA` build (§6.6); a `BAB` build takes its own assembly E-number at design
commit (ADR-0017 rev 2 d4), as M07's second variant does. Firmware is identical across both
(§10) — the FOV is not readable from the device.

## 4. Sensor complement

| # | Device | Ordering part | Function | Rail | I²C address | Address type |
|---|--------|---------------|----------|------|-------------|--------------|
| U1 | Melexis MLX90640 | `MLX90640ESF-BAA-000-TU` | 32 × 24 IR array, 768 pixels, factory calibrated | 3.3 V | `0x33` default | EEPROM-programmable, `0x01`…`0xFF` (`0x00` forbidden), cell `0x240F` |

Named by ADR-0014 d4. `0x33` lies outside the `0x50`–`0x57` block ADR-0014 rev 4 d6 reserves for
the module-ID EEPROM, which this module fits at `0x50` as U2 (§4.2) to hold the flat-field trim of
§6.7. M04 still identifies by strap (§5). **U1's address shall be left at its factory default**;
the boot probe of §10 assumes it.

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
| R1, R2 | I²C pull-ups, SCL and SDA | 2.2 kΩ to 3.3 V | §5.1 |
| U2 | Serial EEPROM — the flat-field store | 24Cxx class, **≥ 4 KB**, 1.7…3.6 V, 400 kHz at 3.3 V, −40…+85 °C, address `0x50` with A0–A2 to GND | §6.7, ADR-0028 d10 |
| C3 | U2 decoupling | 100 nF at the pin | — |

U2 is a store, not a sensor: it measures nothing and publishes nothing. Byte 0 holds the class ID
of ADR-0014 d6 whether or not the carrier reads it; the flat-field record starts at byte 16 (§6.7).
Its ordering part is fixed at schematic capture.

No regulator, no bus switch, no level translator: U1 runs from the header's 3.3 V and its digital
pins are 5 V tolerant (§5.1). M04 generates no module-local rail, so O-43 is not reopened here.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1, U2 |
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
| Speed | **400 kHz.** U1 supports FM+ to 1 MHz, the STM32F405's I²C peripheral tops out at 400 kHz, and DS12 Table 5 note 5 limits U1's EEPROM operations to the same 400 kHz — the ceilings coincide, so one rate serves the calibration read of §6.2, the frame reads and U2. U2 shall be a part rated for 400 kHz at 3.3 V. The 100 kHz platform default of M01 and M02 is set per node by the personality, not by the carrier |
| Levels | SDA and SCL are 5 V tolerant and referenced to the pull-up rail, not to VDD. V_IH 0.7 · VDD, V_IL 0.3 · VDD, V_OL 0.4 V at 3 mA sink |
| Pull-ups | R1, R2, **2.2 kΩ** to 3.3 V, on the module; the carrier carries none (O-51). Sink 1.5 mA against the 3 mA V_OL condition, and against the **10 mA** ceiling DS12 Table 5 note 4 places on the SDA driver for thermal reasons (absolute maximum 40 mA) |
| Sizing check | Fast mode allows 300 ns of rise time. At 2.2 kΩ that admits 161 pF on the segment, against 10 pF at U1, ≈ 8 pF at U2 and short board traces. DS12 Figure 26 uses 1 kΩ, which would sink 3.3 mA — at the V_OL test condition rather than below it |
| Address | U1 `0x33`, U2 `0x50` — the block ADR-0014 rev 4 d6 reserves, occupied here by design (§4.2). No collision, and U2's 5 ms self-timed write cycle falls in commissioning only |

### 5.2 Bus occupancy

One subpage read is the whole RAM image, 834 words = **1668 B**, at 9 clocks per byte:

| Bus clock | One read | Highest refresh rate whose subpage interval exceeds it |
|---|---|---|
| 100 kHz | 150 ms | 4 Hz (250 ms) |
| **400 kHz** | **37.5 ms** | **16 Hz** (62.5 ms) |
| 1 MHz, FM+ | 15.0 ms | 32 Hz — not reachable, the STM32F405 I²C stops at 400 kHz |

At the 8 Hz refresh rate of §6.1 the module performs eight reads per second and holds the bus
**300 ms of every second — 30 %**. The carrier's I²C driver is blocking, so that figure is also
main-loop occupancy; §10 requires the read to be chunked. O-91.

16 Hz is reachable at 600 ms/s. It is not specified: §6.3 shows the interval noise floor does not
move with refresh rate, so the second 300 ms of main loop buys time resolution only.

## 6. Measurement requirements

### 6.1 Acquisition

| Item | Requirement |
|------|-------------|
| Reading pattern | **Chess** (`0x800D` bit 12 = 1, factory default). The device is calibrated in chess pattern; interleaved (TV) mode shall not be used |
| Refresh rate | **8 Hz** (`0x800D` bits 9:7 = `100`) — one subpage each 125 ms, **four complete frames per second**, 240 per interval, 300 ms/s of bus (§5.2) |
| ADC resolution | **19 bit** (`0x800D` bits 11:10 = `11`), not the 18-bit default. DS12 §12.3: higher resolution lowers quantization noise. The compensation chain corrects for a resolution differing from the calibration one through the resolution-control coefficient (DS12 §11.1.17). DS12 states no interaction between resolution and refresh rate; saturation at 19 bit and 8 Hz is a bench check, V15 |
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
gaps and background make mixed pixels the normal case (§6.6, O-88).

| Ta band | Object band | Frame accuracy | Non-uniformity, `BAA` | Non-uniformity, `BAB` |
|---|---|---|---|---|
| 0…50 °C | 0…100 °C | **±1 °C** | zone 1 ±0.5 °C · zone 2 ±1 °C · zone 3 ±2 °C, plus 2 % · \|To−Ta\| | zone 1 ±1 °C · zone 2 ±2 °C |
| −40…0 °C and 50…85 °C | 0…100 °C | ±2 °C | zone 1 ±1 °C · zone 2 ±2 °C · zone 3 ±3 °C, each plus 2 % · \|To−Ta\| | Not specified (TBD in DS12) |
| 0…50 °C | −40…0 °C | ±5 °C | 2 % · \|To−Ta\| | Not specified |

- **Pixel absolute accuracy = frame accuracy + non-uniformity** (DS12 §12.1.1). In zone 1 at the
  operating point of §3 that is **±1.5 °C** for `BAA` and ±2 °C for `BAB`.
- **The zones are defined graphically** (DS12 Figure 18): a central zone 1, a surrounding zone 2,
  and corner patches of zone 3 on `BAA` only. DS12 gives no pixel-index boundaries. O-92.
- **Noise is integration-time limited.** Per-frame RMS noise scales as √(refresh rate), from
  DS12 Figure 19 (`BAA`): 0.09 K at 0.5 Hz, **0.14 K at 1 Hz**, 0.19 at 2 Hz, 0.26 at 4 Hz,
  **0.36 at 8 Hz**, 0.52 at 16 Hz, 1.05 at 64 Hz. Table 14's 0.14 K average (0.1 K minimum,
  σ 0.05 K) and the `BAB` figure of 0.25 K are the **1 Hz** values. Corner pixels are noisier than
  central ones, and noise rises at lower object temperature (DS12 §12.3).
- **The noise of a fixed-length average does not move with refresh rate.** Doubling the rate halves
  the integration per frame and doubles the frame count, and the two cancel: over 60 s the floor is
  0.14 K/√30 = **0.025 K** at 2 Hz, at 8 Hz and at 16 Hz alike (§6.5). Refresh rate buys time
  resolution, not noise.
- **Non-uniformity is the dominant error and it does not average away.** ±0.5 K in zone 1 is
  fixed-pattern — 20 × the temporal floor above. The flat-field trim of §6.7 corrects its offset
  component; its determination protocol is open, O-99.
- **Long-term drift: an additional ±3 °C for objects around room temperature over years**
  (DS12 §12.1.1 note 2) — the operating point of §3, and the largest term in this budget. O-89.
- Ta channel: ±0.5 °C.

### 6.4 Frame statistics

Computed on the node from the compensated field of §6.2, over the 768 pixels less the defective
list, once per second on the mean of that second's four complete frames (§6.1), published at 1 Hz
(§10.1). The 4-frame mean carries 0.18 K of noise against 0.36 K in a single frame (§6.3).

| Statistic | Definition |
|---|---|
| `t_mean` | Arithmetic mean of the valid pixels, K |
| `t_min`, `t_max` | Extremes over the valid pixels, K |
| `t_sigma` | Population standard deviation of the valid pixels, K. Floor set by NETD (§6.3) |
| `gradient_x`, `gradient_y` | Slopes of the least-squares plane fitted to the field, reported as **K across the full frame width and height** |
| `hotspot_mask` | 768 bits, one per pixel, row-major from pixel 1. Bit set where `T > t_mean + max(k · t_sigma, delta_min)` |
| `k`, `delta_min` | Deployment constants (§10). Defaults `k` = 3 and `delta_min` = 1.0 K, the floor ≈ 5 × the noise of the 4-frame mean |

The statistics describe the **whole frame**, not the canopy: no segmentation separates leaf pixels
from structure, growing medium or luminaire (§3.2). O-88.

### 6.5 Interval product

The frame served at §10.2 is the product of every valid device frame of the interval, computed on
the node.

| Item | Requirement |
|------|-------------|
| Interval | 60 s, aligned to the minute of the gateway time base while synchronized; free-running before the first sync pair |
| Frames accumulated | Every complete frame whose handshake and Ta are valid (§10), **nominally 240** at the 8 Hz refresh rate of §6.1. The count is served in the header |
| Mean plane | Per-pixel arithmetic mean over the accumulated frames. 0.36 K per frame at 8 Hz over N = 240 gives **0.025 K**, the fixed-integration-time floor of §6.3 |
| σ plane | Per-pixel standard deviation over the same frames — the discriminator between a pixel that held still and one that moved, on 240 samples |
| Event frame | The last complete frame alone, `n_frames` = 1, σ plane zero |
| Quantization | **The mean plane is `float32`, unquantized** — it is the quantity of record and carries the whole compensation chain's output as computed. The σ plane is `uint8` at 0.02 K per LSB, saturating at 5.10 K, below the 0.025 K floor it describes |
| **Numerics** | The variance shall be accumulated by **Welford's method**, or as deviations from a per-pixel reference. **The naive `Σx² − n·x̄²` form shall not be used**: at canopy temperatures `Σx²` ≈ 5.2 · 10⁶ K², where a `float32` ULP is 0.5 K² against the σ² ≈ 0.02 K² being extracted. The subtraction returns noise, and can return a negative variance |
| Statistics | §6.4's 1 Hz record is computed per frame, unaffected by this accumulation |

### 6.6 Geometry and FOV selection

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
the canopy area that instance is responsible for** (ADR-0014 d4). At the 0.3…0.6 m a cabinet
luminaire plane allows, that is `BAA`; `BAB` applies to a mount far enough back to keep coverage,
at 2.6 × finer sampling. The installed height is fixed by no record. O-86.

### 6.7 Flat-field trim

M04 declares one calibration trim (ADR-0028 d1 step 4).

| Item | Requirement |
|------|-------------|
| Corrects | The **offset component of fixed-pattern non-uniformity**, ±0.5 K in zone 1 (§6.3) — the residual after the device's factory calibration, static and unaffected by averaging |
| Does not correct | The sensitivity component, which scales with `To − Ta`. A one-point field is exact at its determination temperature and degrades away from it; over the 8…32 °C band of §3 that residual is unmeasured. A two-point field needs two uniform targets and is not specified. O-99 |
| Form | 768 × `int16`, 0.01 K per LSB, one offset per pixel, subtracted after the compensation chain of §6.2 and before the statistics of §6.4 and the accumulation of §6.5 |
| Store | **U2** (§4.2), by ADR-0028 d10: the MLX90640 has no user cell for 768 corrections, so the trim lives on the module rather than in the corrected device |
| Layout in U2 | Byte 0 class ID (ADR-0014 d6), bytes 1–15 reserved, byte 16 onward a 32 B header — magic `IGFF`, version, kind, pixel count, **U1's 48-bit device ID**, determination scene temperature and Ta as `float32` K, determination time, CRC-32 — followed by the 1536 B table. 1568 B in all |
| Binding | The header's device ID shall match the `0x2407`–`0x2409` read of §10. A field whose ID does not match is **not applied**, and the mismatch is reported — this is the module-swap failure ADR-0028 alternative A names, closed by construction |
| Voided by | Replacement of U1 (ADR-0028 d8), or a change to any validity condition its `-CC` names (ADR-0028 d7) |
| Absent | The node publishes uncorrected and reports it (ADR-0028 d5): `flat_field = false` in the record of §10.1 and in the frame header of §10.2. Firmware writes no default back |
| Written | Over the bus on a running node (ADR-0028 d4), never at flash time. `uavcan.file.Write` to `/plant/flatfield.bin`; the node validates the CRC and the device ID before committing to U2 |
| Determination | Not specified here. The protocol, the uniform reference target and the choice between a one- and two-point field are open, O-99, and produce the `-CP` / `-CC` records of ADR-0028 d1 |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | **Not measured. Estimated 360 mW.** O-94 |
| Basis of the estimate | M01's measured quiescent 263 mW on `+12 V` (M01 §7.1) plus U1's 82.5 mW referred through the carrier converter at η = 0.85 → 97 mW |
| Module contribution | **82.5 mW** — 25 mA maximum at 3.3 V, all on the header rail |
| Burst reflected to `+12 V` | **None.** U1 draws its current continuously; there is no burst load of the SCD41 class |


### 7.2 Device current

| # | Device | Operating | Rail |
|---|--------|-----------|------|
| U1 | MLX90640 | **25 mA max** (DS12 Table 5); "less than 23 mA" on DS12 §1. No sleep or standby state is offered — measurement runs continuously while the device is powered | 3.3 V |
| U2 | 24Cxx EEPROM | Standby ≈ 1 µA; ≤ 3 mA while writing, which happens at commissioning only (§6.7) | 3.3 V |

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
| T2 | 82.5 mW is dissipated inside the can (§7.2), which heats the ambient-sensing element and gradients the cap (DS12 §14). No further heat source shall sit on this module, and the can shall stand clear of the board's other copper | §7.2, V4 |
| T3 | **The device is calibrated for settled conditions and is explicitly not to be subjected to transient thermal conditions** (DS12 §14). The can shall not sit in the luminaire's direct beam, in a fan discharge, or against a surface that swings with the photoperiod. The board sits under the luminaire at the 65…76 W/m² PAR of M02 §3; steady-state rise above canopy air shall be recorded at bring-up, not assumed | §9 M5, V4 |
| T4 | The accuracy of §6.3 applies only after **4 min** of thermal stabilization from power-on (§6.1). Every frame inside that window is published with `stabilized = false` and is not a measurement of record | §6.1, V4 |

## 9. Optical and mechanical requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | **The obstacle-free cone shall be clear** — 140° for `BAA`, 90° for `BAB`, referenced to the external aperture (DS12 Figure 25). No enclosure part, cable, standoff, connector or neighbouring component may enter it | §4.1, V2 |
| M2 | **No window shall stand in the optical path** unless its transmission is measured in the device's band. DS12 states no spectral passband, and common enclosure materials are opaque in the thermal infrared | §3.2, O-96 |
| M3 | No conformal coating on the can, the external aperture or the window. Coating everywhere else: the growing volume may condense on excursions | §3.1 |
| M4 | Contamination of the optical surface causes unspecified error (DS12 §14). Inspection and cleaning belong to the maintenance procedure; the module offers no fault indication for it | §3.1, O-90 |
| M5 | The optical axis shall be normal to the canopy plane within the device's central-pointing tolerance (5° `BAA`, 3° `BAB`) plus the mounting error, and the can shall be oriented by its **index tab**, not by the can outline | §4.1, §6.6 |
| M6 | U1 is a **four-lead through-hole hermetic can** with 6 ±0.50 mm of lead, on a board otherwise SMT, and is not placed in the reflow pass. The assembly route — hand solder, selective solder, or consignment — shall be fixed before the fab package is ordered | §4.1, O-93 |
| M7 | Mounting height and orientation are fixed at deployment and recorded as deployment metadata. A height change changes the footprint and the per-pixel area (§6.6) and invalidates commissioning done against the previous geometry | §6.6, ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8. **Mounting holes shall be provided and standoffs populated** — the pin map names M04 among the heavier modules | Pin map, header section |
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
| Frame read | Chunked, with the main loop serviced between chunks: one read is 37.5 ms of blocking I²C at 400 kHz, eight times a second, and the node owes a 1 Hz heartbeat and the file service throughout (§5.2). O-91 |
| Flat field | Read U2 at boot, validate the CRC and match the header's device ID against U1's (§6.7). Apply per pixel after compensation, before the statistics and the accumulation. A missing, corrupt or foreign field is reported, not silently ignored, and publication continues uncorrected |
| Statistics | Per §6.4, over the valid-pixel set of §6.2 |
| Frame validity | A frame whose subpage handshake was missed, or whose Ta moved by more than 2 K from the previous frame, is excluded from the interval accumulation and from the 1 Hz record. The count that did contribute is served in the frame header (§10.2) |
| Interval accumulation | Per §6.5, by Welford or by deviations from a per-pixel reference. **`Σx² − n·x̄²` is forbidden** — §6.5 gives the arithmetic |
| Interval alignment | Interval boundaries aligned to the minute of the gateway time base while synchronized; free-running before the first sync pair, with the header's interval bounds telling the reader which it was |
| **Arithmetic precision** | The compensation chain of §6.2 shall be **single precision throughout** — `sqrtf`, `f`-suffixed literals, no implicit promotion. The STM32F405 has no double-precision unit: the same expressions in `double` cost ≈ 21 ms per frame against ≈ 1 ms (§10.3), which hides at 1 Hz and saturates the node at any higher-rate mode |
| Event frame | Offered when the hotspot count crosses `n_event`, or `t_max` exceeds `t_mean + delta_event`; both deployment constants, defaults 8 pixels and 5 K. At most one event frame per interval, and it carries the last complete frame alone (§6.5). O-98 |
| Not derived | Canopy-to-air differential, leaf VPD, DLI — gateway or deferred (§3.2) |
| Deployment constants | ε, Tr, `k` and `delta_min` read from the deployment profile, carried as the `uavcan.register` entries `industryflow.greenhouse.plant.emissivity`, `.reflected_temperature`, `.hotspot_k` and `.hotspot_delta_min`. Volatile: no register but `uavcan.node.id` has a store (ADR-0005 d7), so a commissioned node re-takes them at every restart. The uncommissioned state is ε = 1 and Tr = Ta − 8 K, and the record publishes the values in force |
| Compute and memory budget | §10.3 |
| Message timestamps | `uavcan.time.SynchronizedTimestamp` from the gateway time base, as M01, M02 and M05 |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m04_plant/` when written. **No firmware exists for this class**; neither stream of ADR-0005 d11 is implemented |
| Node-ID | Not a property of the module class: provisioned per instance into carrier flash (ADR-0027), and distinct across the bus. Bring-up assignment for the first instance is **99** |
| Publication rate | 1 s for the statistics record; 1 min, or on event, for the served frame (§10.2) |

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
| `flat_field` | `bool` | A flat-field trim bound to this U1 is in force (§6.7) |
| `frame_seq` | `uint32` | Sequence of the interval frame currently served (§10.2). The record's own statistics are for the last device frame, not the interval |
| `frame_available` | `bool` | An interval frame for this sequence is served at §10.2 (ADR-0005 d11, d12) |

Record size ≈ 140 B, ≈ 20 Cyphal/CAN frames at 1 Hz.

### 10.2 Served frame

| Item | Requirement |
|---|---|
| Mechanism | `uavcan.file.Read` — the node serves, the gateway reads (ADR-0005 d11). No type is minted |
| Trigger | The gateway reads on `frame_available` changing, not by polling |
| Cadence | **One interval frame per minute** (ADR-0005 d12), plus an event frame under §10's event rule |
| Path | `/plant/frame.bin`, fixed |
| Content | The 64 B header below, then the mean plane — 768 × `float32` kelvin, row-major from pixel 1 (row 1, column 1), little-endian — then the σ plane, 768 × `uint8`. **3904 B total** |
| Compensated, not raw | Required by ADR-0005 d12; the raw device image is never served |
| Hold policy | The served file is replaced only at the next offer. A read spanning a replacement is detected by the gateway from the header's `frame_seq` and re-read |
| Cost | ≈ 670 Cyphal/CAN frames per minute at 500 kbit/s, ≈ 200 ms — **0.34 % mean occupancy** |
| Archive rate | 3904 B/min = **5.62 MB per day, 2.05 GB per year, per instance.** Pre-cloud the gateway is the primary durable sink (ADR-0020 d1, d10). O-97 |

Header, little-endian throughout:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic, `IGP1` |
| 4 | 1 | Format version, 1 |
| 5 | 1 | Planes present — bit 0 mean, bit 1 σ |
| 6 | 1 | Columns, 32 |
| 7 | 1 | Rows, 24 |
| 8 | 4 | `frame_seq`, matching the summary record that announced it |
| 12 | 8 | Interval start, µs, gateway time base |
| 20 | 8 | Interval end |
| 28 | 2 | `n_frames` accumulated — 1 marks an event snapshot (§6.5) |
| 30 | 1 | Defective-pixel count |
| 31 | 1 | Flags — bit 0 `stabilized`, bit 1 event-triggered, bit 2 time base unsynchronized, bit 3 flat field applied (§6.7) |
| 32 | 6 | **Device ID**, EEPROM `0x2407`–`0x2409` (§10) |
| 38 | 2 | Reserved |
| 40 | 4 | Emissivity, `float32` |
| 44 | 4 | Reflected temperature, `float32` K |
| 48 | 4 | Ta mean over the interval, `float32` K |
| 52 | 4 | Mean-plane format — `1` = `float32` kelvin, unscaled |
| 56 | 4 | σ-plane scale, `float32` K/LSB = 0.02 |
| 60 | 4 | CRC-32 over bytes 0…59 and 64…3903 |

The header repeats the device ID and the constants in force: a stored frame is interpretable
without the record that announced it, and the transfer CRC does not survive storage.

### 10.3 Compute and memory budget

STM32F405 at 168 MHz, Cortex-M4F with a **single-precision** FPU. Per second at the 8 Hz refresh
rate of §6.1 — four complete frames per second:

| Work | Cost | Share of the second |
|---|---|---|
| I²C, eight subpage reads at 400 kHz | **300 ms** (§5.2) | **30 %** — blocking I/O, not compute |
| Compensation, 4 × 768 pixels, DS12 §11.2, single precision | 4 × ≈ 154 k cycles, **≈ 4 ms** | 0.4 % |
| *The same in `double`* | *4 × ≈ 3.5 M cycles,* **≈ 84 ms** | *8 %* |
| Statistics of §6.4, on the 4-frame mean | ≈ 17 k cycles, 0.1 ms | 0.01 % |
| Interval accumulation, §6.5, four frames | ≈ 48 k cycles, 0.3 ms | 0.03 % |
| Flat-field subtraction, §6.7, four frames | ≈ 9 k cycles, 0.05 ms | 0.01 % |
| Once per minute: σ plane, CRC-32 | ≈ 58 k cycles, 0.35 ms | — |
| Once per minute: serving ≈ 670 CAN frames | ≈ 3 ms | — |

**Compute totals ≈ 4.4 ms of every second — 0.44 %.** The binding constraint is the 300 ms/s of
blocking I²C (§5.2, O-91). The per-minute archive adds no I²C traffic — every frame it averages
was already read for the statistics.

`VSQRT.F32` and `VDIV.F32` are 14 cycles each on the M4F, against ≈ 500 cycles per software
square root and ≈ 70 per multiply. `float32`'s ULP at 300 K is 0.00003 K, against a 0.025 K
interval floor and a 16-bit device word — it carries more digits than the instrument produces.
`double` returns none of them and costs 84 ms/s here, and 336 ms/s at 16 Hz on top of that mode's
600 ms/s of I²C, where the node stops closing its loop.

| RAM | Size |
|---|---|
| Restored calibration parameters — α 1536 B, offset 1536 B, Kta 768 B, Kv 768 B, scalars ≈ 100 B | 4 708 B |
| Device frame buffer, 834 × `uint16` | 1 668 B |
| Compensated field, 768 × `float32` | 3 072 B |
| Interval mean accumulator, 768 × `float32` | 3 072 B |
| Interval M2 accumulator, 768 × `float32` | 3 072 B |
| Flat field, 768 × `int16` (§6.7) | 1 536 B |
| Served file buffer | 3 904 B |
| **Persistent total** | **21 032 B** |
| Transient at start-up — U1's EEPROM image, 832 × `uint16` | 1 664 B |
| **Peak** | **22 696 B** |

Against 128 KB SRAM plus 64 KB CCM on the STM32F405. **The device frame buffer shall not be placed
in CCM** if the I²C is ever moved to DMA: the F4's DMA controllers cannot reach CCM.

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.3 | Frame accuracy against a reference target of known emissivity filling the field, at three temperatures spanning 10…30 °C, after the 4 min settling of T4, with the reference read by a contact thermometer. Frame mean within ±1 °C of the reference; per-pixel spread against the zone figures |
| V2 | §9 M1, §6.6 | Installed footprint measured against the §6.6 table at the installed height, with a warm target moved to each edge of the field; nothing in the frame that is not the intended scene |
| V3 | §5.1, §5.2, §10 | Subpage read time measured at 400 kHz against 37.5 ms, and bus occupancy logged over ≥ 1 h at the 8 Hz refresh rate with zero I²C errors — including the EEPROM restore, which runs at the same rate as its 400 kHz ceiling. Heartbeat continuity and file-service responsiveness confirmed **during** frame reads |
| V4 | §6.1, T2, T3, T4 | Cold power-on logged for ≥ 30 min: Ta and frame mean against a fixed reference target, drift across the first 4 min recorded, and the `stabilized` flag observed to clear. Ta compared against canopy air from an M01 instance in the same volume, against T1's 8 K offset |
| V5 | §4.1 | 1:1 paper printout against the physical part, including the index tab and the lead circle |
| V6 | §5 | Module-ID readback of `0b100` on the carrier in use |
| V7 | §7.1 | Node draw measured on `+12 V` against the 360 mW estimate, and U1's own current against 25 mA |
| V8 | §10.2, §6.4 | An interval frame read by the gateway, its header CRC checked, and the frame-wide mean of its mean plane matched against the arithmetic mean of the 60 `t_mean` values published during that interval, within one quantization step. An event frame read and confirmed to carry `n_frames` = 1 |
| V9 | §6.2 | Defective-pixel list read from the device EEPROM, the count published, and the excluded pixels confirmed absent from the statistics and interpolated in the served frame |
| V10 | §6.2, §6.6 | ε and Tr identified at commissioning against a reference target at canopy distance; the identified values recorded as deployment metadata together with the mounting height |
| V11 | §7.3 | Rail measured at U1's VDD pin under load — DC value against 3.3 V ±0.05 V, ripple against the buck's 400 kHz fundamental |
| V12 | §10 | U1 identified by the three device-ID words at `0x2407`–`0x2409`, non-zero and stable, not by address ACK alone |
| V13 | §6.5, §6.3 | 240 consecutive device frames logged raw alongside the interval frame they produced: the mean plane reproduced offline bit for bit. Per-pixel temporal noise measured against a fixed target — per frame against the 0.36 K of §6.3, and over the interval against 0.025 K. A warm target then swept through part of the field during one interval: the σ plane shall rise on the swept pixels and stay at the noise floor elsewhere |
| V14 | §6.5, §10.3 | Compensation of one frame timed on the target with the cycle counter against the ≈ 1 ms of §10.3, and the build checked for double promotion. A synthetic constant-temperature sequence pushed through the accumulator shall return σ = 0, not noise and not a negative variance |
| V15 | §6.1 | 19-bit resolution confirmed not to saturate the ADC across the operating scene, by imaging the hottest surface in the growing volume — the luminaire — at the mounting distance and confirming no pixel rails. The resolution-control coefficient confirmed applied by comparing one frame computed at 18 and at 19 bit against the same scene |
| V16 | §6.7 | A flat field written over the bus, read back from U2 after a power cycle, and confirmed applied by the record's `flat_field` flag and the frame header. A field carrying a foreign device ID offered to the node and confirmed **refused** and reported. Non-uniformity measured against a uniform target before and after, to record what the correction actually bought |

## 12. Open items

Continues the `O-` namespace shared with the M01, M02, M05, M06, M07 and service-tool
specifications. O-2, O-4, O-6, O-8, O-10, O-11 and O-42 are M06's; O-12 to O-24 are M07's;
O-25 to O-31 are M05's; O-3, O-5, O-7, O-9, O-32 to O-51, O-73 and O-75 are M01's; O-52 to O-72
are M02's; O-74 is M05's; O-76 to O-85 are the service tool's.

| ID | Item | Blocks |
|----|------|--------|
| O-86 | Mounting height and the canopy area one instance covers are not fixed: ADR-0006 defers cabinet dimensions to a mechanical specification, so the selection rule of §6.6 has no input | FOV variant confirmation, M5, M7, V2 |
| O-87 | Leaf emissivity, reflected apparent temperature and the canopy-to-air differential band of §3 are not established. ε = 1 and Tr = Ta − 8 K are placeholders, not values | Absolute canopy temperature, expected range, V10 |
| O-88 | No canopy segmentation exists, so §6.4's statistics are frame-wide and include structure, medium and luminaire pixels. ADR-0014 d4 defers the pipeline | Meaning of every statistic, leaf VPD, hotspot interpretation |
| O-89 | DS12 states an additional ±3 °C over years for objects around room temperature — the operating point of §3 — with no re-referencing scheme. Whether the module is periodically referenced against a known target, against M01's air temperature at a settled night point, or replaced on an interval | Absolute accuracy over service life, calibration protocol |
| O-90 | The device carries no humidity rating in DS12 and its optical surface is exposed in a volume that may condense. Post-condensation validity, recovery and M4's inspection interval are undefined | Excursion handling, data validity, maintenance procedure |
| O-91 | One frame read blocks the I²C driver for 150 ms and the module reads twice a second. §10's chunking requirement is stated but unimplemented and unverified, and the carrier driver's blocking I²C is its precondition | Heartbeat continuity, file service, V3 |
| O-92 | DS12 defines the accuracy zones graphically with no pixel-index boundaries, and gives no per-pixel angular map or lens-distortion figure. Neither per-zone weighting of the statistics nor an exact pixel-to-position mapping can be stated | §6.3 zone application, §6.6 geometry |
| O-93 | The assembly route for a four-lead through-hole hermetic can on an otherwise SMT board is not fixed, and DS12 states no MSL and no soldering profile | Fab package, assembly quote, M6 |
| O-94 | Node power unmeasured | Distribution-board sizing, O-31 |
| O-95 | Rail deviation and ripple at the module unmeasured against §7.3's ±0.05 V recommendation | V11, absolute accuracy |
| O-96 | No IR-transmissive window is specified, and DS12 states no spectral passband against which one could be qualified. Whether the module looks into the growing volume unglazed, or an enclosure window is qualified by measurement | Enclosure design, M2 |
| O-97 | The frame archive is 5.62 MB per day per instance (§10.2), against an ADR-0020 d2 buffer whose bound is set in time and sized for scalar telemetry. Pre-cloud the gateway is the primary durable sink, so retention, downsampling and export of the archive are unowned | Gateway storage sizing, ADR-0020 d2 |
| O-98 | The event-trigger constants `n_event` and `delta_event` (§10) have defaults but no basis: no record establishes what canopy excursion is worth a frame | Event path, V8 |
| O-99 | The flat-field trim of §6.7 has a store (ADR-0028 d10) and no **determination protocol**: the project owns no uniform reference target, and whether a one-point field suffices across the 8…32 °C band or a two-point field is required is unsettled. Until it is determined, ±0.5 K of fixed-pattern non-uniformity stands (§6.3) | Spatial accuracy, the `-CP` / `-CC` records, V16 |

## 13. Maturity

**Pre-schematic.** The complement is fixed by ADR-0014 d4 and every device value here is stated
from DS12. `E0005` has no schematic, no layout, no fab package and no firmware, and the `plant`
DSDL types do not exist.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | ADR-0014 d4 fixes the part ✔; DS12 values transcribed ✔ |
| **Schematic captured** | Parts fixed to ordering part numbers; schematic exists; component values determined | FOV variant confirmed against O-86; U2's ordering part fixed; `E0005-000001` schematic exists |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-93 and O-96 closed; V5 executed |
| **As-built** | Estimates replaced by measured values; §11 executed | `E0005` fabricated and bench-verified; O-94 and O-95 measured |
