<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M02-LIGHT — module specification

- **Status:** Working specification. `E0003-000001` schematic carries the ADR-0014 rev 6 complement; the layout does not
- **Date:** 2026-08-23
- **E-number:** `E0003` · module-ID strap `0b010`
- **Governing ADRs:** ADR-0014 (rev 6), ADR-0002 (rev 3), ADR-0003, ADR-0005 (rev 1), ADR-0016, ADR-0017 (rev 2)
- **Companions:** `M01-CLIMATE-specification.md`, `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M02-LIGHT sensor module: sensor complement, measured quantities and their
derivation, electrical, optical, mechanical and firmware requirements, and their verification.

Not specified here: carrier design (`store/E0001-000003-D-pinmap.md`), cultivation setpoints
(ADR-0003 and `profiles/strawberry-day-neutral-v1.json`), the luminaire and its multi-channel
driver (no actuator module exists), gateway-side handling of published subjects (ADR-0014 d7).

## 2. Identification

| | Value |
|---|---|
| Module class | M02-LIGHT |
| Module-ID strap | `0b010` — STRAP_0 low, STRAP_1 high, STRAP_2 low |
| E-number | `E0003` — fully-populated assembly |
| Bare design | One layout; each standard populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) |
| Carrier | `E0001`, sensor-module header pair 2×12 + 2×8 (ADR-0014 d5) |

## 3. Function

M02 measures the photic environment at the canopy, inside the growing enclosure, facing the
luminaire.

Expected values below are for the reference cultivation profile
`profiles/strawberry-day-neutral-v1.json`, which is the source of truth for setpoints
(ADR-0000 d2): photoperiod 16 h on / 8 h off, transition ramp 30 min at each boundary, DLI
17–20 mol·m⁻²·d⁻¹ at the fruiting canopy, flowering/fruiting spectrum of warm white plus
660 nm red, 730 nm far-red and a 365–385 nm UV-A trace.

Mean canopy PPFD follows from the DLI target and the photoperiod: 17–20 mol·m⁻²·d⁻¹ over
16 h = **295–347 µmol·m⁻²·s⁻¹**, ≈ 65–76 W/m² PAR. This is the same figure M01's T1 carries.

| Published quantity | Sensor | Sensor range | Expected operating range | Accuracy |
|--------------------|--------|--------------|--------------------------|----------|
| Spectral counts, 12 bands, 405…855 nm | U4 | 0…65 535 counts per band, 16-bit | 10…90 % of full scale, held there by autorange (§6.1); zero in the dark period | Relative between bands; absolute only through §6.2 |
| Clear channel (unfiltered Si) | U4 | 0…65 535 counts | As above | Relative |
| PPFD (derived, §6.2) | U4 | — | 295–347 µmol·m⁻²·s⁻¹ mean over the photoperiod; 0…full scale across the 30 min ramps; 0 in the dark period | Dominated by the reconstruction coefficients, O-52 |
| Flicker flag, 50 / 60 Hz | U4 | Flags; ADC5 integrates over `t_int_FD = FD_TIME × 2.78 µs`, FD_TIME 11-bit, raw data optionally written to the FIFO | No flicker expected from a DC-driven fixture; an asserted flag is a driver fault indication | Flag, not a measurement |
| UV-A irradiance, 315…400 nm | U3 | 0…65 535 counts, 20-bit ADC with gain 0.5×…4096× | Trace level per profile; absolute value not set by the profile | 82.8 counts/(µW·cm⁻²) at 365 nm, gain 1024×. **Typical only**, §6.4 |

Publication rate: seconds. The full channel set requires **three integration cycles** — U4 has
six ADCs and fourteen channels, and the SMUX maps at most six per cycle (§6.1).

Location: canopy plane, in the luminaire's line of sight. Relocatable to any canopy point.
All sensors board-mounted; no leads, no cable-borne I²C.

### 3.1 Environmental envelope of the populated module

| | U4 AS7343 | U3 TSL2585 | **Module** |
|---|---|---|---|
| Temperature | −30…+85 °C operating free-air | −30…+85 °C operating free-air | **−30…+85 °C**, set by both sensors |
| Humidity | 5…85 %RH non-condensing | 5…85 %RH non-condensing | **5…85 %RH non-condensing** |
| MSL | 3, 168 h floor life | 3, 168 h floor life | **3** |
| Reflow body temperature | 260 °C peak; ≤ 60 s above 217 °C, ≤ 50 s above 230 °C | 260 °C peak, IPC/JEDEC J-STD-020 | **260 °C peak** |

§3 places the module in the growing volume, which M01 §3 admits may condense on excursions.
That is outside U4's stated operating conditions, not merely outside its accuracy. The
consequence is an unquantified post-excursion validity gap of the same class as O-47. O-57.

### 3.2 Exclusions

The following are outside M02's scope and are specified by the module class named, or by the
layer named.

| Quantity | Owning class or layer |
|----------|-----------------------|
| Air temperature, RH, VPD, CO₂, VOC at the canopy | M01-CLIMATE |
| Canopy surface temperature, leaf VPD | M04-PLANT |
| Ambient irradiance outside the enclosure | M07-AMBIENT |
| Bus voltage and current, door, leak | M05-SAFETY |
| **DLI** — the integral of PPFD over the photoperiod | **Gateway** (ADR-0014 d4, ADR-0016 d5) |
| Luminaire channel drive and spectrum command | No actuator module exists (ADR-0014 d9) |

M02 carries no temperature, humidity, gas, pressure or air-velocity sensor, and no ambient-light
channel — U3 measures UV only.

### 3.3 Partial populations

Optional parts shall be footprints that can be left unpopulated with no other change to the
board. U3 shall not sit in the I²C pull-up path or in the sense path of any other part. U1 and
U2 are not optional: U1 carries U4's bus segment (§5.2) and U2 carries the rail both sensors run
on (§7.3), so neither may be depopulated with U3.

A partially populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) and
requires no firmware change; the boot probe registers responders only (§10).

The U4-only population is the expected variant wherever the fixture carries no UV-A channel.
A population without U4 has no function and is not a defined configuration.

## 4. Sensor complement

| # | Device | Ordering part | Function | Rail | I²C address | Address type |
|---|--------|---------------|----------|------|-------------|--------------|
| U4 | ams OSRAM AS7343 | `AS7343-DLGM` | 11 filtered bands 405…855 nm, clear, flicker | **1.8 V** | `0x39` | Fixed |
| U3 | ams OSRAM TSL2585 | `TSL25853PM` | UV-A 315…400 nm, photopic, IR, flicker | **1.8 V** | `0x39` | Fixed |

Both parts are named by ADR-0014 d4 as revised in rev 6. Neither address is selectable, and they
are the same address: **U3 and U4 collide, and are separated onto two channels of U1** (§5.2).
This is the reason a bus switch is on the module at all.

Neither device occupies the `0x50`–`0x57` block ADR-0014 rev 4 d6 reserves for the module-ID
EEPROM; U1 sits at `0x70` (§4.2). M02 identifies by strap (§5) and carries no EEPROM.

U3 requires no external reference component. The AS7331 named in ADR-0014 rev 5 required a
precision REXT pair; that part and both resistors are withdrawn with it (§12, O-63).

U4 is the primary photic source. U3 measures UV only and contributes nothing to the PPFD
computation (§6.2), whose band is 400…700 nm. U3's photopic and IR channels are not published
(§10.1): they exist on the die to support the vendor's UV-index algorithm, and U4 already
measures the visible spectrum with eleven bands.

### 4.1 Packages

| # | Package | Body size | Notes |
|---|---------|-----------|-------|
| U4 | OLGA-8 | 3.10 × 2.00 × 1.00 mm | Aperture **Ø0.900 mm, centred 0.609 mm from the package centre** along the 3.10 mm axis — DS001046 v6-00 Figure 67, `℄ PART to ℄ ALS`. Not concentric with the package, and not the AS7341's offset. Half cone angle on the sensor **40°** (DS001046 v6-00 §7) |
| U3 | OLGA-6 | 2.00 × 1.00 × 0.35 mm | 6 pads in 2 rows × 3 columns; 0.650 mm column pitch along the long axis, 0.525 mm between rows; pads 0.400 × 0.275 mm. The photodiode group sits at the pin-1 end and is **not** concentric with the package. Land pattern per DS001043 v5-00 Figure 96; package outline Figure 95. The drawing dimensions the die as centred within ±75 µm but does not dimension the photodiode group, so its offset is a measurement, O-70. Angular response: half maximum at ≈ ±45° on both axes, M9 |

Every footprint shall be checked against the physical part with a 1:1 paper printout before
ordering (V6). The check shall include U4's aperture position relative to the pads — the
aperture does not appear in the schematic.

### 4.2 Supporting active parts

| # | Part | LCSC | Function |
|---|------|------|----------|
| U1 | `TCA9543APWR` | C2653307 | Two-channel bidirectional translating I²C switch (§5.2). Separates U3 and U4, which share `0x39`, and performs the 3.3 V ↔ 1.8 V translation the PCA9306 previously performed for U4 alone. TSSOP-14, address `0x70` with A0 and A1 to GND |
| U2 | `TLV70018DCKR` | C133796 | 1.8 V rail for U4 VDD (§7.3). Same part as M01's U4 |

Sensor ordering codes: U3 `TSL25853PM` is LCSC `C17428292`; U4 `AS7343-DLGM` is `C19085986`,
31 in stock at 2026-08-23 and the module's dominant BOM line. Passives: the 4.7 kΩ 0805
pull-ups are `C17673` and the 0 Ω 0805 strap links `C17477`, both JLCPCB **Basic** library, so
neither draws an extended-part loading fee.

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U4, U3 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b010`: STRAP_0 low, STRAP_1 high, STRAP_2 low |
| Header B pin 1 / 2 — 3V3, GND | Strap reference |

