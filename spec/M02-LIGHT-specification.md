<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M02-LIGHT — module specification

- **Status:** Working specification, pre-schematic capture. `E0003` not laid out, not fabricated
- **Date:** 2026-08-13
- **E-number:** `E0003` · module-ID strap `0b010`
- **Governing ADRs:** ADR-0014 (rev 4), ADR-0002 (rev 3), ADR-0003, ADR-0005 (rev 1), ADR-0016, ADR-0017 (rev 2)
- **Companions:** `M01-CLIMATE-specification.md`, `M05-SAFETY-specification.md`, `M06-VENTILATION-specification.md`, `M07-AMBIENT-specification.md`

Rationale for the decisions applied here is in the governing ADRs and is not restated.
Values marked `verify` are not confirmed against the manufacturer datasheet.

## 1. Scope

Requirements for the M02-LIGHT sensor module: sensor complement, measured quantities and their
derivation, electrical, optical, mechanical and firmware requirements, and their verification.

Not specified here: carrier design (`store/E0001-000002-D-pinmap.md`), cultivation setpoints
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
| Spectral counts, 8 bands F1–F8 (415…680 nm) | U1 | 0…65 535 counts per band, 16-bit | 3 300…42 000 counts at AGAIN 64× / 27.8 ms at full fixture output (§6.1); zero in the dark period | Relative between bands; absolute only through §6.2 |
| Clear channel (unfiltered Si) | U1 | 0…65 535 counts | As above | Relative |
| NIR, 910 nm | U1 | 0…65 535 counts | Fixture-dependent; no profile target | Relative |
| PPFD (derived, §6.2) | U1 | — | 295–347 µmol·m⁻²·s⁻¹ mean over the photoperiod; 0…full scale across the 30 min ramps; 0 in the dark period | Dominated by the reconstruction coefficients, O-52 |
| Flicker flag, 50 / 60 Hz | U1 | Flags only; buffered data to 2 kHz | No flicker expected from a DC-driven fixture; asserted flag is a driver fault indication | Flag, not a measurement |
| UV-A irradiance | U2 | `verify`, 16…20-bit unsigned | Trace level per profile; absolute value not set by the profile | `verify`, and see O-55 |
| Ambient light (ALS) | U2 | `verify` | As above | `verify` |

Publication rate: seconds. The full 11-channel set requires **two integration cycles** — the
device has six ADCs and the SMUX maps at most six channels per cycle (§6.1).

Location: canopy plane, in the luminaire's line of sight. Relocatable to any canopy point.
All sensors board-mounted; no leads, no cable-borne I²C.

### 3.1 Environmental envelope of the populated module

| | U1 AS7341 | U2 LTR-390UV-01 | **Module** |
|---|---|---|---|
| Temperature | −30…+70 °C operating free-air | `verify` | **−30…+70 °C**, pending U2 |
| Humidity | 5…85 %RH non-condensing | `verify` | **5…85 %RH non-condensing**, pending U2 |
| MSL | 3, 168 h floor life | `verify` | **3**, pending U2 |

§3 places the module in the growing volume, which M01 §3 admits may condense on excursions.
That is outside U1's stated operating conditions, not merely outside its accuracy. The
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
| **DLI** — the integral of PPFD over the photoperiod | **Gateway** (ADR-0016 d4/d5) |
| Luminaire channel drive and spectrum command | No actuator module exists (ADR-0014 d9) |

DLI is a multi-hour integral against wall-clock time. The node holds neither wall clock nor
integration state across a reset, and ADR-0016 d5 makes gateway-side derived values first-class
telemetry. M02 publishes PPFD; the gateway integrates it. O-58.

M02 carries no temperature, humidity, gas, pressure or air-velocity sensor.

### 3.3 Partial populations

Optional parts shall be footprints that can be left unpopulated with no other change to the
board. U2 shall not sit in the I²C pull-up path or in the sense path of any other part.

A partially populated configuration takes its own assembly E-number (ADR-0017 rev 2 d4) and
requires no firmware change; the boot probe registers responders only (§9).

The U1-only population is the expected variant wherever the fixture carries no UV-A channel.
A population without U1 has no function and is not a defined configuration.

## 4. Sensor complement

