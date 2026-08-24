/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sensors.h"

#include "clock.h"
#include "cyphal.h"
#include "uart.h"
#include "i2c.h"
#include "sht4x.h"
#include "bme68x.h"
#include "scd4x.h"

#include <math.h>

/* Standard SI sample types where one fits, project climate types where none
 * does (ADR-0005 d2/d3). Temperature is kelvin and VPD is pascal because the
 * wire is SI base units and °C/kPa are a gateway display concern. */
#include "uavcan/node/Heartbeat_1_0.h" /* Health constants */
#include "uavcan/node/ExecuteCommand_1_0.h" /* command response status */
#include "uavcan/diagnostic/Severity_1_0.h"
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/pressure/Scalar_1_0.h"
#include "industryflow/greenhouse/climate/RelativeHumidity_1_0.h"
#include "industryflow/greenhouse/climate/Co2Concentration_1_0.h"
#include "industryflow/greenhouse/climate/GasResistance_2_0.h"

/* Default subject-IDs, unregulated range. ADR-0005 d7 wants these register-
 * configurable (uavcan.pub.<name>.id) with these as defaults; baked for now,
 * as on M05. M05 holds 4096..4102, so M01 starts at 4112 and leaves that block
 * room to grow rather than butting against it.
 *
 * Ten subjects, not one record: each sensor's absence has to be expressible on
 * its own (ADR-0005 d8, alternative D). The three T/RH pairs are separate
 * subjects for the same reason plus one more -- only U1's pair is admissible
 * for VPD (M01 spec 4), and a shared subject would lose that distinction. */
#define SUBJ_AIR_TEMPERATURE 4112u /* U1 SHT45 -- primary */
#define SUBJ_AIR_HUMIDITY    4113u /* U1 SHT45 -- primary */
#define SUBJ_AIR_VPD         4114u /* derived on-node from U1 alone */
#define SUBJ_CO2             4115u /* U3 SCD41 */
#define SUBJ_BAROMETRIC      4116u /* U2 BME688 */
#define SUBJ_GAS_RESISTANCE  4117u /* U2 BME688 -- VOC trend */
#define SUBJ_U2_TEMPERATURE  4118u /* U2 secondary */
#define SUBJ_U2_HUMIDITY     4119u /* U2 secondary */
#define SUBJ_U3_TEMPERATURE  4120u /* U3 secondary -- offset uncalibrated, O-45 */
#define SUBJ_U3_HUMIDITY     4121u /* U3 secondary -- offset uncalibrated, O-45 */

/* U2's gas channel runs, but not on the publish tick. The hotplate reaches 320 C
 * for 150 ms per scan, and at 1 Hz that is a 15 % duty cycle of a heater sitting
 * next to U1 -- the board's primary T/RH reference, whose stability is T2
 * (≤ 0.1 K, unverified: V1). Nothing needs that rate: a VOC baseline moves over
 * hours, and Bosch's own BSEC samples gas at 3 s in its low-power mode and 300 s
 * in ultra-low-power. At 10 s the duty is 1.5 %, a tenth of the thermal
 * disturbance, and no trend information is lost.
 *
 * Pressure and the secondary T/RH are NOT on this period. They come from a
 * conversion every publish tick with the gas step disabled -- U2's load-bearing
 * role is the barometer that compensates U3 (spec 6.3), and that wants 1 Hz.
 *
 * M01_GAS_SCAN = 0 parks the hotplate entirely and withholds subject 4117. */
#ifndef M01_GAS_SCAN
#define M01_GAS_SCAN 1
#endif
#ifndef M01_GAS_PERIOD_S
#define M01_GAS_PERIOD_S 10u
#endif
#define GAS_PERIOD_US (M01_GAS_PERIOD_S * 1000000u)

#if M01_GAS_SCAN
/* The sweep. One resistance at one temperature is a scalar whose absolute value
 * means nothing and whose baseline moves; R(T) across setpoints is a shape, and
 * analytes separate by WHERE in temperature they respond. That discrimination is
 * the whole reason the part is on the board (spec 6.4).
 *
 * Four points, 150 ms each, is 600 ms of hotplate per scan -- 6 % duty at the
 * 10 s interval, against 15 % when a single point ran on every 1 s tick. Ten is
 * the device maximum and would put the duty back where it started, next to U1
 * (T2, V1). Ascending order is the wire contract of GasResistance.2.0. */