No other header signal is claimed. `GPIO_1`/`GPIO_2` (PA9/PA10) are not used, so USART1 remains
available as the carrier debug console throughout boot and run (pin map note 6). U4's `INT`,
`GPIO` and `LDR` pins are not routed to the header; `LDR` and `GPIO` are left unconnected per
the device pin description, and §10 polls.

**M02's strap pattern has bit 1 = 1.** Module-ID bit 1 (`STRAP_1`, PA6) reaches the MCU from
carrier revision `E0001-000003` (`firmware/common/carrier/e0001.h`). `E0001-000001` and
`E0001-000002` carry it only with the `J6` pad 4 → `J3` pad 15 link added by hand; without that
link M02 reads back as `0b000` — the reserved value, not another class.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, **one master segment and two switched channels**, all devices board-mounted. The header and U1's own interface sit on the 3.3 V master segment; U4 sits alone on channel 0 and U3 alone on channel 1, both at 1.8 V |
| Speed | 100 kHz standard mode. Platform default set by the shared carrier driver, not by any device on this module: U4 supports 400 kHz, U3 1 MHz, U1 400 kHz. **U1 is the slowest device on the module and sets the ceiling at 400 kHz** |
| Pull-ups | Every segment carries its own, on the module; the carrier carries none. O-51. Master: R2, R3, 4.7 kΩ to 3.3 V. Channel 0: R6, R7, 4.7 kΩ to 1.8 V. Channel 1: R1, R4, 4.7 kΩ to 1.8 V |
| Sizing check | 4.7 kΩ sinks 0.70 mA at 3.3 V and 0.38 mA at 1.8 V, against the 6 mA sink condition both sensors state for V_OL = 0.4 V, and against U3's stated 500 Ω pull-up minimum. Rise time at 100 kHz admits ≈ 250 pF per segment, against board traces, the 10 pF input capacitance each sensor presents and U1's channel capacitance |
| Population | All three pull-up pairs are fitted in every population (§3.3). Channel 1's pair stays fitted with U3 absent: it terminates a switched channel, not a sensor |
| Address separation | **None on the device addresses.** U3 and U4 are both `0x39` and are separated topologically by U1 (§5.2). U1 is `0x70`. No device on this module occupies `0x50`–`0x57` |

### 5.2 Address separation and logic levels

Two devices at one address cannot share a segment. U1 gives each its own channel, and firmware
selects exactly one channel per transaction (§10). **U1 powers up with both channels
deselected**, so `0x39` is unreachable until firmware writes U1's control register — a missing
channel selection presents as both sensors absent, not as a bus error (O-65).

The same switch performs the level translation. U4 has **no VDDIO pin**: its digital pins are
referenced to GND and the device is supplied at 1.8 V (§7.3) while the carrier drives the master
segment at 3.3 V. U3 does have a separate I/O supply range, 1.62…3.3 V, and could have taken
3.3 V directly; it is held at 1.8 V so that both channels share one rail and one analysis.

