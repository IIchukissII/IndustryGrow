<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M07-AMBIENT — boundary-condition node specification

- **Status:** Working specification, pre-schematic-capture
- **Date:** 2026-08-03
- **E-number:** `E0009` (assigned 2026-08-03, see `REGISTRY.md`)
- **Governing ADRs:** ADR-0014 (rev 2), ADR-0001, ADR-0002 (rev 3), ADR-0016, ADR-0017 (rev 1), ADR-0018, ADR-0019
- **Companion:** `M01-M06-air-nodes-specification.md`

Values marked `verify` are not confirmed against the manufacturer datasheet and must not be
released to an `L` document in that state.

---

## 1. Why this node exists

M07 measures the **boundary conditions** of the growing space. A state-space model without
boundary conditions is not identifiable — disturbance cannot be separated from response.

**These are not a third ADR-0016 subspace.** ADR-0016 partitions *state variables* into
biological (they receive profile setpoints) and apparatus (they are bounded and alarmed),
and names survival — the hardware interlock floor — as the third **layer**. Boundary
conditions are none of those: they receive no setpoint, they are not a system tolerance,
and they cut no power. They are **exogenous inputs** to the model — the `u(t)` that
biological and apparatus states respond to — which is a different axis from ADR-0016's
partition, not a further division of it. Nothing in the enclosure regulates them.

ADR-0016's partition therefore has no slot for what M07 publishes, and that is a genuine
gap in an accepted ADR rather than an oversight in this specification — recorded as O-24.
Until it is closed, the gateway-side contract for M07 telemetry is unstated: nothing says
whether an M07 publication reaches the state estimator as an exogenous input or falls
through to setpoint-tracking or apparatus alarming, which are the two handlers ADR-0016
does define.

It is not an optional enrichment. Three already-accepted decisions require it:

| Requirement | Origin |
|-------------|--------|
| Barometric pressure for air-density computation | M06 mass flow |
| Ambient reference port for envelope leakage | M06 U3, envelope ↔ ambient Δp |
| CO₂ reference and cross-calibration basis | M01 / M06 SCD41 instruments |

**Test of class membership:** relocate M07 inside the **growing** enclosure and every quantity
it reports loses its meaning — it would then be measuring the state it exists to be the
reference for. This is the same test that separated M06 from M01. Note that the test is about
the growing enclosure, not about being outdoors: an M07 standing in the room next to the
cabinet passes it (§1.1).

| | Value |
|---|---|
| Module-ID strap | `0b111` |
| E-number | `E0009` |
| Deployment | Outside the growing enclosure — see §1.1 for how far outside |
| Sampling rate | Minutes. Participates in no control loop |

## 1.1 Two deployment variants — indoor and outdoor

"Ambient" is not a synonym for "outdoors." It is **whatever the growing enclosure
exchanges heat and air with, one step out.** For a greenhouse that is the weather. For a
cabinet standing in a room it is the room, and the weather is one step further still.
Measuring the weather while the cabinet actually breathes room air would report a boundary
condition the enclosure never sees.

This produces two variants of **one module class**:

| | M07-AMBIENT **indoor** | M07-AMBIENT **outdoor** |
|---|---|---|
| Measures the ambient of | A cabinet or chamber inside a building | A greenhouse, or a building whose interior is the growing space |
| Physical location | The room or hall surrounding the enclosure | Outside the building envelope, typically mast-mounted |
| Wind group | Not populated | Populated at deployment scale (§2.2) |
| Radiation shield | Only under glazing in direct sun | Mandatory (§5) |
| Environmental qualification | Indoor, as M01–M05 | Outdoor: UV, dew cycling, wide temperature range (§5) |
| Field bus leaves the enclosure | No | **Yes** — O-14 |

**Nothing about the node changes.** Same PCB design, same module-ID strap `0b111`, same
E-number `E0009`, same firmware image. The variants differ in **populated BOM** and in
**housing** — dimension three of ADR-0014 decision 2, the same mechanism that already
lets one climate board serve every zone. There is no `E0009-indoor` and no second strap
pattern; proposing one would be the exact multiplication of designs ADR-0014 exists to
prevent.

**Where both boundaries matter, instantiate the class twice.** A cabinet in a room in a
building has a real chain: the cabinet exchanges with the room, and the room exchanges
with the weather. That is two instances of one class in two zones — ADR-0014 decision 1,
unmodified — distinguished by the gateway's `(module_class, node_role, zone)` triple
(ADR-0014 decision 7), not by anything the node knows about itself.

### Why the indoor variant is not a lesser case