static const uint16_t GAS_SETPOINTS[] = {200u, 250u, 320u, 400u};
#define GAS_STEPS ((uint8_t)(sizeof(GAS_SETPOINTS) / sizeof(GAS_SETPOINTS[0])))
#endif

#define PUBLISH_PERIOD_US 1000000u
#define REPROBE_PERIOD_US 60000000u /* ADR-0014 d8 */

/* O-48: the SCD41 needs an ambient pressure and the barometer that supplies it
 * is U2, which a partial population may omit (spec 3.3). Sea-level standard is
 * the fallback. It is not free -- pressure error propagates into reported CO2
 * at ~1:1 -- but a cabinet near sea level deviates by a few percent at worst,
 * which is inside U3's own +/-50 ppm floor at ambient concentrations. A
 * deployment at altitude must populate U2 or provision this value. */
#define FALLBACK_PRESSURE_HPA 1013u

/* Sanity band for a pressure written to U3, from U2's own specified range
 * (spec 3). A compensation register is not the place to forward a reading that
 * the barometer itself would not claim. */
#define PRESSURE_MIN_HPA 300u
#define PRESSURE_MAX_HPA 1100u

/* Heater reference before U1 has ever been read -- the BME688 datasheet's own
 * reference ambient (spec 6.4 heater profile). */
#define DEFAULT_AMBIENT_C 25.0f

static bool s_u1, s_u2, s_u3;

static uint8_t tid_t1, tid_h1, tid_vpd, tid_co2, tid_baro;
#if M01_GAS_SCAN
static uint8_t tid_gas;
#endif
static uint8_t tid_t2, tid_h2, tid_t3, tid_h3;

static uint64_t s_last_pub, s_last_probe;

/* U2's conversion is ~190 ms of heater dwell and oversampling with the hotplate
 * lit, ~20 ms without it. It is triggered on the publish tick and collected on a
 * later pass of the loop rather than waited out in place: the Cyphal TX queue
 * has to keep flushing, and the watchdog window is not generous enough to spend
 * a fifth of every second inside one driver call. Each subject carries its own
 * timestamp, so U2's samples simply land later in the second than U1's. */
static bool s_u2_pending;
static uint64_t s_u2_due;

/* The setpoint of the conversion now in flight (0 = no hotplate), and when a
 * sweep last started. A sweep runs its steps back to back rather than one per
 * publish tick, so the whole R(T) shape belongs to one moment of air. */
static uint16_t s_u2_setpoint;
static uint64_t s_last_gas;
#if M01_GAS_SCAN
static bool s_sweep_active;
static uint8_t s_sweep_step;
static bool s_sweep_valid;
static float s_sweep_ohm[GAS_STEPS];
#endif

/* Consecutive failed read cycles per device, U1/U2/U3. A device that probed
 * present and then stops answering is a fault; a device that was never fitted
 * is not (spec 3.3). The probe cannot tell those apart on its own -- that is
 * O-37 -- but the transition can: only a device already flagged present is
 * counted here. */
static uint8_t s_fail[3];

#define FAIL_DEGRADED 3u /* consecutive cycles before the node calls it a fault */

/* Vendor ExecuteCommand IDs. The vendor range starts at zero; the standard
 * commands live at the top and are the skeleton's. Both of these are bench
 * operations that spec 10 requires to be commanded rather than automatic. */
#define CMD_U3_SELF_TEST 1u
#define CMD_U1_HEATER    2u

/* The self-test takes 10 s -- several watchdog windows -- so it runs as a state
 * machine across the loop rather than inside the command handler. */
enum { ST_IDLE = 0, ST_STOPPING, ST_RUNNING, ST_RESTORING };
static uint8_t s_selftest;
static uint64_t s_selftest_due;
static bool s_heater_pending;
static uint8_t s_last_health = uavcan_node_Health_1_0_NOMINAL;

static float s_ambient_c = DEFAULT_AMBIENT_C;
static uint16_t s_pressure_hpa = FALLBACK_PRESSURE_HPA;

