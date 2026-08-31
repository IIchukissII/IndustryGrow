/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensors.h"

#include "clock.h"
#include "cyphal.h"
#include "registers.h"
#include "uart.h"
#include "i2c.h"
#include "tca9543a.h"
#include "as7343.h"
#include "tsl2585.h"

#include "uavcan/node/Heartbeat_1_0.h"      /* Health constants */
#include "uavcan/node/ExecuteCommand_1_0.h" /* command response status */
#include "uavcan/diagnostic/Severity_1_0.h"
#include "industryflow/greenhouse/light/SpectralSample_1_0.h"
#include "industryflow/greenhouse/light/PhotonFluxDensity_1_0.h"
#include "industryflow/greenhouse/light/FlickerStatus_1_0.h"
#include "industryflow/greenhouse/light/Irradiance_1_0.h"

/* Default subject-IDs, unregulated range. ADR-0005 d7 wants these register-
 * configurable (uavcan.pub.<name>.id) with these as defaults; baked for now,
 * as on M05 and M01. M05 holds 4096-4102 and M01 4112-4121, so M02 starts at
 * 4128 and leaves M01's block room to grow.
 *
 * 4132 and 4133 were UV-B and UV-C and are RETIRED, not reassigned (M02 spec
 * 10.1): ADR-0014 rev 6 withdrew the AS7331 that measured them, and a gateway
 * holding an older register set must not silently bind a new quantity to an
 * identifier it already knows as something else. Nothing here may use them. */
#define SUBJ_SPECTRUM 4128u /* U4 AS7343 -- 12 bands + clear + settings */
#define SUBJ_PPFD     4129u /* derived on-node from U4, spec 6.2 */
#define SUBJ_FLICKER  4130u /* U4 AS7343 -- ADC5 */
#define SUBJ_UV_A     4131u /* U3 TSL2585 */

#define PUBLISH_PERIOD_US 1000000u
#define REPROBE_PERIOD_US 60000000u

/* The autorange window of M02 spec 6.1: hold the brightest mapped channel
 * between 10 % and 90 % of full scale. */
#define AUTORANGE_LOW  0.10f
#define AUTORANGE_HIGH 0.90f

/* Device indices, in the order the health bitmask reports them. */
#define DEV_U4 0u /* AS7343 -- the board's purpose */
#define DEV_U3 1u /* TSL2585 -- the UV trace */
#define DEV_N  2u

#define FAIL_DEGRADED 3u

/* Vendor ExecuteCommand IDs. Both serve residuals this board actually has and
 * cannot resolve from a datasheet: the autorange has never met a luminaire,
 * and the UV responsivity is a typical with no bounds (spec 6.4, V4). */
#define CMD_SPECTRUM_STATE 1u
#define CMD_UV_RAW         2u

/* The PPFD reconstruction coefficients of M02 spec 6.2, over the ten in-window
 * bands F1 (405 nm) .. F7 (690 nm). Zero is the uncommissioned state and the
 * only honest default: the coefficients belong to one luminaire spectrum and
 * one optical stack identified against a reference quantum sensor, and the
 * project owns no such instrument (O-52). They arrive over the register below,
 * from the deployment profile -- never compiled in (spec 10). */
#define REG_PPFD_COEFF "industryflow.greenhouse.light.ppfd_coeff"
static float s_ppfd_coeff[AS7343_PAR_BANDS];

static bool s_u4, s_u3, s_switch;
static bool s_seen[DEV_N];
static uint8_t s_fail[DEV_N];

static uint8_t tid_spectrum, tid_ppfd, tid_flicker, tid_uv;
static uint64_t s_last_pub, s_last_probe;

/* The working point in force, moved by the autorange one step per publication
 * (spec 6.1). Seeded at the expected full-output point rather than at either
 * extreme: a node that comes up mid-photoperiod is then already in range. */
static uint8_t s_atime = AS7343_ATIME_NORMAL;
static uint8_t s_again = 7u; /* 64x */

static uint8_t s_last_health = uavcan_node_Health_1_0_NOMINAL;

/* The last spectral set, kept for the bench command. */
static as7343_sample_t s_last_spectrum;
static bool s_have_spectrum;

static uint64_t now_ts(void) { return micros64(); }

static void note_read(uint8_t dev, bool ok)
{
    if (ok) {
        s_fail[dev] = 0u;
    } else if (s_fail[dev] < 255u) {
        s_fail[dev]++;
    }
}

