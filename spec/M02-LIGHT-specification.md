<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M02-LIGHT — module specification

- **Status:** Working specification. `E0003-000001` schematic captured and laid out; not fabricated
- **Date:** 2026-08-21
- **E-number:** `E0003` · module-ID strap `0b010`
- **Governing ADRs:** ADR-0014 (rev 5), ADR-0002 (rev 3), ADR-0003, ADR-0005 (rev 1), ADR-0016, ADR-0017 (rev 2)
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
| Spectral counts, 11 bands, 405…855 nm | U4 | 0…65 535 counts per band, 16-bit | 10…90 % of full scale, held there by autorange (§6.1); zero in the dark period | Relative between bands; absolute only through §6.2 |
| Clear channel (unfiltered Si) | U4 | 0…65 535 counts | As above | Relative |
| PPFD (derived, §6.2) | U4 | — | 295–347 µmol·m⁻²·s⁻¹ mean over the photoperiod; 0…full scale across the 30 min ramps; 0 in the dark period | Dominated by the reconstruction coefficients, O-52 |
| Flicker flag, 50 / 60 Hz | U4 | Flags; buffered data to 2 kHz (`verify`) | No flicker expected from a DC-driven fixture; an asserted flag is a driver fault indication | Flag, not a measurement |
| UV-A irradiance, 315…410 nm | U3 | Full scale 170 µW/cm² at GAIN 2048×, 3.49·10⁵ µW/cm² at 1× | Trace level per profile; absolute value not set by the profile | 385 counts/(µW/cm²) at GAIN 2048×, t_int 64 ms |
| UV-B irradiance, 283…315 nm | U3 | Full scale 189 µW/cm² at GAIN 2048×, 3.86·10⁵ µW/cm² at 1× | Expected zero from the fixture; a non-zero reading is a lamp fault | 347 counts/(µW/cm²) at GAIN 2048×, t_int 64 ms |
| UV-C irradiance, 235…285 nm | U3 | Full scale 83 µW/cm² at GAIN 2048×, 1.69·10⁵ µW/cm² at 1× | Expected zero from the fixture; a non-zero reading is a lamp fault | 794 counts/(µW/cm²) at GAIN 2048×, t_int 64 ms |

Publication rate: seconds. The full channel set requires **three integration cycles** — U4 has
six ADCs and fourteen channels, and the SMUX maps at most six per cycle (§6.1).

Location: canopy plane, in the luminaire's line of sight. Relocatable to any canopy point.
All sensors board-mounted; no leads, no cable-borne I²C.

### 3.1 Environmental envelope of the populated module

| | U4 AS7343 | U3 AS7331 | **Module** |
|---|---|---|---|
| Temperature | −30…+85 °C operating free-air | −40…+85 °C operating ambient | **−30…+85 °C**, set by U4 |
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
board. U3 shall not sit in the I²C pull-up path or in the sense path of any other part.

A partially populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) and
requires no firmware change; the boot probe registers responders only (§10).

The U4-only population is the expected variant wherever the fixture carries no UV-A channel.
A population without U4 has no function and is not a defined configuration.

## 4. Sensor complement

| # | Device | Ordering part | Function | Rail | I²C address | Address type |
|---|--------|---------------|----------|------|-------------|--------------|
| U4 | ams OSRAM AS7343 | `AS7343-DLGM` | 11 filtered bands 405…855 nm, clear, flicker | **1.8 V** | `0x39` | Fixed |
| U3 | ams OSRAM AS7331 | `AS7331-AQFM` | UV-A / UV-B / UV-C irradiance | 3.3 V (2.7…3.6 V) | `0x74` | Strapped, A0/A1; `0x74`…`0x77` available |

Both parts are named by ADR-0014 d4 as revised in rev 5. LCSC / JLCPCB ordering codes and stock
are unconfirmed for both. O-62.

U3's slave address is `(1,1,1,0,1,A1,A0)`; A0 and A1 are tied low on the module, giving `0x74`.
U3 additionally requires an external reference resistor at its REXT pin (§7.4) — a precision part,
not a jellybean.

