/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "scd4x.h"
#include "sensirion.h"
#include "i2c.h"
#include "clock.h"

/* Command words and their datasheet execution times. The SCD4x addresses a
 * command with a 16-bit word rather than a register pointer, and the execution
 * time is a hard wait: the device NACKs until it has finished. */
#define CMD_START_PERIODIC   0x21B1u
#define CMD_READ_MEASUREMENT 0xEC05u
#define CMD_STOP_PERIODIC    0x3F86u
#define CMD_GET_TEMP_OFFSET  0x2318u
#define CMD_SET_AMB_PRESSURE 0xE000u
#define CMD_SET_ASC          0x2416u
#define CMD_GET_ASC          0x2313u
#define CMD_PERSIST_SETTINGS 0x3615u
#define CMD_GET_DATA_READY   0xE4B8u
#define CMD_GET_SERIAL       0x3682u
#define CMD_SELF_TEST        0x3639u

#define MS_STOP_PERIODIC 500u
#define MS_PERSIST       800u
#define MS_SHORT         2u /* datasheet 1 ms, rounded to whole SysTick ticks */

/* Data-ready is a 16-bit word whose low 11 bits are the flag; the upper 5 are
 * not part of the answer and must be masked before the zero test. */
#define DATA_READY_MASK 0x07FFu

static int send_command(uint16_t cmd)
{
    const uint8_t b[2] = {(uint8_t)(cmd >> 8), (uint8_t)cmd};
    return (i2c_write(SCD4X_ADDR, b, 2u) < 0) ? -1 : 0;
}

static int send_command_arg(uint16_t cmd, uint16_t arg)
{
    uint8_t b[5];
    b[0] = (uint8_t)(cmd >> 8);
    b[1] = (uint8_t)cmd;
    b[2] = (uint8_t)(arg >> 8);
    b[3] = (uint8_t)arg;
    b[4] = sensirion_crc8(&b[2], 2u); /* CRC covers the argument word only */
    return (i2c_write(SCD4X_ADDR, b, 5u) < 0) ? -1 : 0;
}

/* Command, wait out the execution time, then read `count` CRC-checked words.
 * The wait happens with the bus released -- the device NACKs while busy, and
 * holding the segment through a 500 ms stop would starve U1 and U2. */
static int read_words(uint16_t cmd, uint32_t wait_ms, uint16_t *words, size_t count)
{
    uint8_t buf[9];
    if ((count * 3u) > sizeof(buf)) {
        return -1;
    }
    if (send_command(cmd) < 0) {
        return -2;
    }
    delay_ms(wait_ms);
    if (i2c_read(SCD4X_ADDR, buf, count * 3u) < 0) {
        return -3;
    }
    return (sensirion_unpack(buf, words, count) < 0) ? -4 : 0;
}

/* What the stopped device reported, held for the accessors below. Both are
 * properties of the part, not samples: the serial is fixed for its lifetime and
 * the offset changes only when something writes it, which this driver
 * deliberately cannot do (see the header). */
static uint64_t s_serial;
static float s_temp_offset_c;
static bool s_serial_valid;
static bool s_offset_valid;

/* What the ASC branch below actually did. A configuration that disables ASC and
 * persists it spends one of the EEPROM's 2000 cycles, and a persist that does
 * not stick spends one on every boot for the rest of the board's life -- so
 * which of the two happened is not a detail the log can leave out. */
static bool s_asc_valid;
static bool s_asc_found_on;
static bool s_asc_persisted;

