/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Register map, calibration layout and compensation formulae follow the Bosch
 * BME68x Sensor API (BSD-3-Clause) and the BME688 datasheet. The arithmetic is
 * reproduced rather than reasoned about: the polynomials are the device's
 * factory characterization and have no derivation to check against. Only the
 * floating-point path is implemented -- the F405 has an FPU, and the integer
 * path exists in the vendor API for parts that do not.
 *
 * BSEC is not used and cannot be (ADR-0002 d5) -- see the header.
 */

#include "bme68x.h"
#include "i2c.h"
#include "clock.h"

/* --- Registers ---------------------------------------------------------- */
#define REG_COEFF3     0x00u /* res_heat_val, res_heat_range, range_sw_err */
#define REG_FIELD0     0x1Du /* 17-byte measurement field */
#define REG_RES_HEAT0  0x5Au
#define REG_GAS_WAIT0  0x64u
#define REG_CTRL_GAS_1 0x71u
#define REG_CTRL_HUM   0x72u
#define REG_CTRL_MEAS  0x74u
#define REG_CONFIG     0x75u
#define REG_COEFF1     0x8Au
#define REG_CHIP_ID    0xD0u
#define REG_SOFT_RESET 0xE0u
#define REG_COEFF2     0xE1u
#define REG_VARIANT_ID 0xF0u

#define LEN_COEFF1 23u
#define LEN_COEFF2 14u
#define LEN_COEFF3 5u
#define LEN_COEFF  (LEN_COEFF1 + LEN_COEFF2 + LEN_COEFF3)
#define LEN_FIELD  17u

#define CHIP_ID_BME68X     0x61u
#define VARIANT_GAS_HIGH   0x01u /* BME688 */
#define SOFT_RESET_CMD     0xB6u

#define MODE_SLEEP  0x00u
#define MODE_FORCED 0x01u

#define STATUS_NEW_DATA  0x80u
#define STATUS_GASM_VALID 0x20u
#define STATUS_HEAT_STAB  0x10u
#define GAS_RANGE_MSK     0x0Fu

/* --- Configuration ------------------------------------------------------ *
 * Oversampling codes: 0 = skip, 1 = x1, 2 = x2, 3 = x4, 4 = x8, 5 = x16.
 * Pressure runs at x16 because it compensates the SCD41 and is the one channel
 * whose noise leaves this device (spec 6.3). Temperature and humidity are
 * secondary here -- U1 is the primary (spec 4) -- so they run low.
 * The IIR filter is off: forced mode takes single shots, and a filter would
 * need several of them to settle, biasing every reading toward the last. */
#define OSRS_T 2u
#define OSRS_P 5u
#define OSRS_H 1u
#define FILTER 0u

typedef struct {
    uint16_t t1;
    int16_t  t2;
    int8_t   t3;
    uint16_t p1;
    int16_t  p2;
    int8_t   p3;
    int16_t  p4;
    int16_t  p5;
    int8_t   p6;
    int8_t   p7;
    int16_t  p8;
    int16_t  p9;
    uint8_t  p10;
    uint16_t h1;
    uint16_t h2;
    int8_t   h3;
    int8_t   h4;
    int8_t   h5;
    uint8_t  h6;
    int8_t   h7;
    int8_t   gh1;
    int16_t  gh2;
    int8_t   gh3;
    uint8_t  res_heat_range;
    int8_t   res_heat_val;
    int8_t   range_sw_err;
} calib_t;

static calib_t s_cal;
static uint8_t s_variant;
static float s_t_fine; /* temperature carry-over into the P and H polynomials */

/* --- Bus helpers -------------------------------------------------------- */

static int read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_write_read(BME68X_ADDR, &reg, 1u, buf, len);
}

static int read_reg(uint8_t reg, uint8_t *out)
{
    return read_regs(reg, out, 1u);
}

static int write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t b[2] = {reg, value};
    return i2c_write(BME68X_ADDR, b, 2u);
}