U3's address is strapped to `0x74`, outside the `0x50`–`0x57` block ADR-0014 rev 4 d6 reserves
for the module-ID EEPROM. M02 identifies by strap (§5) and carries no EEPROM.

U4 is the primary photic source. U3 measures UV only and contributes nothing to the PPFD
computation (§6.2), whose band is 400…700 nm.

### 4.1 Packages

| # | Package | Body size | Notes |
|---|---------|-----------|-------|
| U4 | OLGA-8 | 3.10 × 2.00 × 1.00 mm | The optical aperture is not concentric with the package. Its offset from the package centre shall be taken from the AS7343 package outline drawing and not from the AS7341's. `verify`, O-62 |
| U3 | OLGA16 | 2.60 × 3.65 × 1.09 mm | 16 pads, 0.5 mm pitch. The optical aperture is not concentric with the package; its offset shall be taken from the AS7331 package outline drawing. Acceptance angle ±10° (M9) |

Every footprint shall be checked against the physical part with a 1:1 paper printout before
ordering (V6). The check shall include U4's aperture position relative to the pads — the
aperture does not appear in the schematic.

### 4.2 Supporting active parts

| # | Part | LCSC | Function |
|---|------|------|----------|
| U1 | `PCA9306DCUR` | `verify`, O-62 | Two-bit bidirectional I²C level translator between the 3.3 V segment and U4's 1.8 V sub-segment (§5.2). VSSOP-8, 2.3 × 2 mm |
| U2 | `TLV70018DCKR` | C133796 | 1.8 V rail for U4 VDD (§7.3). Same part as M01's U4 |

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
| Bus | I2C1, two segments joined by U1 (§5.2), all devices board-mounted. U3 and the header sit on the 3.3 V segment; U4 alone sits on the 1.8 V sub-segment |
| Speed | 100 kHz standard mode. Platform default set by the shared carrier driver, not by any device on this module: both U4 and U3 support 400 kHz fast mode. **U3 does not support clock stretching** — the driver shall not rely on it |
| Pull-ups | Both segments carry their own, on the module; the carrier carries none. O-51. 3.3 V segment: R2, R3, 4.7 kΩ to 3.3 V. 1.8 V sub-segment: R6, R7, 4.7 kΩ to 1.8 V |
| Sizing check | 4.7 kΩ sinks 0.70 mA at 3.3 V and 0.38 mA at 1.8 V, against U4's 6 mA sink condition for V_OL = 0.4 V. Rise time at 100 kHz admits ≈ 250 pF per segment, against board traces, U4's 10 pF input capacitance and U1's channel capacitance |
| Population | Both pull-up pairs are fitted in every population (§3.3) |
| Address separation | `0x39` and `0x74`; no device on this module occupies `0x50`–`0x57` |

### 5.2 Logic levels at U4

U4 has **no VDDIO pin**. Its digital pins are referenced to GND, and the device is supplied at
1.8 V (§7.3) while the carrier drives the segment at 3.3 V. U1 separates U4 onto its own 1.8 V
sub-segment, so U4's pins never leave the 1.8 V domain.

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

| U1 | Requirement |
|---|---|
| Part | `PCA9306`, two-bit bidirectional pass-FET translator with an internal charge pump (§4.2) |
| V_REF1 / V_REF2 | 1.8 V / 3.3 V, an explicitly stated combination: V_REF1 1.2…3.3 V, V_REF2 1.8…5.5 V. **V_REF1 ≤ V_REF2 shall hold** |
| Channels | SCL and SDA only. U4's `INT`, `GPIO` and `LDR` are unrouted (§5), so no third channel is required |
| EN | Pulled to V_REF2 through R1, 200 kΩ, per the datasheet application diagram. The bus is not isolated in normal operation |
| R_ON | 3.5 Ω typical. At the 3.3 V segment's 0.70 mA sink current it adds ≈ 2 mV to the 0.4 V V_OL that U4 presents to the segment |
| Pull-ups | Required on both sides; sized in §5.1 |
| Decoupling | 100 nF at V_REF2 (C4) |

U3 is supplied at 3.3 V and raises no level question: its digital pins are referenced to V_DDD,
with V_IH ≥ 0.7 · V_DDD, V_IL ≤ 0.3 · V_DDD, V_OL ≤ 0.4 V at 3 mA and an input/output absolute
maximum of V_DD + 0.5 V.