| | Value | Source |
|---|---|---|
| SCL / SDA / GPIO / INT absolute maximum | 3.6 V | Absolute maximum ratings, separate from the 1.98 V V_DD maximum |
| V_IH min | 1.26 V | Electrical characteristics |
| V_IL max | 0.54 V | Electrical characteristics |
| V_OL max | 0.4 V at 6 mA sink | Electrical characteristics |
| Input pin capacitance | 10 pF | Electrical characteristics |
| Leakage into SCL / SDA / INT | ±5 µA | Electrical characteristics |

A 3.3 V segment would satisfy V_IH and stay inside the 3.6 V digital-pin rating with the pins
above V_DD, but the device pin description directs that SCL, SDA and INT be pulled up to 1.8 V
and no recommended-operating-condition table covers pin voltages above V_DD. U1 removes the
question.

U3's limits are the same numbers, from its own datasheet: V_IH 1.26 V, V_IL 0.54 V, V_OL 0.4 V
at 6 mA sink, input capacitance 10 pF, leakage ±5 µA, digital I/O absolute maximum 3.6 V.

| U1 | Requirement |
|---|---|
| Part | `TCA9543A`, two-channel bidirectional translating pass-gate switch with interrupt logic and reset (§4.2) |
| V_CC | **1.8 V**, from U2. The pass-gate clamp V_pass shall sit at or below the lowest bus voltage on the device; at V_CC 1.65…1.95 V the datasheet states V_pass 0.5…1.1 V, below the 1.8 V channel rails. Each segment reaches its own high level through its own pull-up, not through the switch |
| Master side | SCL, SDA at 3.3 V. U1's inputs are 5.5 V tolerant and take V_IH = 0.7 · V_CC = 1.26 V, so a 3.3 V master drives a 1.8 V-powered switch with no further translator |
| Address | `0x70`. A0 and A1 tied to GND |
| RESET | Tied to V_CC. No GPIO is routed to it; the state machine is recovered by a power cycle (§10) |
| INT0, INT1 | Tied to V_CC. Both are inputs, neither sensor's interrupt is routed (§5), and a floating input is not permitted |
| INT | Output, unconnected |
| Channels | Channel 0 — SC0, SD0 — carries U4. Channel 1 — SC1, SD1 — carries U3 |
| R_ON | 10 Ω min / 25 Ω typ / **70 Ω max** at V_CC 1.65…1.95 V — two decades above the PCA9306's 3.5 Ω. At the 1.08 mA a channel and the master segment sink together it adds 76 mV worst case to the low level a sensor presents to the carrier, against a 0.99 V V_IL at the MCU. Both sensors state V_OL at a 6 mA sink; actual sink is 1.08 mA, so the device contribution is far below 0.4 V |
| Capacitance | C_io(OFF) 19 pF max on SCL/SDA, 8 pF max on the channel pins; inside the ≈ 250 pF per-segment budget of §5.1 |
| Pull-ups | Required on the master side and on each channel; sized in §5.1 |
| Decoupling | 100 nF at V_CC (C4) |

## 6. Measurement requirements

### 6.1 Spectral acquisition and autoranging

| Item | Requirement |
|------|-------------|
| ADC | Six independent 16-bit light-to-frequency converters; full scale 65 535 counts per channel |
| Channel mapping | The device's own three-cycle sequencer, not a written SMUX image. Register, cycle contents and when it is written: §10 |
| Cycles per full set | **Three.** Eleven filtered bands + clear + flicker exceeds six ADCs by more than one cycle |
| Integration time | `t_int = (ATIME + 1) × (ASTEP + 1) × 2.78 µs` — DS001046 v6-00 §7 note 4. ATIME at `0x81`, ASTEP at `0xD4`/`0xD5`; neither may be 0 |
| Gain | AGAIN 0.5× … 2048×, 13 steps, CFG1 register `0xC6` bits AGAIN[4:0] |
| Autorange | Firmware shall hold the brightest mapped channel between 10 % and 90 % of full scale by adjusting AGAIN and `t_int` (§10) |
| Published with every sample | The AGAIN and `t_int` in force. Counts without them are not a measurement |

Working point, from the datasheet responsivity anchor — typical counts at AGAIN 1024×, `t_int`
27.8 ms, with each channel illuminated by a monochromatic LED in its band at
E_e = 155 mW/m². Saturation irradiance per channel is `65 535 / R_e × 155 mW/m²`:

| Channel | R_e typ [counts] | Saturation irradiance at AGAIN 1024× | at AGAIN 64× |
|---------|------------------|--------------------------------------|--------------|
| NIR 855 nm | 10 581 | 0.96 W/m² | 15 W/m² |
| F1 405 nm | 5 749 | 1.77 W/m² | 28 W/m² |
| F7 690 nm | 5 435 | 1.87 W/m² | 30 W/m² |
| FXL 600 nm | 4 776 | 2.13 W/m² | 34 W/m² |
| FY 555 nm | 3 747 | 2.71 W/m² | 43 W/m² |
| F6 640 nm | 3 336 | 3.05 W/m² | 49 W/m² |
| F4 515 nm | 3 141 | 3.23 W/m² | 52 W/m² |
| FZ 450 nm | 2 169 | 4.68 W/m² | 75 W/m² |
| F5 550 nm | 1 574 | 6.45 W/m² | 103 W/m² |
| F2 425 nm | 1 756 | 5.78 W/m² | 92 W/m² |
| F8 745 nm | 864 | 11.8 W/m² | 189 W/m² |
| F3 475 nm | 770 | 13.2 W/m² | 211 W/m² |

Canopy PAR is 65–76 W/m² total (§3), distributed across the bands by the fixture's spectrum.
AGAIN 1024× saturates every channel; **AGAIN ≈ 64× is the expected full-output working point**,
and the exact setting depends on how the fixture distributes power across the bands, which is
not known before commissioning. This bounds the working point; it is not a calibration. The
30 min ramps and the dark period are what the autorange requirement exists for.

### 6.2 PPFD

PPFD is a photon count over 400–700 nm. U4 delivers filtered band counts at 405…690 nm centres
within that window, plus 745 nm and 855 nm outside it.

```
PPFD = Σ c_i · n_i          i = F1 (405) … F7 (690)
```

