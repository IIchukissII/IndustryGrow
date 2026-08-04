<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0014 (rev 3): Sensor node taxonomy and module decomposition

- **ID:** ADR-0014 (rev 3)
- **Status:** Proposed
- **Date:** 2026-05-16 (rev 1: 2026-06-14; rev 2: 2026-08-03; rev 3: 2026-08-04)
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0003, ADR-0016, ADR-0017 (rev 2), ADR-0019
- **Supersedes:** ADR-0014 (initial draft, 2026-05-16), ADR-0014 (rev 1, 2026-06-14), ADR-0014 (rev 2, 2026-08-03)

## Revision history

- **rev 1 (2026-06-14)** — Softened the M04-PLANT MLX90640 entry to separate delivered capability (canopy thermal field + on-node summary statistics) from per-leaf temperature for leaf-VPD, now marked deferred pending a canopy-segmentation pipeline and bounded by raw-frame radiometric accuracy. Corrects an over-claim that read leaf-VPD as a current capability and aligns with ADR-0016's state-estimation framing of leaf VPD. No decision changed; telemetry detail unchanged.
- **rev 3 (2026-08-04)** — Records five measurement-method alternatives, H to L, that were held only in the module specification documents: M06's flow element and leakage method, and M07's vane-readout transport, rotating-vane sensor placement and wind-instrument type. Corrects decision 2's illustrative example, which still cited an airflow sensor rev 2 moved off M01. No decision changed.
- **rev 2 (2026-08-03)** — Added two sensor module classes: M06-VENTILATION (air transport) and M07-AMBIENT (boundary conditions outside the growing space). Moved the Renesas FS3000 anemometer out of M01-CLIMATE into M06; M01 is now fully board-mounted at canopy with no displaced sensor. Assigned module-ID straps `0b110` and `0b111`, exhausting the 3-bit field, and recorded the widening of that field as an obligation of the next carrier revision rather than a future trigger. Clarified that instances of a shared sensor type across M01, M06 and M07 do not constitute in-zone redundancy under alternative G. Recorded that M07 has indoor and outdoor deployment variants — one PCB design, one strap, one firmware image, differing only in populated BOM and housing — and that the outdoor variant is the first module deployed outside the building envelope, with its consequences for the field bus. Added deferred items for M07's irradiance quantity and range, wind instrumentation, magnetic interference between rotary readout and heading reference, flow-coefficient identification, the ventilation/pollination subsystem boundary, and the fate of decision 3.

## Context and problem

ADR-0001 (decision 7) committed IndustryGrow to a data model where the cabinet `machine` decomposes into `modules` corresponding to functional subsystems (climate, lighting, irrigation, plant monitoring, pollination, power/safety). ADR-0002 (rev 3) committed to a unified compute platform: a WeAct STM32F4 core board (STM32F405RGT6) on a custom carrier PCB with a sensor-module header — one carrier design across the project, with the sensor module varying between node types.

What ADR-0002 leaves implicit is **how sensors group into modules**: which sensors live on which sensor module PCB, which physical Cyphal node implements which IndustryFlow module, and what the principle is for adding new sensor types. This ADR fills that gap and establishes the taxonomy that future cabinets and community-contributed deployments are expected to follow.

ADR-0001 commits IndustryGrow to scale across deployment sizes — from an apartment cabinet (~1 m³) to a several-hundred-square-meter commercial facility. The taxonomy in this ADR must be the **same architecture at both ends**; only the application of the pattern changes with scale. This is the central architectural concern: a pattern that requires redesign when scaling from cabinet to greenhouse would violate the platform's defining promise.

**rev 3.** The module specifications in `spec/` were restructured to carry requirements only —
measured quantities, ranges, accuracies, verification — with rationale left to the ADRs. Five
measurement-method alternatives had been recorded only in those specification documents and so
had no ADR home: two from M06 (the primary flow element, and the method for obtaining the
envelope leakage characteristic) and three from M07 (vane-readout transport, sensors on the
rotating vane, and wind-instrument type). Each is a decision about how a module class obtains
its quantity, which is this ADR's subject. They are recorded below as alternatives H to L.

A separate concern is the **density of sensors per zone**: within a single zone of relatively uniform environmental conditions, multiplying redundant sensors yields diminishing returns once a single accurate sensor produces dense time-series data — time-series modelling fills in spatial details better than co-located sensor copies. Spatial coverage instead comes from instantiating the same node across **different** zones. The two ideas combine: design for instance multiplication (across zones), reject redundancy multiplication (within a zone).

