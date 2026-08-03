<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# M01-CLIMATE and M06-VENTILATION — consolidated node specification

- **Status:** Working specification, pre-schematic-capture
- **Date:** 2026-08-03
- **E-numbers:** M01-CLIMATE = `E0002`; M06-VENTILATION = `E0008` (assigned 2026-08-03, see `REGISTRY.md`)
- **Governing ADRs:** ADR-0014 (rev 2), ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 1), ADR-0018
- **Supersedes:** `M01-CLIMATE-sensor-complement.md` (2026-08-03, satellite-board draft)

Values marked `verify` are not confirmed against the manufacturer datasheet and must not be
released to an `L` document in that state.

---

## 1. Partition

| | M01-CLIMATE | M06-VENTILATION |
|---|---|---|
| Measures | Air **state** at the plant | Air **transport** through the cabinet |
| ADR-0016 subspace | Biological | Apparatus |
| Location | Canopy | Duct / fan discharge |
| Relocatable | Yes — meaningful at any canopy point | No — meaningless outside the flow path |
| Module-ID strap | `0b001` | `0b110` |
| E-number | `E0002` | `E0008` |

Both are complete Cyphal/CAN nodes on the standard carrier (ADR-0002 rev 3). No satellite boards,
no displaced sensors, no I²C bus buffers, no cable-borne I²C anywhere in either node.

---

## 2. M01-CLIMATE — sensor complement

| # | Sensor | Function | Supply | Rail | I²C address | Address type |
|---|--------|----------|--------|------|-------------|--------------|
| U1 | Sensirion SHT45 | Primary T / RH — VPD source (ADR-0003 d.7) | 1.08–3.6 V | 3.3 V | `0x44` | Fixed (variant `-AD1B`) |
| U2 | Bosch BME688 | Gas / VOC + secondary T, RH, P | 1.71–3.6 V | 3.3 V | `0x76` | Strap — SDO tied low; `0x77` unused |
| U3 | Sensirion SCD41 | True CO₂, photoacoustic NDIR | 2.4–5.5 V | 3.3 V | `0x62` | Fixed |

All board-mounted at canopy. No address conflicts.

### 2.1 Packages

| # | Sensor | Package | Body size |
|---|--------|---------|-----------|
| U1 | SHT45 | DFN-4 | 1.5 × 1.5 × 0.5 mm |
| U2 | BME688 | LGA-8 | 3.0 × 3.0 × 0.93 mm |
| U3 | SCD41 | Custom SMD module | 10.1 × 10.1 × 6.5 mm |

1:1 paper printout check against physical parts is **mandatory** before ordering, for every
footprint. DRC and ERC do not catch footprint mirroring.

### 2.2 Thermal separation

| Heat source | Mechanism | Constraint against U1 |
|-------------|-----------|------------------------|
| U2 BME688 | Gas-sensor hotplate, cycled | Maximum lateral separation; thermal relief slot between U2 and U1 |
| U3 SCD41 | Self-heating, ≈ 1 K typical offset (`verify`) | Maximum lateral separation; no shared copper pour with U1 |

U1 is the declared primary T/RH source. The internal temperature outputs of U2 and U3 are
secondary and are **not** used for VPD.

---

## 3. M06-VENTILATION — sensor complement

| # | Sensor | Function | Supply | Rail | I²C address | Address type |
|---|--------|----------|--------|------|-------------|--------------|
| U1 | Renesas FS3000 | Air velocity in the flow path | 3.3 V ±10 % | 3.3 V | `0x28` | Fixed |
| U2 | Sensirion SDP8xx class, higher range | Differential pressure across the filter — filter loading | `verify` | 3.3 V | `verify` (`0x25` / `0x26` class) | `verify` |
| U3 | Sensirion SDP8xx class, low range | Differential pressure envelope ↔ ambient — leakage characterization | `verify` | 3.3 V | `verify` — must differ from U2, see §3.2 | `verify` |
| U4 | Sensirion SHT45 | In-stream T / RH — density compensation for mass flow | 1.08–3.6 V | 3.3 V | `0x44` | Fixed |

**Not populated, by decision:** a gas sensor of the BME688 class. Its output is an uncalibrated
resistance trend; measured in diluted duct air it answers no question the canopy instance does not
answer better.

