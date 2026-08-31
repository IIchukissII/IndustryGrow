/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "as7343.h"
#include "i2c.h"
#include "clock.h"

/* --- Register map (DS001046 v6-00 section 10.1) ---------------------------
 *
 * Two banks. Registers 0x80 and above are reachable with CFG0's REG_BANK bit
 * clear, 0x20..0x7F with it set. Everything this driver writes lives in the
 * high bank; the low bank is entered once, to read the part number, because an
 * address ACK is not identification (M02 spec 10). */
#define REG_ID        0x5Au /* low bank: part number, reads 0x81 */
#define REG_ENABLE    0x80u
#define REG_ATIME     0x81u
#define REG_STATUS2   0x90u
#define REG_ASTATUS   0x94u /* reading this latches all 36 spectral data bytes */
#define REG_STATUS4   0xBCu
#define REG_CFG0      0xBFu
#define REG_CFG1      0xC6u /* AGAIN[4:0] */
#define REG_ASTEP_L   0xD4u
#define REG_ASTEP_H   0xD5u
#define REG_CFG20     0xD6u /* auto_smux[6:5] */
#define REG_AZ_CONFIG 0xDEu
#define REG_FD_STATUS 0xE3u

#define ID_AS7343 0x81u

#define ENABLE_PON   (1u << 0)
#define ENABLE_SP_EN (1u << 1)
#define ENABLE_FDEN  (1u << 6)

#define CFG0_REG_BANK (1u << 4)

#define CFG20_AUTO_SMUX_18 (3u << 5) /* three cycles, eighteen data registers */

#define STATUS2_AVALID       (1u << 6)
#define STATUS2_ASAT_DIGITAL (1u << 4)
#define STATUS2_ASAT_ANALOG  (1u << 3)

#define STATUS4_INT_BUSY (1u << 0) /* device initialising; do not interact */

#define ASTATUS_ASAT (1u << 7)

#define FD_STATUS_MEASUREMENT_VALID (1u << 5)
#define FD_STATUS_SATURATION        (1u << 4)
#define FD_STATUS_120HZ_VALID       (1u << 3)
#define FD_STATUS_100HZ_VALID       (1u << 2)
#define FD_STATUS_120HZ             (1u << 1)
#define FD_STATUS_100HZ             (1u << 0)
/* Every FD_STATUS flag is write-1-to-clear. */
#define FD_STATUS_ALL 0x3Fu

/* ASTEP is fixed; see the AS7343_ATIME_* note in the header. 999 steps of
 * 2.78 us is 2.78 ms, the device default and the datasheet's anchor. */
#define ASTEP_VALUE 999u
#define ASTEP_TICK_S 2.78e-6f

/* Auto zero every ten integration cycles. Spec 10 requires an explicit
 * interval rather than the 255 default, which runs one only before the first
 * measurement and so tracks no temperature at all. One auto zero costs 15 ms
 * typical; ten cycles at the NORMAL working point is 278 ms, so the duty is
 * about 5 % of acquisition time and roughly one auto zero every three
 * publications. The canopy's thermal time constant is minutes (spec T1), so a
 * faster interval would buy nothing for the cost. */
#define AZ_NTH_ITERATION 10u

/* Where each band lands in the eighteen data registers under auto_smux = 3.
 * CFG20 fixes the cycles as
 *   cycle 1: FZ  FY  FXL NIR VIS FD   -> DATA_0  .. DATA_5
 *   cycle 2: F2  F3  F4  F6  VIS FD   -> DATA_6  .. DATA_11
 *   cycle 3: F1  F7  F8  F5  VIS FD   -> DATA_12 .. DATA_17
 * and this table restates it in ascending peak wavelength, the order of
 * as7343_sample_t.band and of M02 spec 6.3. The assignment inside a cycle is
 * the device's, not ours, and M02 spec V3 -- each fixture channel driven alone
 * -- is what confirms it against a real luminaire. */
static const uint8_t BAND_SLOT[AS7343_BANDS] = {
    12u, /* F1  405 nm */
    6u,  /* F2  425 nm */
    0u,  /* FZ  450 nm */
    7u,  /* F3  475 nm */
    8u,  /* F4  515 nm */
    15u, /* F5  550 nm */
    1u,  /* FY  555 nm */
    2u,  /* FXL 600 nm */
    9u,  /* F6  640 nm */
    13u, /* F7  690 nm */
    14u, /* F8  745 nm */
    3u,  /* NIR 855 nm */
};
#define CLEAR_SLOT 4u /* VIS, read out identically in all three cycles */