| # | Device | Ordering part | LCSC | Function | Rail | I²C address | Address type |
|---|--------|---------------|------|----------|------|-------------|--------------|
| U1 | ams OSRAM AS7341 | `AS7341-DLGM` | C2649486 (`verify`) | 8-band visible spectrum, clear, NIR, flicker | **1.8 V** | `0x39` | Fixed |
| U2 | Lite-On LTR-390UV-01 | `LTR-390UV-01` | C492374 (`verify`) | UV-A intensity, ambient light | 3.3 V (2.4…3.6 V) | `0x53` — **barred, see below** | Fixed, not strappable |

**U2's address is inside a reserved block.** ADR-0014 rev 4 d6 reserves `0x50`–`0x57`
project-wide for the module-ID EEPROM transport. `0x53` is inside that block and U2's address is
fixed and not strappable. M02 identifies by strap (§5) and carries no EEPROM, so nothing on this
module's segment contends for the address; the block has seven other addresses available to the
EEPROM whenever the transport lands. Recorded, not blocking. O-53.

**U2's alternative is discontinued.** ADR-0014 d4 offers the Vishay VEML6075 in place of the
LTR390. Vishay terminated it — last-time-buy 2019-05-31, last shipment 2019-12-31. It is not a
selectable part. O-54.

U1 is the primary photic source. U2's ALS channel shall be published as its own subject and
shall not enter the PPFD computation (§6.2).

### 4.1 Packages

| # | Package | Body size | Notes |
|---|---------|-----------|-------|
| U1 | OLGA-8 | 3.10 × 2.00 × 1.00 ±0.10 mm | Optical aperture ⌀0.900 mm, offset 0.609 mm from part centre toward the ALS axis; photodiode array centre offset 0.299 / 0.919 mm. Pin-1 chamfer 0.250 × 45°. Pad pitch 0.638 mm, 8 × 0.488 × 0.575 mm pads |
| U2 | 6-WFDFN | 2.0 × 2.0 mm, height `verify` | `verify` |

Every footprint shall be checked against the physical part with a 1:1 paper printout before
ordering (V6). The check shall include U1's aperture position relative to the pads — the
aperture is not concentric with the package and does not appear in the schematic.

### 4.2 Supporting active parts

| # | Part | LCSC | Function |
|---|------|------|----------|
| U3 | `TLV70018DCKR` | C133796 | 1.8 V rail for U1 VDD (§7.3). Same part as M01's U4 |

## 5. Interfaces

| Interface | Use |
|-----------|-----|
| Header A pin 1 / 3 — 3V3, GND | Supply |
| Header A pin 4 / 5 — I2C_SCL, I2C_SDA | U1, U2 |
| Header B pins 3–5 — STRAP_0..2 | Tied to `0b010`: STRAP_0 low, STRAP_1 high, STRAP_2 low |
| Header B pin 1 / 2 — 3V3, GND | Strap reference |

No other header signal is claimed. `GPIO_1`/`GPIO_2` (PA9/PA10) are not used, so USART1 remains
available as the carrier debug console throughout boot and run (pin map note 6). U1's `INT` and
`GPIO` pins are not routed to the header; §9 polls.

**M02's strap pattern has bit 1 = 1.** Module-ID bit 1 (`STRAP_1`, PA6) is unrouted on carrier
revision `E0001-000001` (`firmware/common/carrier/e0001.h`), on which M02 reads back as `0b000`
— the reserved value, not another class. Whether `E0001-000002` routes PA6 is not recorded in
the store documents. M02 is the first Phase-1 module affected. O-42.

### 5.1 I²C bus

| | Requirement |
|---|---|
| Bus | I2C1, single local segment, all devices board-mounted |
| Speed | 100 kHz standard mode. Platform default set by the shared carrier driver, not by any device on this module: U1 supports 400 kHz, U2 `verify` |
| Pull-ups | 4.7 kΩ to 3.3 V, on the module. The carrier carries none. O-51 |
| Sizing check | 4.7 kΩ sinks 0.70 mA, against U1's 6 mA sink condition for V_OL = 0.4 V. Rise time at 100 kHz admits ≈ 250 pF, against two devices on board traces and U1's 10 pF input capacitance |
| Population | Pull-ups are fitted in every population (§3.3) |

### 5.2 Logic levels at U1

U1 has **no VDDIO pin**. Its digital pins are referenced to GND, and the device is supplied at
1.8 V (§7.3) while the carrier drives the segment at 3.3 V.

