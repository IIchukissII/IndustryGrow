<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0016 (rev 1): Empirical survey, state-space modeling, and sensor density management

- **ID:** ADR-0016 (rev 1)
- **Status:** Proposed
- **Date:** 2026-05-17 (rev 1: 2026-06-14)
- **Project:** IndustryGrow
- **Parent:** ADR-0001
- **Companions:** ADR-0014, ADR-0015
- **Supersedes:** ADR-0016 (initial draft, 2026-05-17)

## Revision history

- **rev 1 (2026-06-14)** — Folded a directional note into the biological-subspace partition: leaf VPD depends on leaf temperature, reachable by two independent paths (radiometric per ADR-0014, and energy-balance estimation within this ADR's state-estimation mandate), whose divergence is diagnostic of transpiration state — marked "(direction, not yet specified)". Paired with ADR-0014 rev 1, which scopes the radiometric path as deferred. No decision changed.

## Context and problem

ADR-0014 established that IndustryGrow scales by **multiplying instances** of a fixed set of sensor node classes across zones, and explicitly rejected sensor proliferation within a single zone (decisions 1–2, alternative G). The reasoning given there was correct but incomplete: it said "once a single accurate sensor produces dense time-series data, modelling outperforms additional spatial sampling," but **did not specify what kind of modeling, when it happens, or what the relationship is between modeling and sensor density over the lifetime of a deployment**.

ADR-0015 specified that control loops live on the gateway and consume telemetry from sensor nodes. But it left implicit a more powerful possibility: that gateway-side software can not only **react** to telemetry, but also **estimate** state variables that are not directly measured. The control-loop runs against a state estimate that may be only partially observed by physical sensors.

What's missing from the architectural record is the **operational model** for how IndustryGrow deployments relate to their environment over time. Specifically:

1. **How does a new deployment learn its environment?** A cabinet, greenhouse, or chamber has spatial geometry (volume, air-mixing characteristics, light distribution, thermal gradients) that affects how the cultivation system behaves. Without understanding this geometry, sensor placement is guesswork and control is conservative.

2. **How does the sensor density relate to observability requirements?** Operating-phase sensor count should be the **minimum** required to keep the system observable, not the maximum that fits in the budget. Excess sensors during operation add cost, complexity, and failure surface without proportional information gain.

3. **How is environmental knowledge captured, validated, and updated?** Environments are not static — plants grow and change leaf area index, seasons change ambient temperature baselines, equipment wears, geometry is occasionally modified. A deployment needs an explicit way to refresh its understanding of itself.

4. **How is platform's commercial value tied to environmental understanding?** Sensors and PCBs are commodity. The defensible knowledge is **how to identify a deployment's specific dynamics** and **how to operate it efficiently afterwards**. This belongs in the architecture, not as an after-thought.

This ADR makes these decisions explicit and adds **state-space modeling and empirical survey** as first-class concerns of the IndustryGrow architecture.

## Decision drivers

- **Sensor density is a temporal variable, not a fixed property.** High density during identification phases, low density during operation, returns to high density when re-identification is needed.
- **Observability is a property of (model + sensor placement), not just sensor placement.** The same sensor count can give very different observability depending on whether a calibrated model is available.
- **State estimation is a first-class output, not a side effect.** The gateway produces estimated state values, publishes them on Cyphal alongside measured values, and the rest of the system consumes them identically.
- **Models are deployment-specific.** A model calibrated for one greenhouse does not transfer to another greenhouse. Each deployment has its own model lifecycle.
- **State-space modeling is preferred to ML where it applies.** Interpretable, smaller data requirements, regulator-friendly, debuggable. ML is a fallback when state-space identification fails or when the system is genuinely too complex for closed-form description (rare in cultivation environments at the scales we target).
- **The model is part of the profile, not separate from it.** Per ADR-0015, single mutation channel is profile versioning. Model parameters live in the profile and version together with operational setpoints.

## Decision

### State-vector partition (clarifying note)

The deployment state vector partitions by *whose concern* each variable is, and this partition — not the raw variable list — governs how each variable is treated:

- **Biological subspace** — variables plant physiology responds to (leaf VPD, PPFD/spectrum/DLI, CO₂, root-zone temperature, EC/pH, air movement). These are the experiment's factors and the *only* variables that receive setpoints in the profile. Coupling among them is expected and is exactly what the state-space model exists to resolve: a correct model drives each to target through the shared, physically-coupled actuators. Leaf VPD itself depends on leaf temperature, reachable by two independent paths — radiometric measurement (M04-PLANT MLX90640, per ADR-0014) and energy-balance estimation of an otherwise-unmeasured state variable (already within this ADR's state-estimation mandate) — whose divergence is itself diagnostic of transpiration state (direction, not yet specified).
- **Apparatus subspace** — variables the *system* must keep within bounds to realize the biological setpoints and to keep measurement trustworthy (rail voltages, actuator duty, condensation, component temperatures, leak, energy). Observed and bounded/alarmed in software (cf. ADR-0018); they carry no setpoints and do not enter the cultivation hypothesis.

