<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0016 — Empirical survey, state-space modeling and sensor density management

- **Status:** Proposed
- **Date:** 2026-05-17; changed 2026-10-19
- **Parent:** [ADR-0001](ADR-0001-industrygrow-framing.md)
- **Related:** [ADR-0014](ADR-0014-sensor-node-taxonomy.md) (sensor classes multiplied by instance, which decision 8 moves between deployments; condensate input decision 20 reads), [ADR-0015](ADR-0015-gateway-profile-and-control-loops.md) (single mutation channel decision 6 uses; gateway control loops decision 4 extends), [ADR-0020](ADR-0020-gateway-persistence-model.md) (local store the survey phase requires), [ADR-0033](ADR-0033-canopy-imaging-channel.md) (imaging channel that observes leaf area)

## Context

ADR-0014 scales by multiplying instances of fixed sensor classes and holds that a dense time series from one accurate sensor plus a model outperforms spatial sampling. ADR-0015 puts control on the gateway. Neither states what model, when it is built, or how sensor count relates to it over a deployment's life.

A deployment's dynamics — air mixing, light distribution, thermal gradients, actuator response — are specific to it, and they change: leaf area grows, seasons move ambient baselines, equipment wears, geometry is modified. Observability is a property of model plus sensor placement, so the sensor count needed to observe a deployment falls once a calibrated model exists and rises again when the model is stale. Sensors, PCBs and gateways are commodity; identifying a deployment's dynamics and operating it afterwards is not.

State-space identification needs less data than ML, maps states to physical quantities, is auditable, and its residuals locate an error. It fails or is uneconomic for very high-dimensional, biology-dominated or pattern-recognition problems.

## Decision

**State partition.** The state vector partitions by whose concern a variable is. The *biological subspace* holds what plant physiology responds to (leaf VPD, PPFD, spectrum, DLI, CO₂, root-zone temperature, EC/pH, air movement); only these receive profile setpoints, and their coupling through shared actuators is what the model resolves. The *apparatus subspace* holds what the system keeps within bounds to realise those setpoints and keep measurement trustworthy (rail voltages, actuator duty, condensation, component temperatures, leak, energy); it is bounded and alarmed in software and carries no setpoints. Hardware survival interlocks are a third layer below both (ADR-0015 d11). The control objective is to drive the biological subspace to setpoint while holding the apparatus subspace in tolerance. Leaf VPD depends on leaf temperature, which has a radiometric path (ADR-0014) and an energy-balance estimation path; their divergence is diagnostic of transpiration state.

1. **Survey phase: dense temporary instrumentation, no active control.** Multiple sensor instances are distributed over the volume for hours to weeks, set by the environment's time constants, with actuators operator-held or at fixed conditions.
2. **Identification phase: off-line, emitting a profile version.** Survey data yields geometry parameters, a reduced-order state-space model, the minimum observable sensor placement and a parameterised state estimator; the output is a new profile version holding setpoints and model (ADR-0015).
3. **Operating phase: the minimum observable subset stays; the estimator runs on the gateway.** Surplus instances are removed; control consumes estimated state, and the gateway publishes estimates beside the measured values. A deployment returns to survey when its environment changes materially.
4. **The gateway runs the state estimator.** It subscribes to the deployment's sensor publications, updates the estimate at a profile-set rate, publishes estimates as their own subjects and publishes residuals as diagnostics.
5. **Estimated publications are first-class telemetry.** Every consumer treats an estimate as it treats a measurement, and the two stay distinguishable by subject. This is the soft-sensor pattern ADR-0014 relies on in place of spatial redundancy.
6. **Model parameters live in the profile.** Matrices, gains, sensor placement and identification metadata are part of the active profile, so a model change is a profile version and rolls back with the setpoints.
7. **Sustained residual outside its envelope is an alert.** It signals environmental change, equipment failure or a model due for recalibration, and is reported to the platform.
8. **Sensor instances are inventory, not fixtures of a deployment.** An instance moves between deployments as phases require; ADR-0014's class uniformity makes instances interchangeable.
9. **A survey kit is a commercial offering.** It is a packaged, temporarily installed set of sensor instances and gateway hardware with a calibration service, returned or partly retained after identification.
10. **Re-identification is a scheduled operation.** The profile carries its triggers — time, residual threshold or event — and a trigger returns the deployment to survey.
11. **State-space identification is preferred where it applies.** It needs hours to days of data rather than weeks, its states are physical quantities, it is auditable for food production, and its residuals locate the fault.
12. **ML is used where state-space identification fails or is uneconomic.** Its outputs follow the same subject convention as state-space estimates, so consumers see no difference.
13. **Identification is off-line; the gateway runs only the deployed model.** Identification is heavy and infrequent; inference is light and continuous.
14. **The defensible knowledge is calibration and identification, not hardware.** Sensors, PCBs and gateways are commodity.
15. **Survey-as-a-service is a customer entry point.** A customer buys a survey and a calibrated model before committing to operating-phase hardware.
16. **Resource-flux accounting is a derived observable; its implementation is not specified.** The chamber mass balance behind zone-mean CO₂ yields net carbon assimilation from the transient after a light or ventilation step, with the air-exchange rate supplied by identification (decision 2); it extends to water, light and nutrient use efficiency once flow and dosing counters exist.
17. **Model-based estimation is an option, not a mandate.** A deployment may run measurement plus rule-based control only.
18. **No identification algorithm is prescribed.** Subspace, prediction-error and grey-box methods are all admissible; the choice depends on the data.
19. **No on-gateway identification in phase 1.** The gateway class is sized for inference.
20. **Condensate is an apparatus measurement; transpiration derived from it is a gateway soft sensor; their residual is a one-sided envelope-moisture diagnostic.** The condensate count (ADR-0014 d4) converts to transpiration through a commissioned volume per count, published by the gateway under decision 5 as a biological-subspace estimate with no setpoint. Condensate in excess of modelled transpiration at the known VPD is evidence of moisture entering through the envelope; a dry collector is evidence of nothing, because carried-over droplets re-evaporate downstream.

