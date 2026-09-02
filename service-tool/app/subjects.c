/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "subjects.h"

#include <stdio.h>
#include <string.h>

#include "uavcan/si/sample/electric_current/Scalar_1_0.h"
#include "uavcan/si/sample/energy/Scalar_1_0.h"
#include "uavcan/si/sample/power/Scalar_1_0.h"
#include "uavcan/si/sample/pressure/Scalar_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/voltage/Scalar_1_0.h"

#include "industryflow/greenhouse/climate/Co2Concentration_1_0.h"
#include "industryflow/greenhouse/climate/GasResistance_2_0.h"
#include "industryflow/greenhouse/climate/RelativeHumidity_1_0.h"
#include "industryflow/greenhouse/light/FlickerStatus_1_0.h"
#include "industryflow/greenhouse/light/Irradiance_1_0.h"
#include "industryflow/greenhouse/light/PhotonFluxDensity_1_0.h"
#include "industryflow/greenhouse/light/SpectralSample_1_0.h"
#include "industryflow/greenhouse/safety/DoorStatus_1_0.h"
#include "industryflow/greenhouse/safety/LeakStatus_1_0.h"

/* A scalar reading is complete once value, validity and stamp are set; the
 * text is what a tile shows without re-formatting on every repaint. */
static void set_scalar(igrow_reading_t *o, float v, uint64_t ts, bool valid, int decimals)
{
    o->have_value     = true;
    o->value          = v;
    o->valid          = valid;
    o->timestamp_usec = ts;
    (void)snprintf(o->text, sizeof o->text, "%.*f", decimals, (double)v);
}

static void set_state(igrow_reading_t *o, bool on, uint64_t ts, bool valid, const char *label)
{
    o->have_value     = true;
    o->value          = on ? 1.0f : 0.0f;
    o->valid          = valid;
    o->timestamp_usec = ts;
    (void)snprintf(o->text, sizeof o->text, "%s", label);
}

/* --- uavcan.si.sample.* scalars ------------------------------------------ */

#define DECODE_SI(fn, type, field, expr, dec)                                             \
    static bool fn(const uint8_t *p, size_t n, igrow_reading_t *o)                         \
    {                                                                                      \
        type m;                                                                            \
        size_t sz = n;                                                                     \
        if (type##_deserialize_(&m, p, &sz) < 0) {                                         \
            return false;                                                                  \
        }                                                                                  \
        const float field = m.field;                                                       \
        set_scalar(o, (expr), m.timestamp.microsecond, true, dec);                          \
        return true;                                                                       \
    }

DECODE_SI(dec_voltage, uavcan_si_sample_voltage_Scalar_1_0, volt, volt, 3)
DECODE_SI(dec_current, uavcan_si_sample_electric_current_Scalar_1_0, ampere, ampere, 4)
DECODE_SI(dec_power, uavcan_si_sample_power_Scalar_1_0, watt, watt, 3)
DECODE_SI(dec_energy, uavcan_si_sample_energy_Scalar_1_0, joule, joule, 0)
DECODE_SI(dec_pressure, uavcan_si_sample_pressure_Scalar_1_0, pascal, pascal, 1)
/* Kelvin on the wire (ADR-0005); degrees Celsius on the glass. */
DECODE_SI(dec_temperature, uavcan_si_sample_temperature_Scalar_1_0, kelvin, kelvin - 273.15f, 2)

/* --- industryflow.greenhouse.climate ------------------------------------- */

static bool dec_humidity(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_climate_RelativeHumidity_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_climate_RelativeHumidity_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* 0..1 on the wire, percent on the glass. */
    set_scalar(o, m.ratio * 100.0f, m.timestamp.microsecond, true, 2);
    return true;
}

static bool dec_co2(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_climate_Co2Concentration_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_climate_Co2Concentration_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* Mole fraction on the wire, ppm on the glass. */
    set_scalar(o, m.mole_fraction * 1.0e6f, m.timestamp.microsecond, true, 0);
    return true;
}

static bool dec_gas_sweep(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_climate_GasResistance_2_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_climate_GasResistance_2_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* An R(T) sweep is not a scalar. Reading one as a scalar reads garbage, so
     * this reports the sweep and leaves have_value false -- nothing plots it
     * on the time axis. */
    o->timestamp_usec = m.timestamp.microsecond;
    o->valid          = m.valid;
    o->n_points       = (uint8_t)(m.ohm.count < 10U ? m.ohm.count : 10U);
    for (uint8_t i = 0; i < o->n_points; i++) {
        o->sweep_ohm[i] = m.ohm.elements[i];
        o->sweep_celsius[i] =
            (i < m.heater_celsius.count) ? m.heater_celsius.elements[i] : (uint16_t)0U;
    }
    (void)snprintf(o->text, sizeof o->text, "%u pts %s", (unsigned)o->n_points,
                   m.valid ? "ok" : "INVALID");
    return true;
}

/* --- industryflow.greenhouse.safety -------------------------------------- */

static bool dec_door(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_safety_DoorStatus_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_safety_DoorStatus_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* engaged == closed. Plot 1 for the safe state so a dip reads as an event. */
    set_state(o, m.engaged, m.timestamp.microsecond, m.valid, m.engaged ? "CLOSED" : "OPEN");
    return true;
}