static uint8_t s_atime = AS7343_ATIME_NORMAL;
static uint8_t s_again = 7u; /* 64x -- the expected full-output point, spec 6.1 */

static int rd(uint8_t reg, uint8_t *val)
{
    return i2c_write_read(AS7343_ADDR, &reg, 1u, val, 1u);
}

static int wr(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_write(AS7343_ADDR, buf, sizeof(buf));
}

/* The bank bit lives in CFG0, which is itself a high-bank register. Selecting
 * the low bank and then writing CFG0 to leave it is the only way out: the
 * datasheet defines the bit's effect on the 0x20..0x7F window and says nothing
 * about gating CFG0 itself. Kept in one place so there is one thing to be
 * wrong about, and used only by as7343_present(). */
static int set_bank(bool low)
{
    return wr(REG_CFG0, low ? CFG0_REG_BANK : 0u);
}

float as7343_gain_of(uint8_t again)
{
    if (again > AS7343_AGAIN_MAX) {
        again = AS7343_AGAIN_MAX;
    }
    /* Code 0 is 0.5x and every step doubles, so the multiplier is 2^(n-1). */
    return 0.5f * (float)(1u << again);
}

static float integration_time_of(uint8_t atime)
{
    return (float)((uint32_t)atime + 1u) * (float)(ASTEP_VALUE + 1u) * ASTEP_TICK_S;
}

static uint16_t full_scale_of(uint8_t atime)
{
    const uint32_t fs = ((uint32_t)atime + 1u) * (ASTEP_VALUE + 1u);
    return (fs > 65535u) ? 65535u : (uint16_t)fs;
}

bool as7343_present(void)
{
    uint8_t id = 0u;
    if (set_bank(true) < 0) {
        return false;
    }
    const int rc = rd(REG_ID, &id);
    /* Restore the high bank whatever the read did: left in the low bank, every
     * subsequent write would land somewhere else. */
    if (set_bank(false) < 0) {
        return false;
    }
    return (rc == 0) && (id == ID_AS7343);
}

static int write_range(uint8_t atime, uint8_t again)
{
    if (again > AS7343_AGAIN_MAX) {
        again = AS7343_AGAIN_MAX;
    }
    /* ATIME and ASTEP may not both be zero (DS001046 equation 1). ASTEP is
     * fixed at 999 here, so the pair is never degenerate. */
    if (wr(REG_ATIME, atime) < 0) {
        return -1;
    }
    if (wr(REG_ASTEP_L, (uint8_t)(ASTEP_VALUE & 0xFFu)) < 0) {
        return -1;
    }
    if (wr(REG_ASTEP_H, (uint8_t)(ASTEP_VALUE >> 8)) < 0) {
        return -1;
    }
    if (wr(REG_CFG1, again) < 0) {
        return -1;
    }
    s_atime = atime;
    s_again = again;
    return 0;
}

int as7343_init(uint8_t atime, uint8_t again)
{
    /* PON first, then configure, then SP_EN: the datasheet is explicit that a
     * configuration change with SP_EN set yields invalid results. */
    if (wr(REG_ENABLE, ENABLE_PON) < 0) {
        return -1;
    }

    /* INT_BUSY stays asserted for about 300 us after power-on and the device
     * must not be interacted with until it clears. Poll rather than sleep the
     * worst case, and give up rather than spin: a device that never leaves
     * initialisation is a fault, not a slow start. */
    for (unsigned i = 0; i < 20u; i++) {
        uint8_t st4 = 0u;
        if (rd(REG_STATUS4, &st4) < 0) {
            return -1;
        }
        if ((st4 & STATUS4_INT_BUSY) == 0u) {
            break;
        }
        delay_ms(1u);
    }

    if (wr(REG_AZ_CONFIG, AZ_NTH_ITERATION) < 0) {
        return -1;
    }
    /* The channel mapping of spec 6.1 and 10. The device's own ROM sequencer
     * walks the three cycles and files the results in the eighteen data
     * registers; no SMUX RAM image is written, so the vendor's manual SMUX
     * configuration is not used. auto_smux may only be changed with the
     * spectral engine stopped, which is where we are. */
    if (wr(REG_CFG20, CFG20_AUTO_SMUX_18) < 0) {
        return -1;
    }
    if (write_range(atime, again) < 0) {
        return -1;
    }
    /* FDEN puts ADC5 on flicker detection in every cycle, at the device's
     * default FD_TIME and FD_GAIN. Those live at 0xE0 and 0xE2; the datasheet
     * prose naming 0xD8/0xDA is an erratum -- those are the AS7341's addresses
     * -- and the register overview at 0xE0/0xE2 is what this driver follows.
     * Nothing here writes them, so the erratum costs nothing either way. */
    return wr(REG_ENABLE, ENABLE_PON | ENABLE_SP_EN | ENABLE_FDEN);
}