**On U4 and duplication.** M06's SHT45 is not a duplicate of M01's. Mass flow = volumetric flow ×
density, and density requires in-stream temperature and pressure. U3 is required for the validity
of M06's own primary quantity, independent of any difference taken against M01.

### 3.1 Volumetric flow — measurement principle

Volumetric flow is computed as `Q = A · v̄`, from the known duct cross-section and the mean
velocity. FS3000 reports **point** velocity; the ratio of mean to point velocity is the velocity
**profile coefficient**, approximately 0.8–0.85 for fully developed turbulent flow and 0.5 for
laminar (`verify`), and not predictable for a disturbed profile.

**Decision:** the profile coefficient is treated as a **commissioning parameter identified per
installation**, not as a value to be engineered away. For a fixed installation it is a constant.
It is identified once at commissioning and carried in the deployment profile.

**Rejected: a calibrated restriction as the primary flow element.** It requires inserting a
constriction into the air path, i.e. modifying the space being measured. IndustryGrow must
instrument a growing space as found; an instrument that alters its subject does not transfer
across deployments (`MOTIVATION`). A restriction remains available as an optional
commissioning-time reference for identifying the profile coefficient, but is not part of the
operating configuration.

### 3.2 Three distinct differential pressures

Differential pressure appears in this module in more than one role. The roles differ in reference
point and in range, and cannot be assumed to share one sensor.

| Role | Reference | Expected range | Sensor |
|------|-----------|----------------|--------|
| Filter loading | Across the filter | Tens to hundreds of Pa (`verify`) | U2 |
| Envelope leakage | Enclosure interior ↔ ambient | Single Pa (`verify`) | U3 |
| Flow across a restriction | Across a calibrated orifice | — | Not populated; commissioning only (§3.1) |

**Filter loading is not a flow measurement.** Filter resistance rises as the filter loads — that
is the quantity being monitored — so pressure drop across it carries two unknowns in one signal.
With flow measured independently by U1, filter resistance is recovered from `Δp` and `Q`, and
loading is separated from flow. U1 and U2 are therefore not redundant; together they decouple the
system.

### 3.3 Leakage as an identified parameter

Unmetered leakage is a property of the enclosure, not a defect to be engineered out. It is
identified, not specified.

**Rejected: leakage as a difference of measured flows** (supply minus exhaust). It is a small
difference of large numbers; at a few percent measurement error per instrument, the error on the
leakage estimate exceeds the estimate itself.

**Method: fan pressurization characteristic.** Flow is stepped across several operating points and
the corresponding envelope-to-ambient pressure recorded; a power-law leakage characteristic is
fitted to the resulting set. This is the blower-door principle (ISO 9972 class), applied at
cabinet scale.

The usual weakness of the method — extrapolating from an elevated test pressure down to operating
pressure — is much reduced here: a cabinet fan produces envelope pressures already within the
operating range, so the characteristic is identified where it is used.

| Requirement | Note |
|-------------|------|
| Flow excitation | Several fan operating points. Manual in Phase 1; commanded once fan control exists |
| Reference port | U3 must reference ambient outside the enclosure, not another point in the duct |
| Sensor range | Single-Pa resolution — a different part from U2 |

### 3.4 No sealed-envelope assumption

Neither node assumes, requires, or specifies a gas-tight enclosure, in any phase. IndustryGrow
instruments a growing space as found; making nominal envelope leakage a load-bearing construction
requirement would make the method non-transferable, which is the structural defect the project
exists to remove (`MOTIVATION`).

Gas exchange — net assimilation, transpiration — is consequently **not** obtained from a
steady-state balance of flow against concentration difference, which would demand a controlled
envelope. It falls out of the identified model (ADR-0016). Identification is driven by informative
**transients**, not by a large steady-state contrast, which also removes the apparent conflict
between the air-change rate needed to hold VPD (ADR-0003 d.7) and the rate at which a
concentration difference stays above sensor resolution.

Identification does not manufacture information absent from the measurements: absolute sensor
accuracy and cross-calibration between independent instruments remain binding (O-2, O-7).

### 3.5 Layout constraint