static bool dec_leak(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_safety_LeakStatus_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_safety_LeakStatus_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* wet is the alarm, so plot 1 for dry -- same polarity as the door. */
    set_state(o, !m.wet, m.timestamp.microsecond, m.valid, m.wet ? "WET" : "DRY");
    return true;
}

/* --- industryflow.greenhouse.light --------------------------------------- */

static bool dec_ppfd(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_light_PhotonFluxDensity_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_light_PhotonFluxDensity_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    /* mol/(m^2 s) on the wire, umol/(m^2 s) on the glass -- the horticultural unit. */
    set_scalar(o, m.mol_per_square_metre_per_second * 1.0e6f, m.timestamp.microsecond, m.valid, 1);
    return true;
}

static bool dec_irradiance(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_light_Irradiance_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_light_Irradiance_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    set_scalar(o, m.watt_per_square_metre, m.timestamp.microsecond, m.valid, 4);
    return true;
}

static bool dec_flicker(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_light_FlickerStatus_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_light_FlickerStatus_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    const bool any = m.detected_100hz || m.detected_120hz;
    set_state(o, !any, m.timestamp.microsecond, m.valid && !m.saturation,
              m.detected_100hz ? "100 Hz" : (m.detected_120hz ? "120 Hz" : "none"));
    return true;
}

static bool dec_spectrum(const uint8_t *p, size_t n, igrow_reading_t *o)
{
    industryflow_greenhouse_light_SpectralSample_1_0 m;
    size_t sz = n;
    if (industryflow_greenhouse_light_SpectralSample_1_0_deserialize_(&m, p, &sz) < 0) {
        return false;
    }
    o->timestamp_usec = m.timestamp.microsecond;
    o->valid          = !m.saturation;
    o->saturation     = m.saturation;
    o->clear          = m.clear;
    for (unsigned i = 0; i < IGROW_SPECTRUM_BANDS; i++) {
        o->band[i] = m.band[i];
    }
    (void)snprintf(o->text, sizeof o->text, "clear %u%s", (unsigned)m.clear,
                   m.saturation ? " SAT" : "");
    return true;
}

/* --- the table ------------------------------------------------------------
 * Subject-IDs per gateway/files/app/igrow_subjects.py. 4132 and 4133 were UV-B
 * and UV-C and stay RETIRED (M02 spec 10.1) -- nothing may bind them here. */

const igrow_subject_t IGROW_SUBJECTS[] = {
    /* M05-SAFETY (E0006) */
    {4096, "bus voltage", "V", IGROW_SIG_SCALAR, dec_voltage},
    {4097, "bus current", "A", IGROW_SIG_SCALAR, dec_current},
    {4098, "bus power", "W", IGROW_SIG_SCALAR, dec_power},
    {4099, "cabinet temp", "degC", IGROW_SIG_SCALAR, dec_temperature},
    {4100, "door", "", IGROW_SIG_STATE, dec_door},
    {4101, "leak", "", IGROW_SIG_STATE, dec_leak},
    {4102, "bus energy", "J", IGROW_SIG_SCALAR, dec_energy},
    /* M01-CLIMATE (E0002) */
    {4112, "air temp", "degC", IGROW_SIG_SCALAR, dec_temperature},
    {4113, "air humidity", "%", IGROW_SIG_SCALAR, dec_humidity},
    {4114, "VPD", "Pa", IGROW_SIG_SCALAR, dec_pressure},
    {4115, "CO2", "ppm", IGROW_SIG_SCALAR, dec_co2},
    {4116, "pressure", "Pa", IGROW_SIG_SCALAR, dec_pressure},
    {4117, "gas sweep", "ohm", IGROW_SIG_SWEEP, dec_gas_sweep},
    {4118, "U2 temp", "degC", IGROW_SIG_SCALAR, dec_temperature},
    {4119, "U2 humidity", "%", IGROW_SIG_SCALAR, dec_humidity},
    {4120, "U3 temp", "degC", IGROW_SIG_SCALAR, dec_temperature},
    {4121, "U3 humidity", "%", IGROW_SIG_SCALAR, dec_humidity},
    /* M02-LIGHT (E0003) */
    {4128, "spectrum", "counts", IGROW_SIG_SPECTRUM, dec_spectrum},
    {4129, "PPFD", "umol/m2/s", IGROW_SIG_SCALAR, dec_ppfd},
    {4130, "flicker", "", IGROW_SIG_STATE, dec_flicker},
    {4131, "UV-A", "W/m2", IGROW_SIG_SCALAR, dec_irradiance},
};

const size_t IGROW_SUBJECT_COUNT = sizeof IGROW_SUBJECTS / sizeof IGROW_SUBJECTS[0];

const igrow_subject_t *igrow_subject_by_id(uint16_t subject_id)
{
    for (size_t i = 0; i < IGROW_SUBJECT_COUNT; i++) {
        if (IGROW_SUBJECTS[i].subject_id == subject_id) {
            return &IGROW_SUBJECTS[i];
        }
    }
    return NULL;
}

const char *igrow_subject_module(uint16_t subject_id)
{
    if (subject_id >= 4096U && subject_id <= 4111U) {
        return "M05";
    }
    if (subject_id >= 4112U && subject_id <= 4127U) {
        return "M01";
    }
    if (subject_id >= 4128U && subject_id <= 4143U) {
        return "M02";
    }
    return "?";
}