Two of its readings are **more** correct indoors than the outdoor instrument's would be:

- **CO₂ reference.** A cabinet vents into the room, so the room's concentration is what
  depletion inside is measured against. An occupied room sits far above the outdoor
  baseline and swings with occupancy; substituting an outdoor reading would misstate the
  difference by hundreds of ppm and, worse, would do so with a diurnal pattern that
  looks like a plant signal.
- **Temperature and humidity.** The room, not the weather, is what the enclosure walls
  conduct into and what unmetered leakage (§3.3 of the companion specification) exchanges
  with.

### Consequence for scheduling

The indoor variant is gated by **none** of the outdoor open items — O-14 (field bus
outdoors), O-16 (cable entry and breathing vents), O-17 (outdoor part qualification),
O-19 (vane-housing electronics), O-20 and O-21 (magnetic calibration and installation
integrity) all belong to the outdoor variant and the wind group. An indoor M07 is a
board with four I²C sensors in an indoor housing on an in-cabinet bus segment — no
harder to build than M01. This is the practical reason to keep the variants named and
separate rather than treating M07 as a single outdoor module: it separates what is
buildable now from what is not.

---

## 2. Sensor complement

### 2.1 Core — populated in both variants, at every scale

"Ambient" below means the room for the indoor variant and the weather for the outdoor
variant (§1.1). The function is identical; only the volume being measured differs.

| # | Sensor | Function | Supply | I²C address | Address type | Indoor | Outdoor |
|---|--------|----------|--------|-------------|--------------|--------|---------|
| U1 | Sensirion SHT45 | Ambient air T / RH | 1.08–3.6 V | `0x44` | Fixed | ● | ● |
| U2 | Bosch BMP390 class | Absolute barometric pressure | `verify` | `0x76` / `0x77` (`verify`) | Strap | ● — see O-23 | ● |
| U3 | Sensirion SCD41 | Ambient CO₂ reference | 2.4–5.5 V | `0x62` | Fixed | ● | ● |
| U4 | Irradiance sensor | Energy-balance input | `verify` | `verify` | `verify` — see O-12 | ● — different range, O-22 | ● |

**U2 across variants.** Barometric pressure does not differ meaningfully between a room
and the weather outside it — a building is not pressure-sealed at this scale — so a
deployment carrying both variants has one measurement duplicated. Which instance carries
U2, and whether the other leaves the footprint unpopulated, is O-23.

**U4 across variants.** Full sunlight and room light are orders of magnitude apart. One
part is unlikely to cover both without saturating on one end or losing resolution at the
other, which turns O-12's quantity question into a part-range question as well — O-22.

**Not populated, by decision:**

| Omitted | Reason |
|---------|--------|
| Gas sensor (BME688 class) | Outside VOC feeds no accepted computation. Its output is an uncalibrated resistance trend |
| Precipitation | Feeds none of the identified quantities |
| Rate gyroscope | Serves moving platforms. M07 is a fixed installation; a gyro adds power and calibration burden with no identified use |

### 2.2 Wind group — outdoor variant only, at deployment scale and above

Wind is a boundary condition only where infiltration responds to wind pressure. The group is
therefore gated on **two** conditions, not one: the instance must be the outdoor variant
(§1.1), *and* the deployment must be at a scale where wind-driven infiltration is a real
term. The indoor variant never populates it — a room has no wind — and an outdoor
cabinet-scale instance does not either. It is left unpopulated under the partial-BOM
mechanism (ADR-0014 d.2), which is also why everything below can be specified in full
without being built.

| # | Component | Function | Interface |
|---|-----------|----------|-----------|
| — | Anemometer, purchased | Wind speed | Pulse train, see §4. Identified by an SP number (ADR-0019) |
| — | Vane with absolute encoder, purchased | Wind direction | Gray code via shift register on SPI, see §3 |
| U5 | Pulse conditioning (RC + Schmitt trigger) | Anemometer signal path | Timer in external counter mode |
| U6 | 74HC165 class shift register | Parallel Gray code → SPI | Located in the vane housing, not on the M07 board |
| U7 | LSM303AGR class | Heading reference and installation-integrity monitor | I²C, `verify` address. On the M07 board. See §3.5 |

Both instruments are external, in their own weatherproof housings, cabled to the M07 enclosure.
This is not a new construction class: M05-SAFETY already carries door and leak sensors on leads,
and DS18B20-class parts are cabled by nature. A node conditions signals; where the transducer
physically sits does not define its module class (ADR-0014 d.1).

---

## 3. Wind direction — vane readout

