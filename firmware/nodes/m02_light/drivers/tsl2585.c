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
#define REG_SMUX_STEP1_H     0xDFu /* [7:4] saturation-AGC sequencer pattern */
#define REG_SMUX_STEP2_H     0xE1u /* [7:4] predict-AGC sequencer pattern */

/* Both AGC modes reset to 1111b -- enabled for every sequencer step. The
 * pattern shares a register with sequencer-step photodiode mapping this driver
 * does not configure, so it is cleared by read-modify-write, not by a store. */
#define AGC_STEP_PATTERN 0xF0u

/* MEAS_MODE0 as the datasheet resets it: ALS_SCALE 4, every mode bit clear.
 * Written rather than assumed, because the device is never reset here. */
#define MEAS_MODE0_DEFAULT 0x04u

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
    /* Stop the engine before configuring it, and do not assume reset values.
     *
     * NOTHING RESETS THIS DEVICE. It has no reset pin here and shares the
     * module rail, so an MCU restart -- a watchdog, an SWD reset, an OTA --
     * leaves it running with whatever the previous session left behind. Its
     * gain registers are sequencer-owned while a measurement is live, so
     * configuration written over a running engine does not stick: measured on
     * E0003-000001, modulator 2 read back 4x after being written 1024x, and
     * ENABLE carried an FDEN this driver never sets. Clearing PON stops the
     * oscillator and, per the datasheet, clears FDEN and AEN with it, which is
     * the only state reset the part offers.
     *
     * Everything the driver depends on is then written rather than inherited.
     * Configuration goes in before PON, as the device requires; M02 spec 10
     * asks for the gain and integration time to be in place before AEN. */
    if (wr(REG_ENABLE, 0x00u) < 0) {
        return -1;
    }
    if (wr(REG_MEAS_MODE0, MEAS_MODE0_DEFAULT) < 0) {
        return -1;
    }
    if (wr(REG_MOD_CHANNEL_CTRL, 0x00u) < 0) { /* all three modulators enabled */
        return -1;
    }
    if (wr(REG_SMUX_STEP0_L, SMUX_L_UV_ON_MOD2) < 0) {
        return -1;
    }
    if (wr(REG_SMUX_STEP0_H, SMUX_H_UV_ON_MOD2) < 0) {
        return -1;
    }
    /* Disable both AGC modes before the gain is set, so that the gain written
     * below is the last word on it.
     *
     * The device resets with saturation AGC (0xDF[7:4]) and predict AGC
     * (0xE1[7:4]) enabled for all four sequencer steps, and an active AGC OWNS
     * the gain registers -- the datasheet says of each MOD_GAIN field that it
     * is "updated by the AGC, if activated". Left on, it walks modulator 2 off
     * the responsivity anchor of M02 spec 6.4 and the published W/m2 is scaled
     * by a gain the host did not choose. Measured on E0003-000001: modulator 2
     * programmed at 1024x read back at 4x, eight steps down, driven there by
     * analog saturation on the IR modulator rather than by anything in the UV
     * path -- the modulators share the sequencer, so saturation anywhere moves
     * the gain everywhere. ALS_DATA_VALID never asserted while that was going
     * on, which published every UV sample as invalid. */
    uint8_t agc = 0u;
    if (rd(REG_SMUX_STEP1_H, &agc) < 0) {
        return -1;
    }
    if (wr(REG_SMUX_STEP1_H, (uint8_t)(agc & (uint8_t)~AGC_STEP_PATTERN)) < 0) {
        return -1;
    }
    if (rd(REG_SMUX_STEP2_H, &agc) < 0) {
        return -1;
    }
    if (wr(REG_SMUX_STEP2_H, (uint8_t)(agc & (uint8_t)~AGC_STEP_PATTERN)) < 0) {
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
    /* Modulator 2's own saturation, and nothing else's.
     *
     * STATUS2's ALS_DIGITAL_SATURATION is a device-wide flag: it reports that
     * some ALS counter overflowed the selected data format, not which one.
     * Modulators 0 and 1 carry the photopic and IR photodiodes this module
     * does not publish, and indoors they saturate readily while the UV path
     * sits near the bottom of its range -- measured on E0003-000001, where
     * that flag alone marked every UV sample invalid with modulator 2 clear of
     * both its own saturation indicators. A flag that cannot name a modulator
     * cannot invalidate one. The per-register sentinel above and the two
     * modulator-2 flags below are what speak for this channel. */
    const bool saturated = sentinel ||
                           ((st2 & STATUS2_MOD2_ANALOG_SAT) != 0u) ||
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

int tsl2585_bench_dump(tsl2585_bench_t *out)
{
    /* The identity first. At 0x39 the AS7343 answers to the same address, so a
     * dump that reaches the wrong device would otherwise read as a plausible
     * set of TSL2585 registers rather than as a bus-switch fault. */
    if (rd(REG_ID, &out->id) < 0) {
        return -1;
    }

    /* Same order as tsl2585_read_uv(): STATUS2 alone and first, so that what
     * this reports about ALS_DATA_VALID is what the sample path would have
     * seen rather than the cleared state left behind by reading ALS_STATUS. */
    if (rd(REG_STATUS2, &out->status2) < 0) {
        return -1;
    }

    uint8_t reg = REG_ALS_STATUS;
    uint8_t buf[9];
    if (i2c_write_read(TSL2585_ADDR, &reg, 1u, buf, sizeof(buf)) < 0) {
        return -1;
    }
    out->als_status = buf[0];
    out->als_status2 = buf[7];
    out->als_status3 = buf[8];

    if ((rd(REG_ENABLE, &out->enable) < 0) ||
        (rd(REG_MEAS_MODE0, &out->meas_mode0) < 0) ||
        (rd(REG_MOD_CHANNEL_CTRL, &out->mod_ctrl) < 0) ||
        (rd(REG_GAIN_STEP0_L, &out->gain_step0_l) < 0) ||
        (rd(REG_GAIN_STEP0_H, &out->gain_step0_h) < 0) ||
        (rd(REG_SMUX_STEP0_L, &out->smux_step0_l) < 0) ||
        (rd(REG_SMUX_STEP0_H, &out->smux_step0_h) < 0) ||
        (rd(REG_ALS_NR_SAMPLES0, &out->nr_samples0) < 0) ||
        (rd(REG_ALS_NR_SAMPLES1, &out->nr_samples1) < 0) ||
        (rd(REG_SMUX_STEP1_H, &out->agc_asat) < 0) ||
        (rd(REG_SMUX_STEP2_H, &out->agc_predict) < 0)) {
        return -1;
    }
    return 0;
}
