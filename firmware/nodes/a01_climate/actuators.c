/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "actuators.h"

#include <math.h>
#include <string.h>

#include "condition.h"
#include "ds18b20.h"
#include "pca9685.h"

#include "e0001.h"
#include "clock.h"
#include "cyphal.h"
#include "registers.h"
#include "i2c.h"
#include "onewire.h"
#include "pwm.h"
#include "uart.h"
#include "uavcan/node/Heartbeat_1_0.h"       /* Health constants */
#include "uavcan/node/ExecuteCommand_1_0.h"  /* command response status */
#include "uavcan/diagnostic/Severity_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "industryflow/greenhouse/actuator/Demand_1_0.h"
#include "industryflow/greenhouse/actuator/ElementState_1_0.h"
#include "industryflow/greenhouse/actuator/InterlockState_1_0.h"

/* Default subject-IDs (unregulated range), A01 spec 10. Baked, as every
 * personality's are until ADR-0005 d7's registers have a store. */
#define SUBJ_TEMPERATURE 4352u /* + role: U1 WB1, U2 HX1 base, U3 coil, U4 reheater */
#define SUBJ_STATE       4356u /* + element: E1..E6 */
#define SUBJ_INTERLOCK   4362u
#define SUBJ_DEMAND      4368u /* + element: E1..E6 */

enum { E1, E2, E3, E4, E5, E6, ELEMENTS };
enum { U1, U2, U3, U4 };

/* Header PWM lines (A01 spec 5). The two enables are held static, at 0 or 1. */
#define LINE_E1_EN   1u /* PWM_1 -> U5 IN1 */
#define LINE_E3_EN   2u /* PWM_2 -> U6 IN1 */
#define LINE_E1_VREF 3u /* PWM_3 -> U5 VREF through R28/C7 */
#define LINE_E3_VREF 4u /* PWM_4 -> U6 VREF1 through R34/C16 */
#define PWM_HZ 20000u   /* 10 kOhm / 1 uF: ripple well under one IPROPI LSB */

/* U9 channels (A01 spec 5). */
#define U9_E2_FAN    0u
#define U9_E4_EN     1u
#define U9_E5_FAN    2u
#define U9_E6_VALVE  3u
#define U9_E1_PH     4u
#define U9_FW_EN_U5  5u
#define U9_FW_EN_U6  6u
#define U9_E4_VREF   7u
#define U9_CHANNELS  8u

/* Interlock lines T2_N..T5_N on GPIO_1..GPIO_4, pulled up on the module and
 * pulled low by a trip (A01 spec 5, 8). Bit n of the published masks is line n. */
static const struct {
    GPIO_TypeDef *port;
    uint8_t pin;
} TRIP_LINE[4] = {{GPIOA, 9u}, {GPIOA, 10u}, {GPIOA, 15u}, {GPIOB, 12u}};
#define TRIP_T2 0x1u
#define TRIP_T3 0x2u
#define TRIP_T4 0x4u
#define TRIP_T5 0x8u
#define TRIP_BOTH_DRIVERS (TRIP_T2 | TRIP_T3 | TRIP_T4) /* the EN_CHAIN, U5 and U6 */
#define TRIP_U6_ONLY      TRIP_T5                       /* humidity-tract airflow */

/* nFAULT, U5 and U6 wired-OR, on GPIO_5 = PA5, pulled up on the module. */
#define NFAULT_PIN 5u

/* IPROPI: I = V / (A_IPROPI * R_IPROPI). 212 uA/A (DRV8262 datasheet, 7.5) into
 * 7.87 kOhm (R29, R35) puts the 1.98 A string limit at 3.30 V. ADC1 channels 14
 * and 15 are ADC_1 and ADC_2. */
#define ADC_CH_E1 14u
#define ADC_CH_E3 15u
#define AMP_PER_VOLT (1.0f / (212.0e-6f * 7870.0f))

#define STEP_US       100000u  /* conditioning at 10 Hz */
#define STEP_S        0.1f
#define PUBLISH_US    1000000u
#define U9_CHECK_US   1000000u
#define U9_FAIL_LIMIT 3u
#define OW_MAX        8u

/* --- Commissioning constants (A01 spec F7) --------------------------------
 *
 * Node-local, set at commissioning, never carried in the profile (ADR-0015
 * d18). Each is a register with the specification's value as its default.
 * None is persistent yet: like every register but uavcan.node.id they reset to
 * these defaults at power-up (ADR-0005 d7). */