static bool coefficients_loaded(void)
{
    for (unsigned i = 0; i < AS7343_PAR_BANDS; i++) {
        if (s_ppfd_coeff[i] != 0.0f) {
            return true;
        }
    }
    return false;
}

/* U4 carries the board's purpose -- E0003 exists to measure the photic
 * environment at the canopy -- so losing it is CAUTION. U3 measures the
 * profile's UV-A trace, a secondary duty: ADVISORY.
 *
 * Losing U1 is CAUTION and not its own severity: the switch failing takes both
 * sensors with it, and the node then presents as a single absent device at
 * 0x70 rather than as two absent sensors (spec 10). The bitmask is what says
 * which of the three it was. */
static void report_health(void)
{
    const bool fault[DEV_N] = {
        (s_u4 && (s_fail[DEV_U4] >= FAIL_DEGRADED)) || (s_seen[DEV_U4] && !s_u4),
        (s_u3 && (s_fail[DEV_U3] >= FAIL_DEGRADED)) || (s_seen[DEV_U3] && !s_u3),
    };
    uint8_t health = uavcan_node_Health_1_0_NOMINAL;
    if (fault[DEV_U3]) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (fault[DEV_U4] || !s_switch) {
        health = uavcan_node_Health_1_0_CAUTION;
    }
    cyphal_set_health(health);

    if (health != s_last_health) {
        s_last_health = health;
        if (health == uavcan_node_Health_1_0_NOMINAL) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "M02 sensors recovered");
        } else {
            cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_WARNING,
                                  "M02 devices lost or faulted, 1=bad, bitmask U1|U4|U3 =",
                                  (uint32_t)((s_switch ? 0u : 1u) |
                                             (fault[DEV_U4] ? 2u : 0u) |
                                             (fault[DEV_U3] ? 4u : 0u)));
        }
    }
}

/* Probe order is the topology's: the switch on the master segment first, then
 * 0x39 on each channel in turn. Probing 0x39 with no channel selected would
 * find nothing on a perfectly good board, so an absent switch short-circuits
 * to "both sensors absent" rather than to two independent absences (spec 10,
 * O-65). Each probe is backed by a device-specific read; an ACK at 0x39 is not
 * identification, and at this address it could be either sensor reached
 * through the wrong channel. */
static void probe(void)
{
    s_switch = tca9543a_present();
    if (!s_switch) {
        s_u4 = false;
        s_u3 = false;
        return;
    }

    const bool had_u4 = s_u4;
    s_u4 = (tca9543a_select(TCA9543A_CH_U4) == 0) && as7343_present();
    if (s_u4 && !had_u4) {
        if (as7343_init(s_atime, s_again) < 0) { /* configure on (re)appearance */
            s_u4 = false;
        }
    }

    const bool had_u3 = s_u3;
    s_u3 = (tca9543a_select(TCA9543A_CH_U3) == 0) && tsl2585_present();
    if (s_u3 && !had_u3) {
        if (tsl2585_init() < 0) {
            s_u3 = false;
        }
    }

    s_seen[DEV_U4] = s_seen[DEV_U4] || s_u4;
    s_seen[DEV_U3] = s_seen[DEV_U3] || s_u3;
}

static uint8_t m02_command(uint16_t command, const uint8_t *param, size_t param_len)
{
    (void)param; /* no M02 command carries a value: the one deployment constant
                  * this board has is a coefficient SET, and a set belongs in a
                  * register, not in a command parameter (spec 10) */
    (void)param_len;

    switch (command) {
    case CMD_SPECTRUM_STATE: {
        /* What the autorange settled on, and how close to the rails it left
         * the brightest band. This is the number V2 logs across a 24 h cycle,
         * and it is not derivable from the published counts alone without
         * repeating the full-scale arithmetic. */
        if (!s_have_spectrum) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_STATE;
        }
        uint16_t peak = s_last_spectrum.clear;
        for (unsigned i = 0; i < AS7343_BANDS; i++) {
            if (s_last_spectrum.band[i] > peak) {
                peak = s_last_spectrum.band[i];
            }
        }
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M02 spectral gain x1 =", (uint32_t)s_last_spectrum.gain);
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M02 integration us =",
                              (uint32_t)(s_last_spectrum.integration_time * 1.0e6f));
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M02 brightest permille of full scale =",
                              (uint32_t)(((uint32_t)peak * 1000u) /
                                         (uint32_t)s_last_spectrum.full_scale));
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    }
    case CMD_UV_RAW: {
        /* Raw counts and the gain behind them. V4 measures the UV channel
         * against dark and against each fixture channel driven alone, and the
         * published W/m2 has a typical-only responsivity folded into it -- the
         * count is the figure that comparison needs. */
        if (!s_u3) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_STATE;
        }
        tsl2585_uv_t uv = {0};
        if ((tca9543a_select(TCA9543A_CH_U3) != 0) || (tsl2585_read_uv(&uv) != 0)) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_FAILURE;
        }
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M02 UV raw counts =", (uint32_t)uv.raw);
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M02 UV gain x1 =", (uint32_t)uv.gain);
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    }
    default:
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
}