static uint64_t now_ts(void) { return micros64(); }

static void note_read(uint8_t dev, bool ok)
{
    if (ok) {
        s_fail[dev] = 0u;
    } else if (s_fail[dev] < 255u) {
        s_fail[dev]++;
    }
}

/* What the node reports about itself. U1 is the primary: without it there is no
 * VPD and no admissible T/RH, so its loss is CAUTION. U2 and U3 losing their
 * reads costs subjects, not the module's purpose, so that is ADVISORY.
 *
 * The case this exists for is a device that answers its address and then never
 * yields a reading. Before this the node published nothing and still reported
 * NOMINAL, which is the one state a consumer cannot act on. */
static void report_health(void)
{
    uint8_t health = uavcan_node_Health_1_0_NOMINAL;
    if (s_u2 && (s_fail[1] >= FAIL_DEGRADED)) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (s_u3 && (s_fail[2] >= FAIL_DEGRADED)) {
        health = uavcan_node_Health_1_0_ADVISORY;
    }
    if (s_u1 && (s_fail[0] >= FAIL_DEGRADED)) {
        health = uavcan_node_Health_1_0_CAUTION;
    }
    cyphal_set_health(health);

    /* A transition is an event; the level itself is already in every heartbeat.
     * Publishing the change is what tells a consumer WHICH device stopped. */
    if (health != s_last_health) {
        s_last_health = health;
        if (health == uavcan_node_Health_1_0_NOMINAL) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_NOTICE, "M01 sensors recovered");
        } else {
            const uint32_t which = (uint32_t)((s_fail[0] >= FAIL_DEGRADED ? 1u : 0u) |
                                              (s_fail[1] >= FAIL_DEGRADED ? 2u : 0u) |
                                              (s_fail[2] >= FAIL_DEGRADED ? 4u : 0u));
            cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_WARNING,
                                  "M01 reads failing, bitmask U1|U2|U3 =", which);
        }
    }
}

static uint8_t m01_command(uint16_t command)
{
    switch (command) {
    case CMD_U3_SELF_TEST:
        if (!s_u3 || (s_selftest != ST_IDLE)) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_STATE;
        }
        if (scd4x_stop() < 0) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_FAILURE;
        }
        s_selftest = ST_STOPPING;
        s_selftest_due = now_ts() + ((uint64_t)SCD4X_STOP_MS * 1000u);
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    case CMD_U1_HEATER:
        if (!s_u1) {
            return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_STATE;
        }
        s_heater_pending = true; /* fired after the next U3 sample, see spin */
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_SUCCESS;
    default:
        return uavcan_node_ExecuteCommand_Response_1_0_STATUS_BAD_COMMAND;
    }
}

static void service_selftest(void)
{
    if ((s_selftest == ST_IDLE) || (now_ts() < s_selftest_due)) {
        return;
    }
    if (s_selftest == ST_STOPPING) {
        if (scd4x_self_test_begin() < 0) {
            s_selftest = ST_IDLE;
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR, "U3 self-test did not start");
            return;
        }
        s_selftest = ST_RUNNING;
        s_selftest_due = now_ts() + ((uint64_t)SCD4X_SELF_TEST_MS * 1000u);
        return;
    }
    if (s_selftest == ST_RUNNING) {
        bool ok = false;
        if (scd4x_self_test_result(&ok) < 0) {
            cyphal_diagnostic(uavcan_diagnostic_Severity_1_0_ERROR, "U3 self-test unreadable");
        } else {
            cyphal_diagnostic(ok ? uavcan_diagnostic_Severity_1_0_NOTICE
                                 : uavcan_diagnostic_Severity_1_0_ERROR,
                              ok ? "U3 self-test: no malfunction"
                                 : "U3 self-test: MALFUNCTION");
        }
        s_selftest = ST_RESTORING;
        s_selftest_due = now_ts();
        return;
    }
    /* Back to work. allow_persist is false: this is not a boot, and the EEPROM
     * budget is not spent on a diagnostic. */
    if (scd4x_configure(s_pressure_hpa, false) < 0) {
        s_u3 = false;
    }
    s_selftest = ST_IDLE;
}

/* --- Publication helpers ------------------------------------------------ */