/* a01.drive -- D5 dead band, D5 dwell at zero before a reversal (s), D7 slew (1/s). */
static float s_drive[3] = {0.05f, 60.0f, 0.05f};
#define DRIVE_DEAD_BAND 0
#define DRIVE_DWELL_S   1
#define DRIVE_SLEW      2

/* a01.thermal (K, s) -- D9 full scale below, D9 zero at, D9 re-enable below,
 * D10 absolute floor, D11 coil floor, the band over which a floor derates a
 * cooling demand, and the F8 sensor-loss timeout. The band is a node design
 * value the specification does not set (verify at V-tests). */
static float s_thermal[7] = {308.15f, 318.15f, 305.15f, 276.15f, 274.15f, 1.0f, 5.0f};
#define TH_D9_FULL   0
#define TH_D9_ZERO   1
#define TH_D9_REARM  2
#define TH_D10_MIN   3
#define TH_D11_FLOOR 4
#define TH_BAND      5
#define TH_TIMEOUT_S 6

/* a01.fan -- D13: E5 minimum stable duty and start-pulse duration (s). Both are
 * SP0008's and unset (O-128); zero disables each. */
static float s_fan[2] = {0.0f, 0.0f};
#define FAN_E5_MIN   0
#define FAN_E5_PULSE 1

/* a01.valve -- D14: valve fitted (1) or not (0), pulse period (s), minimum open
 * time (s). Not fitted in the baseline population (A01 spec 3.4, O-110). */
static float s_valve[3] = {0.0f, 10.0f, 0.0f};
#define VALVE_FITTED   0
#define VALVE_PERIOD_S 1
#define VALVE_MIN_S    2

/* a01.e1.heat_ph -- the U5 PH level that heats the grow volume: 1 = PH high
 * (OUT1 -> OUT2), 0 = PH low. Set by how the E0012 string is landed on J4. */
static float s_heat_ph[1] = {1.0f};

/* a01.ow.rom -- the ROM code of U1..U4, 0 = unassigned. Four DS18B20 on one bus
 * are indistinguishable until someone who knows where each is mounted says so;
 * an unassigned role publishes nothing and counts as lost (F8). */
static uint64_t s_rom[4];

/* --- Element state ------------------------------------------------------- */

typedef struct {
    float value;          /* last demand, as received */
    float kelvin;
    uint64_t deadline_us;
    bool received;
    bool stale;
    float slewed;         /* conditioned, before the derates */
    float applied;        /* what is driven */
    uint8_t state;
    cond_reversal_t rev;
    uint8_t tid;
} element_t;

static element_t s_el[ELEMENTS];

static uint8_t s_tripped;        /* interlock lines tripped now */
static uint8_t s_latched;        /* lines tripped since reset (F5) */
static bool s_fault;             /* nFAULT seen; latched until reset (F9) */
static bool s_block_cut;         /* D9 hysteresis */
static uint64_t s_e5_pulse_until;
static uint64_t s_valve_cycle_start;
static bool s_valve_open;

static float s_u9_written[U9_CHANNELS];
static bool s_u9_force = true;   /* rewrite every channel on the next step */
static uint8_t s_u9_fails;
static bool s_u9_lost;

static uint64_t s_rom_found[OW_MAX];
static size_t s_rom_found_n;

static uint64_t s_last_step, s_last_pub, s_last_u9_check;
static uint8_t s_tid_temp[4], s_tid_interlock;
static uint8_t s_last_tripped;
static bool s_last_fault;

#define ST(name) industryflow_greenhouse_actuator_ElementState_1_0_STATE_##name

/* --- Demands -------------------------------------------------------------- */

/* Runs from cyphal_spin(): store and return, no I2C. */
static void on_demand(unsigned e, const uint8_t *payload, size_t size)
{
    if ((e == E6) && !(s_valve[VALVE_FITTED] > 0.5f)) {
        return; /* F12: an unpopulated branch accepts no demand */
    }
    industryflow_greenhouse_actuator_Demand_1_0 m;
    size_t sz = size;
    if (industryflow_greenhouse_actuator_Demand_1_0_deserialize_(&m, payload, &sz) < 0) {
        return;
    }
    element_t *el = &s_el[e];
    el->value = m.value;
    el->kelvin = m.kelvin;
    el->deadline_us = micros64() + (uint64_t)m.horizon_ms * 1000u;
    el->received = true;
}