/* --- Calibration -------------------------------------------------------- */

/* Indices into the concatenated coefficient block. The layout is the vendor
 * API's and is not contiguous or ordered -- note that h1 and h2 share byte 24,
 * one taking its low nibble and the other its high. */
enum {
    IDX_T2_LSB = 0, IDX_T2_MSB = 1, IDX_T3 = 2,
    IDX_P1_LSB = 4, IDX_P1_MSB = 5, IDX_P2_LSB = 6, IDX_P2_MSB = 7, IDX_P3 = 8,
    IDX_P4_LSB = 10, IDX_P4_MSB = 11, IDX_P5_LSB = 12, IDX_P5_MSB = 13,
    IDX_P7 = 14, IDX_P6 = 15,
    IDX_P8_LSB = 18, IDX_P8_MSB = 19, IDX_P9_LSB = 20, IDX_P9_MSB = 21, IDX_P10 = 22,
    IDX_H2_MSB = 23, IDX_H2_LSB = 24, IDX_H1_LSB = 24, IDX_H1_MSB = 25,
    IDX_H3 = 26, IDX_H4 = 27, IDX_H5 = 28, IDX_H6 = 29, IDX_H7 = 30,
    IDX_T1_LSB = 31, IDX_T1_MSB = 32,
    IDX_GH2_LSB = 33, IDX_GH2_MSB = 34, IDX_GH1 = 35, IDX_GH3 = 36,
    IDX_RES_HEAT_VAL = 37, IDX_RES_HEAT_RANGE = 39, IDX_RANGE_SW_ERR = 41
};

static uint16_t u16le(const uint8_t *c, unsigned lsb, unsigned msb)
{
    return (uint16_t)(((uint16_t)c[msb] << 8) | c[lsb]);
}

static int read_calibration(void)
{
    uint8_t c[LEN_COEFF];

    /* Three bursts because the block is split across three register ranges. */
    if (read_regs(REG_COEFF1, &c[0], LEN_COEFF1) < 0) {
        return -1;
    }
    if (read_regs(REG_COEFF2, &c[LEN_COEFF1], LEN_COEFF2) < 0) {
        return -2;
    }
    if (read_regs(REG_COEFF3, &c[LEN_COEFF1 + LEN_COEFF2], LEN_COEFF3) < 0) {
        return -3;
    }

    s_cal.t1 = u16le(c, IDX_T1_LSB, IDX_T1_MSB);
    s_cal.t2 = (int16_t)u16le(c, IDX_T2_LSB, IDX_T2_MSB);
    s_cal.t3 = (int8_t)c[IDX_T3];

    s_cal.p1 = u16le(c, IDX_P1_LSB, IDX_P1_MSB);
    s_cal.p2 = (int16_t)u16le(c, IDX_P2_LSB, IDX_P2_MSB);
    s_cal.p3 = (int8_t)c[IDX_P3];
    s_cal.p4 = (int16_t)u16le(c, IDX_P4_LSB, IDX_P4_MSB);
    s_cal.p5 = (int16_t)u16le(c, IDX_P5_LSB, IDX_P5_MSB);
    s_cal.p6 = (int8_t)c[IDX_P6];
    s_cal.p7 = (int8_t)c[IDX_P7];
    s_cal.p8 = (int16_t)u16le(c, IDX_P8_LSB, IDX_P8_MSB);
    s_cal.p9 = (int16_t)u16le(c, IDX_P9_LSB, IDX_P9_MSB);
    s_cal.p10 = c[IDX_P10];

    /* h1 and h2 are 12-bit and overlap in byte 24. */
    s_cal.h1 = (uint16_t)(((uint16_t)c[IDX_H1_MSB] << 4) | (c[IDX_H1_LSB] & 0x0Fu));
    s_cal.h2 = (uint16_t)(((uint16_t)c[IDX_H2_MSB] << 4) | (c[IDX_H2_LSB] >> 4));
    s_cal.h3 = (int8_t)c[IDX_H3];
    s_cal.h4 = (int8_t)c[IDX_H4];
    s_cal.h5 = (int8_t)c[IDX_H5];
    s_cal.h6 = c[IDX_H6];
    s_cal.h7 = (int8_t)c[IDX_H7];

    s_cal.gh1 = (int8_t)c[IDX_GH1];
    s_cal.gh2 = (int16_t)u16le(c, IDX_GH2_LSB, IDX_GH2_MSB);
    s_cal.gh3 = (int8_t)c[IDX_GH3];

    s_cal.res_heat_range = (uint8_t)((c[IDX_RES_HEAT_RANGE] & 0x30u) >> 4);
    s_cal.res_heat_val = (int8_t)c[IDX_RES_HEAT_VAL];
    s_cal.range_sw_err = (int8_t)((int8_t)(c[IDX_RANGE_SW_ERR] & 0xF0u) / 16);
    return 0;
}