Control objective: drive the biological subspace to profile setpoints while holding the apparatus subspace within tolerance.

This partition sits *above* the hardware survival layer (ADR-0014 M05 interlocks), which is not a regulated subspace but an independent override floor (ADR-0015, "safety is separate from control"). Three layers, not two: biological (profile setpoints) / apparatus (software bounds) / survival (hardware interlocks).

### Lifecycle phases

Every IndustryGrow deployment proceeds through three operational phases. Phases repeat — a deployment returns to identification when the environment changes materially (new equipment, modified geometry, seasonal change, new cultivar requirements).

1. **Survey phase.** Dense sensor coverage installed for empirical data collection. Multiple instances of M01-CLIMATE (and other relevant classes) distributed across the deployment volume on a spatial grid or other survey topology. Data accumulates for hours to weeks depending on the time constants of the environment. No active control during survey; the system runs in measurement-only mode with operator-controlled actuators (or held at known fixed conditions).

2. **Identification phase.** Survey data is processed offline (on IndustryFlow side or in a separate analytics environment) to identify:
   - **System geometry parameters** — air-mixing rate, light-distribution pattern, thermal gradient structure under typical operating conditions, response time of each actuator
   - **Reduced-order state-space model** describing the dynamics with a tractable number of state variables (typically tens, not thousands)
   - **Minimum sensor placement** required for the reduced model to remain observable in the control-theoretic sense (rank of observability matrix)
   - **State estimator** (Kalman filter, particle filter, or equivalent ML predictor) parameterized for this deployment

   The output is a new profile version (per ADR-0015) containing both operational setpoints and model parameters. This is the bridge between survey and operating phases.

3. **Operating phase.** Most sensor instances are removed (returned to inventory or shifted to other deployments). The minimum-observable subset remains physically installed. State estimator runs on the gateway as a first-class component of the control loop. Control loops consume estimated state, not raw sensor data. Sensors continue to publish their measurements; the gateway publishes additional **derived state estimates** as separate Cyphal subjects.

### Gateway responsibilities (extension of ADR-0015)

4. **Gateway runs the state estimator.** In addition to control loops (per ADR-0015 decision 8), the gateway hosts the model-derived state estimator. The estimator:
   - Subscribes to all Cyphal sensor publications for the deployment
   - Updates state estimate in memory at fixed rate (typically 1 Hz, configurable per profile)
   - Publishes estimated state values as Cyphal subjects on the same bus, using DSDL types like `industryflow.greenhouse.estimated_co2`, `industryflow.greenhouse.estimated_leaf_vpd`, etc.
   - Logs estimation residuals (difference between measured and predicted values for sensors that are present) as diagnostic publications

5. **Estimated publications are first-class telemetry.** Any consumer (control loops on gateway, IndustryFlow ingestion, debug tools) treats estimated publications identically to measured publications. Subscribers do not need to know whether a value came from a physical sensor or from the estimator. This formalizes a soft-sensor pattern, extending the principle ADR-0014 invoked when it let modelling fill in spatial detail rather than adding sensors (decision-driver "Time-series + model over spatial redundancy"; alternative G).

6. **Model parameters live in the profile.** The deployment's calibrated model (state-space matrices, Kalman gains, sensor placement information, identification metadata including when and how the model was calibrated) is serialized as part of the active profile per ADR-0015. Profile version changes when model changes; rollback is identical to rollback of operational setpoints.

7. **Residual monitoring as fault detection.** The gateway compares estimator predictions to actual measurements continuously. Sustained residual outside a configured envelope is a signal that **the model has drifted** — either the environment changed, equipment failed, or recalibration is due. This becomes an alerting condition reported to IndustryFlow.

### Sensor instance lifecycle management

8. **Sensor instances are inventory items, not deployment-fixed.** A given M01-CLIMATE PCB lives in inventory and is deployed wherever it's needed for the current phase. During survey at greenhouse A, ten M01 instances are installed. After survey, eight return to inventory and may be installed at greenhouse B for its survey. This is enabled by the uniformity principle of ADR-0014 — all M01 instances are interchangeable.

