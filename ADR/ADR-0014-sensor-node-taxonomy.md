<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0014 — Sensor node taxonomy and module decomposition

- **Status:** Accepted
- **Date:** accepted 2026-05-16; changed 2026-10-19
- **Parent:** ADR-0001
- **Related:** ADR-0002 (carrier and core board every node is built from), ADR-0003 (the quantities the profile requires measured), ADR-0016 (soft sensors, apparatus subspace, survey density), ADR-0017 (E-numbers, per-carrier firmware, instance identity), ADR-0018 (M05 realised as the cabinet distribution and monitoring board), ADR-0019 (SP numbers for purchased wind instruments), ADR-0027 (carrier Node-ID store inside the reserved identification block), ADR-0031 (actuator classes in the same class-ID space)

## Context

ADR-0001 decomposes the cabinet machine into modules by functional subsystem, and ADR-0002 builds every node from one carrier design plus a module that varies by node type. Neither states which sensors group onto which module, how a module class is identified, or how the pattern holds from an apartment cabinet to a commercial greenhouse without new node classes.

A **zone** is the spatial extent within which conditions are uniform enough, or predictable enough from one sensing point by dynamic modelling, that one sensor instance per subsystem suffices. An apartment cabinet is one zone; a rack of chambers is a few; a large greenhouse has many, with boundaries found from operational data. Zone count is a deployment choice, not an architectural one.

Within a zone, one accurate sensor with a dense time series and a model outperforms co-located copies. Sensors with different spatial requirements cannot share a board without one of them sitting in the wrong place. High-impedance analog front-ends must be segregated from digital sensors. Every part must be available from distributors with traceable datasheets.

## Decision

1. **One Cyphal node per functional subsystem × zone.** A subsystem maps to one node class (one module design on the carrier); each zone in which that subsystem is monitored is one instance of that class. Scaling multiplies instances and adds no classes.
2. **Instances of one design vary in location, quantity and populated BOM.** An instance may leave footprints unpopulated to specialise for its zone; firmware publishes only what responds (decision 8). The partial BOM lets seven designs cover every zone-specific need without new designs.
3. **A sensor with a spatially incompatible requirement may sit on a short lead from its module.** This is the cabinet-scale answer only; at inter-zone distances, a separate instance is.
4. **The sensor module catalogue is seven designs, each serving one medium or boundary:**
   - **M01-CLIMATE — air state at the canopy.** SHT45 (primary T/RH for VPD, ADR-0003 d7), BME688 (VOC trend as early stress signal), SCD41 (photoacoustic CO₂, needed because the crop depletes CO₂ in a closed cabinet). All board-mounted at canopy; airflow is M06's, because air state and air transport have different valid locations.
   - **M02-LIGHT — photic environment.** AS7343 spectral sensor, whose bands cover every channel ADR-0003 d11 commands, far-red included; TSL2585 UV-A channel as independent check of the UV-A trace, reporting a relative quantity since its responsivity is a typical value; a TCA9543A two-channel I²C switch, because both sensors share one fixed address, which also performs the module's level translation. M02 observes no UV-B or UV-C: no procurable part measures them and the profile commands neither. PPFD is reconstructed on the node from the bands with per-luminaire coefficients carried in the deployment profile; DLI is integrated at the gateway as a soft sensor (ADR-0016 d5), because the node holds no wall clock, no integration state across a reset and no deployment identity.
   - **M03-ANALYTICS — nutrient solution.** pH front-end with a femtoampere-bias op-amp, guard ring and 24-bit Σ-Δ ADC; EC by AC excitation and synchronous demodulation; solution temperature by sheathed 1-Wire sensor; pH and EC chains galvanically isolated; space reserved for ion-selective electrodes. One instance per hydroponic loop at every scale, since its sensors are co-located by necessity.
   - **M04-PLANT — plant level.** MLX90640 thermal imager delivering a canopy thermal field with on-node summary statistics and full frames served over Cyphal file transfer on a slow period or on event. Per-leaf temperature for leaf VPD is not delivered: it needs a canopy-segmentation pipeline, and raw-frame radiometric accuracy does not support it.
   - **M05-SAFETY — cabinet power and safety, sense-only.** Realised as the cabinet distribution and monitoring board (ADR-0018 d9): the sensor-bus INA226 and the S0 energy input (ADR-0018 d5), a TMP117 reporting cabinet temperature, a door reed and a leak strip on leads, report and alert only. It also counts the climate apparatus's condensate collector as a second pulse input on the S0 pattern; the count is an apparatus quantity (ADR-0016 d20), and transpiration derived from it is a gateway soft sensor, never a node quantity. The over-temperature trip is the heating actuator's (ADR-0018 d10).
   - **M06-VENTILATION — air transport.** FS3000 velocity in the flow path, with volumetric flow from the duct cross-section and a velocity-profile coefficient identified per installation; a higher-range differential-pressure sensor across the filter, separating filter loading from flow; a low-range one, envelope to ambient, for the fan-pressurisation leakage characteristic; an in-stream SHT45 for density compensation. No gas sensor: diluted duct air answers nothing the canopy instance does not answer better. No sealed enclosure is assumed; gas exchange comes from the identified model (ADR-0016).
   - **M07-AMBIENT — the boundary one step outside the enclosure.** Ambient is what the enclosure exchanges with: the room for a cabinet, the weather for a greenhouse. One design, one class ID and one firmware image serve an **indoor** and an **outdoor** variant that differ only in populated BOM and housing; where both boundaries are real, they are two instances in two zones (decision 1). It carries SHT45 ambient T/RH, a BMP390-class barometer feeding M06 density and every SCD41's pressure compensation, an SCD41 CO₂ reference, an irradiance sensor, and on the outdoor variant only a pulse-output anemometer, a Gray-coded vane read over SPI and an LSM303AGR-class heading and installation-integrity monitor. Vane and anemometer cross-check each other against the starting threshold. Precipitation is not measured.