## Alternatives

| Dec | Rejected alternative | Why rejected |
|---|---|---|
| 2 | Per-customer custom modeling with no shared framework | Duplicates effort and forfeits a standard identification method the calibration service can be built on |
| 3 | Dense sensor coverage permanently installed, no modeling | Contradicts ADR-0014 d1; cost and failure surface grow with sensor count, and temporal change is still unaddressed |
| 4 | State estimation in the cloud, estimates sent back for control | Network-dependent control, which ADR-0015 rejects |
| 6 | Models in their own platform subsystem with separate versioning | Two audit trails, and a change spanning setpoints and model cannot roll back atomically (ADR-0015 d1) |
| 11 | ML-first modeling as the default | Less interpretable, more data-hungry, regulatory friction in food production, costly training, hard to debug; kept as fallback (decision 12) |
| 13 | On-gateway online identification | Identification is heavy and infrequent and the gateway class is sized for inference |

## Consequences

**Positive**

- Calibration and identification are first-class architectural concerns.
- Survey-as-a-service is a smaller first commitment than a full deployment.
- Operating-phase deployments carry fewer sensors, and the same instances serve several deployments over time.
- Residual monitoring detects equipment faults, drift and model staleness without added hardware.
- One profile version rolls back setpoints and model together.
- The method matches the experimental practice of research and phenotyping customers.

**Negative**

- The calibration service depends on identification engineers the organisation must hire or train.
- The profile schema must carry and validate model parameters.
- The estimator adds continuous compute load on the gateway; large models may need a larger gateway class (ADR-0002).
- Users without identification expertise run rule-based control unless supported by the platform.
- Re-identification needs kit availability and planned downtime.

## Deferred

| Open question | Settled by |
|---|---|
| Wire types for estimated publications and their relation to measured ones | ADR-0005 vocabulary |
| Profile schema for model parameters, residual envelopes and identification metadata | ADR-0009 (profile schema) |
| A category for exogenous inputs in the state partition (`spec/E0009` `O-24`) | Gateway estimator contract |
| Survey kit packaging, mounts and return logistics | Operations |
| Identification toolchain and data formats | First identification campaign |
| Re-identification trigger values | Per cultivar and deployment |
| Training, deployment and monitoring of an ML fallback | A future ML ADR, if ML use becomes significant |
| Claims admissible for model-based observation in food production | Compliance |

## References

- ADR-0014 — sensor classes, condensate input, M04 radiometric path.
- ADR-0015 — profile as single mutation channel; gateway control loops.
- `gateway/control_model.py` — supervisory loop with the observer slot this record fills.
- L. Ljung, *System Identification: Theory for the User*.
- B. D. O. Anderson, J. B. Moore, *Optimal Filtering*.
- B. H. E. Vanthoor et al., "A methodology for model-based greenhouse design".