/* What this personality publishes, for uavcan.node.port.List. */
static const uint16_t M02_SUBJECTS[] = {
    SUBJ_SPECTRUM, SUBJ_PPFD, SUBJ_FLICKER, SUBJ_UV_A,
};

void m02_sensors_init(void)
{
    cyphal_declare_publishers(M02_SUBJECTS,
                              (uint8_t)(sizeof(M02_SUBJECTS) / sizeof(M02_SUBJECTS[0])));
    cyphal_set_command_handler(m02_command);
    if (!registers_add_real32(REG_PPFD_COEFF, s_ppfd_coeff, (uint8_t)AS7343_PAR_BANDS)) {
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_WARNING,
                          "M02 PPFD coefficient register not available");
    }

    i2c_init();

    /* Spec 10's power-on delay. The three parts are ready at 200 us (U4),
     * 500 us (U3) and V_PORR (U1) after their rail crosses threshold, and the
     * boot probe may not talk before the longest of them. In practice the
     * clock, CAN self-test and ATECC probe have already taken far longer, so
     * this is a floor rather than a wait -- and a floor is what survives those
     * steps getting faster. */
    delay_ms(1u);

    probe();
    s_last_pub = now_ts();
    s_last_probe = s_last_pub;

    uart_puts("M02: switch ");
    uart_puts(s_switch ? "present" : "ABSENT");
    uart_puts(", U4 AS7343 ");
    uart_puts(s_u4 ? "present" : "absent");
    uart_puts(", U3 TSL2585 ");
    uart_puts(s_u3 ? "present\r\n" : "absent\r\n");

    cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M02 up, 1=present, bitmask U1|U4|U3 =",
                          (uint32_t)((s_switch ? 1u : 0u) | (s_u4 ? 2u : 0u) |
                                     (s_u3 ? 4u : 0u)));
    if (!coefficients_loaded()) {
        /* Said once, on the bus, because it is the difference between a node
         * that is broken and one that has simply never been commissioned. */
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M02 PPFD uncommissioned: no coefficients, subject 4129 invalid");
    }
}

static void pub_spectrum(const as7343_sample_t *s)
{
    industryflow_greenhouse_light_SpectralSample_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    for (unsigned i = 0; i < AS7343_BANDS; i++) {
        m.band[i] = s->band[i];
    }
    m.clear = s->clear;
    m.gain = s->gain;
    m.integration_seconds = s->integration_time;
    m.saturation = s->saturated;
    uint8_t b[industryflow_greenhouse_light_SpectralSample_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_light_SpectralSample_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_SPECTRUM, &tid_spectrum, b, sz);
    }
}

/* PPFD = sum over F1..F7 of c_i * n_i, with n_i the band count normalized by
 * the gain and integration time it was taken at (spec 6.2). F8 and NIR sit
 * outside the 400-700 nm window and are excluded by construction: the sum runs
 * to AS7343_PAR_BANDS, not to AS7343_BANDS. */
static void pub_ppfd(const as7343_sample_t *s, bool have_sample)
{
    industryflow_greenhouse_light_PhotonFluxDensity_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.valid = have_sample && coefficients_loaded();
    if (m.valid) {
        const float denom = s->gain * s->integration_time;
        float sum = 0.0f;
        for (unsigned i = 0; i < AS7343_PAR_BANDS; i++) {
            sum += s_ppfd_coeff[i] * ((float)s->band[i] / denom);
        }
        m.mol_per_square_metre_per_second = sum;
    }
    uint8_t b[industryflow_greenhouse_light_PhotonFluxDensity_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_light_PhotonFluxDensity_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_PPFD, &tid_ppfd, b, sz);
    }
}

