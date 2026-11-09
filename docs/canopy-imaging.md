<!--
SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Canopy imaging capture

The manual of [ADR-0033](../ADR/ADR-0033-canopy-imaging-channel.md): the P1 capture script and the files the imaging channel draws on, and which decisions have no form yet.

The one code form is `gateway/canopy_capture.py`, the P1 capture of the imaging rollout ([project/ROADMAP.md](../project/ROADMAP.md)). It runs on a Raspberry Pi with a CSI camera under the system picamera2, by hand or from cron, outside the gateway units. One run is one frame: a DNG, a JPEG and a JSON sidecar.

## ADR-0033 dec 1

The camera is driven from the gateway host through picamera2; no firmware, DSDL type or subject refers to it.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::open_camera` | Opens the CSI camera on the host |

## ADR-0033 dec 2, 3

Not yet realised. A run captures one frame under whatever light is on; no code sequences the thermal frame, the dark frame or per-channel exposures, or drives the luminaire to a capture state.

## ADR-0033 dec 4

Not yet realised. No code places a capture in the photoperiod or debits it against the DLI target; the cron line in the script's docstring is the operator's choice.

## ADR-0033 dec 5

Auto exposure, auto white balance, autofocus and noise reduction are off, and exposure, gain, colour gains and lens position are set from configuration; the run refuses while any of them is unset. The raw frame is saved as DNG. The JPEG beside it is the camera pipeline's processed image, not part of the measurement.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::pinned_controls` | Turns the automatic controls off and pins the values |
| `gateway/canopy_capture.py::require_pinned` | Refuses a capture with any pinned value unset |
| `gateway/canopy_capture.py::capture` | Saves the raw DNG and the sidecar |

## ADR-0033 dec 6

Not yet realised. P1 frames carry no reference surface.

## ADR-0033 dec 7

The sidecar records the output of an operator-configured light-state command; no code reads M02 at the capture instant.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::light_state` | Runs the configured command and records its output |

## ADR-0033 dec 8

Not yet realised. No flat-field, per-event dark frame or illumination characterisation exists.

## ADR-0033 dec 9

Software settings come from the configuration file and are recorded twice in the sidecar: as requested and as the camera reports them applied. The run warns when the applied exposure differs from the pinned one. Fiducial pose monitoring is not realised.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::load_config` | Reads the fixed settings |
| `gateway/canopy_capture.py::capture` | Records requested and applied settings per frame |
| `gateway/canopy_capture.py::focus_sweep` | Finds the lens position once, before it is pinned |

## ADR-0033 dec 10

The raw frame is retained with a SHA-256 per file in the sidecar. No segmentation exists, so no mask version is recorded.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::capture` | Retains the DNG and its hash |

## ADR-0033 dec 11, 12

Not yet realised. No shared housing design and no registration code or target exist in the repository; the imager's working distance and window are in [spec/E0005-R-specification.md](../spec/E0005-R-specification.md).

## ADR-0033 dec 13

Not yet realised. No profile field carries a capture schedule; the script is scheduled by cron.

## ADR-0033 dec 14

Not yet realised. No `A02-LIGHT` specification exists under `spec/`.

## ADR-0033 dec 15

Frames go to the configured output directory, outside the ADR-0020 store. `--status` reports frames held, bytes per frame and the projected storage per cycle; no retention bound or export is enforced.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::status` | Storage held and projected |

## ADR-0033 dec 16

Not yet realised. No quantity is derived from imagery.

## ADR-0033 dec 17

Not yet realised. The record leaves lifecycle placement to ADR-0016 identification.

## ADR-0033 dec 18

The luminaire channels the optical path must pass are the reference profile's. Whether far-red reaches the sensor is one of the P1 questions; no code tests it.

| Code | Role |
|---|---|
| `profiles/strawberry-day-neutral-v1.json` | Luminaire band set |

## ADR-0033 dec 19

The P1 camera is a colour sensor; the sidecar records the colour gains in force. No fixed colour transform from reference patches exists.

| Code | Role |
|---|---|
| `gateway/canopy_capture.py::pinned_controls` | Pins the colour gains |

Links: [spec/E0005-R-specification.md](../spec/E0005-R-specification.md), [project/ROADMAP.md](../project/ROADMAP.md), [project/RESEARCH.md](../project/RESEARCH.md).