| | Value | Source |
|---|---|---|
| SCL / SDA / INT absolute maximum | 3.6 V | Absolute maximum ratings, separate from the 2.2 V V_DD maximum |
| V_IH min | 1.26 V | Electrical characteristics |
| V_IL max | 0.54 V | Electrical characteristics |
| V_OL max | 0.4 V at 6 mA sink | Electrical characteristics |
| Leakage into SCL / SDA / INT | ±5 µA | Electrical characteristics |

A 3.3 V segment satisfies V_IH and stays inside the 3.6 V digital-pin rating with the pins
above V_DD. The vendor application example pulls the segment to 1.8 V instead, and no
recommended-operating-condition table covers pin voltages above V_DD. Either the 3.3 V segment
is confirmed with the vendor or a level translator is fitted; the choice changes the schematic.
O-56.

## 6. Measurement requirements

### 6.1 Spectral acquisition and autoranging

| Item | Requirement |
|------|-------------|
| ADC | Six independent 16-bit light-to-frequency converters; full scale 65 535 counts per channel |
| Channel mapping | SMUX, configured after every power-up before any measurement is started |
| Cycles per full set | **Two.** 8 visible + clear + NIR + flicker exceeds six ADCs |
| Integration time | `t_int = (ATIME + 1) × (ASTEP + 1) × 2.78 µs` (`verify`) |
| Gain | AGAIN, 0.5× … 512× (`verify`) |
| Autorange | Firmware shall hold the brightest mapped channel between 10 % and 90 % of full scale by adjusting AGAIN and `t_int` (§9) |
| Published with every sample | The AGAIN and `t_int` in force. Counts without them are not a measurement |

Headroom at the profile's canopy irradiance, from the datasheet responsivity anchor — channel
F5 gives 590 counts at AGAIN 64×, `t_int` 27.8 ms, under a 2700 K warm-white LED at
E_e = 107.67 µW/cm²:

| Item | Value |
|------|-------|
| Canopy irradiance, §3 | 6 500…7 600 µW/cm² PAR |
| Ratio to the anchor condition | 60…71 × |
| F5 at AGAIN 64×, 27.8 ms | ≈ 35 600…41 700 counts, 54…64 % of full scale |
| F1 at the same setting | ≈ 3 300…3 900 counts, 5…6 % of full scale |

AGAIN 64× with `t_int` 27.8 ms therefore covers the full-output photoperiod without saturation
and with the weakest visible band still resolved. It does **not** cover the 30 min ramps or the
dark period; those are the autorange requirement. The anchor is a 2700 K spectrum and the
fixture is warm white plus narrowband channels, so this bounds the working point — it is not a
calibration.

### 6.2 PPFD

PPFD is a photon count over 400–700 nm. U1 delivers eight band counts at 415…680 nm centres.

```
PPFD = Σ c_i · n_i          i = F1 … F8
```

| Item | Requirement |
|------|-------------|
| `n_i` | Band counts normalized by AGAIN and `t_int` (§6.1) |
| `c_i`, reconstruction coefficients | Commissioning parameters identified per luminaire spectrum against a reference quantum sensor, carried in the deployment profile, not compiled in. O-52 |
| Validity | The coefficients hold for the spectrum they were identified under. A profile phase change (§3, vegetative vs flowering) changes the spectrum and requires its own coefficient set |
| Uncovered PAR | 400…402 nm and 706…700 nm fall outside every band's half-maximum (§6.3). Their contribution enters the coefficients as a fixture-specific constant, not as a measurement |
| Accuracy | Not bounded by the device. Dominated by `c_i`. O-52 |

U2's ALS channel is a broadband photopic-class measurement and shall not enter this sum.

### 6.3 Spectral coverage against the profile

Band half-maxima computed from the datasheet centre wavelengths and FWHM.

| Channel | Centre | FWHM | Half-maximum span |
|---------|--------|------|-------------------|
| F1 | 415 nm | 26 nm | 402…428 nm |
| F2 | 445 nm | 30 nm | 430…460 nm |
| F3 | 480 nm | 36 nm | 462…498 nm |
| F4 | 515 nm | 39 nm | 496…535 nm |
| F5 | 555 nm | 39 nm | 536…575 nm |
| F6 | 590 nm | 40 nm | 570…610 nm |
| F7 | 630 nm | 50 nm | 605…655 nm |
| F8 | 680 nm | 52 nm | 654…706 nm |
| NIR | 910 nm | n/a | — |

Against the profile's flowering/fruiting spectrum (ADR-0003 d11):