5. **All modules share one header contract carrying a superset of interfaces.** It carries the carrier's 3.3 V rail, separated analog and digital ground, I²C, SPI with chip selects, general-purpose I/O, ADC inputs, a 1-Wire line, PWM for actuator modules, and module identification by the transport the carrier revision provides (decision 6). Counts and pin assignment per revision are the carrier specification's; a revision adds positions and never repurposes one already assigned.
6. **A module class is an 8-bit class ID, held on the module; the identifier is fixed here, the transport per carrier revision.** Two transports exist: a 3-bit strap carrying `0x01`–`0x07` on strap carrier revisions, and a serial EEPROM, byte 0, read over the header I²C on later revisions. Every sensor class fits the strap range; actuator classes do not. The ID is programmed at manufacture, not writable in the field, and identifies the class, not the instance; instance identity is ADR-0017 d8's and ADR-0027's.
   - I²C addresses `0x50`–`0x57` are reserved project-wide for identification EEPROMs; no sensor on any module occupies them.
   - Encoding: `0x00` reserved; `0x01`–`0x7F` sensor classes; `0x80`–`0xFE` actuator classes, allocated by ADR-0031; `0xFF` is never a class, since an erased EEPROM reads it, and is treated as unidentified.
   - Sensor classes: `0x01` M01-CLIMATE, `0x02` M02-LIGHT, `0x03` M03-ANALYTICS, `0x04` M04-PLANT, `0x05` M05-SAFETY, `0x06` M06-VENTILATION, `0x07` M07-AMBIENT.
   - A firmware image is built for one carrier revision (ADR-0017 d16). Where a revision reallocates the strap positions, a mismatched pairing is an electrical fault one way and a misidentification the other, and is not handled in software; whether a carrier falls back between transports is its pin map's decision.
