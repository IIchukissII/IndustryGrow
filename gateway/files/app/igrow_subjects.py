#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""What the gateway subscribes to, and how each subject's payload is read.

ONE COPY, AND IT IS A COPY. The subject-IDs are compiled into the node firmware
(`nodes/m05_safety/sensors.c`, `nodes/m01_climate/sensors.c`) and described for
an operator in the per-module bring-up protocols under `store/`. ADR-0005
decision 7 makes each one a `uavcan.pub.<name>.id` register with the compiled
value as a default; until the firmware carries those registers there is no way
for a consumer to ask a node what it publishes, so the map is restated here.
When the registers land, this table is read off the bus and this file holds the
types alone.

The DSDL types are NOT restated: they are imported from the packages compiled at
provisioning time from `firmware/dsdl` and the pinned regulated set (ADR-0005
decision 10 -- generated code is not vendored). A wire-format change reaches this
file as an import or attribute error, not as a silently wrong number.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable, Optional

import industryflow.greenhouse.climate as _climate
import industryflow.greenhouse.safety as _safety
import uavcan.si.sample.electric_current
import uavcan.si.sample.energy
import uavcan.si.sample.power
import uavcan.si.sample.pressure
import uavcan.si.sample.temperature
import uavcan.si.sample.voltage

# What a decoded payload becomes in the store: either one scalar in SI units, or
# a structure that has no single scalar. Exactly one of the two is ever set.
Extract = Callable[[Any], "tuple[Optional[float], Optional[dict]]"]


@dataclass(frozen=True)
class Subject:
    subject_id: int
    name: str  # stable key, used in reports; the store keys on subject_id
    unit: str  # SI unit on the wire; degrees and kPa are a display concern
    dtype: type
    extract: Extract


def _scalar(field: str) -> Extract:
    return lambda m: (float(getattr(m, field)), None)


def _door(m: Any) -> "tuple[Optional[float], Optional[dict]]":
    return None, {"engaged": bool(m.engaged), "valid": bool(m.valid)}


def _leak(m: Any) -> "tuple[Optional[float], Optional[dict]]":
    return None, {"wet": bool(m.wet), "valid": bool(m.valid)}


def _gas_sweep(m: Any) -> "tuple[Optional[float], Optional[dict]]":
    # GasResistance.2.0 is an R(T) sweep, not a scalar. Anything reading it as
    # the 1.0 scalar reads garbage, which is why there is no scalar here.
    return None, {
        "heater_celsius": [int(v) for v in m.heater_celsius],
        "ohm": [float(v) for v in m.ohm],
        "valid": bool(m.valid),
    }


SUBJECTS: "tuple[Subject, ...]" = (
    # --- M05-SAFETY (E0006), module class 0x05 ---
    Subject(4096, "bus_voltage", "V", uavcan.si.sample.voltage.Scalar_1_0, _scalar("volt")),
    Subject(4097, "bus_current", "A", uavcan.si.sample.electric_current.Scalar_1_0, _scalar("ampere")),
    Subject(4098, "bus_power", "W", uavcan.si.sample.power.Scalar_1_0, _scalar("watt")),
    Subject(4099, "cabinet_temperature", "K", uavcan.si.sample.temperature.Scalar_1_0, _scalar("kelvin")),
    Subject(4100, "door", "", _safety.DoorStatus_1_0, _door),
    Subject(4101, "leak", "", _safety.LeakStatus_1_0, _leak),
    Subject(4102, "bus_energy", "J", uavcan.si.sample.energy.Scalar_1_0, _scalar("joule")),
    # --- M01-CLIMATE (E0002), module class 0x01 ---
    Subject(4112, "air_temperature", "K", uavcan.si.sample.temperature.Scalar_1_0, _scalar("kelvin")),
    Subject(4113, "air_humidity", "1", _climate.RelativeHumidity_1_0, _scalar("ratio")),
    Subject(4114, "air_vpd", "Pa", uavcan.si.sample.pressure.Scalar_1_0, _scalar("pascal")),
    Subject(4115, "co2", "1", _climate.Co2Concentration_1_0, _scalar("mole_fraction")),
    Subject(4116, "barometric_pressure", "Pa", uavcan.si.sample.pressure.Scalar_1_0, _scalar("pascal")),
    Subject(4117, "gas_resistance_sweep", "ohm", _climate.GasResistance_2_0, _gas_sweep),
    Subject(4118, "u2_temperature", "K", uavcan.si.sample.temperature.Scalar_1_0, _scalar("kelvin")),
    Subject(4119, "u2_humidity", "1", _climate.RelativeHumidity_1_0, _scalar("ratio")),
    Subject(4120, "u3_temperature", "K", uavcan.si.sample.temperature.Scalar_1_0, _scalar("kelvin")),
    Subject(4121, "u3_humidity", "1", _climate.RelativeHumidity_1_0, _scalar("ratio")),
)

BY_ID = {s.subject_id: s for s in SUBJECTS}