| Profile channel | Covered by | Status |
|-----------------|-----------|--------|
| Warm white | F1…F8 | Covered |
| 660 nm red | F7, F8 wings | Covered |
| **730 nm far-red** | **none** | **Blind. 706 nm is F8's half-maximum and the next channel is NIR at 910 nm** |
| 365–385 nm UV-A | none of U1's bands (F1's half-maximum is 402 nm) | U2's function, subject to O-55 |

ADR-0014 d4 justifies U1 as validating the spectrum of ADR-0003 d11. The far-red channel of
that spectrum is not observable by the named complement. O-55.

### 6.4 UV-A

| Item | Requirement |
|------|-------------|
| Profile band | 365…385 nm trace (ADR-0003 d11) |
| U2 UVS peak response | 300…350 nm; the profile band sits on the falling edge |
| Responsivity at 365…385 nm | `verify`. Not stated in the reachable datasheet |
| Published | UV-A irradiance as measured, with no correction for the band offset |
| Function | Presence and trend of the UV-A channel, not its absolute irradiance |

O-55 covers whether a part whose response is centred on the profile band is required.

## 7. Power

### 7.1 Node

| | Value |
|---|---|
| Node draw on `+12 V` | Not measured. O-59 |
| Reference figure | M05 node, 0.25 W (254 mW at 12.12 V, 2026-08-02) |
| Module contribution | Sub-milliwatt at the sensors (§7.2). M02 is the lowest-draw module in the catalog; node draw is the carrier and MCU |
| Burst reflected to `+12 V` | None. No device on this module has a burst load of the SCD41 class |

### 7.2 Device currents

| # | Device | Sleep | Idle | Active | Rail |
|---|--------|-------|------|--------|------|
| U1 | AS7341 | 0.7 µA typ / 5 µA max | 35 µA typ / 60 µA max | 210 µA typ / **300 µA max** | 1.8 V |
| U2 | LTR-390UV-01 | `verify` | `verify` | `verify` | 3.3 V |

U1's figures are stated at V_DD = 1.8 V and exclude current through the LDR pin, which this
module does not use. §7.3 supplies 1.8 V, so they apply as written.

### 7.3 Module-local 1.8 V rail

| Rail | Source | Consumer | Reason |
|------|--------|----------|--------|
| 3.3 V | Header | U2, pull-ups, straps, U3 input | — |
| 1.8 V | U3, from 3.3 V | U1 V_DD only | U1's V_DD range is 1.7…2.0 V with a 2.2 V absolute maximum. 3.3 V destroys the device |

Headroom: U1's minimum V_DD is 1.7 V against 1.8 V nominal. Load is 300 µA maximum, three
orders below U3's rating; no droop budget applies. Local decoupling 100 nF at U1's V_DD pin,
plus U3's input and output capacitors per its datasheet (`verify`).

**M02 is the second module to generate a rail of its own.** O-43 records that module-local
rails sit outside ADR-0002 d3 and the header contract, and that one instance is not a pattern —
to be revisited at the second. This is the second. O-43 is now due.

## 8. Thermal requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| T1 | The board sits in the beam at 65…76 W/m² PAR (§3) plus the fixture's non-PAR emission. Steady-state rise above canopy air shall be recorded at bring-up, not assumed | §3, V2 |
| T2 | U1's responsivity temperature coefficient shall be applied if the datasheet states one, over the range established by T1 | `verify`, O-60 |

No heat source of consequence exists on the board: total sensor dissipation is below 1 mW, and
U3 dissipates 1.5 V × 300 µA = 0.45 mW. Incident radiation dominates, and it is a property of
the installation.

## 9. Optical and mechanical requirements

| ID | Requirement | Reference |
|----|-------------|-----------|
| M1 | An **achromatic bulk diffuser** shall be placed above U1's aperture, meeting or exceeding the datasheet's minimum scattering characteristic (≥ 0 % response to ±70°, against the cosine reference to ±90°) | Datasheet §11.3 |
| M2 | The diffuser's spectral transmission enters every band of §6.2 and shall be flat across 400…700 nm to within `verify`, or its transmission curve shall be folded into `c_i` | §6.2, O-61 |
| M3 | U1's aperture is offset 0.609 mm from the package centre; the enclosure window shall be aligned to the aperture, not to the package | §4.1 |
| M4 | No conformal coating over U1's aperture or U2's optical window | §4.1 |
| M5 | Conformal coating on all other areas. Installed in the growing volume: condensation possible on excursions | §3.1 |
| M6 | The sensing face shall be mounted in the canopy plane, normal to the fixture axis, unshaded by foliage or structure. Shading is the failure mode this module cannot detect | §3, ADR-0014 d7 |
| M7 | Mounted at canopy height; height adjusted as the canopy grows, recorded as deployment metadata. A height change invalidates the §6.2 coefficients | §6.2, ADR-0014 d7 |
| M8 | Seats on the carrier header pair 2×12 + 2×8, supported at both ends; no standoffs required | Pin map, header section |