M06 is installed in an air path that IndustryGrow did not design. FS3000 orientation relative to
flow direction is fixed by its datasheet and constrains board orientation in the enclosure.
Straight-run length upstream of the sensor is a property of the host installation and is not
assumed; its effect is absorbed into the profile coefficient (§3.1).

---

## 4. Power — 3.3 V rail

### 4.1 M01

| # | Sensor | Idle | Typical | Peak | Source |
|---|--------|------|---------|------|--------|
| U1 | SHT45 | ≈ 0.08 µA | ≈ 320 µA during conversion | ≈ 60 mA (heater, optional) | `verify` |
| U2 | BME688 | ≈ 0.15 µA | ≈ 12 mA during heater phase | ≈ 18 mA | `verify` |
| U3 | SCD41 | ≈ 0.15 mA | ≈ 15–18 mA avg, periodic mode | **205 mA** | `verify` |

**Decision:** the TPS54302 3.3 V rail has sufficient steady-state capability alongside the MCU and
CAN transceiver. No rail redesign. The U3 burst is handled by a local bulk capacitor.

#### Bulk capacitor at U3

The capacitor covers the regulator's control-loop response interval, not the full burst. It is
**not** sized to supply the burst outright.

| Parameter | Basis |
|-----------|-------|
| Step current ΔI = I_peak − I_avg | ≈ 190 mA |
| Loop response interval t_r | TPS54302 loop bandwidth — `verify` |
| Allowed droop ΔV | Tightest sensor supply minimum in the complement |
| Required capacitance | C ≥ ΔI · t_r / ΔV, computed at schematic stage |

| Requirement | Value |
|-------------|-------|
| Placement | Directly at U3 supply pins, minimum loop area |
| ESR | Low — ESR × ΔI adds directly to droop |
| Specification basis | **Effective capacitance at 3.3 V DC bias**, not nameplate value |

Class II MLCC (X5R / X7R) loses a large fraction of nameplate capacitance under DC bias. Options:
larger case size and/or higher voltage rating with the manufacturer derating curve checked;
several MLCC in parallel; or polymer / tantalum, which is far less bias-dependent at the cost of
higher ESR.

**SHT45 heater:** disabled in firmware unless condensation recovery is required. If enabled, its
pulse must not overlap the U3 measurement window.

### 4.2 M06

| # | Sensor | Typical | Peak |
|---|--------|---------|------|
| U1 | FS3000 | ≈ 10 mA | `verify` |
| U2 | SDP8xx, filter | `verify` | `verify` |
| U3 | SDP8xx, envelope | `verify` | `verify` |
| U4 | SHT45 | ≈ 320 µA during conversion | ≈ 60 mA (heater, optional) |

No burst load comparable to SCD41. No special bulk-capacitor requirement anticipated, pending
`verify` on U2.

---

## 5. I²C bus — per node

| Node | Bus speed | Set by | Topology |
|------|-----------|--------|----------|
| M01 | **100 kHz** | SCD41 | Single local bus, all sensors board-mounted |
| M06 | `verify` — likely 100 kHz | FS3000 | Single local bus, all sensors board-mounted |

| Sensor max bus speed | | |
|---|---|---|
| SHT45 | 1 MHz | `verify` |
| BME688 | 3.4 MHz | `verify` |
| SCD41 | **100 kHz** | `verify` |
| FS3000 | **100 kHz** | `verify` |
| SDP8xx | `verify` | `verify` |

Local segment capacitance limit is the standard 400 pF in both nodes; with all sensors on-board
this is not a binding constraint.

---

## 6. Mechanical and enclosure

| Node | Sensor | Requirement |
|------|--------|-------------|
| M01 | SHT45 | Diffusion access to cabinet air; no conformal coating over the sensor opening |
| M01 | BME688 | Diffusion access; no conformal coating over the gas port |
| M01 | SCD41 | Air exchange with the measured volume; no conformal coating over the optical cavity |
| M06 | FS3000 | In the flow path at fan discharge or duct; orientation per datasheet flow direction |
| M06 | SDP8xx U2 | Pressure taps upstream and downstream of the filter |
| M06 | SDP8xx U3 | One port referenced to ambient **outside** the enclosure; routing must not pick up duct dynamic pressure |
| M06 | SHT45 | In the flow path, shielded from direct impingement heating |