/* --- Compensation (Bosch BME68x Sensor API, floating-point path) --------- */

static float calc_temperature(uint32_t adc)
{
    float var1 = (((float)adc / 16384.0f) - ((float)s_cal.t1 / 1024.0f)) * (float)s_cal.t2;
    float d = ((float)adc / 131072.0f) - ((float)s_cal.t1 / 8192.0f);
    float var2 = d * d * ((float)s_cal.t3 * 16.0f);
    s_t_fine = var1 + var2;
    return s_t_fine / 5120.0f;
}

static float calc_pressure(uint32_t adc)
{
    float var1 = (s_t_fine / 2.0f) - 64000.0f;
    float var2 = var1 * var1 * ((float)s_cal.p6 / 131072.0f);
    var2 = var2 + (var1 * (float)s_cal.p5 * 2.0f);
    var2 = (var2 / 4.0f) + ((float)s_cal.p4 * 65536.0f);
    var1 = ((((float)s_cal.p3 * var1 * var1) / 16384.0f) + ((float)s_cal.p2 * var1)) / 524288.0f;
    var1 = (1.0f + (var1 / 32768.0f)) * (float)s_cal.p1;

    if (var1 == 0.0f) {
        return 0.0f; /* the vendor API's own divide-by-zero guard */
    }

    float p = 1048576.0f - (float)adc;
    p = ((p - (var2 / 4096.0f)) * 6250.0f) / var1;
    var1 = ((float)s_cal.p9 * p * p) / 2147483648.0f;
    var2 = p * ((float)s_cal.p8 / 32768.0f);
    float var3 = (p / 256.0f) * (p / 256.0f) * (p / 256.0f) * ((float)s_cal.p10 / 131072.0f);
    return p + ((var1 + var2 + var3 + ((float)s_cal.p7 * 128.0f)) / 16.0f);
}

static float calc_humidity(uint16_t adc)
{
    float t = s_t_fine / 5120.0f;
    float var1 = (float)adc - (((float)s_cal.h1 * 16.0f) + (((float)s_cal.h3 / 2.0f) * t));
    float var2 = var1 * (((float)s_cal.h2 / 262144.0f) *
                         (1.0f + (((float)s_cal.h4 / 16384.0f) * t) +
                          (((float)s_cal.h5 / 1048576.0f) * t * t)));
    float var3 = (float)s_cal.h6 / 16384.0f;
    float var4 = (float)s_cal.h7 / 2097152.0f;
    float rh = var2 + ((var3 + (var4 * t)) * var2 * var2); /* percent */

    if (rh > 100.0f) {
        rh = 100.0f;
    } else if (rh < 0.0f) {
        rh = 0.0f;
    }
    return rh;
}

