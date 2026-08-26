/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensors.h"

#include "e0001.h"
#include "clock.h"
#include "cyphal.h"
#include "uavcan/node/Heartbeat_1_0.h"       /* Health constants */
#include "uavcan/node/ExecuteCommand_1_0.h"  /* command response status */
#include "uavcan/diagnostic/Severity_1_0.h"
#include "uart.h"
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

/* Consecutive failed read cycles, U2 INA226 then U1 TMP117. A device that
 * probed present and then stops answering is a fault; one that was never
 * fitted is not. */
static uint8_t s_fail[2];
#define FAIL_DEGRADED 3u

static uint8_t s_last_health = uavcan_node_Health_1_0_NOMINAL;
static bool s_last_door_engaged = true;
static bool s_last_leak_wet;
static bool s_states_seeded;

/* Vendor ExecuteCommand IDs. Both serve residuals this board actually has:
 * the accumulator is volatile, and LEAK_WET_THRESHOLD is provisional and
 * cannot be calibrated without seeing the raw count against real water. */
#define CMD_ENERGY_RESET 1u
#define CMD_LEAK_RAW     2u

static uint64_t now_ts(void) { return micros64(); }

static void note_read(uint8_t dev, bool ok)
{
    if (ok) {
        s_fail[dev] = 0u;
    } else if (s_fail[dev] < 255u) {
        s_fail[dev]++;
    }
}

/* U2 carries the board's purpose -- E0006 exists to meter the +12 V bus
 * (ADR-0018 d5) -- so losing it is CAUTION. U1 reports bay air for bay health
 * (ADR-0018 d11), which is a secondary duty: ADVISORY. */
static void report_health(void)
{
    uint8_t health = uavcan_node_Health_1_0_NOMINAL;
    if (s_tmp117 && (s_fail[1] >= FAIL_DEGRADED)) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (s_ina226 && (s_fail[0] >= FAIL_DEGRADED)) {
        health = uavcan_node_Health_1_0_CAUTION;
    }
    cyphal_set_health(health);

    if (health != s_last_health) {
        s_last_health = health;
        if (health == uavcan_node_Health_1_0_NOMINAL) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "M05 sensors recovered");
        } else {
            cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_WARNING,
                                  "M05 reads failing, bitmask U2|U1 =",
                                  (uint32_t)((s_fail[0] >= FAIL_DEGRADED ? 1u : 0u) |
                                             (s_fail[1] >= FAIL_DEGRADED ? 2u : 0u)));
        }
    }
}

static uint32_t leak_raw_sample(void)
{
    uint16_t raw = 0u;
    (void)leak_sample(NULL, &raw);
    return (uint32_t)raw;
}

static uint8_t m05_command(uint16_t command, const uint8_t *param, size_t param_len)
{
    (void)param;      /* neither M05 command carries a value */
    (void)param_len;

    switch (command) {
    case CMD_ENERGY_RESET:
        s0_reset();
        cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "M05 energy accumulator zeroed");
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    case CMD_LEAK_RAW:
        cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                              "M05 leak raw ADC =", leak_raw_sample());
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    default:
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
}

static void probe(void)
{
    bool ina = i2c_probe(INA226_ADDR);
    if (ina && !s_ina226) {
        (void)ina226_init(); /* configure on (re)appearance */
    }
    s_ina226 = ina;
    s_tmp117 = i2c_probe(TMP117_ADDR);
}

/* What this personality publishes, for uavcan.node.port.List. */
static const uint16_t M05_SUBJECTS[] = {
    SUBJ_BUS_VOLTAGE, SUBJ_BUS_CURRENT, SUBJ_BUS_POWER, SUBJ_CABINET_TEMP,
    SUBJ_DOOR, SUBJ_LEAK, SUBJ_ENERGY,
};