static void pub_temperature(uint16_t subject, uint8_t *tid, float kelvin)
{
    uavcan_si_sample_temperature_Scalar_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.kelvin = kelvin;
    uint8_t b[uavcan_si_sample_temperature_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_temperature_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(subject, tid, b, sz);
    }
}

static void pub_pressure(uint16_t subject, uint8_t *tid, float pascal)
{
    uavcan_si_sample_pressure_Scalar_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.pascal = pascal;
    uint8_t b[uavcan_si_sample_pressure_Scalar_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (uavcan_si_sample_pressure_Scalar_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(subject, tid, b, sz);
    }
}

static void pub_humidity(uint16_t subject, uint8_t *tid, float ratio)
{
    industryflow_greenhouse_climate_RelativeHumidity_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.ratio = ratio;
    uint8_t b[industryflow_greenhouse_climate_RelativeHumidity_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_climate_RelativeHumidity_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(subject, tid, b, sz);
    }
}

static void pub_co2(float mole_fraction)
{
    industryflow_greenhouse_climate_Co2Concentration_1_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.mole_fraction = mole_fraction;
    uint8_t b[industryflow_greenhouse_climate_Co2Concentration_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_climate_Co2Concentration_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_CO2, &tid_co2, b, sz);
    }
}

#if M01_GAS_SCAN
/* The completed sweep, setpoints and resistances index-for-index. `valid` is the
 * AND over the steps: a partial shape is not comparable against a whole one, so
 * a single unstable step invalidates the reading rather than half of it. */
static void pub_gas_sweep(void)
{
    industryflow_greenhouse_climate_GasResistance_2_0 m = {0};
    m.timestamp.microsecond = cyphal_timestamp_usec();
    m.heater_celsius.count = GAS_STEPS;
    m.ohm.count = GAS_STEPS;
    for (uint8_t i = 0; i < GAS_STEPS; i++) {
        m.heater_celsius.elements[i] = GAS_SETPOINTS[i];
        m.ohm.elements[i] = s_sweep_ohm[i];
    }
    m.valid = s_sweep_valid;
    uint8_t b[industryflow_greenhouse_climate_GasResistance_2_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_climate_GasResistance_2_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_GAS_RESISTANCE, &tid_gas, b, sz);
    }
}
#endif

/* --- Derived quantity --------------------------------------------------- */

/* Air VPD, M01 spec 6.1, from U1 alone. Leaf VPD is M04's and is not published
 * here (spec 3.2). Returned in pascal for the standard pressure sample type;
 * the profile band 0.8..1.2 kPa is 800..1200 Pa. */
static float vpd_pascal(float celsius, float rh_ratio)
{
    float es_kpa = 0.61078f * expf((17.27f * celsius) / (celsius + 237.3f));
    return es_kpa * (1.0f - rh_ratio) * 1000.0f;
}

/* --- Presence probing (ADR-0014 d8) ------------------------------------- */

/* A part that stops responding is dropped, and one that comes back is
 * re-initialized -- neither the BME688's configuration nor the SCD41's
 * measurement mode survives the power cycle that a re-appearance implies.
 *
 * The probe cannot distinguish "not fitted" from "failed" from "unpowered"
 * (spec O-37): U2 and U3 sit behind their own regulators, so a dead U4 or U5
 * looks exactly like an unpopulated footprint. All three are absence here. */
static void probe(bool boot)
{
    bool u1 = i2c_probe(SHT4X_ADDR);
    bool u2 = i2c_probe(BME68X_ADDR);
    bool u3 = i2c_probe(SCD4X_ADDR);

    if (u2 && !s_u2) {
        if (bme68x_init() < 0) {
            u2 = false; /* ACKs but will not configure: not usable */
        }
    }
    if (u3 && !s_u3) {
        if (scd4x_configure(s_pressure_hpa, boot) < 0) {
            u3 = false;
        }
    }

    s_u1 = u1;
    s_u2 = u2;
    s_u3 = u3;

    if (!s_u2) {
        s_u2_pending = false; /* nothing will arrive; do not wait for it */
    }
}

/* --- Measurement cycle -------------------------------------------------- */