## Decision drivers

- **One architecture, all scales.** From apartment cabinet to large commercial greenhouse, the same PCB designs, firmware, and DSDL types apply. Scaling is achieved by multiplying instances, not by introducing new node classes.
- **Functional subsystem × zone = Cyphal node-class × instance.** A subsystem maps to a Cyphal node-class (one PCB design). Each zone in which that subsystem operates is one instance of that node-class.
- **Designs few, instances many.** PCB design effort is high; instantiation is cheap.
- **Time-series + model over spatial redundancy within a zone.** A single accurate sensor with dense time-series outperforms multiple redundant sensors in the same zone. Spatial redundancy is justified only when (a) measuring fundamentally different physical quantities not derivable through modelling, or (b) safety redundancy is required for failure-mode coverage.
- **Each sensor at the right point in space.** No sensor should be installed in a compromised location to share a PCB with another sensor that has different spatial requirements.
- **Fault isolation between subsystems.** A failed climate sensor must not take down light measurement or analytics.
- **Industrial-grade sensors only.** All chips on a sensor module must be available from real distributors (LCSC, Mouser, Digi-Key) with traceable datasheets and predictable supply, not anonymous hobby modules.
- **Mixed-signal segregation.** Sensitive analog (high-impedance pH electrode, EC excitation) must be electrically separated from digital sensors. This forces analytics into its own sensor module.
- **Sensor selection must serve the cultivation profile.** ADR-0003 (strawberry day-neutral) specifies what must be measured to enforce the profile.

## Definition: zone

A **zone** is the spatial extent within which environmental conditions are sufficiently uniform — or sufficiently predictable through dynamic modelling from a single sensing point — that one sensor instance per subsystem is adequate. Zone boundaries are deployment-dependent:

- An apartment cabinet (~1 m³, active circulation, well-mixed): typically **one zone for every subsystem.**
- A small commercial cabinet rack (5–20 m³ across 2–4 chambers): **2–4 zones** typically.
- A large commercial greenhouse (50+ m²): **many zones**, with boundaries determined empirically from operational data (gradient mapping, microclimate identification).

Zone count is not an architectural decision — it is a deployment-time choice made by the operator. The architecture must support 1 zone or 50 zones with identical PCB designs and firmware.

## Decision

1. **Principle: one Cyphal node per (functional subsystem × zone).** Each node is built from one carrier PCB (with a WeAct STM32F4 core board, per ADR-0002 rev 3) plus one sensor module specific to its subsystem. A functional subsystem in the cultivation profile (ADR-0003) maps to one Cyphal node-class. Each zone in which that subsystem is monitored maps to one node instance of that class. Apartment-scale deployments typically have one zone per subsystem (5 sensor nodes total in the seed cabinet); large-greenhouse deployments may have tens of zones (50+ instances of the same node-classes).