static void pub_flicker(const as7343_flicker_t *f)
{
    industryflow_greenhouse_light_FlickerStatus_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.valid = f->valid;
    m.saturation = f->saturated;
    m.valid_100hz = f->valid_100hz;
    m.valid_120hz = f->valid_120hz;
    m.detected_100hz = f->detected_100hz;
    m.detected_120hz = f->detected_120hz;
    uint8_t b[industryflow_greenhouse_light_FlickerStatus_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_light_FlickerStatus_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_FLICKER, &tid_flicker, b, sz);
    }
}

static void pub_uv(const tsl2585_uv_t *uv)
{
    industryflow_greenhouse_light_Irradiance_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.watt_per_square_metre = uv->watt_per_square_metre;
    m.valid = uv->valid;
    uint8_t b[industryflow_greenhouse_light_Irradiance_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_light_Irradiance_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_UV_A, &tid_uv, b, sz);
    }
}

/* One autorange step per publication, on the brightest mapped channel (spec
 * 6.1). Gain is the fast handle and moves first; integration time is the last
 * resort at the bottom of the 30 min ramp, where 2048x is already in force and
 * a longer integration is the only thing left that raises signal and full
 * scale together.
 *
 * One step, not a search: the device free-runs, so a set taken during a change
 * would carry a mixture of two settings, and stepping once per second reaches
 * either rail of the thirteen-step ladder well inside a 30 min ramp.
 *
 * The dark period cannot satisfy the 10 % floor at any setting, and this does
 * not try: it walks to maximum sensitivity, stays there, and publishes the
 * counts with the setting that produced them. */
static void autorange(const as7343_sample_t *s)
{
    uint16_t peak = s->clear;
    for (unsigned i = 0; i < AS7343_BANDS; i++) {
        if (s->band[i] > peak) {
            peak = s->band[i];
        }
    }
    const float frac = (float)peak / (float)s->full_scale;

    uint8_t atime = s_atime;
    uint8_t again = s_again;
    if (s->saturated || (frac > AUTORANGE_HIGH)) {
        if (atime != AS7343_ATIME_NORMAL) {
            atime = AS7343_ATIME_NORMAL;
        } else if (again > AS7343_AGAIN_MIN) {
            again--;
        }
    } else if (frac < AUTORANGE_LOW) {
        if (again < AS7343_AGAIN_MAX) {
            again++;
        } else if (atime == AS7343_ATIME_NORMAL) {
            atime = AS7343_ATIME_LONG;
        }
    }

    if ((atime != s_atime) || (again != s_again)) {
        if (as7343_set_range(atime, again) == 0) {
            s_atime = atime;
            s_again = again;
        }
    }
}

static void publish_all(void)
{
    as7343_sample_t spectrum = {0};
    bool have_spectrum = false;

    if (s_u4 && (tca9543a_select(TCA9543A_CH_U4) == 0)) {
        if (as7343_read(&spectrum) == 0) {
            note_read(DEV_U4, true);
            have_spectrum = true;
            s_last_spectrum = spectrum;
            s_have_spectrum = true;
            pub_spectrum(&spectrum);
            /* The flicker ADC rides the same acquisition, so it is read in the
             * same channel selection rather than costing a second one. */
            as7343_flicker_t flicker = {0};
            if (as7343_read_flicker(&flicker) == 0) {
                pub_flicker(&flicker);
            }
        } else {
            note_read(DEV_U4, false);
        }
    }

    /* Published whenever U4 is fitted, valid or not: an uncommissioned node
     * that falls silent on 4129 is indistinguishable from an absent module,
     * and telling those two apart is what the flag is for. */
    if (s_u4) {
        pub_ppfd(&spectrum, have_spectrum);
    }

    if (s_u3 && (tca9543a_select(TCA9543A_CH_U3) == 0)) {
        tsl2585_uv_t uv = {0};
        if (tsl2585_read_uv(&uv) == 0) {
            note_read(DEV_U3, true);
            pub_uv(&uv);
        } else {
            note_read(DEV_U3, false);
        }
    }

    /* Last, so the setting a set was taken at is the one already published
     * with it. */
    if (have_spectrum) {
        autorange(&spectrum);
    }
}

void m02_sensors_spin(void)
{
    const uint64_t now = now_ts();
    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        publish_all();
        report_health();
    }
    if ((now - s_last_probe) >= REPROBE_PERIOD_US) {
        s_last_probe += REPROBE_PERIOD_US;
        probe();
    }
}
