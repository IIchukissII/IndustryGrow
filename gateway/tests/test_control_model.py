# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Structural invariants of the climate model, before any number exists.

Nothing here checks a tuning -- there is none. What it pins is the shape the
model has to keep so that identification can fill it in, and the difference
equation the WeAct will later transliterate from.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import control_model as cm


def test_every_parameter_is_still_a_slot():
    """No coefficient may be committed with a value in it."""
    assert cm.missing_slots(), "a parameter has acquired a value outside DEMO"
    assert len(cm.missing_slots()) == len(cm.DEMO), "DEMO and the slot set have drifted apart"


def test_zoh_matches_the_scalar_solution():
    ad, bd = cm.zoh([[-0.5]], [[2.0]], 0.1)
    assert abs(ad[0][0] - math.exp(-0.05)) < 1e-12
    assert abs(bd[0][0] - 2.0 * (1 - math.exp(-0.05)) / 0.5) < 1e-12


def test_a_is_stable_across_the_fan_range():
    """The LPV model must stay Hurwitz at every flow setting, not only at the
    one point identification happened to fit. A free fit of the thermal-mass
    row can leave det(A) negative -- an A that reads as a cabinet which cools
    when the heater runs -- so this is checked, not assumed."""
    for u_fan in (0.0, 0.25, 0.5, 0.75, 1.0):
        a = _plant(dict(cm.DEMO), u_fan).a
        trace = a[0][0] + a[1][1]
        det = a[0][0] * a[1][1] - a[0][1] * a[1][0]
        assert trace < 0 and det > 0, f"thermal block unstable at u_fan={u_fan}"
        assert a[2][2] < 0, f"vapor state unstable at u_fan={u_fan}"


def test_thermal_mass_row_conserves_energy():
    """The mass exchanges only with the air, so its row sums to zero at every
    flow. Identification must fit A_M_T and A_M_M under that constraint."""
    for u_fan in (0.0, 0.5, 1.0):
        a = _plant(dict(cm.DEMO), u_fan).a
        assert abs(a[1][0] + a[1][1]) < 1e-12


def test_heater_warms_the_cabinet():
    assert _plant(dict(cm.DEMO), 0.5).steady_state_gain()[0][0] > 0


def test_mist_cools_and_humidifies():
    """The coupling the model exists for: one input, two effects."""
    gain = _plant(dict(cm.DEMO), u_fan=0.5).steady_state_gain()
    assert gain[0][1] < 0, "mist must remove sensible heat"
    assert gain[1][1] > 0, "mist must add vapor"


def test_pi_follows_the_book_difference_equation():
    """Listing 8.3: yi += Kid*(e_k + e_k-1), y = Kp*e + yi, Kid = Kp*T0/(2*Tn)."""
    cm.ARW_FACTOR = 0.96
    kp, tn, t0 = 2.0, 1.4, 0.01
    pi = cm.PiArw(kp, tn, t0, lo=-10.0, hi=10.0)
    kid = kp * t0 / (2.0 * tn)

    yi, e1 = 0.0, 0.0
    for ek in (1.0, 1.0, 0.5, -0.25):
        yi += kid * ek + kid * e1
        e1 = ek
        assert abs(pi.step(ek) - (kp * ek + yi)) < 1e-12


def test_anti_windup_unwinds_a_saturated_integrator():
    cm.ARW_FACTOR = 0.96
    pi = cm.PiArw(2.0, 1.4, 0.01, lo=0.0, hi=1.0)
    for _ in range(500):
        pi.step(10.0)
    saturated = pi.yi
    for _ in range(50):
        pi.step(0.0)
    assert pi.yi < saturated, "integrator never recovered from the clamp"


def test_vpd_setpoint_conversion_round_trips():
    for t_leaf in (16.0, 22.0, 30.0):
        for vpd in (0.4, 0.9, 1.6):
            v = cm.vapor_for_vpd(vpd, t_leaf)
            assert abs(cm.leaf_vpd(v, t_leaf) - vpd) < 1e-9


def _plant(params, u_fan):
    saved = {k: getattr(cm, k) for k in params}
    for k, v in params.items():
        setattr(cm, k, v)
    try:
        return cm.Plant(params["T0"], u_fan)
    finally:
        for k, v in saved.items():
            setattr(cm, k, v)