| Item | Requirement |
|------|-------------|
| `n_i` | Band counts normalized by AGAIN and `t_int` (§6.1) |
| `c_i`, reconstruction coefficients | Commissioning parameters identified per luminaire spectrum against a reference quantum sensor, carried in the deployment profile, not compiled in. O-52 |
| Bands excluded from the sum | F8 (745 nm) and NIR (855 nm) are outside the PAR window and shall not enter it. They are published for spectrum validation (§6.3) |
| Validity | The coefficients hold for the spectrum they were identified under. A profile phase change (§3, vegetative vs flowering) changes the spectrum and requires its own coefficient set |
| Accuracy | Not bounded by the device. Dominated by `c_i`. O-52 |

### 6.3 Spectral coverage against the profile

Band half-maxima computed from the datasheet peak wavelengths and FWHM (typical).

| Channel | Peak | FWHM | Half-maximum span |
|---------|------|------|-------------------|
| F1 | 405 nm | 30 nm | 390…420 nm |
| F2 | 425 nm | 22 nm | 414…436 nm |
| FZ | 450 nm | 55 nm | 423…478 nm |
| F3 | 475 nm | 30 nm | 460…490 nm |
| F4 | 515 nm | 40 nm | 495…535 nm |
| F5 | 550 nm | 35 nm | 533…568 nm |
| FY | 555 nm | 100 nm | 505…605 nm |
| FXL | 600 nm | 80 nm | 560…640 nm |
| F6 | 640 nm | 50 nm | 615…665 nm |
| F7 | 690 nm | 55 nm | 663…718 nm |
| F8 | 745 nm | 60 nm | 715…775 nm |
| NIR | 855 nm | 54 nm | 828…882 nm |

Against the profile's flowering/fruiting spectrum (ADR-0003 d11):

| Profile channel | Covered by | Status |
|-----------------|-----------|--------|
| Warm white | F1…F7, FY, FXL, FZ | Covered; the PAR window's 400 nm edge falls inside F1 |
| 660 nm red | F6 (615…665 nm), F7 wing | Covered |
| 730 nm far-red | **F8 (715…775 nm)** | Covered |
| 365–385 nm UV-A | none of U4's bands — F1's half-maximum starts at 390 nm | U3's function, §6.4 |

Every channel of ADR-0003 d11 is observable by the populated module. This is the condition
ADR-0014 rev 5 changed the complement to obtain.

### 6.4 UV

| Item | Requirement |
|------|-------------|
| Profile band | 365…385 nm UV-A trace (ADR-0003 d11) |
| U3 UV channel | Band-pass filtered UV-A photodiode spanning 315…400 nm. The profile band lies inside it |
| Responsivity | 82.8 counts/(µW·cm⁻²) at 365 nm, ALS gain 1024×. **Typical only — the datasheet states neither minimum nor maximum**, so this is a nominal scale factor, not a guaranteed accuracy |
| Visible rejection | UV-to-photopic channel ratio 0.0 % under a 2700 K white source; UV-to-IR ratio 0.2 % under a 940 nm source. The luminaire's visible and far-red output does not enter the UV reading |
| Published | UV-A as one subject (§10.1), scaled by the nominal responsivity above |
| UV-B, UV-C | **Not measured.** ADR-0014 rev 6 withdraws both with the AS7331, and ADR-0003 d11 commands a UV-A trace only. The out-of-band emission check the AS7331's UV-B and UV-C channels supported no longer exists on this module |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured. O-59 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02) |
| Module contribution | 572 µA maximum with both sensors active and U1 switching (§7.2), all on the 1.8 V rail — ≈ 1.03 mW, drawn from 3.3 V through U2 as ≈ 1.89 mW. **No device dominates**: the AS7331 that did, at 2 mA and 3.3 V, is withdrawn by ADR-0014 rev 6. Node draw is still mostly the carrier and MCU |
| Burst reflected to `+12 V` | None. No device on this module has a burst load of the SCD41 class |

### 7.2 Device currents

| # | Device | Sleep | Idle | Active | Rail |
|---|--------|-------|------|--------|------|
| U4 | AS7343 | 0.7 µA typ / 5 µA max | 40 µA typ / 60 µA max | 210 µA typ / **280 µA max** | 1.8 V |
| U3 | TSL2585 | 0.7 µA typ / 5 µA max | 60 µA typ | 235 µA typ / **290 µA max** | 1.8 V |
| U1 | TCA9543A | 0.4 µA typ / 0.55 µA max | — | 2 µA at 100 kHz | 1.8 V |

U4's figures are stated at V_DD = 1.8 V and exclude current through the LDR pin, which this
module does not use. U3's active figure is the ALS state at gain 128× with three modulators
running; its idle figure is `LOWPOWER_IDLE = 1`. U1's operating figure is taken at its 1.65 V
column, the nearest tabulated point below the 1.8 V rail. §7.3 supplies 1.8 V, so all three
apply as written.

### 7.3 Module-local 1.8 V rail

| Rail | Source | Consumer | Reason |
|------|--------|----------|--------|
| 3.3 V | Header | The master-segment pull-ups, straps, U2 input | — |
| 1.8 V | U2, from 3.3 V | U4 V_DD, **U3 V_DD**, U1 V_CC, U1 RESET / INT0 / INT1, both channel pull-up pairs | U4's V_DD range is 1.7…1.98 V and 1.98 V is also its **absolute maximum** — 3.3 V destroys it. U3's range is the same 1.7…1.98 V. U1's V_CC additionally sets the pass-gate clamp (§5.2) |

U4's supply has no headroom above nominal: 1.8 V nominal against a 1.98 V absolute maximum
leaves 180 mV, so the regulator's initial accuracy, load and line regulation and its transient
overshoot shall be shown to stay inside it. Below nominal the margin is 100 mV to the 1.7 V
minimum. The same window now covers U3, which shares the rail. Load is 280 µA at U4, 290 µA at
U3 and 2 µA at U1, plus up to 1.54 mA drawn by the two channel pull-up pairs while all four bus
lines are held low — under 2.2 mA in all, two orders below U2's 200 mA rating, so no droop
budget applies. Local decoupling 100 nF at U4's V_DD pin (C7), 100 nF at U3's V_DD pin (C5) and
100 nF at U1's V_CC pin (C4).

