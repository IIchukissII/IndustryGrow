/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "tsl2585.h"
#include "i2c.h"

/* --- Register map (DS001043 v5-00, Register Overview) --------------------- */
#define REG_UV_CALIB         0x08u /* OTP per-device UV calibration factor */
#define REG_MOD_CHANNEL_CTRL 0x40u
#define REG_ENABLE           0x80u
#define REG_MEAS_MODE0       0x81u /* ALS_SCALE[3:0] */
#define REG_ALS_NR_SAMPLES0  0x85u
#define REG_ALS_NR_SAMPLES1  0x86u
#define REG_ID               0x92u
#define REG_ALS_STATUS       0x94u /* read to update the ALS data registers */
#define REG_STATUS2          0x9Du
#define REG_GAIN_STEP0_L     0xD4u /* modulator 1 high nibble, modulator 0 low */
#define REG_GAIN_STEP0_H     0xD5u /* modulator 2 low nibble */
#define REG_SMUX_STEP0_L     0xDCu /* photodiodes 0-3 to modulators */
#define REG_SMUX_STEP0_H     0xDDu /* photodiodes 4-5 to modulators */

#define ID_TSL2585 0x5Cu

#define ENABLE_PON  (1u << 0)
#define ENABLE_AEN  (1u << 1)

#define STATUS2_ALS_DATA_VALID  (1u << 6)
#define STATUS2_ALS_DIGITAL_SAT (1u << 4)
#define STATUS2_MOD2_ANALOG_SAT (1u << 2)

/* ALS_STATUS bits: one analog-saturation flag and one scaling flag per data
 * register. Only modulator 2's are read here. */
#define ALS_STATUS_DATA2_ANALOG_SAT (1u << 3)
#define ALS_STATUS_DATA2_UNSCALED   (1u << 0)

/* Photodiode-to-modulator map for sequencer step 0, two bits per photodiode:
 * 00 no connection, 01 modulator 0, 10 modulator 1, 11 modulator 2.
 *
 * The device's photodiodes are 0=IR, 1=PHO, 2=IR, 3=UVA, 4=UVA, 5=PHO
 * (DS001043 v5-00 Figure 3). Both UV-A diodes go to modulator 2, which is the
 * modulator whose data lands in ALS_DATA2 and whose gain this driver sets to
 * the responsivity anchor of M02 spec 6.4. Photopic and IR keep modulators 0
 * and 1 so that the parts of the device this module does not publish are still
 * in a defined state. */
#define SMUX_L_UV_ON_MOD2 0xE6u /* PHD3->2, PHD2->1, PHD1->0, PHD0->1 */
#define SMUX_H_UV_ON_MOD2 0x07u /* PHD5->0, PHD4->2 */

/* Gain codes: 0 = 0.5x, doubling per step to 0x0D = 4096x. */
#define GAIN_128X  0x08u
#define GAIN_1024X 0x0Bu

/* M02 spec 6.4's responsivity is stated at ALS gain 1024x and a 100 ms
 * integration time, so those are the settings this driver runs -- a scale
 * factor quoted at one working point and applied at another is a different
 * number. The conversion below still divides by the settings actually in
 * force, because the AGC may move the gain out from under us. */
#define UV_REF_GAIN 1024.0f

/* 82.8 counts per microwatt per square centimetre at 365 nm. TYPICAL ONLY --
 * the datasheet states neither a minimum nor a maximum, so what this driver
 * produces is a nominal scale factor and not a bounded accuracy (spec 6.4,
 * V4). 1 uW/cm2 is 1e-2 W/m2. */
#define UV_COUNTS_PER_UW_PER_CM2 82.8f
#define UW_PER_CM2_TO_W_PER_M2 0.01f

/* Integration time. ATIME = (ALS_NR_SAMPLES + 1) x (SAMPLE_TIME + 1) x
 * 1.388889 us. SAMPLE_TIME keeps its 179 default, which makes the step 250 us,
 * so 400 samples is the 100 ms the responsivity is anchored at. */
#define ALS_NR_SAMPLES 399u

/* Per-device OTP correction, read once at init:
 *   UV_calibrated = UV_raw / (1 - (UV_CALIB - 127) / 100)
 * 127 means the device met the target and the factor is 1. A device that
 * reports something absurd is not trusted with a division. */
static float s_uv_calib_divisor = 1.0f;

/* ALS_SCALE, MEAS_MODE0[3:0]. Data flagged as scaled is the raw result shifted
 * down by this many bits, and has to be shifted back before it means counts. */
static uint8_t s_als_scale = 4u;

static int rd(uint8_t reg, uint8_t *val)
{
    return i2c_write_read(TSL2585_ADDR, &reg, 1u, val, 1u);
}

static int wr(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_write(TSL2585_ADDR, buf, sizeof(buf));
}

static float gain_of(uint8_t code)
{
    if (code > 0x0Du) {
        code = 0x0Du;
    }
    return 0.5f * (float)(1u << code);
}

bool tsl2585_present(void)
{
    uint8_t id = 0u;
    return (rd(REG_ID, &id) == 0) && (id == ID_TSL2585);
}

