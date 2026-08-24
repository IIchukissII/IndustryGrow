/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sht4x.h"
#include "sensirion.h"
#include "i2c.h"
#include "clock.h"

/* Command bytes (SHT4x datasheet). Only the lowest heater level is exposed --
 * see the header. */
#define SHT4X_CMD_MEASURE_HIGH 0xFDu
#define SHT4X_CMD_READ_SERIAL  0x89u
#define SHT4X_CMD_SOFT_RESET   0x94u
#define SHT4X_CMD_HEAT_20MW_01S 0x15u

/* Datasheet maxima are 8.3 ms for the high-repeatability conversion and 1 ms
 * for a soft reset; rounded up to whole SysTick milliseconds. */
#define SHT4X_MEASURE_MS 10u
#define SHT4X_RESET_MS   2u
/* 0.1 s of heating plus the conversion the device appends to it. */
#define SHT4X_HEATER_MS  115u

/* The SHT4x takes a bare command byte and answers on a SEPARATE transaction --
 * there is no register pointer and no repeated start. The bus must be released
 * while the conversion runs, which is also what lets the SCD41 be serviced in
 * the same second on the same segment. */
static int command_then_read(uint8_t cmd, uint32_t wait_ms, uint16_t *words, size_t count)
{
    uint8_t buf[6];
    if ((count * 3u) > sizeof(buf)) {
        return -1;
    }
    if (i2c_write(SHT4X_ADDR, &cmd, 1u) < 0) {
        return -2;
    }
    delay_ms(wait_ms);
    if (i2c_read(SHT4X_ADDR, buf, count * 3u) < 0) {
        return -3;
    }
    if (sensirion_unpack(buf, words, count) < 0) {
        return -4; /* CRC: treat as no reading, never as a plausible one */
    }
    return 0;
}

int sht4x_read(float *celsius, float *rh_ratio)
{
    uint16_t w[2];
    int rc = command_then_read(SHT4X_CMD_MEASURE_HIGH, SHT4X_MEASURE_MS, w, 2u);
    if (rc < 0) {
        return rc;
    }

    /* Datasheet transfer functions, both over the full 16-bit range. */
    float t = -45.0f + (175.0f * (float)w[0] / 65535.0f);
    float rh = -6.0f + (125.0f * (float)w[1] / 65535.0f); /* percent */

    /* The RH transfer function is defined outside 0..100 %RH so that the sensor
     * can report its own noise near the rails; the physical quantity is not.
     * Clamping here keeps a saturated reading from entering VPD as a negative
     * vapour-pressure deficit. */
    if (rh < 0.0f) {
        rh = 0.0f;
    } else if (rh > 100.0f) {
        rh = 100.0f;
    }

    if (celsius) {
        *celsius = t;
    }
    if (rh_ratio) {
        *rh_ratio = rh / 100.0f;
    }
    return 0;
}

int sht4x_serial(uint32_t *out)
{
    uint16_t w[2];
    int rc = command_then_read(SHT4X_CMD_READ_SERIAL, 1u, w, 2u);
    if (rc < 0) {
        return rc;
    }
    if (out) {
        *out = ((uint32_t)w[0] << 16) | w[1];
    }
    return 0;
}

int sht4x_soft_reset(void)
{
    const uint8_t cmd = SHT4X_CMD_SOFT_RESET;
    if (i2c_write(SHT4X_ADDR, &cmd, 1u) < 0) {
        return -1;
    }
    delay_ms(SHT4X_RESET_MS);
    return 0;
}

int sht4x_heater_pulse(void)
{
    uint16_t w[2];
    return command_then_read(SHT4X_CMD_HEAT_20MW_01S, SHT4X_HEATER_MS, w, 2u);
}