## 6. Measurement requirements

### 6.1 Spectral acquisition and autoranging

| Item | Requirement |
|------|-------------|
| ADC | Six independent 16-bit light-to-frequency converters; full scale 65 535 counts per channel |
| Channel mapping | SMUX, configured after every power-up before any measurement is started |
| Cycles per full set | **Three.** Eleven filtered bands + clear + flicker exceeds six ADCs by more than one cycle |
| Integration time | `t_int = (ATIME + 1) × (ASTEP + 1) × 2.78 µs` (`verify`) |
| Gain | AGAIN, 0.5× … 2048× (`verify`) |
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
| U3 UV-A channel | Responsivity spans ≈ 315…410 nm, peak ≈ 340 nm — the profile band lies inside it |
| U3 UV-B, UV-C channels | ≈ 283…315 nm (peak ≈ 295 nm) and ≈ 235…285 nm (peak ≈ 250 nm). The fixture commands neither; both are expected to read zero |
| Published | Per-channel irradiance as measured, three subjects (§10.1) |
| Fault use | A non-zero UV-B or UV-C reading indicates emission the profile does not command and is an alerting condition at the gateway, not a control input |

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured. O-59 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02) |
| Module contribution | Sub-milliwatt at U4 (§7.2). **U3 dominates the module**: 2 mA maximum active and 970 µA maximum in standby against U4's 280 µA, ≈ 6.6 mW at 3.3 V. Node draw is still mostly the carrier and MCU |
| Burst reflected to `+12 V` | None. No device on this module has a burst load of the SCD41 class |

### 7.2 Device currents

| # | Device | Sleep | Idle | Active | Rail |
|---|--------|-------|------|--------|------|
| U4 | AS7343 | 0.7 µA typ / 5 µA max | 40 µA typ / 60 µA max | 210 µA typ / **280 µA max** | 1.8 V |
| U3 | AS7331 | 1 µA max (power down) | 970 µA max (standby) | 1.42 mA typ / **2 mA max** | 3.3 V |

U4's figures are stated at V_DD = 1.8 V and exclude current through the LDR pin, which this
module does not use. §7.3 supplies 1.8 V, so they apply as written.

### 7.3 Module-local 1.8 V rail

| Rail | Source | Consumer | Reason |
|------|--------|----------|--------|
| 3.3 V | Header | U3, U1 V_REF2, the 3.3 V pull-ups, straps, U2 input | — |
| 1.8 V | U2, from 3.3 V | U4 V_DD, U1 V_REF1, the 1.8 V pull-ups | U4's V_DD range is 1.7…1.98 V, and 1.98 V is also its **absolute maximum**. 3.3 V destroys the device |

U4's supply has no headroom above nominal: 1.8 V nominal against a 1.98 V absolute maximum
leaves 180 mV, so the regulator's initial accuracy, load and line regulation and its transient
overshoot shall be shown to stay inside it. Below nominal the margin is 100 mV to the 1.7 V
minimum. Load is 280 µA maximum at U4, plus up to 0.77 mA drawn by R6 and R7 while both bus
lines are held low and U1's V_REF1 current — under 1.1 mA in all, two orders below U2's 200 mA
rating, so no droop budget applies. Local decoupling 100 nF at U4's V_DD pin, plus U2's input
and output capacitors per its datasheet (`verify`). O-62.

### 7.4 U3 supply and reference

| Item | Requirement |
|------|-------------|
| Supply pins | V_DDA (pin 3) and V_DDD (pin 10), each decoupled 100 nF at the pin |
| Common rail | Both shall be fed from the **same** 3.3 V rail. `V_DDA − V_DDD` has a ±0.3 V **absolute maximum**, so two independent regulators are not permitted |
| Grounds | V_SSA (pins 1, 2, 5, 6, 15, 16) and V_SSD (pin 11) are separate nets, routed separately and joined by a single 0 Ω link placed beside the device |
| REXT | 3.3 MΩ ±1 % (3.267…3.333 MΩ), TCR ≤ 50 ppm/K, from pin 4 to V_DDA. The device does not operate without it, and it sets the internal reference — its tolerance enters every irradiance reading |
| Placement | REXT and the supply decoupling shall sit on the same PCB side as U3. The analog supply shall be placed as close to U3 as the layout allows |