#define DEMAND_HANDLER(E)                                                         \
    static void on_demand_##E(uint8_t from, const uint8_t *payload, size_t size) \
    {                                                                             \
        (void)from;                                                               \
        on_demand(E, payload, size);                                              \
    }
DEMAND_HANDLER(E1)
DEMAND_HANDLER(E2)
DEMAND_HANDLER(E3)
DEMAND_HANDLER(E4)
DEMAND_HANDLER(E5)
DEMAND_HANDLER(E6)

static const cyphal_message_fn DEMAND_FN[ELEMENTS] = {
    on_demand_E1, on_demand_E2, on_demand_E3, on_demand_E4, on_demand_E5, on_demand_E6,
};

/* --- Inputs --------------------------------------------------------------- */

static void read_lines(void)
{
    uint8_t t = 0u;
    for (unsigned i = 0; i < 4u; i++) {
        if ((TRIP_LINE[i].port->IDR & (1u << TRIP_LINE[i].pin)) == 0u) {
            t |= (uint8_t)(1u << i);
        }
    }
    s_tripped = t;
    s_latched |= t;
    if ((GPIOA->IDR & (1u << NFAULT_PIN)) == 0u) {
        s_fault = true;
    }
}

/* Latest reading of `role` if it is no older than the F8 timeout. */
static bool fresh(unsigned role, uint64_t now, float *kelvin)
{
    uint64_t at = 0u;
    if ((s_rom[role] == 0u) || !ds18b20_latest(role, kelvin, &at)) {
        return false;
    }
    return (now - at) <= (uint64_t)(s_thermal[TH_TIMEOUT_S] * 1.0e6f);
}

/* --- Conditioning ---------------------------------------------------------- */

/* The tail every thermoelectric element shares: dead band (D5), reversal dwell
 * (D3, D5), slew (D7) where the element takes one, then the derates as factors
 * on each sign. A stop -- stale demand, trip, fault, lost input -- is immediate
 * and is never slewed. */
static void run_tec(element_t *el, float demand, bool slew, float f_pos, float f_neg,
                    bool tripped, bool lost, uint64_t now)
{
    const float banded = cond_dead_band(demand, s_drive[DRIVE_DEAD_BAND]);
    const float target = cond_reversal_gate(&el->rev, banded, now, s_drive[DRIVE_DWELL_S]);

    const bool stop = el->stale || s_fault || tripped || lost || s_u9_lost;
    if (stop) {
        el->slewed = 0.0f;
    } else if (slew) {
        el->slewed = cond_slew(el->slewed, target, s_drive[DRIVE_SLEW] * STEP_S);
    } else {
        el->slewed = target;
    }
    cond_reversal_track(&el->rev, el->slewed, now);

    const float f = (el->slewed > 0.0f) ? f_pos : f_neg;
    el->applied = el->slewed * f;

    if (s_fault) {
        el->state = ST(FAULT);
    } else if (tripped) {
        el->state = ST(TRIPPED);
    } else if (banded == 0.0f) {
        el->state = (el->applied != 0.0f) ? ST(RUN) : ST(OFF); /* RUN while ramping down */
    } else if (el->applied == 0.0f) {
        el->state = ST(INHIBITED); /* reversal dwell, lost input, or a derate at zero */
    } else if (f < 1.0f) {
        el->state = ST(DERATED);
    } else {
        el->state = ST(RUN);
    }
}

