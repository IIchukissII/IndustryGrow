/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensors.h"

#include "e0001.h"
#include "clock.h"
#include "cyphal.h"
#include "i2c.h"
#include "ina226.h"
#include "tmp117.h"
#include "s0.h"
#include "leak.h"

/* Standard SI sample types + the project safety/energy types (ADR-0005). */
#include "uavcan/si/sample/voltage/Scalar_1_0.h"
#include "uavcan/si/sample/electric_current/Scalar_1_0.h"
#include "uavcan/si/sample/power/Scalar_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/energy/Scalar_1_0.h"
#include "industryflow/greenhouse/safety/DoorStatus_1_0.h"
#include "industryflow/greenhouse/safety/LeakStatus_1_0.h"

/* Default subject-IDs (unregulated range). ADR-0005 d7: these should be
 * register-configurable (uavcan.pub.<name>.id) with these as defaults; baked
 * for now, register entries to follow. */
#define SUBJ_BUS_VOLTAGE  4096u
#define SUBJ_BUS_CURRENT  4097u
#define SUBJ_BUS_POWER    4098u
#define SUBJ_CABINET_TEMP 4099u
#define SUBJ_DOOR         4100u
#define SUBJ_LEAK         4101u
#define SUBJ_ENERGY       4102u

#define REED_PIN 15u /* GPIO_3 = PA15 (E0006: door reed) */

#define PUBLISH_PERIOD_US 1000000u
#define REPROBE_PERIOD_US 60000000u

static bool s_ina226;
static bool s_tmp117;

static uint8_t tid_v, tid_i, tid_p, tid_t, tid_door, tid_leak, tid_energy;
static uint64_t s_last_pub, s_last_probe;

static uint64_t now_ts(void) { return micros64(); }

static void probe(void)
{
    bool ina = i2c_probe(INA226_ADDR);
    if (ina && !s_ina226) {
        (void)ina226_init(); /* configure on (re)appearance */
    }
    s_ina226 = ina;
    s_tmp117 = i2c_probe(TMP117_ADDR);
}

void sensors_init(void)
{
    i2c_init();
    s0_init();
    leak_init();

    /* Reed (PA15) input, pull-up; GPIOA clock already on from e0001_init(). */
    GPIOA->MODER &= ~(3u << (REED_PIN * 2u));
    GPIOA->PUPDR &= ~(3u << (REED_PIN * 2u));
    GPIOA->PUPDR |= (1u << (REED_PIN * 2u));

    probe();
    s_last_pub = now_ts();
    s_last_probe = s_last_pub;
}

static void pub_voltage(float v)
{
    uavcan_si_sample_voltage_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.volt = v;
    uint8_t b[uavcan_si_sample_voltage_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_voltage_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_BUS_VOLTAGE, &tid_v, b, sz);
    }
}

static void pub_current(float a)
{
    uavcan_si_sample_electric_current_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.ampere = a;
    uint8_t b[uavcan_si_sample_electric_current_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_electric_current_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_BUS_CURRENT, &tid_i, b, sz);
    }
}

static void pub_power(float w)
{
    uavcan_si_sample_power_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.watt = w;
    uint8_t b[uavcan_si_sample_power_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_power_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_BUS_POWER, &tid_p, b, sz);
    }
}

static void pub_temperature(float kelvin)
{
    uavcan_si_sample_temperature_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.kelvin = kelvin;
    uint8_t b[uavcan_si_sample_temperature_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_temperature_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_CABINET_TEMP, &tid_t, b, sz);
    }
}

static void pub_door(void)
{
    /* NO reed to GND with pull-up: engaged (closed) reads low. Polarity TBD
     * against the E0006 wiring. */
    bool engaged = (GPIOA->IDR & (1u << REED_PIN)) == 0u;
    industryflow_greenhouse_safety_DoorStatus_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.engaged = engaged;
    m.valid = true;
    uint8_t b[industryflow_greenhouse_safety_DoorStatus_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_safety_DoorStatus_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_DOOR, &tid_door, b, sz);
    }
}

static void pub_leak(void)
{
    industryflow_greenhouse_safety_LeakStatus_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.wet = leak_is_wet();
    m.valid = true; /* TODO: false until gated excitation is actually driven */
    uint8_t b[industryflow_greenhouse_safety_LeakStatus_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_safety_LeakStatus_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_LEAK, &tid_leak, b, sz);
    }
}

static void pub_energy(void)
{
    uavcan_si_sample_energy_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.joule = s0_energy_joule();
    uint8_t b[uavcan_si_sample_energy_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_energy_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_ENERGY, &tid_energy, b, sz);
    }
}

static void publish_all(void)
{
    if (s_ina226) {
        float v = 0.0f, a = 0.0f, w = 0.0f;
        if (ina226_read(&v, &a, &w) == 0) {
            pub_voltage(v);
            pub_current(a);
            pub_power(w);
        }
    }
    if (s_tmp117) {
        float k = 0.0f;
        if (tmp117_read_kelvin(&k) == 0) {
            pub_temperature(k);
        }
    }
    /* Always present (GPIO/ADC, no I2C probe). */
    pub_door();
    pub_leak();
    pub_energy();
}

void sensors_spin(void)
{
    uint64_t now = now_ts();
    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        publish_all();
    }
    if ((now - s_last_probe) >= REPROBE_PERIOD_US) {
        s_last_probe += REPROBE_PERIOD_US;
        probe();
    }
}
