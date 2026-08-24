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
#include "uavcan/si/sample/temperature/Scalar_1_0.h"
#include "uavcan/si/sample/pressure/Scalar_1_0.h"
#include "industryflow/greenhouse/climate/RelativeHumidity_1_0.h"
#include "industryflow/greenhouse/climate/Co2Concentration_1_0.h"
#include "industryflow/greenhouse/climate/GasResistance_1_0.h"

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

static uint8_t tid_t1, tid_h1, tid_vpd, tid_co2, tid_baro, tid_gas;
static uint8_t tid_t2, tid_h2, tid_t3, tid_h3;

static uint64_t s_last_pub, s_last_probe;

/* U2's conversion is ~190 ms of heater dwell and oversampling. It is triggered
 * on the publish tick and collected on a later pass of the loop rather than
 * waited out in place: the Cyphal TX queue has to keep flushing, and the
 * watchdog window is not generous enough to spend a fifth of every second
 * inside one driver call. Each subject carries its own timestamp, so U2's
 * samples simply land later in the second than U1's. */
static bool s_gas_pending;
static uint64_t s_gas_due;

static float s_ambient_c = DEFAULT_AMBIENT_C;
static uint16_t s_pressure_hpa = FALLBACK_PRESSURE_HPA;

static uint64_t now_ts(void) { return micros64(); }

/* --- Publication helpers ------------------------------------------------ */

static void pub_temperature(uint16_t subject, uint8_t *tid, float kelvin)
{
    uavcan_si_sample_temperature_Scalar_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
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
    m.timestamp.microsecond = now_ts();
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
    m.timestamp.microsecond = now_ts();
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
    m.timestamp.microsecond = now_ts();
    m.mole_fraction = mole_fraction;
    uint8_t b[industryflow_greenhouse_climate_Co2Concentration_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_climate_Co2Concentration_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_CO2, &tid_co2, b, sz);
    }
}

static void pub_gas(float ohm, bool valid)
{
    industryflow_greenhouse_climate_GasResistance_1_0 m = {0};
    m.timestamp.microsecond = now_ts();
    m.ohm = ohm;
    m.heater_celsius = BME68X_HEATER_CELSIUS;
    m.valid = valid;
    uint8_t b[industryflow_greenhouse_climate_GasResistance_1_0_SERIALIZATION_BUFFER_SIZE_BYTES_];
    size_t sz = sizeof(b);
    if (industryflow_greenhouse_climate_GasResistance_1_0_serialize_(&m, b, &sz) >= 0) {
        cyphal_publish(SUBJ_GAS_RESISTANCE, &tid_gas, b, sz);
    }
}

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
        s_gas_pending = false; /* nothing will arrive; do not wait for it */
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
        return;
    }

    /* U1 is also the best ambient reference on the board for U2's heater
     * calculation -- it is the part furthest from every heat source (spec T3). */
    s_ambient_c = celsius;

    pub_temperature(SUBJ_AIR_TEMPERATURE, &tid_t1, celsius + 273.15f);
    pub_humidity(SUBJ_AIR_HUMIDITY, &tid_h1, rh);
    pub_pressure(SUBJ_AIR_VPD, &tid_vpd, vpd_pascal(celsius, rh));
}

static void service_co2(void)
{
    if (!s_u3) {
        return;
    }
    /* Polled, not free-running: periodic mode has one interval, 5 s, and no
     * other exists (spec 3). Four polls in five find nothing new. */
    bool ready = false;
    if ((scd4x_data_ready(&ready) < 0) || !ready) {
        return;
    }

    float co2 = 0.0f, celsius = 0.0f, rh = 0.0f;
    if (scd4x_read_measurement(&co2, &celsius, &rh) < 0) {
        return;
    }
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

static void start_gas_cycle(void)
{
    if (!s_u2 || s_gas_pending) {
        return;
    }
    if (bme68x_trigger(s_ambient_c) < 0) {
        return;
    }
    s_gas_pending = true;
    s_gas_due = now_ts() + ((uint64_t)bme68x_meas_duration_ms() * 1000u);
}

static void finish_gas_cycle(void)
{
    bme68x_data_t d = {0};
    if (bme68x_read(&d) < 0) {
        return;
    }
    pub_pressure(SUBJ_BAROMETRIC, &tid_baro, d.pressure_pa);
    pub_temperature(SUBJ_U2_TEMPERATURE, &tid_t2, d.celsius + 273.15f);
    pub_humidity(SUBJ_U2_HUMIDITY, &tid_h2, d.rh_ratio);
    pub_gas(d.gas_ohm, d.gas_valid);

    /* The barometer's real job (spec 6.3): U3's compensation register. */
    compensate_co2_pressure(d.pressure_pa);
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
        uart_puts(bme68x_is_bme688() ? "present, variant BME688\r\n"
                                     : "present, variant BME680 (alternative)\r\n");
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
}

void m01_sensors_init(void)
{
    i2c_init();

    /* Boot probe. persist_settings is permitted only here: the watchdog is not
     * running yet, so the 800 ms EEPROM write cannot trip it. */
    probe(true);
    log_population();

    s_last_pub = now_ts();
    s_last_probe = s_last_pub;
}

void m01_sensors_spin(void)
{
    uint64_t now = now_ts();

    if ((now - s_last_pub) >= PUBLISH_PERIOD_US) {
        s_last_pub += PUBLISH_PERIOD_US;
        publish_primary();
        service_co2();
        start_gas_cycle();
    }

    if (s_gas_pending && (now >= s_gas_due)) {
        s_gas_pending = false;
        finish_gas_cycle();
    }

    if ((now - s_last_probe) >= REPROBE_PERIOD_US) {
        s_last_probe += REPROBE_PERIOD_US;
        probe(false);
    }
}
