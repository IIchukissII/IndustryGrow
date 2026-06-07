<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0014: Sensor node taxonomy and module decomposition

- **ID:** ADR-0014
- **Status:** Proposed
- **Date:** 2026-05-16
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0002 (rev 3), ADR-0003

## Context and problem

ADR-0001 (decision 7) committed IndustryGrow to a data model where the cabinet `machine` decomposes into `modules` corresponding to functional subsystems (climate, lighting, irrigation, plant monitoring, pollination, power/safety). ADR-0002 (rev 3) committed to a unified compute platform: a WeAct STM32F4 core board (STM32F405RGT6) on a custom carrier PCB with a sensor-module header — one carrier design across the project, with the sensor module varying between node types.

What ADR-0002 leaves implicit is **how sensors group into modules**: which sensors live on which sensor module PCB, which physical Cyphal node implements which IndustryFlow module, and what the principle is for adding new sensor types. This ADR fills that gap and establishes the taxonomy that future cabinets and community-contributed deployments are expected to follow.

ADR-0001 commits IndustryGrow to scale across deployment sizes — from an apartment cabinet (~1 m³) to a several-hundred-square-meter commercial facility. The taxonomy in this ADR must be the **same architecture at both ends**; only the application of the pattern changes with scale. This is the central architectural concern: a pattern that requires redesign when scaling from cabinet to greenhouse would violate the platform's defining promise.

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
   - **Populated BOM.** Same PCB design with different chips populated, used to specialize an instance for a specific role (e.g., a climate node in an air-handling zone may populate the airflow sensor and leave the CO₂ sensor unpopulated, or vice versa). Unpopulated chips have unused footprints; firmware probes I²C addresses at boot and publishes only Cyphal subjects for sensors that respond.

   The partial-BOM mechanism is the architectural lever that lets the same five PCB designs cover every conceivable zone-specific specialization. It is most useful at medium and large scale, where different zones have different sensing needs. At apartment scale, instances are typically single per subsystem and fully populated.

3. **Spatial requirements at apartment scale: short-lead sensor extension.** When a single instance of a sensor module must cover sensors with spatially incompatible requirements (e.g., M01-CLIMATE with airflow sensing needed at the fan outlet rather than at canopy), individual sensors may be mounted on short leads (≤30 cm) from the main sensor module PCB. I²C at ≤30 cm with shielded twisted pair and proper pull-ups is reliable at 100 kHz. This is the apartment-scale solution; it does not extend to inter-zone distances at larger deployments — there, separate instances are the correct answer.

4. **Sensor module catalog (five PCB designs):**

   **M01-CLIMATE — air environment sensing.**
   - Sensirion SHT45 — primary T/RH for VPD computation (ADR-0003 decision 7). I²C, ±1.5 %RH, industrial.
   - Bosch BME688 — gas (VOC) + secondary T/H/P. I²C. Parallel to SHT45 for VOC trend monitoring as early plant-stress signal.
   - Sensirion SCD41 — true CO₂ (photoacoustic NDIR). I²C. ADR-0003 specifies ambient CO₂ without enrichment, but monitoring is required because plants deplete CO₂ in a closed cabinet during photoperiod.
   - Renesas FS3000 — thermal anemometer for airflow over canopy. I²C. Verifies pollination-fan operation and informs leaf-boundary-layer modelling.
   - All I²C on one bus. In apartment-scale deployments: one instance, with FS3000 either on the PCB at canopy location or on a short lead to the fan outlet per installation. In larger deployments: multiple instances per zone, populated as the zone requires.

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
   - Melexis MLX90640 — 32×24 thermal imager (768 pixels). I²C. Leaf-temperature distribution for leaf-VPD computation and early transpiration-anomaly detection.
   - On-node aggregation: summary statistics (mean canopy temperature, max/min, gradient, hotspot mask) at 1 Hz; full frames pushed at 5-minute intervals or on event/alarm via the Cyphal file transfer service.
   - Reserved space for future leaf-level sensors. Apartment scale: one per canopy area. Larger deployments: one per growing zone.

   **M05-SAFETY — power monitoring and safety interlocks.**
   - TI INA226 × N — bidirectional current/voltage sensing on heater, pumps, LED drivers, dosing peristaltics. I²C, addressable via address straps. INA226 must be on or very near the sensor module PCB; shunts connect via short kelvin-sense leads (typically ≤20 cm).
     - *Refined by ADR-0018 (rev 1):* power monitoring is **not** per-load on the zone module. M05 is realized as the cabinet-level distribution + monitoring board, carrying a **single** INA226 on the `+12 V` sensor bus; per-load and per-section current monitoring are dropped. All actuator / high-power energy is captured by a COTS DIN kWh meter read over S0 — there is no actuator-side current monitor (no DC-actuator-aggregate INA, no per-actuator shunt). The energy meter feeds offline anomaly models, not control.
   - TI TMP117 — independent thermal safety (cabinet over-temperature cutoff). I²C, ±0.1 °C.
     - *Refined by ADR-0018 decision 10:* M05 is **sense-only**. The TMP117 sits on the board and provides the *reported* cabinet/enclosure temperature only — it is not a cutoff and not the trip element. The MCU/bus-independent over-temperature **trip** (analog thermistor/PT1000 + comparator → relay-enable, sensor on a lead in the grow volume) is **not on M05** — it lives at the heating actuator, co-located with the element it cuts. M05 hosts no trip, no comparator, and no relay-enable.
   - Reed switch on cabinet door — GPIO, on a wire from the door to the module. *(Refined by ADR-0018: report/alert only — no automatic cutoff.)*
   - Leak-detection strip(s) — ADC channel, on a wire from the strip location to the module. *(Refined by ADR-0018: report/alert only; response is software-mediated — the gateway commands the pump off over Cyphal. Not a hardware interlock.)*
   - Apartment scale: one instance with the on-board TMP117 (cabinet air), a reed wire to the door, and a leak wire under the reservoir; the grow-volume over-temperature trip sensor is on a lead but belongs to the heating actuator, not M05 (ADR-0018 decision 10). Larger deployments: one instance per safety-critical zone or load cluster.

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
   - `0b110`, `0b111` — reserved for additional sensor module classes. Actuator modules use a separate ID space allocated in a future actuator-taxonomy ADR.

   **Module-ID identifies the class (PCB design), not the instance.** Multiple instances of the same class in different zones share the strap pattern; they are distinguished by Cyphal Node-ID and by gateway-resolved tagging (see decision 7).