static void condition(uint64_t now)
{
    for (unsigned e = 0; e < ELEMENTS; e++) {
        s_el[e].stale = !s_el[e].received || (now >= s_el[e].deadline_us);
    }

    float t_u1 = 0.0f, t_u2 = 0.0f, t_u3 = 0.0f;
    const bool u1 = fresh(U1, now, &t_u1);
    const bool u2 = fresh(U2, now, &t_u2);
    const bool u3 = fresh(U3, now, &t_u3);

    /* D9, F8: losing U1 is the ceiling reached. */
    const float f9 = u1 ? cond_block_derate(t_u1, s_thermal[TH_D9_FULL], s_thermal[TH_D9_ZERO],
                                            s_thermal[TH_D9_REARM], &s_block_cut)
                        : 0.0f;
    const float band = s_thermal[TH_BAND];

    /* E1, signed, positive heats. D10: cooling is held off the higher of the
     * floor that came with the demand and the node's own minimum. */
    {
        element_t *el = &s_el[E1];
        const float d = el->stale ? 0.0f : cond_clamp(el->value, -1.0f, 1.0f);
        float floor_k = s_thermal[TH_D10_MIN];
        if (isfinite(el->kelvin) && (el->kelvin > floor_k)) {
            floor_k = el->kelvin;
        }
        const float g = u2 ? cond_floor_derate(t_u2, floor_k, band) : 0.0f;
        run_tec(el, d, true, f9, f9 * g, (s_tripped & TRIP_BOTH_DRIVERS) != 0u,
                !u1 || !u2, now);
    }

    /* E2, main-tract fan. Safe output is full (D8). */
    {
        element_t *el = &s_el[E2];
        el->applied = el->stale ? 1.0f : cond_clamp(el->value, 0.0f, 1.0f);
        el->state = s_u9_lost ? ST(INHIBITED) : ((el->applied > 0.0f) ? ST(RUN) : ST(OFF));
    }

    /* E3, subcooler, cooling only. D11: the demand carries the coil setpoint
     * (F10); without one there is nothing to condition against. */
    {
        element_t *el = &s_el[E3];
        const float d = el->stale ? 0.0f : cond_clamp(el->value, 0.0f, 1.0f);
        const bool has_setpoint = isfinite(el->kelvin);
        float setpoint = s_thermal[TH_D11_FLOOR];
        if (has_setpoint && (el->kelvin > setpoint)) {
            setpoint = el->kelvin;
        }
        const float h = u3 ? cond_floor_derate(t_u3, setpoint, band) : 0.0f;
        run_tec(el, d, true, f9 * h, 0.0f,
                (s_tripped & (TRIP_BOTH_DRIVERS | TRIP_U6_ONLY)) != 0u,
                !u1 || !u3 || !has_setpoint, now);
    }

    /* E5, humidity-tract fan. Safe output is full (D8); D13 start pulse and
     * minimum stable duty, both unset until O-128 settles them. */
    {
        element_t *el = &s_el[E5];
        const bool was_off = !(el->applied > 0.0f);
        float d = el->stale ? 1.0f : cond_clamp(el->value, 0.0f, 1.0f);
        if (d < s_fan[FAN_E5_MIN]) {
            d = 0.0f;
        }
        if (was_off && (d > 0.0f) && (s_fan[FAN_E5_PULSE] > 0.0f)) {
            s_e5_pulse_until = now + (uint64_t)(s_fan[FAN_E5_PULSE] * 1.0e6f);
        }
        el->applied = ((d > 0.0f) && (now < s_e5_pulse_until)) ? 1.0f : d;
        el->state = s_u9_lost ? ST(INHIBITED) : ((el->applied > 0.0f) ? ST(RUN) : ST(OFF));
    }

    /* E4, reheater, heating only. D12: inhibited while the tract's air is not
     * moving -- T5 in hardware, E5 below its minimum here. */
    {
        element_t *el = &s_el[E4];
        const float d = el->stale ? 0.0f : cond_clamp(el->value, 0.0f, 1.0f);
        const float e5 = s_el[E5].applied;
        const bool no_air = (s_fan[FAN_E5_MIN] > 0.0f) ? (e5 < s_fan[FAN_E5_MIN]) : !(e5 > 0.0f);
        run_tec(el, d, false, f9, 0.0f,
                (s_tripped & (TRIP_BOTH_DRIVERS | TRIP_U6_ONLY)) != 0u,
                !u1 || no_air, now);
    }

    /* E6, CO2 valve, pulse-dosed (D14). Inhibited while E5 is not running. */
    {
        element_t *el = &s_el[E6];
        const float d = (el->stale || !(s_valve[VALVE_FITTED] > 0.5f))
                            ? 0.0f
                            : cond_clamp(el->value, 0.0f, 1.0f);
        const bool inhibited = !(s_el[E5].applied > 0.0f);
        el->applied = inhibited ? 0.0f : d;
        const float period = (s_valve[VALVE_PERIOD_S] > 0.0f) ? s_valve[VALVE_PERIOD_S] : 1.0f;
        if ((now - s_valve_cycle_start) >= (uint64_t)(period * 1.0e6f)) {
            s_valve_cycle_start = now;
        }
        float open_s = el->applied * period;
        if (open_s < s_valve[VALVE_MIN_S]) {
            open_s = 0.0f;
        }
        s_valve_open = (now - s_valve_cycle_start) < (uint64_t)(open_s * 1.0e6f);
        el->state = (inhibited && (d > 0.0f)) ? ST(INHIBITED)
                                              : ((el->applied > 0.0f) ? ST(RUN) : ST(OFF));
    }
}