2. **Designs few, instances many — across three dimensions.** Sensor module designs are reusable. **Instances of the same PCB design may vary in three independent dimensions:**

   - **Location.** Same PCB stocked identically, placed at different points in space (typical at all scales when multiple instances exist).
   - **Quantity.** Same PCB instantiated more or fewer times as zone count scales.
   - **Populated BOM.** Same PCB design with different chips populated, used to specialize an instance for a specific role — an M01-CLIMATE instance in a zone where CO₂ is monitored elsewhere may leave the SCD41 unpopulated; an M07-AMBIENT indoor instance leaves the whole wind group as bare footprints. Unpopulated chips have unused footprints; firmware probes I²C addresses at boot and publishes only Cyphal subjects for sensors that respond. *(rev 3: the previous example cited M01's airflow sensor, which rev 2 moved to M06.)*

   The partial-BOM mechanism is the architectural lever that lets the same seven PCB designs cover every conceivable zone-specific specialization. It is most useful at medium and large scale, where different zones have different sensing needs. At apartment scale, instances are typically single per subsystem and fully populated.

3. **Spatial requirements at apartment scale: short-lead sensor extension.** When a single instance of a sensor module must cover sensors with spatially incompatible requirements (e.g., M01-CLIMATE with airflow sensing needed at the fan outlet rather than at canopy), individual sensors may be mounted on short leads (≤30 cm) from the main sensor module PCB. I²C at ≤30 cm with shielded twisted pair and proper pull-ups is reliable at 100 kHz. This is the apartment-scale solution; it does not extend to inter-zone distances at larger deployments — there, separate instances are the correct answer.

4. **Sensor module catalog (seven PCB designs):**

   **M01-CLIMATE — air environment sensing.**
   - Sensirion SHT45 — primary T/RH for VPD computation (ADR-0003 decision 7). I²C, ±1.5 %RH, industrial.
   - Bosch BME688 — gas (VOC) + secondary T/H/P. I²C. Parallel to SHT45 for VOC trend monitoring as early plant-stress signal.
   - Sensirion SCD41 — true CO₂ (photoacoustic NDIR). I²C. ADR-0003 specifies ambient CO₂ without enrichment, but monitoring is required because plants deplete CO₂ in a closed cabinet during photoperiod.
   - Airflow is **not** measured by this module. The Renesas FS3000 anemometer moved to M06-VENTILATION in rev 2: air *state* at the plant and air *transport* through the cabinet are different measured quantities with different valid locations, and a single module cannot hold both without a displaced sensor.
   - All I²C on one bus, all sensors board-mounted at canopy. No leads, no displaced sensors. In apartment-scale deployments: one instance. In larger deployments: multiple instances per zone, populated as the zone requires.

   **M02-LIGHT — photic environment sensing.**
   - ams OSRAM AS7341 — 11-channel spectral sensor. I²C. Provides per-channel intensity (validates spectrum from multi-channel LED driver per ADR-0003 decision 11) and integrated PPFD proxy for DLI accounting (ADR-0003 decision 12).
   - LiteOn LTR390 (or Vishay VEML6075) — UV-A intensity. I²C. Independent verification that the UV-A channel from ADR-0003 decision 11 is operating.
   - Simplest module in the catalog. Apartment-scale: one instance under the LED fixture. Larger deployments: per-fixture or per-zone instances.

   **M03-ANALYTICS — hydroponic solution sensing.** *Most complex board in the set; significant analog mixed-signal content.*
   - **pH front-end.** BNC input for an industrial pH electrode (replaceable cartridge). High-impedance FET-input op-amp (TI **LMP7721**, 3 fA input bias current) with guard ring around the input node on the PCB. 24-bit Σ-Δ ADC (TI ADS1256 or Microchip MCP3561). Temperature compensation from a paired DS18B20 in solution.
   - **EC front-end.** AC excitation (1–10 kHz square wave) + synchronous demodulation. Reference implementation: Analog Devices **AD5933** impedance analyzer (I²C; integrates excitation, ADC, and on-chip DFT).
   - **Solution temperature.** Maxim DS18B20 in stainless-steel sheath (industrial 1-Wire); Pt1000 + dedicated front-end if higher precision is required.
   - **Galvanic isolation between pH and EC chains** via Analog Devices ADuM-series digital isolators.
   - **Reserved space for future ion-selective electrodes** (DO, ORP, Ca²⁺, NO₃⁻).
   - Sensors are co-located by physical necessity (all in or near the reservoir). One instance per hydroponic loop, regardless of deployment scale.

   **M04-PLANT — plant-level sensing.**
   - Melexis MLX90640 — 32×24 thermal imager (768 pixels). I²C. Delivers a canopy thermal field (temperature distribution) and on-node summary statistics, supporting early transpiration-anomaly detection. Per-leaf temperature for leaf-VPD computation is a deferred capability: it requires a canopy-segmentation pipeline that does not yet exist, and raw-frame radiometric accuracy (≈±1 °C, on an exponential saturation curve) makes leaf-VPD from raw frames unreliable until that pipeline lands.
   - On-node aggregation: summary statistics (mean canopy temperature, max/min, gradient, hotspot mask) at 1 Hz; full frames pushed at 5-minute intervals or on event/alarm via the Cyphal file transfer service.
   - Reserved space for future leaf-level sensors. Apartment scale: one per canopy area. Larger deployments: one per growing zone.

   **M05-SAFETY — power monitoring and safety sensing.** *(Sense-only; the over-temperature interlock lives at the heating actuator — ADR-0018 decision 10.)*
   - TI INA226 × N — bidirectional current/voltage sensing on heater, pumps, LED drivers, dosing peristaltics. I²C, addressable via address straps. INA226 must be on or very near the sensor module PCB; shunts connect via short kelvin-sense leads (typically ≤20 cm).
     - *Refined by ADR-0018 (rev 1):* power monitoring is **not** per-load on the zone module. M05 is realized as the cabinet-level distribution + monitoring board, carrying a **single** INA226 on the `+12 V` sensor bus; per-load and per-section current monitoring are dropped. All actuator / high-power energy is captured by a COTS DIN kWh meter read over S0 — there is no actuator-side current monitor (no DC-actuator-aggregate INA, no per-actuator shunt). The energy meter feeds offline anomaly models, not control.
   - TI TMP117 — independent thermal safety (cabinet over-temperature cutoff). I²C, ±0.1 °C.
     - *Refined by ADR-0018 decision 10:* M05 is **sense-only**. The TMP117 sits on the board and provides the *reported* cabinet/enclosure temperature only — it is not a cutoff and not the trip element. The MCU/bus-independent over-temperature **trip** (analog thermistor/PT1000 + comparator → relay-enable, sensor on a lead in the grow volume) is **not on M05** — it lives at the heating actuator, co-located with the element it cuts. M05 hosts no trip, no comparator, and no relay-enable.
   - Reed switch on cabinet door — GPIO, on a wire from the door to the module. *(Refined by ADR-0018: report/alert only — no automatic cutoff.)*
   - Leak-detection strip(s) — ADC channel, on a wire from the strip location to the module. *(Refined by ADR-0018: report/alert only; response is software-mediated — the gateway commands the pump off over Cyphal. Not a hardware interlock.)*
   - Apartment scale: one instance with the on-board TMP117 (cabinet air), a reed wire to the door, and a leak wire under the reservoir; the grow-volume over-temperature trip sensor is on a lead but belongs to the heating actuator, not M05 (ADR-0018 decision 10). Larger deployments: one instance per safety-critical zone or load cluster.

   **M06-VENTILATION — air transport sensing.** *(Introduced in rev 2. Not yet laid out or fabricated.)*
   - Renesas FS3000 — thermal anemometer, air velocity in the flow path. I²C. Moved here from M01-CLIMATE. Volumetric flow is computed from the known cross-section and a velocity profile coefficient identified per installation.
   - Differential-pressure sensor, higher range (Sensirion SDP8xx class) — pressure drop across the filter. I²C. Read together with velocity, it yields filter resistance and so separates filter loading from flow.
   - Differential-pressure sensor, low range (Sensirion SDP8xx class) — enclosure interior referenced to ambient. I²C. Supports identification of the envelope leakage characteristic by fan pressurization.
   - Sensirion SHT45 — in-stream T/RH. I²C. Required for density compensation of mass flow; not a duplicate of M01's SHT45, which measures the air state the plant experiences for VPD (ADR-0003 decision 7).
   - Deliberately **not** populated: a gas sensor of the BME688 class. Its output is an uncalibrated resistance trend; measured in diluted duct air it answers no question the canopy instance answers better.
   - No sealed enclosure is assumed, required or specified in any phase. Gas exchange is obtained from the identified model (ADR-0016), driven by informative transients, not from a steady-state balance of flow against concentration difference.

   **M07-AMBIENT — boundary conditions outside the growing space.** *(Introduced in rev 2. Not yet laid out or fabricated.)*
   - **"Ambient" is whatever the growing enclosure exchanges with, one step out — not a synonym for outdoors.** For a greenhouse that is the weather; for a cabinet standing in a room it is the room, and an outdoor CO₂ or temperature reading would state a boundary condition that enclosure never sees. M07 therefore has two deployment variants, **indoor** and **outdoor**, which differ in populated BOM and housing only: same PCB design, same strap `0b111`, same firmware image, no second module class. This is decision 2's third dimension, not an exception to it. Where both boundaries are real — a cabinet, in a room, in a building — the answer is two *instances* of the one class in two zones (decision 1), distinguished by the gateway's role-and-zone tagging (decision 7); the node itself never knows which variant it is. Every sensor below is read against that definition of ambient.
   - Sensirion SHT45 — ambient air T/RH. I²C. Boundary condition for the thermal and moisture balance; without it, disturbance cannot be separated from response.
   - Absolute barometric pressure sensor (Bosch BMP390 class). I²C. Barometric pressure has no meaningful value inside the enclosure; it feeds density computation for M06 mass flow and pressure compensation for every SCD41 in the deployment.
   - Sensirion SCD41 — ambient CO₂. I²C. Reference against which depletion inside is measured, and the only physically sound basis for cross-calibrating independent CO₂ instruments. For a cabinet the room is the correct reference, not the outdoor baseline: a cabinet vents into the room, and an occupied room runs far above outdoor with a diurnal swing that would otherwise be read as a plant signal.
   - Irradiance sensor — energy-balance input for any growing space with a window or translucent wall. Quantity and part deferred; see deferred decisions.
   - Wind speed — purchased anemometer with pulse output, identified by an SP number (ADR-0019), conditioned by hardware RC and Schmitt trigger and read by a timer in external counter mode. Wind is a boundary condition only for the outdoor variant at deployment scale and above; the group is left unpopulated on the indoor variant and at cabinet scale, under the partial-BOM mechanism.
   - Wind direction — purchased vane with a Gray-coded absolute encoder, serialized in the vane housing and read over SPI. Gray coding is required because the vane hunts continuously across code boundaries. Encoder resolution is a property of the instrument and does not enter the header contract. I²C is rejected for this link: it is open-drain and capacitance-bound, which is the constraint that ruled out cabled sensor extension in the first place.
   - Heading reference and installation-integrity monitor (accelerometer + magnetometer, LSM303AGR class). I²C, polled at minute scale. Mast guides on the enclosure fix the enclosure-to-vane-base orientation mechanically, so the mounting offset is a constant of the assembly and the magnetometer supplies the heading term of the wind azimuth directly. It does not remove a manual commissioning step — it exchanges entering an azimuth for calibrating magnetic distortion on site. Its justification is that a profile-entered azimuth never notices a mast turned, struck, or resettled after installation, whereas a live heading does; the accelerometer likewise reports a mast out of plumb or displaced. This is drift detection applied to the installation itself. A rate gyroscope is **not** populated: it serves moving platforms, and M07 is a fixed installation.
   - Vane and anemometer cross-check each other for failure: a static vane with the anemometer above its starting threshold indicates a seized or detached vane, and the converse indicates a seized anemometer. The discriminator is measured speed against the starting threshold, never a bare zero, since below that threshold both instruments are legitimately still. This is failure-mode coverage in the sense admitted by alternative G, obtained from instruments already required for their own sake.
   - Precipitation is not measured; it feeds none of the identified quantities.
   - Instruments on leads are not a new construction class. M05-SAFETY already carries door and leak sensors on leads, and 1-Wire parts are cabled by nature. A node conditions signals; where the transducer physically sits does not define its module class (decision 1).
   - The **outdoor** variant is the first module deployed outside the building envelope. Environmental qualification, radiation shielding of the air sensors, and the consequences for a field bus leaving the enclosure are specification concerns, tracked in the module's own documents. None of them constrain the indoor variant, which sits on an in-cabinet bus segment under the same environmental assumptions as M01–M05.

5. **Sensor-module header — standardized signal allocation.** All sensor modules use the same physical header on the carrier PCB. The pinout exposes a superset of interfaces; modules use what they need:
   - 3.3 V (sensor power; the only on-carrier rail, from the carrier's TPS54302 buck)
   - GND × 2 (analog + digital separation where applicable)
   - I²C: SDA, SCL
   - SPI: MOSI, MISO, SCK, 2× CS
   - 4× GPIO (configurable as inputs, outputs, or alternate-function)
   - 2× ADC (12-bit, from STM32 internal)
   - 1-Wire bus
   - 4× PWM (used by actuator modules; sensor modules typically leave unused)
   - 3× module-ID strap pins (tied to GND/VCC on each module; firmware reads the strap pattern to identify which module class is plugged in — 8 possible IDs)

6. **Module-ID assignments:**
   - `0b000` — reserved (default / unplugged / unknown)
   - `0b001` — M01-CLIMATE
   - `0b010` — M02-LIGHT
   - `0b011` — M03-ANALYTICS
   - `0b100` — M04-PLANT
   - `0b101` — M05-SAFETY
   - `0b110` — M06-VENTILATION
   - `0b111` — M07-AMBIENT

   Actuator modules use a separate ID space allocated in a future actuator-taxonomy ADR.

   **The 3-bit module-ID field is exhausted.** All eight identifiers are assigned. The field is not widened in this revision: five carrier boards already exist with three strap pins, no eighth sensor class is defined, and widening the field changes the carrier↔module header contract in decision 5. It is instead recorded as an **obligation of the next carrier revision**: the module-ID field is widened to four strap pins, giving sixteen identifiers, whenever the carrier is next revised for any reason. No eighth sensor module class may be defined before that revision ships.

   **Module-ID identifies the class (PCB design), not the instance.** Multiple instances of the same class in different zones share the strap pattern; they are distinguished by Cyphal Node-ID and by gateway-resolved tagging (see decision 7).

7. **Per-instance role and zone tagging at the gateway.** Each Cyphal Node-ID is mapped at the gateway configuration to a `(module_class, node_role, zone)` triple. Examples:
   - Apartment cabinet: `(M01-CLIMATE, canopy, zone-0)`, `(M05-SAFETY, default, zone-0)` — every node in zone-0, default roles.
   - Multi-zone greenhouse: `(M01-CLIMATE, canopy, zone-NE)`, `(M01-CLIMATE, canopy, zone-NW)`, `(M01-CLIMATE, exhaust, ventilation)`, etc.

   Firmware on the node does **not** carry role or zone information. It publishes whatever Cyphal subjects correspond to sensors it detects on the I²C bus. The gateway annotates incoming data with `node_role` and `zone` tags from its configuration. This is what makes one PCB design + one firmware image serve every role at every scale.

8. **Firmware sensor-presence probing at boot.** Each module's firmware probes the expected I²C addresses for its sensor population at initialization. Sensors that respond are registered and their Cyphal publications are activated. Sensors that don't respond are logged and remain inactive — no publication, no error spam. This makes partial-BOM populations work cleanly across scales and also serves as runtime tolerance for sensor failure or post-build chip removal. Periodic re-probe (e.g., every 60 s) handles transient I²C errors that might otherwise leave a sensor disabled for the rest of the runtime.

9. **Out of scope for this ADR:**
   - **Actuator modules** (LED drivers, pump drivers, heater control, dosing peristaltic control). These follow the same carrier + module pattern but require their own taxonomy ADR. The scale-aware multi-instance pattern is expected to apply analogously.
   - **Camera.** The cabinet camera is not a Cyphal node — it connects directly to the gateway. Covered by the gateway and platform layers.
   - **Detailed PCB layout** for any of the seven sensor modules.
   - **Detailed pH/EC front-end schematic, layout, and isolation strategy.** May warrant its own ADR once analytics-module schematic capture begins.
   - **Zone-definition methodology for large greenhouses.** How to identify zones empirically, how dense the initial sensor coverage should be before model identifies zones — operational concern, not architectural. Touches future predictive-ML modules (ADR-0001 decision 4).
   - **`node_role` and `zone` representation in the IndustryFlow data model.** Touches ADR-IF-0001 (production_unit entity) or its extension.

## Alternatives considered

**A. One giant sensor module with everything on it.** *Rejected:* poor fault isolation, large board, contradicts ADR-0001's functional-subsystem decomposition.

**B. One module per individual sensor.** *Rejected:* most modules would be half-empty, manufacturing NRE dominates, 15–20 module designs to maintain.

**C. Group by interface type, not by function.** E.g., one "I²C sensor module" mixing sensors from different subsystems. *Rejected:* contradicts ADR-0001's subsystem-functional decomposition; loses fault isolation.

**D. Split each functional subsystem into multiple distinct PCB designs** (e.g., Climate-T-RH + Climate-CO₂ + Climate-Airflow as three separate designs). *Rejected:* multiplies design effort without proportional benefit. Partial-BOM multi-instance (decision 2) addresses the same need without new designs.

**E. Delegate analytics (pH/EC) to external industrial transmitters with RS-485 / Modbus.** *Rejected:* introduces a vendor-specific second protocol, loses access to raw electrode signals, fragments the supply chain.

**F. Single PCB per subsystem with all sensors at one point in space.** This was the implicit assumption of the original ADR-0014 draft. *Rejected on revision:* climate and safety subsystems contain sensors with genuinely incompatible spatial requirements. At apartment scale, the answer is short-lead sensor extension (decision 3); at larger scales, the answer is multi-instance per zone (decisions 1 and 2). Both use the same PCB design.

**G. Sensor proliferation for redundancy within a zone.** *Rejected:* once a single accurate sensor produces dense time-series data, modelling outperforms additional spatial sampling within the same zone. Multiplying co-located sensors raises cost, calibration complexity, and BoM footprint without proportional information gain. This rejection concerns duplicate measurement of the *same* quantity in the *same* zone. It does not apply to sensors of a shared type distributed across M01-CLIMATE, M06-VENTILATION and M07-AMBIENT: these measure different quantities at locations that are not interchangeable — air state at the plant, air transport through the duct, and the boundary condition outside the enclosure. Redundancy is justified only for safety failure-mode coverage (and even there, the heating actuator's analog over-temperature trip in the grow volume, paired with the M01 climate sensor, provides cross-subsystem redundancy for grow-volume over-temperature without duplication within either subsystem; M05's on-board TMP117 measures the cabinet — a different volume — and is not part of this redundancy).

**H. A calibrated restriction as M06's primary flow element.** An orifice plate or similar, with flow derived from the pressure drop across it. *Rejected:* it requires inserting a constriction into the air path, which modifies the space being measured. Volumetric flow is instead computed from the duct cross-section and a velocity profile coefficient identified per installation (decision 4). A restriction remains available as an optional commissioning-time reference for identifying that coefficient; it is not part of the operating configuration.

**I. Envelope leakage as a difference of measured flows.** Supply flow minus exhaust flow. *Rejected:* it is a small difference of large numbers — at a few percent measurement error per instrument, the error on the leakage estimate exceeds the estimate. The selected method is a fan pressurization characteristic: flow is stepped across several operating points, envelope-to-ambient pressure is recorded at each, and a power-law characteristic is fitted (ISO 9972 class, applied at cabinet scale). A cabinet fan produces envelope pressures already within the operating range, so the characteristic is identified at the pressures at which it is used.

**J. Parallel Gray code from M07's vane encoder direct to GPIO.** Static levels, no clocking, no timing. *Rejected:* the header contract of decision 5 carries named interfaces, not a count of general-purpose lines, and encoder resolution would propagate into that contract. The selected transport is SPI via a shift register in the vane housing (decision 4), which keeps encoder resolution a property of the purchased instrument. I²C is rejected for the same link: it is open-drain and capacitance-bound.

**K. Sensors mounted on M07's rotating vane.** An air-velocity sensor and a magnetometer on the vane body, kept aligned with the flow by the vane itself. *Rejected:* powering and reading a continuously rotating body requires slip rings or a wireless link with local power generation. A thermal anemometer of the FS3000 class also saturates silently at its range ceiling, which appears in a wind record as calm during a gale; a cup anemometer has no such ceiling.

**L. Ultrasonic or analog-output wind instruments.** *Rejected on cost:* an ultrasonic instrument has no moving parts and yields both wind quantities from one device, at materially higher cost — reconsider if bearing wear proves limiting. Analog outputs (0–5 V, 4–20 mA) are available on industrial models with integrated electronics, also at higher cost. A pulse output fits the hardware pattern already established for S0 energy metering: RC network and Schmitt trigger, timer in external counter mode.

## Consequences

### Positive

- One architecture, all scales. Apartment cabinet and 200 m² greenhouse use identical PCB designs, firmware, and DSDL — they differ only in instance count and gateway-side tagging.
- Seven sensor module designs cover the full project. Instance multiplication is the only scaling mechanism; no new node-classes are introduced as deployments grow.
- Each module is independently replaceable. Fault in one functional subsystem does not propagate to others.
- Module-ID straps make firmware class-identification automatic at boot. Gateway role-and-zone tagging distinguishes instances.
- Partial-BOM mechanism gives operators a finely-graded specialization tool without requiring new PCB designs.
- Time-series + model principle keeps sensor count proportional to operational need, not to area to be covered.

### Negative

- The analytics module (M03) remains a complex mixed-signal analog design. Reserve dedicated engineering effort before schematic capture.
- At medium and large scales, instance counts and gateway-configuration management grow proportional to zone count. This makes gateway-side configuration tooling (zone definition, role assignment, validation) a real engineering surface — touches future deployment-tooling work.
- Firmware sensor-presence probing must be robust against transient I²C errors. Periodic re-probe handles this but is an explicit firmware requirement.
- Module-ID space is bounded (3 strap pins = 8 IDs). Extending to 4 pins is straightforward when needed.
- On-node aggregation in M04-PLANT is a non-trivial firmware concern. Memory budget on STM32F405 is comfortable (192 KB RAM vs 1.5 KB per frame), but firmware design needs care.

## Deferred decisions

- **Actuator-module taxonomy** — separate future ADR. The scale-aware multi-instance + partial-BOM pattern is expected to apply analogously.
- **Detailed pH/EC front-end schematic, layout, and isolation strategy** — may warrant its own ADR.
- **Zone-definition methodology for non-trivial deployments** — operational concern; out of scope until first multi-zone deployment.
- **Sensor-module versioning and revision policy** — how a module evolves (e.g., M01 v1 → v2 when SCD41 is EOL'd) without breaking deployments.
- **`node_role` and `zone` representation in IndustryFlow data model** — touches ADR-IF-0001.
- **PCB schematics, gerbers, and BOMs for all seven modules** — published in the hardware reference repository under CERN-OHL-S. Specification artifacts, not ADRs.
- **M07 irradiance quantity and range.** Whether ambient irradiance is reported as PPFD, for direct comparability with M02-LIGHT inside, or as broadband irradiance or illuminance. The quantities are not interchangeable and must not be mixed in one time series. M02's sensor is calibrated for a narrowband luminaire spectrum at closed-space levels and cannot be assumed usable under broadband sunlight orders of magnitude brighter. The indoor/outdoor variant split adds a second, separate question on the same part: full sunlight and room light are orders of magnitude apart, so whether one part serves both variants — or the variants populate different parts, or need different footprints — is unresolved and must be settled before the footprint is committed.
- **M07 barometric duplication across variants.** Barometric pressure reads effectively the same in a room and outside it, so a deployment carrying both M07 variants measures it twice. Which instance carries the sensor, whether the other leaves the footprint unpopulated, and which instance the deployment profile names as the pressure source for M06's density computation and every SCD41's pressure compensation.
- **Magnetic compatibility between the vane encoder and the heading reference.** Where the purchased encoder reads its tracks magnetically, its field must not reach M07's magnetometer. Mast-to-base separation is expected to settle this, but it is a check at model selection rather than an assumption.
- **Magnetic distortion calibration for M07.** Whether hard-iron and soft-iron coefficients are established on site with the assembly mast-mounted, or beforehand at the risk of the installed environment invalidating them. The coefficients belong to the deployment, not to the module design.
- **1-Wire is absent from the header contract.** Decision 5 names the interfaces a sensor module may use, and 1-Wire is not among them, yet DS18B20-class parts are intended for systematic use. Whether the header gains a dedicated line, or 1-Wire is assigned to a general-purpose pin by convention, is unresolved and is properly settled at the next carrier revision alongside the module-ID field widening.
- **M06 velocity profile coefficient.** How the coefficient relating point velocity to mean velocity is identified at commissioning without permanently altering the air path being measured. Flow error propagates linearly into every transport quantity derived from M06.
- **Ventilation / pollination subsystem boundary.** Which fans belong to which functional subsystem, and therefore which `node_role` values M06 takes. The original FS3000 entry justified airflow sensing as pollination-fan verification, while M06 is framed around air transport; the boundary is now load-bearing for role assignment.
- **Fate of decision 3.** The short-lead mechanism was motivated by M01's displaced FS3000, which rev 2 removed. Whether decision 3 remains as a general provision or is retired is unresolved; M05's door and leak leads are GPIO and ADC, not I²C, and are unaffected either way.
- **Whether the actuator "separate ID space" is separate *pins* or a separate *namespace on the same pins*.** Decision 6 says actuator modules use a separate ID space; decision 9 says they follow the same carrier + module pattern; ADR-0002 rev 3 decision 3 lists exactly one header on the carrier — the sensor-module header — and decision 5 there speaks of the type being read via "sensor-module strap pins". If actuator modules plug into that same header and are identified by those same three straps, then this revision's exhaustion of the field removes every identifier an actuator module could take, and the carrier revision becomes a hard prerequisite for the **first actuator node** — ROADMAP stage 5, the Phase 1 → Phase 2 boundary and a step on the critical path to first cultivation. If instead actuators carry their own physically separate strap field, that field does not exist in any accepted decision and must be added to the carrier description. **This revision does not settle it, and the exhaustion statement above should not be read as having settled it.** It is properly resolved together with the field widening, and before the actuator-taxonomy ADR is written rather than by it.

## References

- ADR-0001: IndustryGrow framing — functional subsystem = module; platform scales from apartment to commercial greenhouse.
- ADR-0002 (rev 3): Field bus architecture — WeAct STM32F4 core board on carrier PCB.
- ADR-0003: Strawberry day-neutral cultivation profile — defines what must be measured.
- Sensirion SHT45, SCD41 datasheets.
- Bosch BME688 datasheet.
- Renesas FS3000 datasheet.
- Sensirion SDP8xx differential-pressure sensor datasheet.
- ISO 9972 — determination of air permeability of buildings by fan pressurization; the method class applied at cabinet scale in alternative I.
- Bosch BMP390 barometric pressure sensor datasheet.
- ST LSM303AGR accelerometer / magnetometer datasheet.
- ADR-0016: Empirical survey and state-space modeling — survey-phase instrumentation density and inventory return.
- ADR-0019: Purchased-part (SP) identification — applies if wind instrumentation is procured rather than built.
- ams OSRAM AS7341 datasheet.
- Melexis MLX90640 datasheet.
- Texas Instruments LMP7721 datasheet — femtoampere-bias-current op-amp for the pH front-end.
- Analog Devices AD5933 datasheet — impedance analyzer for EC.
- Analog Devices ADuM-series datasheets — digital isolators for pH/EC galvanic separation.
- Texas Instruments INA226, TMP117 datasheets — safety and power monitoring.