void m05_sensors_init(void)
{
    cyphal_declare_publishers(M05_SUBJECTS,
                              (uint8_t)(sizeof(M05_SUBJECTS) / sizeof(M05_SUBJECTS[0])));
    cyphal_set_command_handler(m05_command);
    /* Last console output on this personality: leak_init() claims PA9 (GPIO_1,
     * shared with USART1_TX) as the leak excitation drive, so the debug UART
     * goes silent from here. Everything the common boot path printed still
     * arrives; M01 keeps its console because E0002 claims no header GPIO. */
    uart_puts("debug console ends here (PA9 -> leak excitation)\r\n");

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

    /* The population, on the bus rather than only on a console that this
     * personality loses at sensors_init() anyway (PA9 is the leak excitation). */
    cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M05 up, present bitmask U2|U1 =",
                          (uint32_t)((s_ina226 ? 1u : 0u) | (s_tmp117 ? 2u : 0u)));
}

static void pub_voltage(float v)
{
    uavcan_si_sample_voltage_Scalar_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
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
    m.timestamp.microsecond = cyphal_timestamp_usec();
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
    m.timestamp.microsecond = cyphal_timestamp_usec();
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
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.kelvin = kelvin;
    uint8_t b[uavcan_si_sample_temperature_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_temperature_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_CABINET_TEMP, &tid_t, b, sz);
    }
}

static void pub_door(void)
{
    /* E0006 puts the reed on J5, switching PA15 to GND through R7 with C4 for
     * debounce and no external pull-up (the internal one above holds the pin
     * high). The NO reed closes when the door is shut, so engaged reads low --
     * confirmed on the bench against a fitted reed. */
    bool engaged = (GPIOA->IDR & (1u << REED_PIN)) == 0u;
    if (s_states_seeded && (engaged != s_last_door_engaged)) {
        cyphal_diagnostic(engaged ? uavcan_diagnostic_Severity_1_0_NOTICE
                                  : uavcan_diagnostic_Severity_1_0_WARNING,
                          engaged ? "M05 door shut" : "M05 door OPEN");
    }
    s_last_door_engaged = engaged;
    industryflow_greenhouse_safety_DoorStatus_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
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
    m.timestamp.microsecond = cyphal_timestamp_usec();
    bool wet = false;
    const bool leak_ok = leak_sample(&wet, NULL);
    m.wet = wet;
    if (s_states_seeded && (m.wet != s_last_leak_wet)) {
        cyphal_diagnostic(m.wet ? uavcan_diagnostic_Severity_1_0_WARNING
                                : uavcan_diagnostic_Severity_1_0_NOTICE,
                          m.wet ? "M05 leak WET" : "M05 leak dry");
    }
    s_last_leak_wet = m.wet;
    /* The excitation is gated and driven (leak.c); what `valid` reports is
     * whether the conversion completed. A timed-out ADC leaves a stale value in
     * the register, and calling that dry would be inventing a state. */
    m.valid = leak_ok;
    uint8_t b[industryflow_greenhouse_safety_LeakStatus_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_safety_LeakStatus_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_LEAK, &tid_leak, b, sz);
    }
}

static void pub_energy(void)
{
    uavcan_si_sample_energy_Scalar_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
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
            note_read(0, true);
            pub_voltage(v);
            pub_current(a);
            pub_power(w);
        } else {
            note_read(0, false);
        }
    }
    if (s_tmp117) {
        float k = 0.0f;
        if (tmp117_read_kelvin(&k) == 0) {
            note_read(1, true);
            pub_temperature(k);
        } else {
            note_read(1, false);
        }
    }
    /* Always present (GPIO/ADC, no I2C probe). */
    pub_door();
    pub_leak();
    pub_energy();
}

void m05_sensors_spin(void)
{
    uint64_t now = now_ts();
    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        publish_all();
        s_states_seeded = true; /* the first pass establishes door and leak, not a transition */
        report_health();
    }
    if ((now - s_last_probe) >= REPROBE_PERIOD_US) {
        s_last_probe += REPROBE_PERIOD_US;
        probe();
    }
}