int tsl2585_init(void)
{
    /* Every configuration register before PON. The device is explicit that PON
     * is set only once the host has initialised the rest, which is the order
     * used here; M02 spec 10 asks for the gain and integration time to be in
     * place before AEN, and they are. */
    if (wr(REG_MOD_CHANNEL_CTRL, 0x00u) < 0) { /* all three modulators enabled */
        return -1;
    }
    if (wr(REG_SMUX_STEP0_L, SMUX_L_UV_ON_MOD2) < 0) {
        return -1;
    }
    if (wr(REG_SMUX_STEP0_H, SMUX_H_UV_ON_MOD2) < 0) {
        return -1;
    }
    if (wr(REG_GAIN_STEP0_L, (uint8_t)((GAIN_128X << 4) | GAIN_128X)) < 0) {
        return -1;
    }
    if (wr(REG_GAIN_STEP0_H, GAIN_1024X) < 0) {
        return -1;
    }
    if (wr(REG_ALS_NR_SAMPLES0, (uint8_t)(ALS_NR_SAMPLES & 0xFFu)) < 0) {
        return -1;
    }
    if (wr(REG_ALS_NR_SAMPLES1, (uint8_t)(ALS_NR_SAMPLES >> 8)) < 0) {
        return -1;
    }

    uint8_t mm0 = 0u;
    if (rd(REG_MEAS_MODE0, &mm0) < 0) {
        return -1;
    }
    s_als_scale = (uint8_t)(mm0 & 0x0Fu);

    /* The OTP calibration factor. A divisor at or below zero would be the
     * device telling us its own response is negative, which it is not; keep 1
     * and let V4 find the discrepancy rather than publish an infinity. */
    uint8_t calib = 127u;
    if (rd(REG_UV_CALIB, &calib) < 0) {
        return -1;
    }
    const float divisor = 1.0f - (((float)calib - 127.0f) / 100.0f);
    s_uv_calib_divisor = (divisor > 0.05f) ? divisor : 1.0f;

    if (wr(REG_ENABLE, ENABLE_PON) < 0) {
        return -1;
    }
    return wr(REG_ENABLE, ENABLE_PON | ENABLE_AEN);
}

int tsl2585_read_uv(tsl2585_uv_t *out)
{
    /* STATUS2 first and on its own. Its ALS_DATA_VALID bit reports a completed
     * cycle "since the last readout of ALS_STATUS", so reading ALS_STATUS
     * first -- which the data registers require -- would clear the very thing
     * being asked about. */
    uint8_t st2 = 0u;
    if (rd(REG_STATUS2, &st2) < 0) {
        return -1;
    }

    /* ALS_STATUS through ALS_STATUS3 in one transaction: reading ALS_STATUS is
     * what updates the data registers, and consecutive bytes are what makes
     * the data and the gain that produced it describe the same cycle. */
    uint8_t reg = REG_ALS_STATUS;
    uint8_t buf[9]; /* 0x94 ALS_STATUS, 0x95..0x9A data, 0x9B/0x9C gain status */
    if (i2c_write_read(TSL2585_ADDR, &reg, 1u, buf, sizeof(buf)) < 0) {
        return -1;
    }

    const uint8_t als_status = buf[0];
    uint32_t raw = (uint32_t)buf[5] | ((uint32_t)buf[6] << 8); /* ALS_DATA2 */
    const uint8_t gain_code = (uint8_t)(buf[8] & 0x0Fu);       /* ALS_STATUS3 */

    /* 0xFFFF is analog saturation and 0xFFFE is a result the selected data
     * format could not express. Both are sentinels, not counts. */
    const bool sentinel = (raw >= 0xFFFEu);
    const bool saturated = sentinel ||
                           ((st2 & (STATUS2_MOD2_ANALOG_SAT | STATUS2_ALS_DIGITAL_SAT)) != 0u) ||
                           ((als_status & ALS_STATUS_DATA2_ANALOG_SAT) != 0u);

    /* Scaled data is the result shifted down; shift it back before it means
     * counts. The flag is per data register and reads inverted: set means the
     * register holds the unscaled value. */
    if ((als_status & ALS_STATUS_DATA2_UNSCALED) == 0u) {
        raw <<= s_als_scale;
    }

    const float gain = gain_of(gain_code);
    const float counts = (float)raw / s_uv_calib_divisor;
    /* Normalise to the working point the responsivity is quoted at, then apply
     * it. Integration time is fixed by init(), so it enters as a constant; the
     * gain does not, because the device's AGC may have moved it. */
    const float at_reference = counts * (UV_REF_GAIN / gain);
    const float uw_per_cm2 = at_reference / UV_COUNTS_PER_UW_PER_CM2;

    out->raw = (uint16_t)((raw > 0xFFFFu) ? 0xFFFFu : raw);
    out->gain = gain;
    out->watt_per_square_metre = uw_per_cm2 * UW_PER_CM2_TO_W_PER_M2;
    out->valid = ((st2 & STATUS2_ALS_DATA_VALID) != 0u) && !saturated;
    return 0;
}
