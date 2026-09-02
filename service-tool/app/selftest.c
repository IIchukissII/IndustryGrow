/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "selftest.h"

#include <math.h>
#include <string.h>

#include "can_port.h"
#include "console.h"
#include "cyphal_rx.h"
#include "model.h"
#include "stm32h7xx_hal.h"

#include "industryflow/greenhouse/climate/RelativeHumidity_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/voltage/Scalar_1_0.h"

static bool     s_active;
static uint32_t s_started_ms;
static uint32_t s_next_temp_ms;
static uint32_t s_next_volt_ms;
static uint32_t s_next_hum_ms;
static bool     s_hum_dropped;

static uint8_t s_tid_temp;
static uint8_t s_tid_volt;
static uint8_t s_tid_hum;

bool selftest_active(void)
{
    return s_active;
}

bool selftest_toggle(void)
{
    if (s_active) {
        s_active = false;
        /* Back to the state the panel boots in, and drop every synthetic
         * reading with it -- none of it may survive into a real session. */
        (void)can_init(can_current_profile(), true);
        model_init();
        console_printf("self-test off\r\n");
        return false;
    }

    /* A sweep in progress re-initialises the interface every few seconds and
     * would drop us straight back out of loopback. */
    can_hunt_abort();

    if (!can_init_mode(can_current_profile(), CAN_MODE_LOOPBACK)) {
        console_printf("self-test: could not enter loopback\r\n");
        return false;
    }
    model_init();

    const uint32_t now = HAL_GetTick();
    s_active        = true;
    s_started_ms    = now;
    s_next_temp_ms  = now;
    s_next_volt_ms  = now;
    s_next_hum_ms   = now;
    s_hum_dropped   = false;
    console_printf("self-test on: internal loopback, synthetic publishers\r\n");
    console_printf("  air temperature 1 Hz, bus voltage 2 Hz, air humidity 1 Hz for %lu s\r\n",
                   (unsigned long)(SELFTEST_DROP_AFTER_MS / 1000U));
    return true;
}

static void publish_temperature(uint32_t now)
{
    uavcan_si_sample_temperature_Scalar_1_0 m;
    uavcan_si_sample_temperature_Scalar_1_0_initialize_(&m);
    m.timestamp.microsecond = (uint64_t)now * 1000ULL;
    /* A slow swing so the plot has a shape and the min/max are not degenerate. */
    m.kelvin = 293.15f + 2.0f * sinf((float)now / 8000.0f);

    uint8_t buf[uavcan_si_sample_temperature_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  sz = sizeof buf;
    if (uavcan_si_sample_temperature_Scalar_1_0_serialize_(&m, buf, &sz) >= 0) {
        (void)cyphal_publish(4112, buf, sz, &s_tid_temp);
    }
}

static void publish_voltage(uint32_t now)
{
    uavcan_si_sample_voltage_Scalar_1_0 m;
    uavcan_si_sample_voltage_Scalar_1_0_initialize_(&m);
    m.timestamp.microsecond = (uint64_t)now * 1000ULL;
    m.volt                  = 12.0f + 0.15f * sinf((float)now / 3000.0f);

    uint8_t buf[uavcan_si_sample_voltage_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  sz = sizeof buf;
    if (uavcan_si_sample_voltage_Scalar_1_0_serialize_(&m, buf, &sz) >= 0) {
        (void)cyphal_publish(4096, buf, sz, &s_tid_volt);
    }
}

static void publish_humidity(uint32_t now)
{
    industryflow_greenhouse_climate_RelativeHumidity_1_0 m;
    industryflow_greenhouse_climate_RelativeHumidity_1_0_initialize_(&m);
    m.timestamp.microsecond = (uint64_t)now * 1000ULL;
    m.ratio                 = 0.55f + 0.03f * sinf((float)now / 5000.0f);

    uint8_t buf[industryflow_greenhouse_climate_RelativeHumidity_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t  sz = sizeof buf;
    if (industryflow_greenhouse_climate_RelativeHumidity_1_0_serialize_(&m, buf, &sz) >= 0) {
        (void)cyphal_publish(4113, buf, sz, &s_tid_hum);
    }
}

void selftest_spin(void)
{
    if (!s_active) {
        return;
    }
    const uint32_t now = HAL_GetTick();

    if ((int32_t)(now - s_next_temp_ms) >= 0) {
        s_next_temp_ms = now + 1000U;
        publish_temperature(now);
    }
    if ((int32_t)(now - s_next_volt_ms) >= 0) {
        s_next_volt_ms = now + 500U;
        publish_voltage(now);
    }

    if (!s_hum_dropped) {
        if ((now - s_started_ms) >= SELFTEST_DROP_AFTER_MS) {
            s_hum_dropped = true;
            console_printf("self-test: humidity publisher stopped - expect LATE then STALE\r\n");
        } else if ((int32_t)(now - s_next_hum_ms) >= 0) {
            s_next_hum_ms = now + 1000U;
            publish_humidity(now);
        }
    }
}