7. **Role and zone are tagged at the gateway, not on the node.** The gateway maps each Node-ID to a `(module_class, node_role, zone)` triple from its configuration. The node publishes the subjects of the sensors it detects and carries no role or zone, so one design and one image serve every role at every scale.
8. **Firmware probes sensor presence at boot and re-probes periodically.** Sensors that respond are registered and published; sensors that do not are logged and stay silent. The re-probe recovers a sensor lost to a transient bus error.
9. **Out of scope:** actuator modules (ADR-0031); the camera, which connects to the gateway and is not a Cyphal node; PCB layout of any module; the pH/EC front-end schematic and isolation detail; zone-identification methodology for large deployments; the representation of `node_role` and `zone` in the IndustryFlow data model (ADR-IF-0001).

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 1 | **A.** One module carrying every sensor | Poor fault isolation, a large board, and against ADR-0001's subsystem decomposition |
| 1 | **B.** One module per individual sensor | Mostly empty boards; NRE dominates across 15–20 designs |
| 1 | **C.** Group sensors by interface type, not function | Against the subsystem decomposition; loses fault isolation |
| 1 | **E.** Delegate pH/EC to external transmitters over RS-485 / Modbus | A second, vendor-specific protocol; no raw electrode signal; fragmented supply chain |
| 1 | **G.** Sensor proliferation for redundancy within a zone | A model on one dense time series outperforms co-located copies; shared sensor types across M01, M06 and M07 measure different quantities at non-interchangeable locations and are not redundancy |
| 2 | **D.** Split each subsystem into several PCB designs | Multiplies design effort; the partial BOM meets the same need |
| 3 | **F.** One PCB per subsystem with every sensor at one point | Climate and safety sensors have incompatible spatial requirements |
| 4 | Airflow sensor on M01-CLIMATE | Air transport and air state have different valid locations; one module holds both only with a displaced sensor |
| 4 | **Q.** AS7341 on M02, far-red reported unverified or inferred from command | Its bands are blind at the commanded far-red; inferring a channel from its own command is not a measurement |
| 4 | **R.** LTR390 for UV-A | Response peaks below the commanded band; its fixed address sits in the reserved identification block |
| 4 | **R.** VEML6075 for UV-A/UV-B | Terminated by the manufacturer; no successor measures UV-A and UV-B separately |
| 4 | **S.** Discrete far-red photodiode with interference filter | Adds an analog chain and ADC channel for one band a spectral part carries, and a second calibration path; reopens mixed-signal content outside M03 |
| 4 | **T.** AS7331, waiting for supply | Not procurable on any surveyed channel; its two ordering codes differ only in reel size |
| 4 | **U.** Another integrated UV part | Every surveyed alternative is withdrawn or unprocurable |
| 4 | **V.** Discrete GaN or SiC UV photodiodes with an analog front end | Surviving parts are expensive, unstocked raw diodes; sub-nanoamp currents need a guarded transimpedance stage and own calibration, mixed-signal content on the simplest module |
| 4 | **W.** Move the AS7343 off the shared address | Its address is fixed, and it is the only spectral part meeting ADR-0003 d11 with distributor stock |
| 4 | **X.** Separate the two sensors with the level translator's enable | A pass-FET pair cannot separate two devices on one address in both directions; a second translator spends header GPIO |
| 4 | Per-leaf leaf VPD from raw thermal frames as a delivered capability | Needs canopy segmentation; raw-frame accuracy does not support it |
| 4 | Per-load INA226 monitoring with terminal blocks on the zone module | Replaced by source-side metering at the cabinet (ADR-0018 d5) |
| 4 | TMP117 on M05 as the over-temperature cutoff | The trip must be MCU-independent and sit with the element it cuts (ADR-0018 d10) |
| 4 | **H.** Calibrated restriction as M06's primary flow element | Inserts a constriction into the air path being measured; remains usable only as a commissioning reference |
| 4 | **I.** Envelope leakage as supply flow minus exhaust flow | A small difference of large numbers; the error exceeds the estimate |
| 4 | **J.** Parallel Gray code from the vane direct to GPIO | Encoder resolution would enter the header contract, which carries named interfaces, not line counts |
| 4 | **J.** I²C for the vane link | Open-drain and capacitance-bound over a cable |
| 4 | **K.** Sensors on M07's rotating vane | Needs slip rings or wireless power; a thermal anemometer saturates silently, reading a gale as calm |
| 4 | **L.** Ultrasonic or analog-output wind instruments | Higher cost; a pulse output fits the existing S0 counting path. Reconsidered if bearing wear limits |
| 4 | **Y.** Condensate rate on M04-PLANT as a transpiration measurement | The transducer is in the apparatus drain; transpiration follows only through a conversion with an unobserved loss term |
| 4 | **Z.** Condensate rate published by A01-CLIMATE | An actuator publishes no process variable (ADR-0031 d8) |
| 4 | **AA.** Level sensor in the condensate vessel | Gives volume not rate, is lost on emptying or overflow, and is dominated by meniscus and tilt in a small vessel |
| 6 | **M.** Widen the parallel strap to four or eight pins | Four is one growth step from exhaustion; eight spends header pins and resistors on a value fixed at manufacture |
| 6 | **N.** I²C port expander with strap resistors | Keeps identity as wiring and adds parts against one EEPROM, at the same address cost |
| 6 | **O.** SPI shift register | Spends a chip select M03 and M07 contend for; a class never changes, unlike the vane value |
| 6 | **P.** Resistor ladder into an ADC pin | 256 codes need ratiometric accuracy a divider cannot deliver |
| 6 | A physically separate strap field for actuator modules | No such field exists on the carrier; a namespace in the same field needs no pins |

