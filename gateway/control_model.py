#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cabinet climate plant and supervisory control loop, in the ADR-0016 form.

The outer loop of ADR-0015 d18, written before the actuators exist so that
identification (ADR-0016, roadmap stages 7-8) has somewhere to put its numbers.
Every plant and controller coefficient is a named slot and every slot is `None`
until a survey produces it. Nothing here is tuned; nothing here is a value.

No actuator drives one variable alone, so the plant is MIMO from the start
rather than a set of independent PID loops:

    lamp   -> PPFD, and a sensible heat input
    mist   -> a vapor source, and a latent heat SINK (evaporative cooling)
    heater -> sensible heat, and VPD through the temperature it raises
    fan    -> the transport coefficient on all three, not an additive input

The fan is therefore bilinear, not linear, and is carried as an LPV parameter:
A(u_fan) = A0 + u_fan*A1, B(u_fan) = B0 + u_fan*B1. Identification runs the
survey at two or more fixed flow settings and solves for A0/A1 and B0/B1; it
never sweeps the fan inside a single identification run.

Algorithm sources -- Philippsen, *Einstieg in die Regelungstechnik mit Python*,
4th ed., Hanser 2022. Listings are at einstieg-rt.de/web<nnn>:

    PI with anti-reset-windup     listing 8.3 (web803), section 8.6
                                  the listing section 2.6 ports to a
                                  microcontroller -- so it is also the node-side
                                  algorithm, not only the gateway one
    trapezoidal PI discretization listing 8.2 (web802), section 8.5.3
    stationary decoupling filter  listing 7.1 (web701), section 7.5
    disturbance feedforward       section 7.1
    cascade structure             section 7.2
    state controller / observer   listings 7.5, 7.6 (web705, web706), 7.8-7.10
    dead-time (Smith predictor)   listing 6.7 (web607), section 6.8

Pure standard library on purpose: this must run on the bench host and on the
gateway with no package the gateway does not already need.

    python3 control_model.py             # lists the empty slots, and stops
    python3 control_model.py --demo      # placeholder numbers, structure only