/* --- Outputs --------------------------------------------------------------- */

static void u9_write(uint8_t ch, float duty)
{
    if (!s_u9_force && (s_u9_written[ch] == duty)) {
        return;
    }
    if (pca9685_set(ch, duty) == 0) {
        s_u9_written[ch] = duty;
        s_u9_fails = 0u;
    } else if (s_u9_fails < 255u) {
        s_u9_fails++;
    }
}

static void drive(void)
{
    const float e1 = s_el[E1].applied;
    const float e3 = s_el[E3].applied;
    const float e4 = s_el[E4].applied;

    /* MCU side first when stopping, so a lost U9 still leaves U5 and U6's
     * first bridge without an enable. */
    pwm_set(LINE_E1_EN, (e1 != 0.0f) ? 1.0f : 0.0f);
    pwm_set(LINE_E1_VREF, (e1 < 0.0f) ? -e1 : e1);
    pwm_set(LINE_E3_EN, (e3 != 0.0f) ? 1.0f : 0.0f);
    pwm_set(LINE_E3_VREF, e3);

    if (s_u9_lost) {
        return;
    }
    /* PH changes only while E1 is at zero -- the reversal dwell guarantees it --
     * so leaving it where it was at zero costs nothing. */
    const bool heat_high = s_heat_ph[0] > 0.5f;
    if (e1 > 0.0f) {
        u9_write(U9_E1_PH, heat_high ? 1.0f : 0.0f);
    } else if (e1 < 0.0f) {
        u9_write(U9_E1_PH, heat_high ? 0.0f : 1.0f);
    }
    u9_write(U9_E4_VREF, e4);
    u9_write(U9_E4_EN, (e4 != 0.0f) ? 1.0f : 0.0f);
    u9_write(U9_FW_EN_U5, (e1 != 0.0f) ? 1.0f : 0.0f);
    u9_write(U9_FW_EN_U6, ((e3 != 0.0f) || (e4 != 0.0f)) ? 1.0f : 0.0f);
    /* A LOW U9 output leaves the 2N7002 off and the fan at full (F11). */
    u9_write(U9_E2_FAN, 1.0f - s_el[E2].applied);
    u9_write(U9_E5_FAN, 1.0f - s_el[E5].applied);
    u9_write(U9_E6_VALVE, s_valve_open ? 1.0f : 0.0f);
    s_u9_force = false;

    if (s_u9_fails >= U9_FAIL_LIMIT) {
        s_u9_lost = true;
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR,
                          "A01 U9 lost: E1 and E3 stopped from the header; E4 holds its last drive");
    }
}

/* F11: a U9 that stopped answering, or that answers but has reset and gone back
 * to sleep, is re-initialized and every channel rewritten. */
static void check_u9(void)
{
    const int cfg = pca9685_configured();
    if (cfg == 1) {
        if (s_u9_lost) {
            s_u9_lost = false;
            s_u9_fails = 0u;
            s_u9_force = true;
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "A01 U9 recovered");
        }
        return;
    }
    if (pca9685_init() == 0) {
        s_u9_force = true;
        s_u9_fails = 0u;
        if (s_u9_lost) {
            s_u9_lost = false;
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "A01 U9 re-initialized");
        }
    } else if (!s_u9_lost) {
        s_u9_lost = true;
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR, "A01 U9 not answering");
    }
}

/* --- Publications ---------------------------------------------------------- */

static void pub_temperature(unsigned role, float kelvin)
{
    uavcan_si_sample_temperature_Scalar_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.kelvin = kelvin;
    uint8_t b[uavcan_si_sample_temperature_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_temperature_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish((uint16_t)(SUBJ_TEMPERATURE + role), &s_tid_temp[role], b, sz);
    }
}