**U4 sits behind a supply filter**, per DS001046 v6-00 §11.1: R5, 22 Ω, in series from the
1.8 V rail into C6, 4.7 µF, at U4's V_DD, with C7's 100 nF on the same side. The filtered node
is `+1V8_U4`; U3 and U1 stay on the unfiltered rail. Drop across R5 is 6.2 mV at U4's 280 µA
maximum, against 100 mV of headroom to the 1.7 V minimum. M10 keeps R5, C6 and C7 on the
opposite face from U4, so all three reach it through the via that serves U4's V_DD.

U2's own capacitors, per TLV700 (SBVS121): the output is stable on an **effective** capacitance
of 0.1 µF or more with ESR under 200 mΩ — effective meaning after bias and temperature derating,
not the nameplate — and the datasheet characterises the part at C_OUT = 1 µF. An input capacitor
is not required for stability, but 0.1…1 µF low-ESR is recommended and becomes necessary above
2 Ω source impedance. The board fits C1 + C2 = 2 × 10 µF at the input and C3 = 4.7 µF at the
output, both far above the minimum after derating. Both shall sit as close to the pins as the
layout allows, with the output capacitor's ground returned directly to U2's GND pin.

### 7.4 U3 supply

| Item | Requirement |
|------|-------------|
| Supply pin | V_DD (pin 1), decoupled 100 nF at the pin (C5) |
| Range | 1.7…1.8…1.98 V, the same window as U4 and from the same rail (§7.3). 1.98 V is also the **absolute maximum** |
| Ground | V_SS (pin 3). **One ground net.** The AS7331's split V_SSA / V_SSD, its ±0.3 V rail-to-rail absolute maximum and the 0 Ω joining link are withdrawn with the part; the module has no analog ground |
| External reference | **None.** The device needs no REXT and no precision component of any kind |
| I/O supply | The digital pins take 1.62…3.3 V independently of V_DD. This module holds them at 1.8 V (§5.2) |
| VSYNC / GPIO (pin 2) | Unconnected, and safe so. At reset `VSYNC_GPIO_INT` (`0xF8`) = `0x02`: `VSYNC_GPIO_IN_EN` = 0, so the pin is not an input, and `VSYNC_GPIO_OUT` = 1 leaves the open-drain output released HIGH — the datasheet's stated default, chosen to draw nothing through a pull-up. Firmware shall not set `VSYNC_GPIO_IN_EN` while the net is unconnected |
| INT (pin 5) | Unconnected. Open-drain output, unused, as U4's INT is; at reset `INT_IN_EN` = 0, so it is not an input either |
| Placement | **The sensing face carries the optical parts and nothing else.** C5 sits on the opposite face, directly behind U3, and the decoupling loop closes through vias. Keeping the face clear of every other part is an optical requirement (§9 M6) and outranks a same-side decoupling loop |

**M02 is the second module to generate a rail of its own.** O-43 records that module-local
rails sit outside ADR-0002 d3 and the header contract, and that one instance is not a pattern —
to be revisited at the second. This is the second. O-43 is now due.

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | The board sits in the beam at 65…76 W/m² PAR (§3) plus the fixture's non-PAR emission. Steady-state rise above canopy air shall be recorded at bring-up, not assumed | §3, V2 |
| T2 | **DS001046 v6-00 publishes no responsivity temperature coefficient**, only note 1 to its electrical characteristics: functionality varies with temperature across the operating range. No coefficient is applied. What the device does provide is auto zero of the spectral-engine **offsets** (§10) — that tracks offset drift, not responsivity. Responsivity against temperature stays uncharacterised, and V2 is the measurement that would quantify it | §10, V2 |

No heat source of consequence exists on the board: total sensor dissipation is below 1 mW, and
U2 dissipates 1.5 V × 572 µA = 0.86 mW. Incident radiation dominates, and it is a property of
the installation.

## 9. Optical and mechanical requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | An **achromatic diffuser shall be placed above U4's aperture.** DS001046 v6-00 §11.3 states it as a requirement, and the device has no integrated diffuser — the built-in feature is an aperture. The datasheet's optical characteristics are stated for the full response *including* a diffuser (note 1 to §6), so without one the responsivity behind §6.2's coefficients does not apply. **Kimoto OptSaver L-57**, a volume diffuser; parameters and the reason for choosing it over the 100 PBU are below | §6.2, §9.1 |
| M2 | The diffuser sits in the measurement path of every band of §6.2. The L-57 transmits **60 %**, a constant that enters every band and is absorbed by `c_i` at V1 rather than corrected separately. Residual wavelength dependence is folded into `c_i` the same way | §6.2, V1 |
| M3 | Neither U4's nor U3's aperture is concentric with its package; each enclosure window shall be aligned to the aperture, not to the package outline | §4.1 |
| M4 | No conformal coating over U4's aperture or U3's optical window | §4.1 |
| M5 | Conformal coating on all other areas. Installed in the growing volume: condensation possible on excursions | §3.1 |
| M6 | The sensing face shall be mounted in the canopy plane, normal to the fixture axis, unshaded by foliage or structure. Shading is the failure mode this module cannot detect | §3, ADR-0014 d7 |
| M7 | Mounted at canopy height; height adjusted as the canopy grows, recorded as deployment metadata. A height change invalidates the §6.2 coefficients | §6.2, ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8, supported at both ends; no standoffs required | Pin map, header section |
| M9 | U3's normalized angular response falls to half maximum at **≈ ±45°** on both axes, near-symmetric (DS001043 v5-00 Figures 13, 14). No window or aperture above U3 shall clip inside that cone. Both figures characterise the **photopic** channel against a white LED; the UV channel is not separately characterised, so the photopic curve is the bound window design shall use | §4.1 |
| M10 | **The sensing face carries U3 and U4 and no other part.** Every other component, the header pair included, sits on the opposite face. Nothing but the two apertures may stand in the canopy's line of sight | §7.4, M6 |

**M10 makes the board double-sided SMT.** The `-D-fab` package therefore carries a paste layer
and a position file for each side (ADR-0017 d18), and assembly takes two placement passes where
earlier revisions took one plus through-hole headers. The cost is accepted: a sensing face
carrying nothing but the two apertures needs no clearance check, no masking and no per-part
shading assessment against the enclosure window, which is what keeps final assembly simple.

### 9.1 Diffuser

AS7343 UG001009 v2-00 §4 names the two parts ams OSRAM ship on the evaluation kit and gives
their parameters. Both are Kimoto films:

| Parameter | 100 PBU | **OptSaver L-57** |
|---|---|---|
| Thickness | 125 µm | **100 µm** |
| Transmission | 66 % | **60 %** |
| Haze | 89.5 % | **93.1 %** |
| Half-angle | 35.5° | **57°** |

**L-57 is specified.** The guide separates surface diffusers, which suit a fixed geometry and
buy transmission by narrowing the radiation pattern, from volume diffusers, which are nearly
Lambertian and achromatic and give a reading that does not depend on the direction of the
incoming light. M7 adjusts the module's height as the canopy grows and ADR-0003 d11 changes the
spectrum by phase, so neither the geometry nor the spectral content is fixed — the case the
guide assigns to the volume type. The 100 PBU's spectral response additionally varies with
incidence angle (UG001009 Figure 17); the L-57 is the part the guide offers for better angular
consistency at otherwise similar figures, and its 57° half-angle sits closer to the ±70° that
DS001046 Figure 66 asks for than the 100 PBU's 35.5°.

The 6 percentage points of transmission given up against the 100 PBU cost nothing here: §6.1
autoranges on AGAIN and `t_int`, and the loss is a constant inside `c_i`.

M5 and M1 conflict in the same way M01's M4 does: a coating step over a diffuser is a coating
step over the measurement path. The diffuser is fitted after coating or masked during it. O-61.

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b010` — STRAP_0 low, STRAP_1 high, STRAP_2 low |
| Power-on delay | U4 NAKs deterministically for **200 µs typical** after V_DD crosses its POR threshold, and interrupts shall be ignored across that window (DS001046 v6-00 §8). U3 is ready to receive I²C commands 0.5 ms after power-on. U1 releases from power-on reset above V_PORR 1.5 V. The boot probe shall not issue a transaction before the longest of the three has elapsed |
| Bus channel | **U1 powers up with both channels deselected.** Firmware shall write U1's control register to select exactly one channel before addressing a sensor, and shall not leave both channels selected — `0x39` answers on both (§5.2). Channel 0 is U4, channel 1 is U3 |
| Boot probe addresses | `0x70` on the master segment, then `0x39` on each channel in turn. An ACK is not identification (M01 §10 precedent); each probe shall be backed by a device-specific read: **U4 `ID` at `0x5A` reads `0x81`** (with `REVID` `0x59` and `AUXID` `0x58`), **U3 `ID` at `0x92` reads `0x5C`** (with `REV_ID` `0x91` = `0x11`). Probing `0x39` without a channel selected shall be treated as a U1 fault, not as two absent sensors |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| Channel mapping | `auto_smux` = 3 in CFG20 (`0xD6`), written after every power-up with the spectral engine stopped. The device's ROM sequencer then walks the three cycles of §6.1 and files eighteen results; no SMUX RAM image is written and the vendor's manual SMUX configuration is not used. The cycles are fixed by the device: FZ FY FXL NIR VIS FD, then F2 F3 F4 F6 VIS FD, then F1 F7 F8 F5 VIS FD, into `DATA_0`…`DATA_17` in that order. V3 confirms the assignment against a real luminaire |
| U3 power-up state | U3 resets with its ALS disabled. Firmware shall write every configuration register — modulator SMUX, gains, sample count — then `PON`, then `AEN`. **`PON` follows the configuration, it does not precede it**: DS001043 v5-00 states `PON` is set only once the host has initialised all other registers. The UV channel is read at its own gain, independent of the photopic and IR channels |
| Auto zero | `AZ_CONFIG` (`0xDE`) sets how often the spectral-engine offsets are reset to track device temperature. `AZ_NTH_ITERATION` defaults to 255 iterations; one auto zero takes 15 ms typical. Firmware shall set the interval explicitly rather than inherit the default, and shall carry the 15 ms in the acquisition budget |
| Acquisition | Three integration cycles per full channel set (§6.1); flicker detection mapped to its own ADC when used |
| Autorange | AGAIN and `t_int` adjusted to hold the brightest mapped channel between 10 % and 90 % of full scale. The setting in force is published with the sample |
| Derived | PPFD per §6.2, from coefficients read out of the deployment profile, over F1…F7 only |
| Not derived | DLI. Owned by the gateway (§3.2, ADR-0014 d4) |
| Deployment constants | `c_i` per §6.2 read from the deployment profile, not compiled in. Carried as the `uavcan.register` entry `industryflow.greenhouse.light.ppfd_coeff`, a `real32[10]` in the §6.3 band order truncated to the PAR window. Volatile: no register but `uavcan.node.id` has a store (ADR-0005 d7), so a commissioned node re-takes them at every restart. All-zero is the uncommissioned state, and 4129 publishes `valid = false` under it |
| Rail failure | Failure of U2 removes U4, U3 **and U1** from the bus, so the module presents as a single absent device at `0x70` rather than as two absent sensors. The boot probe handles this as absence; *not fitted*, *failed* and *unpowered* remain indistinguishable (O-37) |
| U1 recovery | U1's RESET is tied to V_CC (§5.2), so a stuck channel is recoverable only by a node power cycle. Firmware shall report a channel that does not respond rather than attempt a reset it has no line for (O-65) |
| Message timestamps | `uavcan.time.SynchronizedTimestamp` from the gateway time base: the node is a synchronization slave to subject 7168 (ADR-0002 d11), tracking the master as an offset against its own monotonic clock. 0 (UNKNOWN) before the first pair of sync messages and again after the master has been silent for 3 s. Accuracy is milliseconds — reception is timestamped in the polled main loop |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m02_light/` — `sensors.{h,c}`, `module_id.h` and drivers for U1, U3 and U4. Written against the datasheets; no part of §11 is executed |
| Node-ID | Not a property of the module class: provisioned per instance into carrier flash (ADR-0027), and distinct across the bus. Bring-up assignment for the first instance is **98** |
| Publication rate | 1 s, subject to the three-cycle acquisition time of §6.1 |

### 10.1 Published subjects

Baked defaults in the unregulated range. ADR-0005 d7 makes these `uavcan.pub.<name>.id`
register entries. M05 holds 4096–4102; M01 holds 4112–4121.