## Consequences

**Positive**

- A cabinet and a greenhouse use identical designs, firmware and DSDL, differing in instance count and gateway tagging.
- Each module is replaceable on its own, and a fault in one subsystem does not propagate to another.
- Module identification is automatic at boot; the 8-bit field serves sensor and actuator classes and widens without hardware change.
- The partial BOM specialises an instance without a new design, and sensor count tracks operational need rather than area.
- M02 observes every channel the profile commands.

**Negative**

- M03 is a complex mixed-signal design.
- Gateway configuration (zones, roles, validation) grows with zone count and needs tooling.
- Two carrier identification transports coexist, so every node records its carrier revision and flashing is a step that can be got wrong.
- EEPROM identity depends on the I²C bus; an unreadable ID is unidentified, never a guessed class.
- M02's spectral sensor needs three integration cycles per full set, lowering its publication rate.
- Both M02 sensors are single-sourced at one vendor, and no second UV-A source exists.
- Every M02 transaction is preceded by a switch-channel write; a wrong channel presents as a silently absent sensor, so the presence probe runs per channel.
- UV-B, UV-C and out-of-band UV emission are not observable on M02.
- On-node PPFD depends on luminaire coefficients the module cannot detect going stale.
- M04 on-node aggregation is a non-trivial firmware concern.
- The condensate count needs a second pulse input that the first M05 board does not carry.
- The outdoor M07 is the first module outside the building envelope; its qualification and the field bus leaving the enclosure fall to its specification.

## Deferred

| Open question | Settled by |
|---|---|
| pH/EC front-end schematic, layout and isolation | M03 schematic capture; may take its own ADR |
| Zone-identification methodology for multi-zone deployments | First multi-zone deployment |
| Module versioning when a named part reaches end of life | First module revision forced by supply |
| `node_role` and `zone` in the IndustryFlow data model | ADR-IF-0001 or its extension |
| M07 irradiance quantity and range, and whether one part serves both variants | Before the M07 footprint is committed |
| Reference instrument for the PPFD coefficients | Commissioning procedure; bounds every reported DLI |
| Which M07 instance carries barometric pressure when both variants are deployed | M07 deployment profile |
| Magnetic compatibility of the vane encoder with the heading reference | Wind-instrument model selection |
| On-site or prior hard- and soft-iron calibration for M07 | M07 commissioning procedure |
| M06 velocity-profile coefficient identification without altering the air path | M06 commissioning procedure |
| `node_role` values M06 takes across ventilation and pollination | M06 specification |
| Whether decision 3 remains a general provision | Next module needing a lead for an I²C sensor |
| Volume per count of the condensate collector, and the second pulse input on M05 | A01 mechanical design (`O-109`), next M05 revision (`O-116`) |

## References

- Module specifications: `spec/E0002`, `E0003`, `E0005`, `E0006`, `E0008`, `E0009` `-R-specification.md`; carrier header and pin maps: `spec/E0001-R-specification.md`.
- `REGISTRY.md` — E-numbers per module class.
- Firmware: `firmware/common/node/main.c` (class-ID read), `firmware/nodes/*/sensors.c` (presence probing).
- ISO 9972 — fan-pressurisation air permeability, applied at cabinet scale for M06.
- Datasheets: Sensirion SHT45, SCD41, SDP8xx; Bosch BME688, BMP390; Renesas FS3000; ams OSRAM AS7343, TSL2585; TI TCA9543A, LMP7721, INA226, TMP117; Melexis MLX90640; Analog Devices AD5933, ADuM series; ST LSM303AGR.
