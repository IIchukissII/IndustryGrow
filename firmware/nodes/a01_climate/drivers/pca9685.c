/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "pca9685.h"

#include "clock.h"
#include "i2c.h"

/* Register map, PCA9685 datasheet rev 4, 7.3. */
#define REG_MODE1     0x00u
#define REG_MODE2     0x01u
#define REG_LED0_ON_L 0x06u
#define REG_PRESCALE  0xFEu

#define MODE1_AI    0x20u /* register auto-increment */
#define MODE1_SLEEP 0x10u
#define MODE2_OUTDRV 0x04u /* totem pole */

#define LED_FULL 0x10u /* bit 4 of LEDn_ON_H / LEDn_OFF_H */

/* round(25 MHz / (4096 * f)) - 1, datasheet eq. 1. */
#define PRESCALE_VALUE 3u

static int write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t b[2] = {reg, value};
    return i2c_write(PCA9685_ADDR, b, sizeof(b));
}

int pca9685_init(void)
{
    /* PRE_SCALE is writable only while the oscillator sleeps. */
    if (write_reg(REG_MODE1, MODE1_AI | MODE1_SLEEP) != 0) {
        return -1;
    }
    if (write_reg(REG_PRESCALE, PRESCALE_VALUE) != 0) {
        return -1;
    }
    if (write_reg(REG_MODE2, MODE2_OUTDRV) != 0) {
        return -1;
    }
    if (write_reg(REG_MODE1, MODE1_AI) != 0) {
        return -1;
    }
    delay_ms(1u); /* oscillator start, 500 us max */
    return 0;
}

int pca9685_configured(void)
{
    const uint8_t reg = REG_MODE1;
    uint8_t mode1 = 0u;
    if (i2c_write_read(PCA9685_ADDR, &reg, 1u, &mode1, 1u) != 0) {
        return -1;
    }
    return ((mode1 & (MODE1_AI | MODE1_SLEEP)) == MODE1_AI) ? 1 : 0;
}

int pca9685_set(uint8_t channel, float duty)
{
    if (channel > 15u) {
        return -1;
    }
    uint8_t b[5] = {(uint8_t)(REG_LED0_ON_L + 4u * channel), 0u, 0u, 0u, 0u};
    if (!(duty > 0.0f)) {
        b[4] = LED_FULL; /* full off; also catches NaN */
    } else if (duty >= 1.0f) {
        b[2] = LED_FULL; /* full on */
    } else {
        uint32_t off = (uint32_t)(duty * 4096.0f + 0.5f);
        if (off == 0u) {
            b[4] = LED_FULL;
        } else {
            if (off > 4095u) {
                off = 4095u;
            }
            b[3] = (uint8_t)(off & 0xFFu);
            b[4] = (uint8_t)(off >> 8);
        }
    }
    return i2c_write(PCA9685_ADDR, b, sizeof(b));
}