static void publish_primary(void)
{
    if (!s_u1) {
        return;
    }
    float celsius = 0.0f, rh = 0.0f;
    if (sht4x_read(&celsius, &rh) < 0) {
        note_read(0, false);
        return;
    }
    note_read(0, true);

    /* U1 is also the best ambient reference on the board for U2's heater
     * calculation -- it is the part furthest from every heat source (spec T3). */
    s_ambient_c = celsius;

    pub_temperature(SUBJ_AIR_TEMPERATURE, &tid_t1, celsius + 273.15f);
    pub_humidity(SUBJ_AIR_HUMIDITY, &tid_h1, rh);
    pub_pressure(SUBJ_AIR_VPD, &tid_vpd, vpd_pascal(celsius, rh));
}

static void service_co2(void)
{
    if (!s_u3 || (s_selftest != ST_IDLE)) {
        return; /* the device is stopped and under test; nothing else may address it */
    }
    /* Polled, not free-running: periodic mode has one interval, 5 s, and no
     * other exists (spec 3). Four polls in five find nothing new. */
    bool ready = false;
    if (scd4x_data_ready(&ready) < 0) {
        note_read(2, false);
        return;
    }
    if (!ready) {
        return; /* nothing new at this poll is normal, not a failure */
    }

    float co2 = 0.0f, celsius = 0.0f, rh = 0.0f;
    if (scd4x_read_measurement(&co2, &celsius, &rh) < 0) {
        note_read(2, false);
        return;
    }
    note_read(2, true);
    pub_co2(co2);
    pub_temperature(SUBJ_U3_TEMPERATURE, &tid_t3, celsius + 273.15f);
    pub_humidity(SUBJ_U3_HUMIDITY, &tid_h3, rh);
}

static void compensate_co2_pressure(float pascal)
{
    uint16_t hpa = (uint16_t)((pascal / 100.0f) + 0.5f);
    if ((hpa < PRESSURE_MIN_HPA) || (hpa > PRESSURE_MAX_HPA)) {
        return; /* not a pressure U2 would claim; leave the last good value */
    }
    if (hpa == s_pressure_hpa) {
        return; /* the register is already right; keep the segment quiet */
    }
    if (!s_u3) {
        s_pressure_hpa = hpa; /* remember it for whenever U3 appears */
        return;
    }
    if (scd4x_set_ambient_pressure_hpa(hpa) == 0) {
        s_pressure_hpa = hpa;
    }
}

static void start_u2_cycle(void)
{
    if (!s_u2 || s_u2_pending) {
        return;
    }
    const uint64_t now = now_ts();
    uint16_t setpoint = 0u;
#if M01_GAS_SCAN
    /* A sweep already in flight continues; otherwise the interval arms one. */
    if (!s_sweep_active && ((now - s_last_gas) >= GAS_PERIOD_US)) {
        s_sweep_active = true;
        s_sweep_step = 0u;
        s_sweep_valid = true;
        s_last_gas = now;
    }
    if (s_sweep_active) {
        setpoint = GAS_SETPOINTS[s_sweep_step];
    }
#endif
    if (bme68x_trigger(s_ambient_c, setpoint) < 0) {
        note_read(1, false);
#if M01_GAS_SCAN
        s_sweep_active = false; /* an incomplete shape is not published */
#endif
        return;
    }
    s_u2_setpoint = setpoint;
    s_u2_pending = true;
    s_u2_due = now + ((uint64_t)bme68x_meas_duration_ms(setpoint) * 1000u);
}

static void finish_u2_cycle(void)
{
    bme68x_data_t d = {0};
    if (bme68x_read(&d) < 0) {
        note_read(1, false);
#if M01_GAS_SCAN
        s_sweep_active = false;
#endif
        return;
    }
    note_read(1, true);

    /* Every conversion yields pressure and the secondary T/RH, but a sweep's
     * later steps land inside the same second as its first: publishing each of
     * them would put the same air on the wire four times. Only the step that
     * opens a sweep speaks for it. */
#if M01_GAS_SCAN
    const bool sweep_head = s_sweep_active && (s_sweep_step == 0u);
#else
    const bool sweep_head = false;
#endif
    if ((s_u2_setpoint == 0u) || sweep_head) {
        pub_pressure(SUBJ_BAROMETRIC, &tid_baro, d.pressure_pa);
        pub_temperature(SUBJ_U2_TEMPERATURE, &tid_t2, d.celsius + 273.15f);
        pub_humidity(SUBJ_U2_HUMIDITY, &tid_h2, d.rh_ratio);

        /* The barometer's real job (spec 6.3): U3's compensation register. */
        compensate_co2_pressure(d.pressure_pa);
    }

#if M01_GAS_SCAN
    if (s_sweep_active) {
        s_sweep_ohm[s_sweep_step] = d.gas_ohm;
        if (!d.gas_valid) {
            s_sweep_valid = false;
        }
        s_sweep_step++;
        if (s_sweep_step >= GAS_STEPS) {
            s_sweep_active = false;
            pub_gas_sweep();
        }
    }
#endif
}

