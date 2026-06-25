/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ina226.h"
#include "i2c.h"

/* Register map. */
#define REG_CONFIG 0x00u
#define REG_BUSV   0x02u
#define REG_POWER  0x03u
#define REG_CURRENT 0x04u
#define REG_CALIB  0x05u

/* Fixed LSBs (datasheet): bus voltage 1.25 mV, power = 25 * current LSB. */
#define INA226_BUSV_LSB_V 0.00125f

/* --- E0006 BOM values (ADR-0000): set to the populated shunt + design max.
 *     Placeholders until the E0006 BOM is finalized. --- */
#define INA226_RSHUNT_OHMS 0.010f
#define INA226_MAX_CURRENT_A 1.0f

#define INA226_CURRENT_LSB (INA226_MAX_CURRENT_A / 32768.0f)

int ina226_init(void)
{
    /* avg=1, Vbus/Vsh conv 1.1 ms, mode = shunt+bus continuous. */
    if (i2c_write_reg16(INA226_ADDR, REG_CONFIG, 0x4127u) < 0) {
        return -1;
    }
    /* CAL = 0.00512 / (Current_LSB * Rshunt). */
    float cal_f = 0.00512f / (INA226_CURRENT_LSB * INA226_RSHUNT_OHMS);
    uint16_t cal = (uint16_t)cal_f;
    return i2c_write_reg16(INA226_ADDR, REG_CALIB, cal);
}

int ina226_read(float *bus_v, float *current_a, float *power_w)
{
    uint16_t raw;
    if (bus_v) {
        if (i2c_read_reg16(INA226_ADDR, REG_BUSV, &raw) < 0) {
            return -1;
        }
        *bus_v = (float)raw * INA226_BUSV_LSB_V;
    }
    if (current_a) {
        if (i2c_read_reg16(INA226_ADDR, REG_CURRENT, &raw) < 0) {
            return -2;
        }
        *current_a = (float)(int16_t)raw * INA226_CURRENT_LSB;
    }
    if (power_w) {
        if (i2c_read_reg16(INA226_ADDR, REG_POWER, &raw) < 0) {
            return -3;
        }
        *power_w = (float)raw * (25.0f * INA226_CURRENT_LSB);
    }
    return 0;
}