static void pub_state(unsigned e)
{
    industryflow_greenhouse_actuator_ElementState_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.applied = s_el[e].applied;
    m.state = s_el[e].state;
    m.stale = s_el[e].stale;
    uint8_t b[industryflow_greenhouse_actuator_ElementState_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_actuator_ElementState_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish((uint16_t)(SUBJ_STATE + e), &s_el[e].tid, b, sz);
    }
}

static void pub_interlock(void)
{
    industryflow_greenhouse_actuator_InterlockState_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.tripped = s_tripped;
    m.latched = s_latched;
    uint8_t b[industryflow_greenhouse_actuator_InterlockState_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_actuator_InterlockState_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_INTERLOCK, &s_tid_interlock, b, sz);
    }
}

static void report(uint64_t now)
{
    bool sensor_lost = false;
    for (unsigned role = 0; role < 4u; role++) {
        float k = 0.0f;
        if (fresh(role, now, &k)) {
            pub_temperature(role, k);
        } else if (s_rom[role] != 0u) {
            sensor_lost = true;
        }
    }
    for (unsigned e = 0; e < ELEMENTS; e++) {
        if ((e == E6) && !(s_valve[VALVE_FITTED] > 0.5f)) {
            continue; /* F12 */
        }
        pub_state(e);
    }
    pub_interlock();

    if (s_tripped != s_last_tripped) {
        cyphal_diagnostic_u32((s_tripped != 0u) ? uavcan_diagnostic_Severity_1_0_WARNING
                                                : uavcan_diagnostic_Severity_1_0_NOTICE,
                              "A01 interlock lines tripped, bitmask T5|T4|T3|T2 =", s_tripped);
        s_last_tripped = s_tripped;
    }
    if (s_fault && !s_last_fault) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR,
                          "A01 driver nFAULT: thermoelectric drive latched off until reset");
        s_last_fault = true;
    }

    /* A fault stops every thermoelectric element for the rest of the boot; a
     * lost U9 leaves one of them out of reach. A trip is the interlock doing its
     * job, and an unassigned or silent sensor costs an element, not the node. */
    uint8_t health = uavcan_node_Health_1_0_NOMINAL;
    if (sensor_lost || (s_tripped != 0u)) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (s_u9_lost) {
        health = uavcan_node_Health_1_0_CAUTION;
    }
    if (s_fault) {
        health = uavcan_node_Health_1_0_WARNING;
    }
    cyphal_set_health(health);
}

/* --- Commands -------------------------------------------------------------- */

#define CMD_LIST_ROMS   1u
#define CMD_STRING_AMPS 2u

static void put_hex64(char *out, uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 15; i >= 0; i--) {
        out[15 - i] = hex[(v >> (4 * i)) & 0xFu];
    }
    out[16] = '\0';
}

static float adc_volts(uint8_t ch)
{
    ADC1->SQR3 = ch;
    ADC1->SR &= ~ADC_SR_EOC;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t g = 100000u;
    while (!(ADC1->SR & ADC_SR_EOC) && --g) {
    }
    return (g == 0u) ? -1.0f : (float)(ADC1->DR & 0xFFFu) * (3.3f / 4095.0f);
}

static uint8_t a01_command(uint16_t command, const uint8_t *param, size_t param_len)
{
    (void)param;
    (void)param_len;
    switch (command) {
    case CMD_LIST_ROMS: {
        /* What the boot search found, for assigning a01.ow.rom. */
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "A01 1-Wire devices found at boot =", (uint32_t)s_rom_found_n);
        for (size_t i = 0; i < s_rom_found_n; i++) {
            char text[40] = "A01 1-Wire ROM ";
            put_hex64(&text[strlen(text)], s_rom_found[i]);
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, text);
        }
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    }
    case CMD_STRING_AMPS: {
        /* IPROPI of E1 and E3, the V2 cross-check. E4's IPROPI2 reaches a test
         * point only. */
        const float v1 = adc_volts(ADC_CH_E1);
        const float v3 = adc_volts(ADC_CH_E3);
        if ((v1 < 0.0f) || (v3 < 0.0f)) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_FAILURE;
        }
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "A01 E1 string mA =", (uint32_t)(v1 * AMP_PER_VOLT * 1000.0f));
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "A01 E3 string mA =", (uint32_t)(v3 * AMP_PER_VOLT * 1000.0f));
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    }
    default:
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
}

/* --- Bring-up -------------------------------------------------------------- */