9. **Survey kits as a commercial offering.** A "survey kit" is a packaged set of sensor instances (typically 10–20 M01-CLIMATE, 5–10 M02-LIGHT, 2–3 M04-PLANT, and supporting gateway hardware) designed for temporary installation. Calibration service comes with the kit. After identification, the kit either returns to the platform operator's inventory or stays partially with the customer for the operating-phase subset.

10. **Empirical re-identification is a scheduled operation, not an exception.** Profiles include re-identification triggers (time-based, residual-threshold-based, or event-based — e.g., "after major equipment change"). When triggered, the deployment temporarily returns to survey phase, dense sensors are reinstalled, identification re-runs, new profile is generated, sensors return to inventory. This is the deployment equivalent of recalibrating a measurement instrument.

### Model identification methodology (architecture-level, not algorithm-level)

11. **State-space identification is preferred to ML where applicable.** The reasoning:
    - **Smaller data requirements** — closed-form identification needs hours to days of data, ML typically needs weeks
    - **Interpretable** — state variables map to physical quantities (zone-mean CO₂, west-side temperature, canopy-level RH) that operators can reason about
    - **Regulator-friendly** — for food-production deployments, predictive ML models are increasingly viewed skeptically by regulators; state-space models grounded in physics are easier to audit
    - **Debuggable** — when a model is wrong, residuals point to where it's wrong, not just that it's wrong

12. **ML is acceptable where state-space identification fails or is uneconomic.** Specifically: very-high-dimensional environments (large greenhouses with many partitioned zones), highly nonlinear biology-dominated dynamics (e.g., predicting yield from environmental history), and pattern recognition tasks (anomaly detection in time series). Where ML is used, it should produce outputs that conform to the same Cyphal-subject convention as state-space estimators, so downstream consumers don't see the difference.

13. **Models are not on the gateway in their training/identification form.** Identification happens off-line, in IndustryFlow-side or operator-side analytics environments. The gateway runs only the **deployed model** (the matrices, weights, or rules emitted from identification) in real-time inference mode. Training/identification is heavyweight and infrequent; inference is lightweight and continuous.

### Relationship to commercial offering

14. **The defensible IP is calibration and identification, not sensors.** Sensors are commodity. PCBs are commodity. Gateway hardware is commodity. **Knowing how to identify a particular deployment's dynamics and operate it efficiently afterwards** is the defensible knowledge. This positions IndustryGrow not as a "sells boxes" business but as a "applied platform expertise" business — consonant with how high-end industrial measurement and control systems are commercialized.

15. **Survey-as-a-service as a customer entry point.** New customers don't need to buy a full deployment to start. They can buy a survey of their existing facility, receive a calibrated model and recommendations, and decide downstream whether to invest in operating-phase sensors and control infrastructure. Lower friction to first revenue.

### Emergent observables (forward direction)

16. **Resource-flux accounting as an emergent observable (direction, not yet specified).** The same quasi-closed-chamber mass-balance behind zone-mean CO₂ also yields net carbon assimilation as a soft-sensor (decision 5), from the `dC/dt` transient after a light/ventilation step; its limiting unknown — the air-exchange rate — is already an identification-phase output (decision 2), so neither new hardware nor new survey machinery is required. Generalizes to resource-use efficiency (WUE/LUE/NUE) once Phase-2 flow/dosing counters exist. Recorded as a direction; implementation deferred.

## Non-goals explicitly captured

17. **No claim that this is the only modeling approach.** Some operators will not want or need model-based estimation and will run IndustryGrow as a pure measurement + rule-based control system. The architecture supports both modes; this ADR specifies the **option**, not a mandate.

18. **No specification of which state-space identification algorithm to use.** Subspace identification (N4SID, MOESP), prediction-error methods, grey-box methods — all are valid and the choice depends on data characteristics. This is an implementation concern, not an architectural one.

19. **No on-gateway model identification in Phase 1.** Identification is off-line. The gateway hardware (Pi 3B+) is sized for inference only. On-gateway online identification may become relevant in future commercial gateway tiers but is out of scope here.

## Alternatives considered

**A. Sensor proliferation without modeling.** Dense sensor coverage permanently installed in every deployment. *Rejected:* contradicts ADR-0014 decision 1 and alternative G; expensive at scale; failure surface grows with sensor count; does not address temporal change (sensors still need to be calibrated themselves, replaced, and interpreted).

**B. ML-first modeling as default.** Train a neural network on operational data; use it for state prediction. *Rejected as default* (still acceptable as fallback per decision 12): less interpretable, more data-hungry, regulatory friction in food applications, training infrastructure expensive, harder to debug when wrong.