int scd4x_configure(uint16_t ambient_hpa, bool allow_persist)
{
    /* A device that has just re-appeared on a probe need not be the one that
     * answered last time. Nothing carries over until it has answered again. */
    s_serial_valid = false;
    s_offset_valid = false;
    s_asc_valid = false;
    s_asc_persisted = false;

    /* Almost every configuration command is rejected while periodic
     * measurement runs, and the device may already be measuring after a warm
     * reset that left it alone. Stop first, unconditionally. */
    if (send_command(CMD_STOP_PERIODIC) < 0) {
        return -1;
    }
    delay_ms(MS_STOP_PERIODIC);

    /* Identity and offset are read HERE, in the only window this driver ever
     * has for them: the device is stopped, and every path out of this function
     * either leaves it stopped or starts it measuring. The boot log used to
     * read them afterwards, and a measuring SCD41 rejects both -- which cost
     * far more than the two log fields it lost, because the NACK wedged the
     * whole I2C segment and silenced all ten of M01's subjects (bench
     * 2026-08-24; see clear_stale_flags(), common/drivers/i2c.c).
     *
     * Neither read is allowed to fail the configuration: they inform the log,
     * they do not condition the measurement. */
    uint16_t w[3] = {0};
    if (read_words(CMD_GET_SERIAL, MS_SHORT, w, 3u) == 0) {
        s_serial = ((uint64_t)w[0] << 32) | ((uint64_t)w[1] << 16) | w[2];
        s_serial_valid = true;
    }
    if (read_words(CMD_GET_TEMP_OFFSET, MS_SHORT, w, 1u) == 0) {
        s_temp_offset_c = 175.0f * (float)w[0] / 65535.0f;
        s_offset_valid = true;
    }

    /* Read ASC before writing it. This is the whole reason the EEPROM survives:
     * the write and the persist happen only on a device that is not already in
     * the wanted state (spec 10). */
    uint16_t asc = 0;
    if (read_words(CMD_GET_ASC, MS_SHORT, &asc, 1u) < 0) {
        return -2;
    }
    s_asc_found_on = (asc != 0u);
    s_asc_valid = true;
    if (asc != 0u) {
        if (send_command_arg(CMD_SET_ASC, 0u) < 0) {
            return -3;
        }
        delay_ms(MS_SHORT);
        if (allow_persist) {
            if (send_command(CMD_PERSIST_SETTINGS) < 0) {
                return -4;
            }
            delay_ms(MS_PERSIST);
            s_asc_persisted = true;
        }
    }

    /* Seed the compensation so the first samples are not taken against the
     * device default; U2 refreshes it every cycle from here on. */
    if (send_command_arg(CMD_SET_AMB_PRESSURE, ambient_hpa) < 0) {
        return -5;
    }
    delay_ms(MS_SHORT);

    if (send_command(CMD_START_PERIODIC) < 0) {
        return -6;
    }
    return 0;
}

int scd4x_data_ready(bool *ready)
{
    uint16_t w = 0;
    if (read_words(CMD_GET_DATA_READY, MS_SHORT, &w, 1u) < 0) {
        return -1;
    }
    if (ready) {
        *ready = (w & DATA_READY_MASK) != 0u;
    }
    return 0;
}

int scd4x_read_measurement(float *co2_mole_fraction, float *celsius, float *rh_ratio)
{
    uint16_t w[3];
    if (read_words(CMD_READ_MEASUREMENT, MS_SHORT, w, 3u) < 0) {
        return -1;
    }

    /* w[0] is ppm as an integer. Depletion below the 400 ppm specified band is
     * reported as measured and NOT clamped: in a closed cabinet during
     * photoperiod it is the real condition (spec 6.3). */
    if (co2_mole_fraction) {
        *co2_mole_fraction = (float)w[0] * 1e-6f;
    }
    if (celsius) {
        *celsius = -45.0f + (175.0f * (float)w[1] / 65535.0f);
    }
    if (rh_ratio) {
        float rh = (float)w[2] / 65535.0f;
        if (rh > 1.0f) {
            rh = 1.0f;
        }
        *rh_ratio = rh;
    }
    return 0;
}

int scd4x_set_ambient_pressure_hpa(uint16_t hpa)
{
    if (send_command_arg(CMD_SET_AMB_PRESSURE, hpa) < 0) {
        return -1;
    }
    delay_ms(MS_SHORT);
    return 0;
}

int scd4x_get_temperature_offset(float *celsius)
{
    if (!s_offset_valid) {
        return -1;
    }
    if (celsius) {
        *celsius = s_temp_offset_c;
    }
    return 0;
}

int scd4x_serial(uint64_t *out)
{
    if (!s_serial_valid) {
        return -1;
    }
    if (out) {
        *out = s_serial;
    }
    return 0;
}

int scd4x_asc_status(bool *found_on, bool *persisted)
{
    if (!s_asc_valid) {
        return -1;
    }
    if (found_on) {
        *found_on = s_asc_found_on;
    }
    if (persisted) {
        *persisted = s_asc_persisted;
    }
    return 0;
}

int scd4x_stop(void)
{
    return send_command(CMD_STOP_PERIODIC);
}

int scd4x_self_test_begin(void)
{
    return send_command(CMD_SELF_TEST);
}

int scd4x_self_test_result(bool *malfunction_free)
{
    uint8_t buf[3];
    if (i2c_read(SCD4X_ADDR, buf, sizeof(buf)) < 0) {
        return -1;
    }
    uint16_t w = 0;
    if (sensirion_unpack(buf, &w, 1u) < 0) {
        return -2;
    }
    if (malfunction_free) {
        *malfunction_free = (w == 0u); /* the device reports 0 for "no malfunction" */
    }
    return 0;
}