int as7343_set_range(uint8_t atime, uint8_t again)
{
    if ((atime == s_atime) && (again == s_again)) {
        return 0;
    }
    if (wr(REG_ENABLE, ENABLE_PON | ENABLE_FDEN) < 0) { /* SP_EN low to write */
        return -1;
    }
    if (write_range(atime, again) < 0) {
        return -1;
    }
    return wr(REG_ENABLE, ENABLE_PON | ENABLE_SP_EN | ENABLE_FDEN);
}

int as7343_read(as7343_sample_t *out)
{
    uint8_t st2 = 0u;
    if (rd(REG_STATUS2, &st2) < 0) {
        return -1;
    }
    if ((st2 & STATUS2_AVALID) == 0u) {
        return -1; /* no set completed yet; the caller publishes nothing */
    }
    /* AVALID says a set has completed, NOT that this one is new. The device
     * free-runs and the bit stays asserted, so a spectral engine that stopped
     * converting reads back as its last set forever, with no flag to say so:
     * the counts would keep publishing while nothing measured them. There is
     * no sample counter on this part to close that gap, and an all-equal
     * vector is a legitimate dark-period reading rather than evidence. What
     * catches it is the DATA over a 24 h cycle (M02 spec V2), not the node's
     * health -- the same distinction M01 spec O-75 was learned on. */

    /* ASTATUS then the thirty-six data bytes, in ONE transaction. Reading
     * ASTATUS latches the whole set to that read, and only consecutive bytes
     * are guaranteed concurrent -- split into two transfers, the tail could
     * come from the next integration. */
    uint8_t reg = REG_ASTATUS;
    uint8_t buf[1u + (18u * 2u)];
    if (i2c_write_read(AS7343_ADDR, &reg, 1u, buf, sizeof(buf)) < 0) {
        return -1;
    }

    for (unsigned i = 0; i < AS7343_BANDS; i++) {
        const unsigned o = 1u + (BAND_SLOT[i] * 2u);
        out->band[i] = (uint16_t)((uint16_t)buf[o] | ((uint16_t)buf[o + 1u] << 8));
    }
    const unsigned c = 1u + (CLEAR_SLOT * 2u);
    out->clear = (uint16_t)((uint16_t)buf[c] | ((uint16_t)buf[c + 1u] << 8));

    out->gain = as7343_gain_of(s_again);
    out->integration_time = integration_time_of(s_atime);
    out->full_scale = full_scale_of(s_atime);
    /* Saturation from both places that report it: ASTATUS carries the flag
     * latched with this data set, STATUS2 separates analog from digital. The
     * published flag does not distinguish them -- either one makes the counts
     * a ceiling rather than a measurement. */
    out->saturated = ((buf[0] & ASTATUS_ASAT) != 0u) ||
                     ((st2 & (STATUS2_ASAT_ANALOG | STATUS2_ASAT_DIGITAL)) != 0u);
    return 0;
}

int as7343_read_flicker(as7343_flicker_t *out)
{
    uint8_t fd = 0u;
    if (rd(REG_FD_STATUS, &fd) < 0) {
        return -1;
    }
    out->valid = (fd & FD_STATUS_MEASUREMENT_VALID) != 0u;
    out->saturated = (fd & FD_STATUS_SATURATION) != 0u;
    out->valid_100hz = (fd & FD_STATUS_100HZ_VALID) != 0u;
    out->valid_120hz = (fd & FD_STATUS_120HZ_VALID) != 0u;
    out->detected_100hz = (fd & FD_STATUS_100HZ) != 0u;
    out->detected_120hz = (fd & FD_STATUS_120HZ) != 0u;
    /* Write-1-to-clear, so the next read describes the next measurement and
     * not the disjunction of every one since boot. A failure to clear is not
     * reported: the flags just read are good, and it is the read after this
     * one that would be stale. */
    (void)wr(REG_FD_STATUS, FD_STATUS_ALL);
    return 0;
}