M5 and M1 conflict in the same way M01's M4 does: a coating step over a diffuser is a coating
step over the measurement path. The diffuser is fitted after coating or masked during it. O-61.

## 10. Firmware requirements

| Item | Requirement |
|------|-------------|
| Module-ID strap | `0b010` — STRAP_0 low, STRAP_1 high, STRAP_2 low. Subject to O-42 (§5) |
| Power-on delay | U1 NAKs deterministically for ≈ 200 µs after V_DD crosses its POR threshold. The boot probe shall not issue a transaction before that interval has elapsed |
| Boot probe addresses | `0x39`, `0x53`. An ACK is not identification (M01 §10 precedent); each probe shall be backed by a device-specific read — U1's ID register, U2's part-ID register (`verify`) |
| Re-probe interval | ≈ 60 s (ADR-0014 d8) |
| Publish rule | Responders only; partial populations require no rebuild |
| SMUX | Configured after every power-up, before the first measurement is started. Reference configuration from the vendor application note |
| Acquisition | Two integration cycles per full channel set (§6.1); flicker detection mapped to ADC5 when used |
| Autorange | AGAIN and `t_int` adjusted to hold the brightest mapped channel between 10 % and 90 % of full scale. The setting in force is published with the sample |
| Derived | PPFD per §6.2, from coefficients read out of the deployment profile |
| Not derived | DLI. Owned by the gateway (§3.2) |
| Deployment constants | `c_i` per §6.2 read from the deployment profile, not compiled in |
| Rail failure | Failure of U3 removes U1 from the bus. The boot probe handles this as absence; *not fitted*, *failed* and *unpowered* remain indistinguishable (O-37) |
| Role and zone | Not held by the node; assigned by the gateway (ADR-0014 d7) |
| Node directory | `firmware/nodes/m02_light/` — does not exist |
| Node-ID | 96 is M05's, 97 is M01's; M02 takes **98**, static for bring-up (ADR-0005 d6) |
| Publication rate | 1 s, subject to the two-cycle acquisition time of §6.1 |

### 10.1 Published subjects

Baked defaults in the unregulated range. ADR-0005 d7 makes these `uavcan.pub.<name>.id`
register entries. M05 holds 4096–4102; M01 holds 4112–4121.

| ID | Quantity | Source | Type |
|----|----------|--------|------|
| 4128 | Spectral counts F1–F8, clear, NIR, with AGAIN and `t_int` | U1 | `industryflow.greenhouse.light.SpectralSample` |
| 4129 | PPFD | derived, §6.2 | `industryflow.greenhouse.light.PhotonFluxDensity` (mol·m⁻²·s⁻¹) |
| 4130 | Flicker flags | U1 | `industryflow.greenhouse.light.FlickerStatus` |
| 4131 | UV-A irradiance | U2 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |
| 4132 | Ambient light | U2 | `industryflow.greenhouse.light.Irradiance` (W·m⁻²) |

Five subjects, not one record: a partial population must be able to omit U2's two (ADR-0005 d8).
The eleven channels of 4128 are one device and are not separable, so they are one subject.

The `light` sub-namespace is named by ADR-0005 d1 and contains no files. All four types are
minted because the standard set carries no spectral, photon-flux, irradiance or flicker sample
type (ADR-0005 d2). All are unscaled SI — mol·m⁻²·s⁻¹, W·m⁻² — for the reason ADR-0005 rev 1
gave for joule over watt-hour: µmol and lux are display conventions, and display is the
gateway's concern.

## 11. Verification