**M02 is the second module to generate a rail of its own.** O-43 records that module-local
rails sit outside ADR-0002 d3 and the header contract, and that one instance is not a pattern —
to be revisited at the second. This is the second. O-43 is now due.

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | The board sits in the beam at 65…76 W/m² PAR (§3) plus the fixture's non-PAR emission. Steady-state rise above canopy air shall be recorded at bring-up, not assumed | §3, V2 |
| T2 | U4's responsivity temperature coefficient shall be applied if the datasheet states one, over the range established by T1 | `verify`, O-60 |

No heat source of consequence exists on the board: total sensor dissipation is below 1 mW, and
U2 dissipates 1.5 V × 280 µA = 0.42 mW. Incident radiation dominates, and it is a property of
the installation.

## 9. Optical and mechanical requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | An **achromatic diffuser** shall be placed above U4's aperture unless the AS7343's own application optical requirements show its integrated diffuser to be sufficient for a hemispherical source. Which applies is unconfirmed | `verify`, O-61 |
| M2 | Any diffuser fitted under M1 sits in the measurement path of every band of §6.2; its spectral transmission shall be flat across 400…700 nm to within `verify`, or its transmission curve shall be folded into `c_i` | §6.2, O-61 |
| M3 | Neither U4's nor U3's aperture is concentric with its package; each enclosure window shall be aligned to the aperture, not to the package outline | §4.1 |
| M4 | No conformal coating over U4's aperture or U3's optical window | §4.1 |
| M5 | Conformal coating on all other areas. Installed in the growing volume: condensation possible on excursions | §3.1 |
| M6 | The sensing face shall be mounted in the canopy plane, normal to the fixture axis, unshaded by foliage or structure. Shading is the failure mode this module cannot detect | §3, ADR-0014 d7 |
| M7 | Mounted at canopy height; height adjusted as the canopy grows, recorded as deployment metadata. A height change invalidates the §6.2 coefficients | §6.2, ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8, supported at both ends; no standoffs required | Pin map, header section |
| M9 | U3's angle of incidence is specified as **±10°**. The sensing face and any window above U3 shall hold the source inside that cone, or the UV readings are not the quantity §6.4 defines | §4.1, O-61 |

M5 and M1 conflict in the same way M01's M4 does: a coating step over a diffuser is a coating
step over the measurement path. The diffuser is fitted after coating or masked during it. O-61.

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b010` — STRAP_0 low, STRAP_1 high, STRAP_2 low |
| Power-on delay | U4 NAKs deterministically during initialization after V_DD crosses its POR threshold. The boot probe shall not issue a transaction before that interval has elapsed. Interval `verify`. U3 needs 1.2 ms typ / 2 ms max from power-down to the first measurement |
| Boot probe addresses | `0x39`, `0x74`. An ACK is not identification (M01 §10 precedent); each probe shall be backed by a device-specific read. U3's is **AGEN at `0x02`, reset value `0x21`** (DEVID `0b0010` in bits 7:4, MUT `0b0001` in bits 3:0); U4's ID register is `verify` |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| SMUX | Configured after every power-up, before the first measurement is started. Reference configuration from the vendor application note for the AS7343 — the AS7341's does not apply |
| U3 power-up state | U3 resets into power-down with `OSR:PD = 1` and into the CONFIGURATION state. Firmware shall clear `PD`, write CREG1..CREG3, then switch `OSR:DOS` to MEASUREMENT; the control registers are not writable from the measurement state |
| Acquisition | Three integration cycles per full channel set (§6.1); flicker detection mapped to its own ADC when used |
| Autorange | AGAIN and `t_int` adjusted to hold the brightest mapped channel between 10 % and 90 % of full scale. The setting in force is published with the sample |
| Derived | PPFD per §6.2, from coefficients read out of the deployment profile, over F1…F7 only |
| Not derived | DLI. Owned by the gateway (§3.2, ADR-0014 d4) |
| Deployment constants | `c_i` per §6.2 read from the deployment profile, not compiled in |
| Rail failure | Failure of U2 removes U4 from the bus. The boot probe handles this as absence; *not fitted*, *failed* and *unpowered* remain indistinguishable (O-37) |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m02_light/` — does not exist |
| Node-ID | 96 is M05's, 97 is M01's; M02 takes **98**, static for bring-up (ADR-0005 d6) |
| Publication rate | 1 s, subject to the three-cycle acquisition time of §6.1 |