### 3.1 Encoding: Gray code

The vane hunts continuously around a mean direction, so it crosses code boundaries constantly.
Plain binary encoding fails at exactly those crossings: a transition such as `0111 → 1000` changes
every bit at once, and a read landing mid-transition can return an arbitrary value, including the
opposite direction. Gray code changes one bit per step by construction, so boundary ambiguity
resolves to one of two **adjacent** positions.

**Decision:** absolute encoder, Gray-coded, purchased. Decoding to binary is trivial in firmware
(`b = g; while (g >>= 1) b ^= g;`).

### 3.2 Transport: shift register on SPI

The encoder's parallel output is serialized in the vane housing by a 74HC165-class shift register
and reaches the node over SPI.

| | Assessment |
|---|---|
| **Parallel Gray code direct to GPIO** | Not a bus — static levels, no clocking, no timing, no state. Electrically the most robust option, but it cannot be expressed in the header contract of ADR-0014 d.5, which carries named interfaces rather than a count of general-purpose lines. Encoder resolution would also propagate into that contract |
| **SPI via shift register** | **Selected.** Unidirectional, push-pull, clocked by the master, no pull-ups, no bus-capacitance ceiling. Already present in the d.5 header |
| **I²C** | **Rejected.** Open-drain, bidirectional, 400 pF ceiling. This is precisely the constraint that drove the abandoned buffered-satellite design; it is not being reintroduced |

Consequence: encoder resolution becomes a property of the purchased instrument and does not touch
the header contract. Four bits (16 points, 22.5°) or twelve bits present the same three lines to
the node.

### 3.3 Cost of the shift register

Active electronics move into the vane housing, which previously had none. That housing then needs
supply and ground on the cable, and inherits condensation, temperature-range, and ingress concerns
in its own right.

### 3.4 Rejected: sensors mounted on the rotating vane

Placing FS3000 and a magnetometer on the vane itself is attractive on paper — the vane keeps the
anemometer's channel aligned with the flow, turning FS3000's single-axis limitation into a normal
operating condition, and a magnetometer on the rotating body would give absolute wind direction
with no reference at all.

**Rejected:** powering and reading a continuously rotating body requires slip rings or a wireless
link with local power generation. That is a self-contained engineering problem outside the scope
of controlled-environment agriculture. Recorded here so the idea is not re-proposed as novel.

FS3000 also saturates silently at its range ceiling, which in a wind record reads as calm during a
gale. A cup anemometer has no such ceiling.

### 3.5 Heading reference and installation-integrity monitor

An accelerometer/magnetometer pair (LSM303AGR class) is **populated**, on the M07 board, inside the
enclosure.

#### What makes it work here

Wind azimuth is `enclosure heading + mounting offset + encoder reading`. The M07 enclosure carries
**mast guides**, which fix the enclosure-to-vane-base orientation mechanically. The mounting offset
is therefore a constant of the assembly, not a variable of the installation, and the magnetometer
supplies the first term directly.

#### What it does not do

It does not remove a manual commissioning step; it **exchanges one for another**. Entering an
azimuth by hand is replaced by calibrating hard-iron and soft-iron distortion on site. Mast,
fixings, and nearby ferrous structure can bias an uncalibrated heading by tens of degrees. The
calibration routine requires rotating the assembly through all orientations, which is awkward once
mast-mounted; calibrating beforehand risks the surrounding environment invalidating the result.

#### The actual justification

A profile-entered azimuth is correct on the day of installation and **never notices** that the mast
was turned, struck, or resettled afterwards. A live heading does. This is drift detection applied
to the installation itself, which is a category the project already treats as part of the method
(`MOTIVATION`).

The accelerometer is not merely tilt compensation for the magnetometer. It is an
**installation-integrity monitor**: mast out of plumb, mast displaced, assembly struck or shifted.
Polled at minute scale, its power cost is negligible.

A rate gyroscope remains **not populated**: it serves moving platforms, and M07 is a fixed
installation.

### 3.6 Cross-check between vane and anemometer

Two instruments already required for their own sake yield a failure check neither provides alone.

| Observation | Inference |
|-------------|-----------|
| Anemometer reports speed well above the starting threshold, vane direction static | Vane seized, detached, or its cable open |
| Vane moving, anemometer static above the threshold | Anemometer seized or its pulse path broken |
| Both static, other channels indicate air movement | Cable or conditioning fault common to both |
| Both static, low wind | **Calm — not a fault.** Below the cup starting threshold the vane also drifts idly |