static float calc_gas_resistance(uint16_t adc, uint8_t range)
{
    if (s_variant == VARIANT_GAS_HIGH) {
        uint32_t var1 = 262144u >> range;
        int32_t var2 = 4096 + (((int32_t)adc - 512) * 3);
        return 1000000.0f * (float)var1 / (float)var2;
    }

    /* BME680 (gas-low) path -- the approved alternative of spec 4.2. Different
     * polynomial AND a per-range lookup, which is why variant_id is checked. */
    static const float k1[16] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -0.8f,
                                 0.0f, 0.0f, -0.3f, -0.5f, 0.0f, -1.0f, 0.0f, 0.0f};
    static const float k2[16] = {0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.7f, 0.0f, -0.8f,
                                 -0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float range_f = (float)(1u << range);
    float var1 = 1340.0f + (5.0f * (float)s_cal.range_sw_err);
    float var2 = var1 * (1.0f + (k1[range] / 100.0f));
    float var3 = 1.0f + (k2[range] / 100.0f);
    return 1.0f / (var3 * 0.000000125f * range_f * ((((float)adc - 512.0f) / var2) + 1.0f));
}

/* Heater resistance code for a target hotplate temperature, given where the
 * plate starts from. Capped at 400 C by the device's own limit. */
static uint8_t calc_res_heat(float target_c, float ambient_c)
{
    if (target_c > 400.0f) {
        target_c = 400.0f;
    }
    float var1 = ((float)s_cal.gh1 / 16.0f) + 49.0f;
    float var2 = (((float)s_cal.gh2 / 32768.0f) * 0.0005f) + 0.00235f;
    float var3 = (float)s_cal.gh3 / 1024.0f;
    float var4 = var1 * (1.0f + (var2 * target_c));
    float var5 = var4 + (var3 * ambient_c);
    return (uint8_t)(3.4f * ((var5 * (4.0f / (4.0f + (float)s_cal.res_heat_range)) *
                              (1.0f / (1.0f + ((float)s_cal.res_heat_val * 0.002f)))) - 25.0f));
}

/* Heater dwell as the device encodes it: a 6-bit value and a 2-bit multiplier
 * of 4, so the programmed duration is quantized, not exact. */
static uint8_t calc_gas_wait(uint16_t ms)
{
    if (ms >= 0xFC0u) {
        return 0xFFu; /* the maximum the encoding can express */
    }
    uint8_t factor = 0u;
    while (ms > 0x3Fu) {
        ms /= 4u;
        factor++;
    }
    return (uint8_t)(ms + (factor * 64u));
}

/* --- Public API --------------------------------------------------------- */

int bme68x_init(void)
{
    uint8_t id = 0;
    if (read_reg(REG_CHIP_ID, &id) < 0) {
        return -1;
    }
    if (id != CHIP_ID_BME68X) {
        return -2;
    }

    if (write_reg(REG_SOFT_RESET, SOFT_RESET_CMD) < 0) {
        return -3;
    }
    delay_ms(10u); /* datasheet start-up time after reset is 2 ms */

    /* chip_id cannot separate BME688 from BME680 (spec 10) -- this can. */
    if (read_reg(REG_VARIANT_ID, &s_variant) < 0) {
        return -4;
    }

    if (read_calibration() < 0) {
        return -5;
    }

    /* Sleep mode is required before ctrl_hum/ctrl_meas take a new setting. */
    if (write_reg(REG_CTRL_MEAS, MODE_SLEEP) < 0) {
        return -6;
    }
    if (write_reg(REG_CTRL_HUM, OSRS_H) < 0) {
        return -7;
    }
    if (write_reg(REG_CONFIG, (uint8_t)(FILTER << 2)) < 0) {
        return -8;
    }
    /* Mode stays sleep here; bme68x_trigger() writes the same register with the
     * forced bit, which is what starts a conversion. */
    if (write_reg(REG_CTRL_MEAS, (uint8_t)((OSRS_T << 5) | (OSRS_P << 2))) < 0) {
        return -9;
    }
    return 0;
}

bool bme68x_is_bme688(void)
{
    return s_variant == VARIANT_GAS_HIGH;
}