7. **Per-instance role and zone tagging at the gateway.** Each Cyphal Node-ID is mapped at the gateway configuration to a `(module_class, node_role, zone)` triple. Examples:
   - Apartment cabinet: `(M01-CLIMATE, canopy, zone-0)`, `(M05-SAFETY, default, zone-0)` — every node in zone-0, default roles.
   - Multi-zone greenhouse: `(M01-CLIMATE, canopy, zone-NE)`, `(M01-CLIMATE, canopy, zone-NW)`, `(M01-CLIMATE, exhaust, ventilation)`, etc.

   Firmware on the node does **not** carry role or zone information. It publishes whatever Cyphal subjects correspond to sensors it detects on the I²C bus. The gateway annotates incoming data with `node_role` and `zone` tags from its configuration. This is what makes one PCB design + one firmware image serve every role at every scale.

8. **Firmware sensor-presence probing at boot.** Each module's firmware probes the expected I²C addresses for its sensor population at initialization. Sensors that respond are registered and their Cyphal publications are activated. Sensors that don't respond are logged and remain inactive — no publication, no error spam. This makes partial-BOM populations work cleanly across scales and also serves as runtime tolerance for sensor failure or post-build chip removal. Periodic re-probe (e.g., every 60 s) handles transient I²C errors that might otherwise leave a sensor disabled for the rest of the runtime.

9. **Out of scope for this ADR:**
   - **Actuator modules** (LED drivers, pump drivers, heater control, dosing peristaltic control). These follow the same carrier + module pattern but require their own taxonomy ADR. The scale-aware multi-instance pattern is expected to apply analogously.
   - **Camera.** The cabinet camera is not a Cyphal node — it connects directly to the gateway. Covered by the gateway and platform layers.
   - **Detailed PCB layout** for any of the five sensor modules.
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

**G. Sensor proliferation for redundancy within a zone.** *Rejected:* once a single accurate sensor produces dense time-series data, modelling outperforms additional spatial sampling within the same zone. Multiplying co-located sensors raises cost, calibration complexity, and BoM footprint without proportional information gain. Redundancy is justified only for safety failure-mode coverage (and even there, the heating actuator's analog over-temperature trip in the grow volume, paired with the M01 climate sensor, provides cross-subsystem redundancy for grow-volume over-temperature without duplication within either subsystem; M05's on-board TMP117 measures the cabinet — a different volume — and is not part of this redundancy).

## Consequences

### Positive

- One architecture, all scales. Apartment cabinet and 200 m² greenhouse use identical PCB designs, firmware, and DSDL — they differ only in instance count and gateway-side tagging.
- Five sensor module designs cover the full project. Instance multiplication is the only scaling mechanism; no new node-classes are introduced as deployments grow.
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
- **PCB schematics, gerbers, and BOMs for all five modules** — published in the hardware reference repository under CERN-OHL-S. Specification artifacts, not ADRs.

## References

- ADR-0001: IndustryGrow framing — functional subsystem = module; platform scales from apartment to commercial greenhouse.
- ADR-0002 (rev 3): Field bus architecture — WeAct STM32F4 core board on carrier PCB.
- ADR-0003: Strawberry day-neutral cultivation profile — defines what must be measured.
- Sensirion SHT45, SCD41 datasheets.
- Bosch BME688 datasheet.
- Renesas FS3000 datasheet.
- ams OSRAM AS7341 datasheet.
- Melexis MLX90640 datasheet.
- Texas Instruments LMP7721 datasheet — femtoampere-bias-current op-amp for the pH front-end.
- Analog Devices AD5933 datasheet — impedance analyzer for EC.
- Analog Devices ADuM-series datasheets — digital isolators for pH/EC galvanic separation.
- Texas Instruments INA226, TMP117 datasheets — safety and power monitoring.