static void pin_input(GPIO_TypeDef *port, uint32_t pin)
{
    port->MODER &= ~(3u << (pin * 2u));
    port->PUPDR &= ~(3u << (pin * 2u)); /* the module holds every pull */
}

static void adc_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    GPIOC->MODER |= (3u << (4u * 2u)) | (3u << (5u * 2u)); /* PC4, PC5 analog */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0; /* PCLK2/4 = 21 MHz */
    ADC1->SMPR1 |= (7u << ((ADC_CH_E1 - 10u) * 3u)) | (7u << ((ADC_CH_E3 - 10u) * 3u));
    ADC1->SQR1 = 0u;
    ADC1->CR2 |= ADC_CR2_ADON;
}

static const uint16_t A01_SUBJECTS[] = {
    SUBJ_TEMPERATURE + 0u, SUBJ_TEMPERATURE + 1u, SUBJ_TEMPERATURE + 2u, SUBJ_TEMPERATURE + 3u,
    SUBJ_STATE + 0u, SUBJ_STATE + 1u, SUBJ_STATE + 2u,
    SUBJ_STATE + 3u, SUBJ_STATE + 4u, SUBJ_STATE + 5u,
    SUBJ_INTERLOCK,
};

void a01_actuators_init(void)
{
    cyphal_declare_publishers(A01_SUBJECTS,
                              (uint8_t)(sizeof(A01_SUBJECTS) / sizeof(A01_SUBJECTS[0])));
    cyphal_set_command_handler(a01_command);

    (void)registers_add_real32("a01.drive", s_drive, 3u);
    (void)registers_add_real32("a01.thermal", s_thermal, 7u);
    (void)registers_add_real32("a01.fan", s_fan, 2u);
    (void)registers_add_real32("a01.valve", s_valve, 3u);
    (void)registers_add_real32("a01.e1.heat_ph", s_heat_ph, 1u);
    (void)registers_add_natural64("a01.ow.rom", s_rom, 4u);

    /* Header lines first, so nothing the MCU drives is undefined while U9 and
     * the bus come up. The drivers sleep until U9's firmware enables rise. */
    pwm_init(PWM_HZ);

    i2c_init();
    s_u9_lost = (pca9685_init() != 0);
    s_u9_force = true;

    /* Last console output: GPIO_1 and GPIO_2 carry T2 and T3 on this module,
     * and USART1 shares them (E0001 spec F7). */
    uart_puts("debug console ends here (PA9/PA10 -> interlock lines T2/T3)\r\n");
    for (unsigned i = 0; i < 4u; i++) {
        pin_input(TRIP_LINE[i].port, TRIP_LINE[i].pin);
    }
    pin_input(GPIOA, NFAULT_PIN);
    adc_init();

    /* F2: enumerate the bus. Roles are assigned by register, not guessed. */
    ow_init();
    s_rom_found_n = ow_search(s_rom_found, OW_MAX);
    ds18b20_init(s_rom);

    for (unsigned e = 0; e < ELEMENTS; e++) {
        cond_reversal_init(&s_el[e].rev);
        s_el[e].stale = true;
        if ((e == E6) && !(s_valve[VALVE_FITTED] > 0.5f)) {
            continue; /* F12 -- the subscription waits for a fitted valve and a restart */
        }
        (void)cyphal_subscribe((uint16_t)(SUBJ_DEMAND + e),
                               industryflow_greenhouse_actuator_Demand_1_0_EXTENT_BYTES_,
                               DEMAND_FN[e]);
    }

    const uint64_t now = micros64();
    s_last_step = now;
    s_last_pub = now;
    s_last_u9_check = now;
    s_valve_cycle_start = now;

    cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "A01 up, 1-Wire devices found =", (uint32_t)s_rom_found_n);
    if (s_u9_lost) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR, "A01 U9 not answering at boot");
    }
}

void a01_actuators_spin(void)
{
    ds18b20_poll();

    const uint64_t now = micros64();
    if ((now - s_last_step) >= STEP_US) {
        s_last_step += STEP_US;
        read_lines();
        condition(now);
        drive();
    }
    if ((now - s_last_u9_check) >= U9_CHECK_US) {
        s_last_u9_check += U9_CHECK_US;
        check_u9();
    }
    if ((now - s_last_pub) >= PUBLISH_US) {
        s_last_pub += PUBLISH_US;
        report(now);
    }
}