| ID | Quantity | Source | Type |
|----|----------|--------|------|
| 4128 | Spectral counts, 12 bands + clear, with AGAIN and `t_int` | U4 | `industryflow.greenhouse.light.SpectralSample` |
| 4129 | PPFD | derived, §6.2 | `industryflow.greenhouse.light.PhotonFluxDensity` (mol·m⁻²·s⁻¹) |
| 4130 | Flicker flags | U4 | `industryflow.greenhouse.light.FlickerStatus` |
| 4131 | UV-A irradiance | U3 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |
| ~~4132~~ | ~~UV-B irradiance~~ — retired 2026-08-23 by ADR-0014 rev 6; U3 has no UV-B channel | — | — |
| ~~4133~~ | ~~UV-C irradiance~~ — retired 2026-08-23 by ADR-0014 rev 6; U3 has no UV-C channel | — | — |

Four subjects, not one record: a partial population must be able to omit U3's one
(ADR-0005 d8). U4's channels are one device read in one acquisition and are not separable, so
they are one subject.

**4132 and 4133 are retired, not reassigned.** The identifiers stay withdrawn so that a gateway
holding an older register set cannot silently bind a new quantity to an identifier it already
knows as UV-B or UV-C.

4131 keeps `Irradiance` in W·m⁻², computed from the nominal responsivity of §6.4. That figure is
typical-only, so the subject carries a nominal rather than a guaranteed absolute value; §6.4
states the limit and V4 measures it.

The `light` sub-namespace is named by ADR-0005 d1. All four types are
minted because the standard set carries no spectral, photon-flux, irradiance or flicker sample
type (ADR-0005 d2). All are unscaled SI — mol·m⁻²·s⁻¹, W·m⁻² — for the reason ADR-0005 rev 1
gave for joule over watt-hour: µmol and lux are display conventions, and display is the
gateway's concern.

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.2 | `c_i` identified against a reference quantum sensor in the canopy plane at ≥ 3 fixture output levels, per profile phase spectrum, **with the M1 diffuser and the enclosure window fitted** — the coefficients absorb the whole optical stack, so identifying them without it measures a different instrument. The project owns no quantum sensor; procedure and instrument undefined, O-52 |
| V2 | §6.1, T1 | One full 24 h cycle logged: no channel saturated and none below 10 % of full scale at any point of either 30 min ramp or the photoperiod. U4 die temperature proxy and canopy air temperature recorded against a reference thermometer |
| V3 | §6.3 | Each fixture channel driven alone at full output; the band counts recorded per channel. The 730 nm far-red channel shall register on F8 |
| V4 | §6.4 | The fixture's UV-A channel driven alone at full output; U3's UV channel recorded against dark. Each remaining fixture channel then driven alone at full output; the UV channel shall stay at dark level, which is the visible-rejection claim of §6.4 measured rather than assumed |
| V5 | §5.2 | Both channels' SCL and SDA measured idling at the 1.8 V rail, not at 3.3 V; each sensor addressed through its own U1 channel over ≥ 1 h at 100 kHz with zero bus errors. The low level presented to the carrier measured against the 76 mV R_ON budget |
| V11 | §5.2, §10 | With one channel selected, `0x39` answers once and once only. With both channels selected, the bus fault is observed and recorded — this is the failure the topology exists to prevent, and it shall be seen once deliberately |
| V6 | §4.1 | 1:1 paper printout against the physical part, both devices, including U4's aperture offset relative to the pads |
| V7 | §5 | Module-ID readback of `0b010` on the carrier in use |
| V8 | §9 M1, M2 | Band counts with and without the fitted diffuser under the same fixture setting; the ratio per band is the diffuser's transmission term for §6.2 |
| V9 | §7.3 | U4's V_DD measured at the pin under load and during U2's start-up transient, against the 1.98 V absolute maximum |
| V10 | §7.3, §7.4 | U3's V_DD measured at the pin under load and during U2's start-up transient, against the 1.98 V absolute maximum — the same measurement V9 makes at U4, on the shared rail |
| V12 | §10 | U3 identified by reading `ID` at `0x92` and matching `0x5C`, with `REV_ID` `0x91` = `0x11`, not by address ACK alone. At `0x39` an ACK could equally be U4 reached through the wrong channel |

## 12. Open items

Continues the `O-` namespace shared with the M01, M05, M06 and M07 specifications. O-2, O-4,
O-6, O-8, O-10, O-11 and O-42 are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's;
O-3, O-5, O-7, O-9, O-32 to O-50 are M01's, and O-51 is M01's item renumbered out of its
collision with M06's O-42.