The last row is the one that matters: the discriminator is measured speed relative to the starting
threshold, not the presence of a zero. Under ADR-0014 alternative G, redundancy is admissible for
failure-mode coverage, and here it costs nothing — no sensor was added for it.

### 3.7 Magnetic compatibility with the encoder

Where the purchased encoder reads its tracks magnetically rather than optically, its field must not
reach U7. The vane sits at the top of the mast in its own housing; U7 sits in the enclosure at the
base, so separation is expected to settle this. It is nonetheless a **check at model selection**,
not an assumption — O-13.

## 4. Wind speed

**Decision:** purchased anemometer with a pulse output, identified by an SP number (ADR-0019).

Frequency is proportional to wind speed. The signal path follows the pattern already established
in this project for S0 energy metering: RC network and Schmitt trigger in hardware, timer in
external counter mode, no software debouncing. A low-frequency square wave is the most
cable-tolerant signal in the whole node.

The pickup magnet, where the instrument uses one, is internal to the purchased device and metres
away from the M07 board.

| Item | Requirement |
|------|-------------|
| Pulses-per-revolution and the speed conversion constant | Properties of the specific model. They live in the SP record and the deployment profile, **not** in firmware. Substituting another anemometer would otherwise corrupt the series silently |
| Starting threshold | A cup anemometer has a speed below which it does not turn. Zero therefore means *unknown*, not *calm*, and telemetry must distinguish the two |

| Alternative | Why not |
|-------------|---------|
| Ultrasonic instrument | No moving parts and both quantities from one device, at materially higher cost. Reconsider if bearing wear proves limiting |
| Analog output (0–5 V, 4–20 mA) | Found on industrial models with integrated electronics, at higher cost. Pulse output is both cheaper and a better fit for the established hardware pattern |

## 5. Outdoor deployment — consequences

**This entire section applies to the outdoor variant only** (§1.1). The indoor variant
inherits the environmental assumptions of M01–M05 unchanged: an indoor housing on an
in-cabinet bus segment, no radiation shield unless it sits under glazing in direct sun, no
UV, no dew cycling, no outdoor temperature range, and no field bus leaving the enclosure.
Nothing below gates it.

The outdoor variant is the **first module deployed outside the building envelope**. That is
a new class of requirement for the project, not a variant of an existing one.

| Concern | Requirement |
|---------|-------------|
| Radiation shield | Mandatory for U1 and U3. Direct sun on an unshielded housing biases air temperature by several kelvin, corrupting every boundary condition derived from it |
| Air access vs. water ingress | U1, U3 and U4 need air exchange; the housing needs weather protection. These conflict and are resolved mechanically, not electrically |
| Temperature range | Outdoor range exceeds the indoor modules' assumptions. Every part's operating range requires re-check, not inheritance from M01 |
| Condensation | Repeated dew cycling on an outdoor board. Conformal coating everywhere except sensor apertures |
| UV | Housing material, not board |

### 5.1 The field bus now leaves the enclosure

This is the consequence with the widest blast radius and it belongs to ADR-0002, not here.

| Concern | Note |
|---------|------|
| Surge and transient protection | A bus run outdoors, possibly up a mast, is exposed in a way no in-cabinet segment is |
| Grounding and shield policy | Currently framed for a fully enclosed installation |
| Cable length and termination | An outdoor run may change the physical topology assumptions |
| Power | +12 V distribution (ADR-0018) now leaves the enclosure alongside the bus |

Tracked as O-14. **Not** resolved in this document.

---

## 6. Firmware

| Item | Value |
|------|-------|
| Module-ID strap | `0b111` |
| Boot probe addresses | `0x44`, BMP390 address `verify`, `0x62`, U4 `verify`, U7 `verify` |
| Re-probe interval | ≈ 60 s (ADR-0014 d.8) |
| Publish rule | Only responders registered. Neither the unpopulated wind group nor the indoor/outdoor variant split (§1.1) requires a firmware variant — the boot probe finds what is fitted |
| Variant awareness | **None.** The node does not know whether it is the indoor or the outdoor instance. Which ambient it measures is a `zone` and `node_role` fact held by the gateway (ADR-0014 d.7), exactly as for two M01 instances in different zones |
| Wind speed acquisition | Timer in external counter mode; conditioning in hardware, no software debouncing |
| Wind direction | SPI read of the shift register, then Gray-to-binary decode, then addition of the U7 heading and the assembly's constant mounting offset |
| Heading and tilt | U7 polled at minute scale. Tilt-compensated heading. Hard-iron and soft-iron coefficients are deployment data, not firmware constants |
| Installation integrity | Tilt and heading deviation from the commissioned values published as a diagnostic, not silently corrected |
| Vane / anemometer cross-check | Evaluated against the anemometer starting threshold, never against a bare zero (§3.6) |
| Role and zone | Assigned by the gateway from the position tree (ADR-0014 d.7), not by the node |