### 10.1 Published subjects

Baked defaults in the unregulated range. ADR-0005 d7 makes these `uavcan.pub.<name>.id`
register entries. M05 holds 4096–4102; M01 holds 4112–4121.

| ID | Quantity | Source | Type |
|----|----------|--------|------|
| 4128 | Spectral counts, 11 bands + clear, with AGAIN and `t_int` | U4 | `industryflow.greenhouse.light.SpectralSample` |
| 4129 | PPFD | derived, §6.2 | `industryflow.greenhouse.light.PhotonFluxDensity` (mol·m⁻²·s⁻¹) |
| 4130 | Flicker flags | U4 | `industryflow.greenhouse.light.FlickerStatus` |
| 4131 | UV-A irradiance | U3 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |
| 4132 | UV-B irradiance | U3 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |
| 4133 | UV-C irradiance | U3 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |

Six subjects, not one record: a partial population must be able to omit U3's three
(ADR-0005 d8). U4's channels are one device read in one acquisition and are not separable, so
they are one subject.

The `light` sub-namespace is named by ADR-0005 d1 and contains no files. All four types are
minted because the standard set carries no spectral, photon-flux, irradiance or flicker sample
type (ADR-0005 d2). All are unscaled SI — mol·m⁻²·s⁻¹, W·m⁻² — for the reason ADR-0005 rev 1
gave for joule over watt-hour: µmol and lux are display conventions, and display is the
gateway's concern.

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.2 | `c_i` identified against a reference quantum sensor in the canopy plane at ≥ 3 fixture output levels, per profile phase spectrum. The project owns no quantum sensor; procedure and instrument undefined, O-52 |
| V2 | §6.1, T1 | One full 24 h cycle logged: no channel saturated and none below 10 % of full scale at any point of either 30 min ramp or the photoperiod. U4 die temperature proxy and canopy air temperature recorded against a reference thermometer |
| V3 | §6.3 | Each fixture channel driven alone at full output; the band counts recorded per channel. The 730 nm far-red channel shall register on F8 |
| V4 | §6.4 | The fixture's UV-A channel driven alone at full output; U3's three channels recorded against dark. UV-B and UV-C shall stay at dark level |
| V5 | §5.2 | U4's SCL and SDA measured idling at the 1.8 V rail, not at 3.3 V, and U4 addressed through U1 over ≥ 1 h at 100 kHz with zero bus errors |
| V6 | §4.1 | 1:1 paper printout against the physical part, both devices, including U4's aperture offset relative to the pads |
| V7 | §5 | Module-ID readback of `0b010` on the carrier in use |
| V8 | §9 M1, M2 | Band counts with and without the fitted diffuser under the same fixture setting; the ratio per band is the diffuser's transmission term for §6.2 |
| V9 | §7.3 | U4's V_DD measured at the pin under load and during U2's start-up transient, against the 1.98 V absolute maximum |
| V10 | §7.4 | `V_DDA − V_DDD` measured at U3's pins at power-up and in steady state, against the ±0.3 V absolute maximum. REXT measured in circuit against 3.267…3.333 MΩ |
| V11 | §10 | U3 identified by reading AGEN at `0x02` and matching `0x21`, not by address ACK alone |

## 12. Open items

Continues the `O-` namespace shared with the M01, M05, M06 and M07 specifications. O-2, O-4,
O-6, O-8, O-10, O-11 and O-42 are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's;
O-3, O-5, O-7, O-9, O-32 to O-50 are M01's, and O-51 is M01's item renumbered out of its
collision with M06's O-42.