| ID | Item | Blocks |
|----|------|--------|
| O-52 | PPFD reconstruction coefficients `c_i`: identification procedure, reference instrument, and per-phase-spectrum validity. The project owns no reference quantum sensor | PPFD accuracy, deployment profile, V1 |
| ~~O-53~~ | ~~U3's fixed `0x53` inside the reserved `0x50`–`0x57` block~~ — closed 2026-08-13 by ADR-0014 rev 5. Still closed after ADR-0014 rev 6: U3 is now the TSL2585 at `0x39` and U1 sits at `0x70`, so no device on this module occupies the block | — |
| ~~O-54~~ | ~~ADR-0014 d4's alternative UV part, the Vishay VEML6075, was terminated in 2019~~ — closed 2026-08-13 by ADR-0014 rev 5, which removes it and the LTR390 from d4 | — |
| ~~O-55~~ | ~~730 nm far-red unobservable, and the UV-A band on the shoulder of the UV sensor's response~~ — closed 2026-08-13 by ADR-0014 rev 5: F8 at 745 nm covers 730 nm (§6.3) and the AS7331's UV-A channel is centred at 360 nm (§6.4) | — |
| ~~O-56~~ | ~~U4's digital pins operated at 3.3 V, above V_DD, inside the 3.6 V absolute maximum but against the device pin description~~ — closed 2026-08-19 by fitting U1 in `store/E0003-000001.kicad_sch`: U4 sits on its own 1.8 V segment with its own pull-ups (§5.1, §5.2). The part at U1 became the `TCA9543A` on 2026-08-23 (ADR-0014 rev 6); the item stays closed, since the translation it required is still performed | — |
| O-57 | Condensation on excursions places U4 outside its 5–85 %RH non-condensing operating conditions. Post-excursion validity and recovery undefined, as for O-47 | Excursion handling, data validity |
| O-58 | DLI is integrated at the gateway (§3.2). Restart and gap policy across a gateway restart or a telemetry outage is unspecified | Gateway control loop, ADR-0015 |
| O-59 | Node power unmeasured | Distribution-board sizing, O-31 |
| ~~O-60~~ | ~~U4 responsivity temperature coefficient not established~~ — closed 2026-08-23: DS001046 v6-00 states none, so T2's conditional is not met and no coefficient is applied. Auto zero (`0xDE`) covers offset drift only, and is now a firmware requirement (§10). Absolute PPFD stability against temperature is an empirical question V2 already instruments | — |
| ~~O-61~~ | ~~Whether U4's integrated diffuser suffices or an external one is required~~ — closed 2026-08-23: DS001046 v6-00 §11.3 requires an achromatic diffuser and the device has no integrated one; the optical characteristics already assume a diffuser is fitted. Minimum scatter characteristic in Figure 66, now carried by M1. Part selection moves to O-71; the coating-order conflict is resolved in §9 | — |
| ~~O-62~~ | ~~`verify` values in this document~~ — closed 2026-08-23 against DS001046 v6-00 (AS7343), DS001043 v5-00 (TSL2585), SCPS206B (TCA9543A) and SBVS121 (TLV700). U4: `t_int` formula confirmed as written, AGAIN 0.5×…2048× at CFG1 `0xC6`, aperture Ø0.900 mm at 0.609 mm offset, POR 200 µs typical, `ID` `0x5A` = `0x81`. U3: `ID` `0x92` = `0x5C`. U2 capacitors stated in §7.3. Ordering codes in §4.2. The one item that cannot close from a datasheet — U3's photodiode-group offset — moves to O-70 | — |
| O-70 | DS001043 v5-00 dimensions the TSL2585 die as centred within ±75 µm but does not dimension the photodiode group, so U3's aperture offset from the package centre is unstated. Establish it by measurement against the physical part, with V6 | Enclosure window alignment, M3, V6 |
| ~~O-71~~ | ~~Diffuser not selected~~ — closed 2026-08-23 from AS7343 UG001009 v2-00 §4, the guide DS001046 §11.3 points at: **Kimoto OptSaver L-57**, with the 100 PBU as the alternative. Parameters and rationale in §9.1 | — |
| ~~O-72~~ | ~~U4 fed straight from the 1.8 V rail, without DS001046 §11.1's series filter~~ — closed 2026-08-23: R5 (22 Ω) and C6 (4.7 µF) fitted, the filtered node is `+1V8_U4`, and C7 moved to the device side of R5 (§7.3) | — |
| ~~O-63~~ | ~~AS7331 CAD content still in the repository and still attributed~~ — closed 2026-08-23: `E0003-000001` references none of it, so the footprint, the 3D model and `LicenseRef-SnapEDA-unstated` are removed with their `LICENSE.md` and `REUSE.toml` rows | — |
| ~~O-64~~ | ~~U3's angular response not stated as an acceptance half-angle~~ — closed 2026-08-23 from DS001043 v5-00 Figures 13 and 14: half maximum at ≈ ±45° on both axes, now carried by M9. The figures are the photopic channel's; M9 records that the UV channel is not separately characterised | — |
| O-65 | U1 has no reset line: RESET is tied to V_CC (§5.2), so a channel stuck low is recoverable only by a node power cycle. Whether a GPIO from the header shall drive RESET instead is a layout-affecting decision, and the header has four spare GPIO | Firmware recovery path, §10, layout |
| ~~O-66~~ | ~~U3's VSYNC/GPIO reset state unconfirmed~~ — closed 2026-08-23: `0xF8` reset `0x02` sets `VSYNC_GPIO_IN_EN` = 0 and `VSYNC_GPIO_OUT` = 1, an open-drain output released HIGH. `INT_IN_EN` = 0 likewise. Leaving both pins unconnected is correct (§7.4) | — |
| ~~O-68~~ | ~~`E0003-000001` is double-sided SMT, changing the fab package and the assembly quote~~ — closed 2026-08-23: not a question to resolve. Double-sided assembly is the consequence of M10 and the cost is accepted (§9) | — |
| O-69 | The `GND` pour is on `F.Cu` only. The sensor face has no plane behind it, one board thickness from the luminaire's driver. U3's and U4's grounds reach the top pour through vias | EMC, sensor face |

## 13. Maturity

**Schematic captured.** Both sensors are fixed by ADR-0014 rev 6 and both are stated from their
datasheets. `store/E0003-000001.kicad_sch` carries U4, U3, U1, the 1.8 V rail, all three I²C
pull-up pairs and the module-ID straps.

**Layout drawn and clean.** `E0003-000001-D-src.zip` holds a two-layer board on the same
104.902 × 46.990 mm envelope as `E0002-000001`, with J1/J2 at the same positions relative to the
outline, so the modules and cases interchange.

| Item | Value |
|---|---|
| `B.Cu` | U3 and U4 only. U3 at (152.87, 80.79), U4 at (161.13, 80.88) — 8.3 mm apart on one axis, so both sample the same point in the canopy plane |
| `F.Cu` | Every other part, plus J1/J2 |
| `GND` pour | `F.Cu` only. O-69 |
| Tracks | 0.2 mm throughout; 13 vias |
| DRC | **0 violations, 0 unconnected pads** |
| Schematic parity | **0 issues** |

`E0003-000001.kicad_dru` is empty. The two 0.13 mm waivers it carried existed for the PCA9306's
and the AS7331's 0.5 mm pitch; TSSOP-14 at 0.65 mm and the OLGA-6 land pattern at 0.25 mm
between pads both clear the 0.2 mm board default, so the rules are removed rather than re-scoped.

U2's ground leaves eastward and turns south: routed west it crossed the only corridor an IN-to-EN
strap can use, and left the pad one thermal spoke short of the board minimum.

R5 and C6 are re-used for U4's supply filter (§7.3); R11 stays a deliberate gap. Those three served the AS7331 and were withdrawn
with it. R1 and R4 are re-used as channel 1's pull-up pair rather than left as gaps.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** | Complement and requirements fixed; values estimated or `verify` | ADR-0014 rev 6 fixes both parts ✔ |
| **Schematic captured** ← here | Parts fixed to ordering part numbers; schematic exists; component values determined | U3 ordering part resolved ✔ (`TSL25853PM`); U1 resolved ✔ (`TCA9543APWR`); schematic exists ✔ |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-60, O-61, O-62, O-64, O-66, O-71 and O-72 closed ✔; O-70 open; V6 not executed |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0003` fabricated and bench-verified; O-59 measured |