/* --- Boot --------------------------------------------------------------- */

/* Print a signed value in thousandths, e.g. 4000 -> "4.000". The console has no
 * float formatter and is not getting one for three boot lines. */
static void put_milli(int32_t milli)
{
    if (milli < 0) {
        uart_putc('-');
        milli = -milli;
    }
    uart_put_u32((uint32_t)(milli / 1000));
    uart_putc('.');
    uint32_t frac = (uint32_t)(milli % 1000);
    uart_putc((char)('0' + ((frac / 100u) % 10u)));
    uart_putc((char)('0' + ((frac / 10u) % 10u)));
    uart_putc((char)('0' + (frac % 10u)));
}

static void log_population(void)
{
    /* Unlike M05, M01 claims no header GPIO (spec 5), so USART1 stays the debug
     * console through boot and run -- this log survives. */
    uart_puts("M01 sensor probe:\r\n");

    uart_puts("  U1 SHT45  0x44: ");
    if (s_u1) {
        /* An ACK at 0x44 is not proof of an SHT45: M05's INA226 answers the
         * same address, so running this image on an E0006 makes the part look
         * present. The serial read is the discriminator -- a foreign chip fails
         * the CRC, and every later reading fails it too, so nothing garbled is
         * ever published. The strap self-check catches the same error earlier. */
        uint32_t sn = 0;
        if (sht4x_serial(&sn) == 0) {
            uart_puts("present, serial ");
            uart_put_u32(sn);
        } else {
            uart_puts("ACK but no valid answer -> NOT an SHT45");
        }
        uart_puts("\r\n");
    } else {
        uart_puts("absent -> no T/RH, no VPD\r\n");
    }

    uart_puts("  U2 BME68x 0x76: ");
    if (s_u2) {
        uart_puts(bme68x_is_bme688() ? "present, variant BME688"
                                     : "present, variant BME680 (alternative)");
        /* Which of U2's two roles is running, and at what thermal cost to U1
         * (spec 6.4; T2, V1). */
#if M01_GAS_SCAN
        uart_puts(", gas sweep ");
        uart_put_u32(GAS_STEPS);
        uart_puts(" setpoints every ");
        uart_put_u32(M01_GAS_PERIOD_S);
        uart_puts(" s\r\n");
#else
        uart_puts(", gas scan PARKED (hotplate off, 4117 not published)\r\n");
#endif
    } else {
        uart_puts("absent -> CO2 pressure falls back to 1013 hPa (O-48)\r\n");
    }

    uart_puts("  U3 SCD41  0x62: ");
    if (s_u3) {
        uint64_t sn = 0;
        uart_puts("present");
        if (scd4x_serial(&sn) == 0) {
            uart_puts(", serial ");
            uart_put_u32((uint32_t)(sn >> 32));
            uart_putc(':');
            uart_put_u32((uint32_t)sn);
        }
        float offset = 0.0f;
        if (scd4x_get_temperature_offset(&offset) == 0) {
            /* Records what is actually in the device. 4.000 is the factory
             * default, and it is NOT this board's value -- until V7 measures
             * one, U3's T and RH carry an uncalibrated bias (spec O-45). */
            uart_puts(", T offset ");
            put_milli((int32_t)(offset * 1000.0f));
            uart_puts(" C (uncalibrated, O-45)");
        }
        /* ASC is disabled on this module by design (spec 6.3), which makes FRC
         * mandatory -- and FRC has no entry point yet (spec 6.3.1, O-5), so
         * "off" is the whole of U3's calibration state today. Printed every
         * boot because "was on" on a board this firmware has already configured
         * is the one symptom of a persist that is not sticking. */
        bool asc_on = false, asc_persisted = false;
        if (scd4x_asc_status(&asc_on, &asc_persisted) == 0) {
            if (!asc_on) {
                uart_puts(", ASC already off (no EEPROM write)");
            } else if (asc_persisted) {
                uart_puts(", ASC was ON -> off, persisted (one EEPROM cycle)");
            } else {
                uart_puts(", ASC was ON -> off in RAM only, NOT persisted");
            }
        }
        uart_puts("\r\n");
    } else {
        uart_puts("absent -> no CO2\r\n");
    }

    /* What the bus cost to get here. Zero failures is the healthy answer; a
     * non-zero count with every device reported present is the signature of a
     * bus that answers addresses and completes nothing (spec O-37). */
    uint32_t att = 0u, fail = 0u;
    i2c_stats(&att, &fail);
    uart_puts("  I2C: ");
    uart_put_u32(att);
    uart_puts(" transactions, ");
    uart_put_u32(fail);
    uart_puts(" failed\r\n");
}