| ID | Item | Blocks |
|----|------|--------|
| O-52 | PPFD reconstruction coefficients `c_i`: identification procedure, reference instrument, and per-phase-spectrum validity. The project owns no reference quantum sensor | PPFD accuracy, deployment profile, V1 |
| ~~O-53~~ | ~~U3's fixed `0x53` inside the reserved `0x50`–`0x57` block~~ — closed 2026-08-13 by ADR-0014 rev 5: U3 is the AS7331, strapped to `0x74`. No device on this module occupies the block | — |
| ~~O-54~~ | ~~ADR-0014 d4's alternative UV part, the Vishay VEML6075, was terminated in 2019~~ — closed 2026-08-13 by ADR-0014 rev 5, which removes it and the LTR390 from d4 | — |
| ~~O-55~~ | ~~730 nm far-red unobservable, and the UV-A band on the shoulder of the UV sensor's response~~ — closed 2026-08-13 by ADR-0014 rev 5: F8 at 745 nm covers 730 nm (§6.3) and the AS7331's UV-A channel is centred at 360 nm (§6.4) | — |
| ~~O-56~~ | ~~U4's digital pins operated at 3.3 V, above V_DD, inside the 3.6 V absolute maximum but against the device pin description~~ — closed 2026-08-19 by fitting U1 (`PCA9306`) in `store/E0003-000001.kicad_sch`: U4 sits on a 1.8 V sub-segment with its own pull-ups (§5.1, §5.2) | — |
| O-57 | Condensation on excursions places U4 outside its 5–85 %RH non-condensing operating conditions. Post-excursion validity and recovery undefined, as for O-47 | Excursion handling, data validity |
| O-58 | DLI is integrated at the gateway (§3.2). Restart and gap policy across a gateway restart or a telemetry outage is unspecified | Gateway control loop, ADR-0015 |
| O-59 | Node power unmeasured | Distribution-board sizing, O-31 |
| O-60 | U4 responsivity temperature coefficient not established; T2 unquantified | Absolute PPFD stability |
| O-61 | Whether U4's integrated diffuser suffices or an external achromatic diffuser is required, its spectral transmission flatness, and its order against the conformal-coating step. Any diffuser sits in the measurement path of every band | Enclosure design, §6.2 coefficients, M1/M2/M5 |
| O-62 | `verify` values in this document, reduced 2026-08-18 by the AS7331 datasheet (DS001047 v4-00): U3's electrical, optical and package figures are now stated. Outstanding: U4's `t_int` formula, AGAIN range, aperture offset, POR interval and ID register; U3's aperture offset; U2 capacitor values; LCSC / JLCPCB ordering codes and stock for both sensors, for U1, and for the REXT and strap-link resistors | `L` release |

## 13. Maturity

**Schematic captured.** Both sensors are fixed by ADR-0014 rev 5 and both are now stated from
their datasheets. `store/E0003-000001.kicad_sch` carries U4, U3, the 1.8 V rail, U1 with both
I²C pull-up pairs, and the module-ID straps.

**Layout drawn, not yet clean.** `store/E0003-000001.kicad_pcb` is a two-layer board on the
same 104.902 × 46.990 mm envelope as `E0002-000001`, with J1/J2 at the same positions relative
to the outline, so the module mates and cases alike. All 113 pads match the schematic netlist
and §7.4's placement requirement is met — R4, R5, C5, C6 and U3 are all on `B.Cu`. Custom design
rules are in `store/E0003-000001.kicad_dru`, scoped to U1 and U3, whose 0.5 mm pitch puts their
pads inside the 0.2 mm board default. Open before the layout can be released: one unrouted
connection on `Net-(U2-EN)` (U2.1→U2.3, the 1.8 V LDO enable), single-spoke thermal relief on
J1.8, J2.8 and U1.1, and 10 isolated `GND` pour islands. The module stays at **Schematic
captured** until these close together with O-62.

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** | Complement and requirements fixed; values estimated or `verify` | ADR-0014 rev 5 fixes both parts ✔ |
| **Schematic captured** ← here | Parts fixed to ordering part numbers; schematic exists; component values determined | U3 ordering part resolved ✔ (`AS7331-AQFM`); schematic exists ✔; O-56 closed ✔ (U1 fitted). U1's ordering code is still `verify` (O-62) |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-62 closed; V6 executed |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0003` fabricated and bench-verified; O-59 measured |
