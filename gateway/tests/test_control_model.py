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


def test_ambient_slots_drop_out_when_no_ambient_node():
    """A survey with no ambient node cannot produce these, so they must not be
    reported as missing -- that would demand numbers nobody can measure."""
    with_amb = set(cm.missing_slots(ambient=True))
    without = set(cm.missing_slots(ambient=False))
    assert with_amb - without == set(cm.AMBIENT_SLOTS)


def test_absent_ambient_is_not_computed_and_not_defaulted():
    """The whole point: passing None omits the term, it does not substitute a
    value. The two demands must differ by exactly the ambient feedforward."""
    saved = {k: getattr(cm, k) for k in cm.DEMO}
    for k, v in cm.DEMO.items():
        setattr(cm, k, v)
    try:
        amb = (18.0, 7.0)
        state = (20.0, 9.0, 19.0)
        with_amb = cm.OuterLoop().step((22.0, 0.90), state, (1.0, 0.5), amb)
        without = cm.OuterLoop().step((22.0, 0.90), state, (1.0, 0.5), None)
        expected = cm.mat_vec(cm.M_FF_AMB, list(amb))
        for a, b, e in zip(without, with_amb, expected, strict=True):
            assert 0.0 < a < 1.0 and 0.0 < b < 1.0, "clamped; the test proves nothing"
            assert abs((b - a) - e) < 1e-12
    finally:
        for k, v in saved.items():
            setattr(cm, k, v)


def test_losing_ambient_does_not_move_the_operating_point():
    """Under a constant ambient the two configurations must settle in the same
    place: the feedforward buys transient speed against an ambient CHANGE, not
    a different equilibrium. What is lost is identifiability and response time,
    which is why this is checked rather than assumed."""
    with_amb = _settle((18.0, 7.0))
    without = _settle(None)
    assert abs(with_amb[0] - without[0]) < 0.05
    assert abs(with_amb[1] - without[1]) < 0.005


def test_the_vpd_loop_holds_its_setpoint_either_way():
    """Temperature cannot hold here -- the lamp outheats the only cooling path,
    so the heater demand sits clamped at zero and a positive offset remains.
    The mist has authority in both directions, so VPD must reach setpoint."""
    for ambient in ((18.0, 7.0), None):
        assert abs(_settle(ambient)[1] - 0.90) < 0.01, f"ambient={ambient}"


def _settle(ambient, hours=3.0):
    saved = {k: getattr(cm, k) for k in cm.DEMO}
    for k, v in cm.DEMO.items():
        setattr(cm, k, v)
    try:
        plant = cm.Plant(cm.T0, 0.5)
        plant.x = [20.0, 20.0, 9.0]
        loop = cm.OuterLoop()
        for _ in range(int(hours * 3600 / cm.T0)):
            t_air, _, v_air = plant.x
            d = loop.step((22.0, 0.90), (t_air, v_air, t_air - 1.0), (1.0, 0.5), ambient)
            plant.step([d[0], 1.0, d[1], 0.5], [18.0, 7.0])
        t_air, _, v_air = plant.x
        return t_air, cm.leaf_vpd(v_air, t_air - 1.0)
    finally:
        for k, v in saved.items():
            setattr(cm, k, v)