/* What this personality publishes, for uavcan.node.port.List. 4117 is listed
 * only in a build that scans: a port list that advertises a subject the node
 * never sends is worse than no list at all. */
static const uint16_t M01_SUBJECTS[] = {
    SUBJ_AIR_TEMPERATURE, SUBJ_AIR_HUMIDITY, SUBJ_AIR_VPD, SUBJ_CO2,
    SUBJ_BAROMETRIC,
#if M01_GAS_SCAN
    SUBJ_GAS_RESISTANCE,
#endif
    SUBJ_U2_TEMPERATURE, SUBJ_U2_HUMIDITY, SUBJ_U3_TEMPERATURE, SUBJ_U3_HUMIDITY,
};

void m01_sensors_init(void)
{
    cyphal_declare_publishers(M01_SUBJECTS,
                              (uint8_t)(sizeof(M01_SUBJECTS) / sizeof(M01_SUBJECTS[0])));
    cyphal_set_command_handler(m01_command);
    i2c_init();

    /* Boot probe. persist_settings is permitted only here: the watchdog is not
     * running yet, so the 800 ms EEPROM write cannot trip it. */
    probe(true);
    log_population();

    s_last_pub = now_ts();
    s_last_probe = s_last_pub;

    /* The population, on the bus rather than only on a console nobody reads. */
    cyphal_diagnostic_u32(uavcan_diagnostic_Severity_1_0_NOTICE,
                          "M01 up, present bitmask U1|U2|U3 =",
                          (uint32_t)((s_u1 ? 1u : 0u) | (s_u2 ? 2u : 0u) | (s_u3 ? 4u : 0u)));
}

void m01_sensors_spin(void)
{
    uint64_t now = now_ts();

    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        publish_primary();
        service_co2();
        /* Spec 10: a heater pulse shall not overlap a U3 measurement window.
         * Immediately after a poll is the furthest point from the next one. */
        if (s_heater_pending && s_u1 && (s_selftest == ST_IDLE)) {
            s_heater_pending = false;
            cyphal_diagnostic(sht4x_heater_pulse() == 0
                                  ? uavcan_diagnostic_Severity_1_0_NOTICE
                                  : uavcan_diagnostic_Severity_1_0_ERROR,
                              "U1 condensate-recovery pulse, 20 mW 0.1 s");
        }
        start_u2_cycle();
        report_health();
    }

    service_selftest();

    if (s_u2_pending && (now >= s_u2_due)) {
        s_u2_pending = false;
        finish_u2_cycle();
#if M01_GAS_SCAN
        /* The remaining steps of a sweep do not wait for the next publish tick:
         * four points spread over four seconds would not describe one moment of
         * air. Back to back the whole shape takes ~760 ms. */
        if (s_sweep_active) {
            start_u2_cycle();
        }
#endif
    }

    if ((now - s_last_probe) >= REPROBE_PERIOD_US) {
        s_last_probe += REPROBE_PERIOD_US;
        probe(false);
    }
}