uint32_t bme68x_meas_duration_ms(void)
{
    static const uint32_t cycles[6] = {0u, 1u, 2u, 4u, 8u, 16u};

    /* Datasheet timing model: 1963 us per oversampling cycle, plus fixed
     * allowances for TPH switching and the gas measurement. */
    uint32_t us = (cycles[OSRS_T] + cycles[OSRS_P] + cycles[OSRS_H]) * 1963u;
    us += 477u * 4u; /* TPH switching */
    us += 477u * 5u; /* gas measurement */
    us += 500u;      /* round to the nearest millisecond */
    return (us / 1000u) + 1u + BME68X_HEATER_MS; /* +1 ms wake-up */
}

int bme68x_trigger(float ambient_celsius)
{
    /* Heater profile slot 0, recomputed each cycle: the code that reaches the
     * setpoint depends on ambient, and ambient moves. */
    if (write_reg(REG_RES_HEAT0, calc_res_heat((float)BME68X_HEATER_CELSIUS, ambient_celsius)) < 0) {
        return -1;
    }
    if (write_reg(REG_GAS_WAIT0, calc_gas_wait(BME68X_HEATER_MS)) < 0) {
        return -2;
    }

    /* run_gas sits at a different bit on the two variants (0x20 high, 0x10
     * low); nb_conv = 0 selects heater slot 0. */
    uint8_t run_gas = (s_variant == VARIANT_GAS_HIGH) ? 0x20u : 0x10u;
    if (write_reg(REG_CTRL_GAS_1, run_gas) < 0) {
        return -3;
    }

    /* Writing the mode bits is the trigger. The device returns itself to sleep
     * when the conversion completes. */
    uint8_t ctrl_meas = (uint8_t)((OSRS_T << 5) | (OSRS_P << 2) | MODE_FORCED);
    return (write_reg(REG_CTRL_MEAS, ctrl_meas) < 0) ? -4 : 0;
}

int bme68x_read(bme68x_data_t *out)
{
    uint8_t b[LEN_FIELD];
    if (read_regs(REG_FIELD0, b, LEN_FIELD) < 0) {
        return -1;
    }
    if ((b[0] & STATUS_NEW_DATA) == 0u) {
        return -2; /* conversion not finished -- caller waited too little */
    }

    uint32_t adc_pres = ((uint32_t)b[2] << 12) | ((uint32_t)b[3] << 4) | ((uint32_t)b[4] >> 4);
    uint32_t adc_temp = ((uint32_t)b[5] << 12) | ((uint32_t)b[6] << 4) | ((uint32_t)b[7] >> 4);
    uint16_t adc_hum = (uint16_t)(((uint16_t)b[8] << 8) | b[9]);

    /* The gas result lands in a different byte pair per variant, and each pair
     * carries its own range code and status bits. */
    uint16_t adc_gas;
    uint8_t gas_range;
    uint8_t gas_status;
    if (s_variant == VARIANT_GAS_HIGH) {
        adc_gas = (uint16_t)(((uint16_t)b[15] << 2) | (b[16] >> 6));
        gas_range = b[16] & GAS_RANGE_MSK;
        gas_status = b[16];
    } else {
        adc_gas = (uint16_t)(((uint16_t)b[13] << 2) | (b[14] >> 6));
        gas_range = b[14] & GAS_RANGE_MSK;
        gas_status = b[14];
    }

    /* Temperature first, unconditionally: t_fine feeds both other polynomials. */
    float celsius = calc_temperature(adc_temp);

    if (out) {
        out->celsius = celsius;
        out->pressure_pa = calc_pressure(adc_pres);
        out->rh_ratio = calc_humidity(adc_hum) / 100.0f;
        out->gas_ohm = calc_gas_resistance(adc_gas, gas_range);
        /* Both bits, not either: a completed conversion on a plate that never
         * reached its setpoint is a resistance at an unknown temperature, which
         * is worse for a trend than no reading (spec 6.4). */
        out->gas_valid = ((gas_status & STATUS_GASM_VALID) != 0u) &&
                         ((gas_status & STATUS_HEAT_STAB) != 0u);
    }
    return 0;
}