---

## 7. Firmware

| Item | M01 | M06 |
|------|-----|-----|
| Module-ID strap | `0b001` | `0b110` |
| Boot probe addresses | `0x44`, `0x76`, `0x62` | `0x28`, `0x44`, two SDP addresses `verify` |
| Re-probe interval | ≈ 60 s (ADR-0014 d.8) | ≈ 60 s |
| Publish rule | Only responders registered; partial BOM requires no firmware rebuild | Same |
| Cross-compensation | BME688 barometric pressure → SCD41 pressure-compensation register | In-stream T, P → density for mass flow; U1 flow + U2 Δp → filter resistance |
| Primary T/RH source | U1 SHT45 only | — |
| SCD41 automatic self-calibration | Behaviour in a sealed cabinet unresolved — O-5 | — |

Node role and zone are assigned by the gateway from the position tree (ADR-0014 d.7), not by the
node. M06's role values depend on the ventilation / pollination subsystem boundary — O-6.

---

## 8. Open items

The `O-` namespace is shared with `M07-AMBIENT-specification.md`; items O-12 onward are
listed there. Items needing an ADR to move are tracked on the project kanban.

| ID | Item | Blocks |
|----|------|--------|
| ~~O-1~~ | ~~E-number assignment for M01 and M06~~ — **closed 2026-08-03.** M01 already held `E0002`; M06 assigned `E0008`, the next free number (ADR-0017 d5, sequential, not reserved by class) | — |
| O-2 | All `verify` values unconfirmed against datasheets, including the SDP8xx part selection within the family | `L` release |
| O-3 | M01 bulk capacitor value and dielectric — depends on SCD41 burst duration and TPS54302 loop response | Schematic capture |
| O-4 | Identification procedure for the velocity profile coefficient at commissioning — what reference the coefficient is identified against without permanently altering the air path | Commissioning method, deployment profile |
| O-5 | SCD41 automatic self-calibration in a closed cabinet that may never reach the 400 ppm baseline | Firmware, CO₂ data validity |
| O-6 | Ventilation / pollination subsystem boundary — which fans belong to which subsystem, hence M06's `node_role` values | Role assignment, gateway profile |
| O-7 | Cross-calibration procedure for two independent CO₂ instruments (M01 and any M06 survey population), required before any concentration difference is meaningful | Survey method, research scope |
| O-8 | Functional-subsystem enumeration has no canonical home — ADR-0001 d.7 contains no list, yet ADR-0002, ADR-0014 and ADR-0017 each restate one and cite it as the source | Adding `ventilation` to the enumeration |
| O-10 | SDP8xx part selection: two ranges, two I²C addresses within one family — confirm the family supports two distinct addresses on one bus, otherwise a second bus or a different part is required | M06 schematic |
| O-11 | Flow excitation for leakage identification in Phase 1 — no fan control exists; whether manual stepping is acceptable as a commissioning procedure | Commissioning method |
| O-9 | Fate of ADR-0014 d.3 — the short-lead provision lost its motivating example when FS3000 moved | ADR-0014 hygiene |

---

## 9. Documents affected

| Document | Change | Status |
|----------|--------|--------|
| ADR-0014 | rev 2 — M06 and M07 classes, straps `0b110` / `0b111`, FS3000 moved, catalog 5 → 7, alternative G clarification, module-ID field exhausted, new deferred items | **Applied 2026-08-03** |
| REGISTRY.md | `E0008` (M06) and `E0009` (M07) added; `E0002` note no longer lists FS3000 | **Applied 2026-08-03** |
| README.md | Sensor module catalog five → seven; airflow removed from M01 | **Applied 2026-08-03** |
| ROADMAP.md | M01–M05 scoping made explicit as the Phase-1 set, with M06/M07 defined but unscheduled | **Applied 2026-08-03** |
| ADR-0002 (rev 3) | Restates the subsystem enumeration; also implicated by the field bus leaving the enclosure (O-14) | Reported stale — revision not authorized |
| ADR-0017 (rev 1) | Restates the enumeration; scopes E-numbers to `M01–M05` | Reported stale — revision not authorized |
| ADR-0001 | **No edit required** — decision 7 contains no enumeration to extend | — |