"""

from __future__ import annotations

import argparse
import math
import sys

# --------------------------------------------------------------------------
# State, input and output vocabulary
# --------------------------------------------------------------------------
# x = [T_air, T_mass, v_air]
#       T_air   degC     air temperature, measured by M01
#       T_mass  degC     lumped thermal mass (walls, substrate, structure)
#       v_air   g/m^3    absolute humidity of the air
#
# u = [u_heat, u_lamp, u_mist, u_fan]   normalized demand 0..1 (ADR-0015 d18)
# w = [T_amb, v_amb]                    ambient, measured by M07
# y = [T_air, RH, PPFD]                 what M01 and M02 publish

STATES = ("T_air", "T_mass", "v_air")
INPUTS = ("u_heat", "u_lamp", "u_mist", "u_fan")
DISTURBANCES = ("T_amb", "v_amb")

# --------------------------------------------------------------------------
# Plant parameter slots -- ALL UNIDENTIFIED
# --------------------------------------------------------------------------
# A0: flow-independent dynamics, 1/s
A0_T_T = None  # air self-loss to ambient at zero flow
A0_T_M = None  # thermal mass -> air coupling
A0_M_T = None  # air -> thermal mass coupling
A0_M_M = None  # thermal mass self-loss
A0_V_V = None  # vapor loss (leakage, condensation) at zero flow

# A1: the fan's contribution to those same couplings, 1/s per unit fan duty
A1_T_T = None  # forced convection to ambient
A1_T_M = None  # forced convection to the thermal mass
A1_M_T = None
A1_M_M = None  # ... and its matching diagonal term
A1_V_V = None  # vapor carried out by air exchange

# The thermal mass exchanges only with the air, so energy conservation ties the
# mass row: A_M_M = -A_M_T at every flow. Identification must fit the two under
# that constraint. A free fit can land on a det(A) < 0 that looks plausible per
# coefficient and inverts to a negative process gain -- a cabinet that cools
# when the heater runs. gateway/tests checks the constraint and Hurwitz-ness.

# B0: input matrix at zero flow
B0_T_HEAT = None  # K/s per unit heater duty
B0_T_LAMP = None  # K/s per unit lamp duty         -- the lamp's heat
B0_T_MIST = None  # K/s per unit mist duty, NEGATIVE -- latent heat sink
B0_T_FAN = None  # K/s per unit fan duty          -- motor dissipation
B0_V_MIST = None  # (g/m^3)/s per unit mist duty   -- vapor source

# B1: the fan's contribution to the inputs, per unit fan duty
B1_T_MIST = None  # flow improves evaporation, so it deepens the cooling
B1_V_MIST = None  # flow improves evaporation, so it raises the vapor yield

# E: disturbance matrix. The plant always has this term -- the cabinet exchanges
# with the room regardless. What an absent ambient node removes is the ability to
# IDENTIFY it: ambient enters through the same exchange path as the loss, so in
# the ideal lumped model E_T_AMB = -(A0_T_T + u_fan*A1_T_T), one coefficient
# split across two matrices. With no ambient regressor the two cannot be
# separated, A0_T_T absorbs the loss at whatever ambient the run happened to sit
# at, and the identified model is valid only near that point. A modelling limit,
# not a control failure -- the loop still regulates, the integrator carries it.
E_T_AMB = None
E_V_AMB = None

# Static output gain
C_PPFD_LAMP = None  # umol/m^2/s per unit lamp duty, at the canopy

# Transport dead time from actuator to sensor, seconds. Above roughly 10 % of
# the dominant time constant these stop being ignorable and section 6.8's
# Smith predictor replaces the plain PI.
TT_HEAT = None
TT_LAMP = None
TT_MIST = None
TT_FAN = None

# --------------------------------------------------------------------------
# Controller parameter slots -- ALL UNIDENTIFIED
# --------------------------------------------------------------------------
T0 = None  # s, supervisory loop sample time on the gateway
DEMAND_TTL = None  # s, validity deadline that rides with each demand

KP_T = None  # air-temperature loop, book symbols Kp / Tn
TN_T = None
KP_V = None  # leaf-VPD loop
TN_V = None
# ARW correction divisor, book listing 8.3: Tikorr = ARW_FACTOR * Tn / T0.
# The book's worked value for ARW_FACTOR is 0.96; it stays a slot because it
# trades overshoot against how fast the integrator recovers from saturation.
ARW_FACTOR = None

# Stationary decoupling filter, section 7.5 / listing 7.1. Identity means "the
# RGA says the loops do not interact enough to need one". Computed by
# stationary_decoupler() from the plant's steady-state gain.
D_DEC = None

# Feedforward, section 7.1, split by whether its input is always available.
# Both are derived from the plant matrices during identification.
#   M_FF_DRIVE  [u_heat, u_mist] <- [u_lamp, u_fan]      always present: the
#               gateway commands both, so their values are known by definition.
#               This is what cancels the lamp's heat before the PI sees it.
#   M_FF_AMB    [u_heat, u_mist] <- [T_amb, v_amb]       OPTIONAL, and computed
#               only when an ambient node is publishing.
M_FF_DRIVE = None
M_FF_AMB = None

# Reserved for the ADR-0016 swap: state feedback and an observer replace the
# PI + decoupler pair once a reduced-order model is identified (stage 8).
# Sections 7.8-7.10, listings 7.5 and 7.6.
K_STATE = None  # 2x3 state-feedback gain
L_OBS = None  # 3x2 observer gain

# Node-side conditioning (ADR-0015 d18). Hardware properties, set at
# commissioning per ADR-0028 -- not profile content.
PWM_PERIOD = None  # s
MIN_ON = None  # s
MIN_OFF = None  # s
SLEW_PER_S = None  # demand units per second
SAFE_OUTPUT = None  # per actuator class; the taxonomy ADR owns which


# Parameters that exist only when an ambient node is publishing. An absent input
# is not defaulted and its terms are not computed -- the same rule the node
# firmware already applies to an unpopulated sensor (ADR-0014 d2: probe the bus,
# publish only what answers). Substituting a plausible constant for a missing
# measurement is the failure this forbids: the loop would then feed forward a
# number nobody measured and no residual would show it.
AMBIENT_SLOTS = ("E_T_AMB", "E_V_AMB", "M_FF_AMB")


def missing_slots(ambient=True):
    """Parameters still awaiting a number, for the configuration in use.

    With no ambient node the ambient slots are not missing -- they are not
    applicable, and reporting them would demand numbers no survey can produce.
    """
    skip = ("STATES", "INPUTS", "DISTURBANCES", "AMBIENT_SLOTS")
    if not ambient:
        skip += AMBIENT_SLOTS
    return sorted(k for k, v in globals().items() if k.isupper() and k not in skip and v is None)


# --------------------------------------------------------------------------
# Psychrometrics -- the nonlinear part of the output map
# --------------------------------------------------------------------------
def sat_vapor_pressure(t_c):
    """Saturation vapor pressure, kPa (Magnus)."""
    return 0.61094 * math.exp(17.625 * t_c / (t_c + 243.04))


def vapor_pressure(v_gm3, t_c):
    """Partial vapor pressure, kPa, from absolute humidity in g/m^3."""
    return v_gm3 * (t_c + 273.15) / 2166.74


def relative_humidity(v_gm3, t_c):
    return vapor_pressure(v_gm3, t_c) / sat_vapor_pressure(t_c)


def vapor_for_vpd(vpd_kpa, t_leaf_c):
    """Absolute humidity, g/m^3, that realizes a leaf-VPD setpoint.

    Inverse of leaf_vpd(). The profile carries a VPD setpoint (ADR-0003 d7),
    but the humidity loop regulates v_air: the mist input raises v_air and so
    LOWERS VPD, and a positive-gain PI cannot close a negative-gain loop.
    Converting here keeps the loop linear in the state and confines the
    psychrometric nonlinearity to the setpoint path.
    """
    e = sat_vapor_pressure(t_leaf_c) - vpd_kpa
    return e * 2166.74 / (t_leaf_c + 273.15)


def leaf_vpd(v_gm3, t_leaf_c):
    """Leaf VPD, kPa -- the regulated variable of ADR-0003 d7.

    t_leaf_c is the canopy temperature M04 measures radiometrically. It is not
    air temperature, and that difference is what makes this a plant variable
    rather than a climate variable.
    """
    return sat_vapor_pressure(t_leaf_c) - vapor_pressure(v_gm3, t_leaf_c)


# --------------------------------------------------------------------------
# Small dense linear algebra, enough for n = 3
# --------------------------------------------------------------------------
def mat_mul(a, b):
    return [
        [sum(a[i][k] * b[k][j] for k in range(len(b))) for j in range(len(b[0]))]
        for i in range(len(a))
    ]


def mat_add(a, b):
    return [[p + q for p, q in zip(ra, rb, strict=True)] for ra, rb in zip(a, b, strict=True)]


def mat_scale(a, s):
    return [[v * s for v in row] for row in a]


def mat_vec(a, v):
    return [sum(ai * vi for ai, vi in zip(row, v, strict=True)) for row in a]


def eye(n):
    return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]


def invert(a):
    n = len(a)
    m = [row[:] + e[:] for row, e in zip(a, eye(n), strict=True)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(m[r][c]))
        if abs(m[p][c]) < 1e-15:
            raise ValueError("singular matrix")
        m[c], m[p] = m[p], m[c]
        d = m[c][c]
        m[c] = [v / d for v in m[c]]
        for r in range(n):
            if r != c and m[r][c]:
                f = m[r][c]
                m[r] = [v - f * w for v, w in zip(m[r], m[c], strict=True)]
    return [row[n:] for row in m]


def expm(a):
    """Matrix exponential, by scaling and squaring with a Taylor series."""
    n = len(a)
    norm = max(sum(abs(v) for v in row) for row in a)
    s = max(0, math.ceil(math.log(norm / 0.5, 2))) if norm > 0.5 else 0
    x = mat_scale(a, 1.0 / (2**s))
    term = out = eye(n)
    for k in range(1, 20):
        term = mat_scale(mat_mul(term, x), 1.0 / k)
        out = mat_add(out, term)
    for _ in range(s):
        out = mat_mul(out, out)
    return out


def zoh(a, b, dt):
    """Zero-order-hold discretization; exact, and valid for a singular A.

    Exponentiates the augmented block [[A, B], [0, 0]], so no inverse of A is
    needed -- A is singular whenever the model carries a pure integrator, which
    is the CO2 case of section 4.5.
    """
    n, m = len(a), len(b[0])
    aug = [row[:] + b[i][:] for i, row in enumerate(a)]
    aug += [[0.0] * (n + m) for _ in range(m)]
    e = expm(mat_scale(aug, dt))
    return [row[:n] for row in e[:n]], [row[n:] for row in e[:n]]


# --------------------------------------------------------------------------
# Plant
# --------------------------------------------------------------------------
class Plant:
    """x' = A(u_fan) x + B(u_fan) u + E w, sampled at dt, with input dead time.

    E w is unconditional: the cabinet exchanges with the room whether or not
    anything is measuring the room. What an absent ambient node removes is the
    controller's knowledge of w and the ability to identify E -- not the term.
    """

    def __init__(self, dt, u_fan):
        self.a = mat_add(
            [[A0_T_T, A0_T_M, 0.0], [A0_M_T, A0_M_M, 0.0], [0.0, 0.0, A0_V_V]],
            mat_scale([[A1_T_T, A1_T_M, 0.0], [A1_M_T, A1_M_M, 0.0], [0.0, 0.0, A1_V_V]], u_fan),
        )
        self.b = mat_add(
            [
                [B0_T_HEAT, B0_T_LAMP, B0_T_MIST, B0_T_FAN],
                [0.0, 0.0, 0.0, 0.0],
                [0.0, 0.0, B0_V_MIST, 0.0],
            ],
            mat_scale(
                [[0.0, 0.0, B1_T_MIST, 0.0], [0.0, 0.0, 0.0, 0.0], [0.0, 0.0, B1_V_MIST, 0.0]],
                u_fan,
            ),
        )
        e = [[E_T_AMB, 0.0], [0.0, 0.0], [0.0, E_V_AMB]]
        self.ad, self.bd = zoh(self.a, self.b, dt)
        self.ed = zoh(self.a, e, dt)[1]
        self.x = [0.0, 0.0, 0.0]
        self.delay = [[0.0] * max(1, round(t / dt)) for t in (TT_HEAT, TT_LAMP, TT_MIST, TT_FAN)]

    def step(self, u, w):
        ud = []
        for line, ui in zip(self.delay, u, strict=True):
            line.append(ui)
            ud.append(line.pop(0))
        self.x = [
            p + q + r
            for p, q, r in zip(
                mat_vec(self.ad, self.x), mat_vec(self.bd, ud), mat_vec(self.ed, w), strict=True
            )
        ]
        return self.x

    def steady_state_gain(self, cols=(0, 2)):
        """G(0) = -A^-1 B, restricted to the regulating inputs.

        The matrix section 7.5 reads the coupling from, and the one the RGA of
        RESEARCH.md L3 is computed on. Default columns are heater and mist --
        the two inputs the loop actually manipulates.
        """
        g = mat_mul(mat_scale(invert(self.a), -1.0), self.b)
        return [[g[r][c] for c in cols] for r in (0, 2)]


def rga(g):
    """Relative Gain Array of a square gain matrix.

    Reads out where independent single-loop control would fight itself, and so
    decides whether D_DEC may stay the identity. Diagonal near 1 means the
    pairing is clean; near 0.5, or negative, means it is not.
    """
    gi = invert(g)
    return [[g[i][j] * gi[j][i] for j in range(len(g))] for i in range(len(g))]


def stationary_decoupler(g):
    """Stationary decoupling filter for a 2x2 plant, section 7.5 / listing 7.1.

    Unity diagonal, off-diagonals cancelling the cross gains. Always realizable
    -- it is P-only, so no inverted transfer function can turn improper, which
    is what rules out the dynamic filter in general.
    """
    return [[1.0, -g[0][1] / g[0][0]], [-g[1][0] / g[1][1], 1.0]]


# --------------------------------------------------------------------------
# PI with anti-reset-windup -- book listing 8.3
# --------------------------------------------------------------------------
class PiArw:
    """Trapezoidal PI with back-calculation anti-reset-windup.

    Difference equation and the windup correction follow listing 8.3: the
    integrator advances by Kid*(e_k + e_k-1), and whenever the previous output
    was clipped, the clipped-off amount is fed back through Tikorr so the
    integrator unwinds instead of running to the largest machine number.

    Held to that structure deliberately -- it is the same listing the book
    ports to a microcontroller in section 2.6, so the C on the WeAct is a
    transliteration of this and can be diffed against it line by line.
    """

    def __init__(self, kp, tn, t0, lo=0.0, hi=1.0):
        self.kp = kp
        self.lo, self.hi = lo, hi
        self.kid = kp * t0 / (2.0 * tn)
        self.tikorr = ARW_FACTOR * tn / t0
        self.yi = 0.0  # integrator state
        self.e1 = 0.0  # previous error
        self.ykk = 0.0  # previous unlimited output
        self.yk = 0.0  # previous limited output

    def step(self, ek):
        intkorr = (self.ykk - self.yk) / self.tikorr
        self.yi += self.kid * ek + self.kid * self.e1 - intkorr
        self.e1 = ek
        self.ykk = self.kp * ek + self.yi
        self.yk = min(self.hi, max(self.lo, self.ykk))
        return self.yk


# --------------------------------------------------------------------------
# Outer loop -- ADR-0015 d8, as bounded by d18
# --------------------------------------------------------------------------
class OuterLoop:
    """Setpoints in, actuator demands out. Holds no actuator timing.

    Structure is section 7.1 feedforward + section 7.5 decoupler + two PI
    controllers. The feedforward is what makes the lamp and the fan tractable:
    both are known inputs, so their effect on temperature is subtracted before
    the PI sees it rather than being left as an unexplained disturbance.
    """

    def __init__(self):
        # The PI limits are wide: the decoupler and feedforward sit between the
        # controllers and the actuator, so the real 0..1 clamp is applied after
        # them. ARW then acts on the demand the actuator could truly deliver.
        self.pi_t = PiArw(KP_T, TN_T, T0, lo=-2.0, hi=2.0)
        self.pi_v = PiArw(KP_V, TN_V, T0, lo=-2.0, hi=2.0)

    def step(self, setpoint, measured, drive, ambient=None):
        """setpoint = (T_sp, vpd_sp)
        measured  = (T_air, v_air, T_leaf)
        The VPD setpoint is converted to a v_air setpoint here, so the loop
        itself is linear and the psychrometrics stay in the setpoint path.
        drive     = (u_lamp, u_fan) -- commanded, so always known
        ambient   = (T_amb, v_amb), or None when no ambient node is publishing.
                    None means the ambient feedforward is not computed. It is
                    never replaced by a default; the PI integrator absorbs the
                    unmeasured ambient instead, more slowly and with no warning
                    that it is doing so.
        returns   = (demand_heat, demand_mist), each 0..1
        """
        t_air, v_air, t_leaf = measured
        e_t = setpoint[0] - t_air
        e_v = vapor_for_vpd(setpoint[1], t_leaf) - v_air
        out = mat_vec(D_DEC, [self.pi_t.step(e_t), self.pi_v.step(e_v)])
        out = [p + q for p, q in zip(out, mat_vec(M_FF_DRIVE, list(drive)), strict=True)]
        if ambient is not None:
            out = [p + q for p, q in zip(out, mat_vec(M_FF_AMB, list(ambient)), strict=True)]
        return tuple(min(1.0, max(0.0, v)) for v in out)


# --------------------------------------------------------------------------
# Node-side conditioning -- ADR-0015 d18
# --------------------------------------------------------------------------
class Conditioner:
    """What the actuator node does to a demand, modelled so the reference
    trajectory is the one the cabinet would actually follow."""

    def __init__(self, dt):
        self.dt = dt
        self.y = 0.0
        self.age = 0.0

    def step(self, demand, fresh):
        self.age = 0.0 if fresh else self.age + self.dt
        if self.age > DEMAND_TTL:
            self.y = SAFE_OUTPUT
            return self.y
        lim = SLEW_PER_S * self.dt
        self.y += min(lim, max(-lim, demand - self.y))
        return self.y


# --------------------------------------------------------------------------
# Placeholder numbers -- STRUCTURE ONLY, never a tuning
# --------------------------------------------------------------------------
DEMO = {
    "A0_T_T": -1.0 / 900,
    "A0_T_M": 1.0 / 1800,
    "A0_M_T": 1.0 / 5400,
    "A0_M_M": -1.0 / 5400,
    "A1_M_M": -1.0 / 1800,
    "A0_V_V": -1.0 / 1200,
    "A1_T_T": -1.0 / 600,
    "A1_T_M": 1.0 / 1200,
    "A1_M_T": 1.0 / 1800,
    "A1_V_V": -1.0 / 900,
    "B0_T_HEAT": 0.010,
    "B0_T_LAMP": 0.004,
    "B0_T_MIST": -0.003,
    "B0_T_FAN": 0.0005,
    "B0_V_MIST": 0.020,
    "B1_T_MIST": -0.002,
    "B1_V_MIST": 0.010,
    "E_T_AMB": 1.0 / 900,
    "E_V_AMB": 1.0 / 1200,
    "C_PPFD_LAMP": 300.0,
    "TT_HEAT": 20.0,
    "TT_LAMP": 5.0,
    "TT_MIST": 15.0,
    "TT_FAN": 2.0,
    "T0": 1.0,
    "DEMAND_TTL": 5.0,
    "KP_T": 0.30,
    "TN_T": 600.0,
    "KP_V": 0.40,
    "TN_V": 300.0,
    "ARW_FACTOR": 0.96,
    "M_FF_DRIVE": [[0.0, 0.0], [0.0, 0.0]],
    "M_FF_AMB": [[-0.02, 0.0], [0.0, -0.01]],
    "PWM_PERIOD": 10.0,
    "MIN_ON": 1.0,
    "MIN_OFF": 1.0,
    "SLEW_PER_S": 0.10,
    "SAFE_OUTPUT": 0.0,
    "K_STATE": [[0.0] * 3, [0.0] * 3],
    "L_OBS": [[0.0] * 2, [0.0] * 2, [0.0] * 2],
    "D_DEC": [[1.0, 0.0], [0.0, 1.0]],
}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--demo", action="store_true", help="load placeholder numbers and emit a reference CSV"
    )
    ap.add_argument("--hours", type=float, default=6.0)
    ap.add_argument(
        "--no-ambient",
        action="store_true",
        help="run with no ambient node: the ambient terms are not computed",
    )
    ap.add_argument(
        "--fan", type=float, default=0.5, help="fan operating point; the LPV scheduling variable"
    )
    args = ap.parse_args(argv)

    ambient = not args.no_ambient
    if not args.demo:
        gaps = missing_slots(ambient=ambient)
        print(f"unidentified parameters: {len(gaps)}")
        for k in gaps:
            print("  " + k)
        print("\nfill these from an ADR-0016 survey; --demo runs the structure only")
        return 1

    globals().update(DEMO)
    dt = T0
    plant = Plant(dt, args.fan)

    # Read the coupling before deciding to compensate it (section 7.5): the RGA
    # says whether the two loops interact, the decoupler cancels it if they do.
    g0 = plant.steady_state_gain()
    globals()["D_DEC"] = stationary_decoupler(g0)

    loop = OuterLoop()
    heat, mist = Conditioner(dt), Conditioner(dt)
    plant.x = [20.0, 20.0, 9.0]

    print("# DEMO_ONLY -- placeholder coefficients, not a tuning")
    print(f"# ambient node: {'present' if ambient else 'absent, feedforward not computed'}")
    print(f"# G(0) = {g0}")
    print(f"# RGA  = {rga(g0)}")
    print("t_s,T_air,T_mass,v_air,RH,leaf_VPD,PPFD,u_heat,u_mist,u_lamp")
    for k in range(int(args.hours * 3600 / dt)):
        t = k * dt
        u_lamp = 1.0 if (t % 86400) < 16 * 3600 else 0.0
        t_air, t_mass, v_air = plant.x
        t_leaf = t_air - 1.0  # placeholder; M04 measures this (ADR-0014)
        d_heat, d_mist = loop.step(
            (22.0, 0.90),
            (t_air, v_air, t_leaf),
            (u_lamp, args.fan),
            (18.0, 7.0) if ambient else None,
        )
        u = [heat.step(d_heat, True), u_lamp, mist.step(d_mist, True), args.fan]
        plant.step(u, [18.0, 7.0])
        if k % 60 == 0:
            print(
                f"{t:.0f},{t_air:.3f},{t_mass:.3f},{v_air:.3f},"
                f"{relative_humidity(v_air, t_air):.3f},"
                f"{leaf_vpd(v_air, t_leaf):.4f},{C_PPFD_LAMP * u_lamp:.1f},"
                f"{u[0]:.3f},{u[2]:.3f},{u_lamp:.1f}"
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