| ID | Verifies | Method |
|----|----------|--------|
| V1 | §6.2 | `c_i` identified against a reference quantum sensor in the canopy plane at ≥ 3 fixture output levels, per profile phase spectrum. The project owns no quantum sensor; procedure and instrument undefined, O-52 |
| V2 | §6.1, T1 | One full 24 h cycle logged: no channel saturated and none below 10 % of full scale at any point of either 30 min ramp or the photoperiod. U1 die temperature proxy and canopy air temperature recorded against a reference thermometer |
| V3 | §6.3 | The fixture's 730 nm far-red channel driven alone at full output; F8 and NIR counts recorded. Confirms or refutes the blind band. O-55 |
| V4 | §6.4 | The fixture's UV-A channel driven alone at full output; U2 counts recorded against dark |
| V5 | §5.2 | U1 addressed over ≥ 1 h at the 3.3 V segment with zero bus errors, or the level translator fitted. O-56 |
| V6 | §4.1 | 1:1 paper printout against the physical part, both devices, including U1's aperture offset relative to the pads |
| V7 | §5 | Module-ID readback of `0b010` on the carrier revision in use. O-42 |
| V8 | §9 M1, M2 | Band counts with and without the fitted diffuser under the same fixture setting; the ratio per band is the diffuser's transmission term for §6.2 |

## 12. Open items

Continues the `O-` namespace shared with the M01, M05, M06 and M07 specifications. O-2, O-4,
O-6, O-8, O-10, O-11 and O-42 are M06's; O-12 to O-24 are M07's; O-25 to O-31 are M05's;
O-3, O-5, O-7, O-9, O-32 to O-50 are M01's, and O-51 is M01's item renumbered out of its
collision with M06's O-42.

| ID | Item | Blocks |
|----|------|--------|
| O-52 | PPFD reconstruction coefficients `c_i`: identification procedure, reference instrument, and per-phase-spectrum validity. The project owns no reference quantum sensor | PPFD accuracy, deployment profile, V1 |
| O-53 | U2's fixed address `0x53` lies inside the `0x50`–`0x57` block reserved by ADR-0014 rev 4 d6 for the module-ID EEPROM. M02 identifies by strap and carries no EEPROM, so the address is uncontended today; an EEPROM later fitted to an M02 shall take one of the other seven | EEPROM transport, when it lands |
| O-54 | ADR-0014 d4's alternative UV part, the Vishay VEML6075, was terminated in 2019 and is not selectable | ADR-0014 d4 hygiene |
| O-55 | 730 nm far-red (ADR-0003 d11) falls between F8's 706 nm half-maximum and NIR at 910 nm, and the 365–385 nm UV-A band sits on the falling edge of U2's 300–350 nm peak. The named complement cannot verify two of the four profile spectrum channels | Spectrum-verification claim, ADR-0014 d4 revision |
| O-56 | U1's digital pins operated at 3.3 V, above V_DD, inside the 3.6 V absolute maximum but outside the vendor application example and outside any recommended-operating-condition table | Schematic capture, level-translation decision |
| O-57 | Condensation on excursions places U1 outside its 5–85 %RH non-condensing operating conditions. Post-excursion validity and recovery undefined, as for O-47 | Excursion handling, data validity |
| O-58 | DLI is integrated at the gateway (§3.2). Restart and gap policy across a gateway restart or a telemetry outage is unspecified | Gateway control loop, ADR-0015 |
| O-59 | Node power unmeasured | Distribution-board sizing, O-31 |
| O-60 | U1 responsivity temperature coefficient not established; T2 unquantified | Absolute PPFD stability |
| O-61 | Diffuser part, spectral transmission flatness, and its order against the conformal-coating step are unfixed. The diffuser sits in the measurement path of every band | Enclosure design, §6.2 coefficients, M1/M2/M5 |
| O-62 | `verify` values in this document: all U2 electrical and optical figures, U1's `t_int` and AGAIN ranges, LCSC ordering codes, U3 capacitor values | `L` release |

## 13. Maturity

**Pre-schematic.** U1 and U3 are fixed to ordering part numbers. U2's selection is open: the
part ADR-0014 d4 names measures off the profile's UV-A band and its alternative is discontinued
(O-54, O-55).

| Rung | Content | Reached when |
|------|---------|--------------|
| **Pre-schematic** ← here | Complement and requirements fixed; values estimated or `verify` | — |
| **Schematic captured** | Parts fixed to ordering part numbers; schematic exists; component values determined | O-55 closed for U2; O-56 decided |
| **Schematic-frozen** | Remaining `verify` resolved, footprints checked against physical parts. `L` releases here | O-62 closed; V6 executed |
| **As-built** | Estimates replaced by measured values; verification §11 executed | `E0003` fabricated and bench-verified; O-59 measured |
