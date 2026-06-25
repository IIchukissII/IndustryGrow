/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "tmp117.h"
#include "i2c.h"

#define TMP117_REG_TEMP 0x00u
#define TMP117_LSB_C 0.0078125f /* 7.8125 m°C per LSB */

int tmp117_read_kelvin(float *kelvin)
{
    uint16_t raw;
    if (i2c_read_reg16(TMP117_ADDR, TMP117_REG_TEMP, &raw) < 0) {
        return -1;
    }
    float celsius = (float)(int16_t)raw * TMP117_LSB_C;
    if (kelvin) {
        *kelvin = celsius + 273.15f;
    }
    return 0;
}