---

## 7. Open items

The `O-` namespace is shared with `M01-M06-air-nodes-specification.md`; items O-2 to O-11
are listed there. Items needing an ADR to move are tracked on the project kanban.

The **Variant** column records which of the two deployment variants (§1.1) an item gates.
Items marked *outdoor* do not block an indoor M07 in any way.

| ID | Variant | Item | Blocks |
|----|---------|------|--------|
| ~~O-1~~ | both | ~~E-number assignment for M07~~ — **closed 2026-08-03.** Assigned `E0009`, the next free number after M06's `E0008` (ADR-0017 d5, sequential, not reserved by class) | — |
| O-12 | both | Irradiance quantity: PPFD for comparability with M02-LIGHT, or broadband irradiance, or illuminance. Not interchangeable, must not be mixed in one time series. M02's part is calibrated for a narrowband luminaire at closed-space levels and cannot be assumed usable under sunlight | U4 part selection |
| O-22 | both | Irradiance **range** across variants: full sunlight and room light are orders of magnitude apart. Whether one part covers both, or the variants populate different parts on the same footprint, or the footprint itself must differ. Compounds O-12 — quantity and range are separate questions and both must be answered before U4 is chosen | U4 part selection, footprint |
| O-23 | both | Barometric duplication: U2 reads effectively the same value indoors and outdoors, so a deployment carrying both variants measures it twice. Which instance carries U2, whether the other leaves the footprint unpopulated, and which one the deployment profile names as the pressure source for M06 density and SCD41 compensation | Partial-BOM rule, deployment profile |
| O-13 | outdoor | Encoder track readout — optical or magnetic. If magnetic, confirm its field does not reach U7 at the mast-to-base separation | Vane model selection |
| O-19 | outdoor | Supply, condensation and temperature qualification for the shift register in the vane housing | Vane housing design |
| O-20 | outdoor | Hard-iron and soft-iron calibration procedure for U7 — performed on site with the assembly mast-mounted, or beforehand with the risk that the installed environment invalidates it. Coefficients belong to the deployment, not the module | Commissioning method |
| O-21 | outdoor | Commissioned reference values for tilt and heading, and the deviation thresholds at which an installation-integrity diagnostic is raised | Commissioning method, telemetry schema |
| O-14 | outdoor | Field bus leaving the enclosure: surge protection, grounding, topology, outdoor +12 V | ADR-0002 revision, ADR-0018 review |
| O-15 | outdoor | Anemometer and vane model selection, SP records, and the conversion constants they carry | M07 BOM, deployment profile schema |
| O-16 | outdoor | Cable-entry and breathing-vent design for the M07 enclosure — several entries, and diurnal condensation inside a sealed box | Enclosure design |
| O-17 | outdoor | Outdoor temperature and condensation qualification for every part inherited from indoor modules | BOM release |
| O-18 | both | 1-Wire is not part of the ADR-0014 d.5 header contract, yet DS18B20-class parts are intended for systematic use | Header contract review at the next carrier revision |
| O-24 | both | ADR-0016's state-vector partition has no category for **exogenous inputs**. Biological and apparatus partition *state*; survival is an interlock layer. Everything M07 publishes is `u(t)`, which fits none of them, so the gateway-side handling contract for M07 telemetry is unstated — nothing routes it to the state estimator rather than to setpoint tracking or apparatus alarming | ADR-0016 revision; gateway estimator contract |

---

## 8. Phase position

M07 is **defined, not scheduled**. Phase-gate order stands: E0001 bring-up, then E0006 fabrication,
then any new module. Neither M06 nor M07 is laid out or fabricated, and the ADR-0014 rev 2 catalog
records them as such so the catalog is not later misread as an inventory of existing hardware.

The two variants of §1.1 do **not** sit at the same distance from buildable. The indoor
variant is four I²C sensors in an indoor housing on an in-cabinet bus segment; once U4 is
settled (O-12, O-22) nothing else blocks it. The outdoor variant carries eight further open
items, one of which — O-14, the field bus leaving the enclosure — is an ADR-0002 revision,
not a module concern. If M07 is ever pulled forward, it will be the indoor variant that
moves; that is a scheduling fact worth recording now, while it is cheap, rather than
discovering it when the module is scheduled.