**C. Per-customer custom modeling.** Each deployment is uniquely modeled with no shared framework. *Rejected:* duplicates effort across customers; loses the platform-level efficiency of standardized identification methodology; harder to commoditize the calibration service.

**D. Model lives outside the profile mechanism.** Models stored in their own subsystem on IndustryFlow with their own versioning. *Rejected:* contradicts ADR-0015's single-mutation-channel principle; creates two parallel audit trails; harder to roll back changes that span both operational setpoints and model parameters.

**E. State estimation on the cloud rather than on the gateway.** Gateway sends raw telemetry to IndustryFlow, IndustryFlow runs the estimator, sends estimated state back to gateway for control. *Rejected:* exactly the network-dependent control architecture that ADR-0015 already rejected. Estimation must run locally so that plants don't depend on internet connectivity.

## Consequences

### Positive

- **Defensible commercial value is now architecturally explicit.** Calibration and identification are first-class concerns, not after-thoughts; this aligns the platform with how high-end industrial measurement is sold.
- **Customer entry friction lowers** — survey-as-a-service is a smaller commitment than full deployment, providing a natural sales funnel.
- **Operating-phase deployments are cheaper** because they use fewer sensors — capital efficiency improves over the deployment's lifetime.
- **Sensor instances become inventory**, not deployment-specific — same hardware serves multiple customers over time as their survey phases shift.
- **Fault detection through residual monitoring** comes "for free" from running the model — sustained anomaly in residuals signals equipment problems, environmental drift, or model staleness.
- **Cultivation profile becomes more powerful** — not just setpoints but a complete operational specification including model parameters; rollback covers everything atomically.
- **Educational and research customers** (academic phenotyping, agronomy departments, seed companies) directly benefit from this approach since it matches their existing experimental methodology; this strengthens the addressable market in that segment.

### Negative

- **Identification competence becomes a critical organizational capability.** Without skilled identification engineers, the platform's calibration service is undeliverable. This is a hiring concern and a training concern.
- **Profile schema (ADR-0009) becomes more complex** — must accommodate model parameters in addition to operational setpoints. Care needed to keep the schema versionable and validatable.
- **Gateway compute load increases.** Running a state estimator at 1 Hz on top of control loops is more than just protocol bridging. Pi 3B+ remains adequate for typical-size models but the headroom shrinks; commercial deployments with large models may want Pi 4 or beyond (consistent with ADR-0002 rev 3 upgrade path).
- **Customers without identification expertise** cannot operate the system in model-mode without platform support. This is fine commercially (it's the lock-in mechanism for the platform business) but means open-source community users may default to non-model rule-based operation.
- **Re-identification scheduling adds operational complexity** — operators need to know when to invoke survey phase, kits need to be available, downtime needs to be planned. This is genuine operational overhead.

## Deferred decisions

- **DSDL types for estimated publications.** Naming convention, schema versioning, semantic relationship to corresponding measured publications. Touches future DSDL ADR.
- **Profile schema extension for model parameters.** State-space matrices, Kalman gains, residual envelopes, identification metadata. Touches ADR-0009 (deferred profile schema).
- **Survey kit packaging and logistics.** Mechanical design of temporary sensor mounts, kit contents per deployment scale, return-shipment workflow. Operational specification.
- **Identification toolchain.** Which off-line tools, scripting environment, data formats from IndustryFlow to identification environment. Tooling concern.
- **Triggers for re-identification.** Specific residual thresholds, time-based defaults, event-based triggers from operator actions. Per-cultivar/per-deployment tuning.
- **ML fallback architecture.** When state-space fails, how is an ML model trained, deployed, monitored? Touches future ML ADR if ML use becomes significant.
- **Regulatory positioning.** For food-production deployments, what claims can be made about model-based observation versus direct measurement? Compliance concern.

## References

- ADR-0001: IndustryGrow framing — platform-not-product orientation, scale-aware architecture.
- ADR-0014: Sensor node taxonomy — multi-instance scaling; time-series-over-spatial-redundancy principle.
- ADR-0015: Gateway profile caching and local control loops — profile as single mutation channel.
- Ljung, L. "System Identification: Theory for the User" — canonical reference for state-space identification methodology.
- Anderson, B.D.O., Moore, J.B. "Optimal Filtering" — Kalman filter foundations.
- Greenhouse climate models in literature: Vanthoor et al., "A methodology for model-based greenhouse design" — example of state-space approach to greenhouse modeling.
