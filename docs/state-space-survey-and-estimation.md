<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Survey, identification and state estimation

The manual of [ADR-0016](../ADR/ADR-0016-empirical-survey-state-space-modeling-and-sensor-density-management.md): the code that carries the survey, identification and estimator decisions, and which of them have none.

The one code form is `gateway/control_model.py`: the cabinet climate plant in state-space form and the supervisory loop, with every plant, controller, state-feedback and observer coefficient a named slot left unset. It runs on demand only; no unit installs or runs it.

## ADR-0016 dec 1

Not yet realised. No code places the gateway in a survey mode or holds actuators fixed for one; the local store records at one rate ([gateway-persistence.md](gateway-persistence.md)).

## ADR-0016 dec 2, 11, 18

The identification output has slots to fill: plant matrices as an LPV pair in fan flow, controller gains, and the state-feedback and observer gains. No identification toolchain exists, and the slots prescribe no method.

| Code | Role |
|---|---|
| `gateway/control_model.py::Plant` | Plant in state-space form, discretised by zero-order hold |
| `gateway/control_model.py::missing_slots` | Lists the coefficients identification must supply |
| `gateway/control_model.py::K_STATE` | State-feedback gain slot |
| `gateway/control_model.py::L_OBS` | Observer gain slot |

## ADR-0016 dec 3, 4

Not yet realised. No gateway code runs an estimator, subscribes for one, or publishes estimates or residuals; the observer gain in `gateway/control_model.py` is unset and unused by any loop.

## ADR-0016 dec 5

Not yet realised. No estimated subject exists in `firmware/dsdl/industryflow` or `gateway/files/app/igrow_subjects.py`.

## ADR-0016 dec 6

Not yet realised. The reference profile `profiles/strawberry-day-neutral-v1.json` carries no model section; the profile schema is deferred.

## ADR-0016 dec 7

Not yet realised. No code computes a residual or raises an alert from one.

## ADR-0016 dec 8, 9, 14, 15

Not yet realised. These are inventory and commercial positions; no repository file carries them.

## ADR-0016 dec 10

Not yet realised. No profile field carries a re-identification trigger, and no code acts on one.

## ADR-0016 dec 12

Not yet realised. No ML model or ML-derived subject exists in the repository.

## ADR-0016 dec 13, 19

Identification is absent from the gateway: no gateway file fits a model. The gateway-side code holds only the model structure and the slots identification fills off-line.

| Code | Role |
|---|---|
| `gateway/control_model.py` | Deployed-model structure, no identification |

## ADR-0016 dec 16

Not yet realised. No code computes a mass balance or a resource flux.

## ADR-0016 dec 17

The outer loop is two PI controllers with a stationary decoupler and feedforward; it uses neither the state-feedback nor the observer slot.

| Code | Role |
|---|---|
| `gateway/control_model.py::OuterLoop` | PI and decoupler path |
| `gateway/control_model.py::stationary_decoupler` | Decoupler from the steady-state gain |

## ADR-0016 dec 20

Not yet realised. The only condensate code is the M01 heater pulse that clears condensation from its humidity sensor (`firmware/nodes/m01_climate/sensors.c`); no code counts collected condensate, converts it to transpiration or forms the envelope-moisture residual.

Links: [gateway-profile-and-control.md](gateway-profile-and-control.md), [gateway-persistence.md](gateway-persistence.md).
