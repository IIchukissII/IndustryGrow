/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "mlx90640.h"

#include "clock.h"
#include "i2c.h"

#include <math.h>
#include <string.h>

/* --- device map (DS12 section 10.7) --------------------------------------- */

#define REG_STATUS   0x8000u
#define REG_CONTROL1 0x800Du
#define RAM_BASE     0x0400u
#define RAM_WORDS    832u
#define EE_BASE      0x2400u
#define EE_WORDS     832u

/* Status register: bits 2:0 name the subpage just measured, bit 3 is "new data
 * in RAM" and is cleared by the customer. The write-back value also sets bit 4
 * (overwrite enabled) and bit 5 (start of measurement), which is the sequence
 * DS12 section 10.5 prescribes and what the vendor driver writes. */
#define STATUS_NEW_DATA 0x0008u
#define STATUS_CLEAR    0x0030u

/* Control register 1, written explicitly at start-up (M04 spec 10):
 *   bit 12    = 1     chess reading pattern, the pattern the part is calibrated in
 *   bits 11:10 = 11   19-bit ADC, not the 18-bit default (M04 spec 6.1)
 *   bits  9:7 = 100   8 Hz refresh: one subpage every 125 ms, 4 frames/s
 *   bits  6:4 = 000   subpage 0 selected (has effect only with repeat enabled)
 *   bit 3     = 0     subpages alternate
 *   bit 2     = 0     overwrite enabled: a missed read is overwritten, not stalled
 *   bit 0     = 1     subpage mode enabled
 * The device's own default is 0x1901: chess, 18 bit, 2 Hz. */
#define CTRL1_VALUE 0x1E01u

/* Words per chunk of the RAM read. One whole image is 1668 bytes, 37.5 ms of
 * blocking I2C at 400 kHz; 128 words is 256 bytes and about 5.8 ms, so no
 * single pass of the main loop is held long enough to cost a heartbeat or a
 * file-service response (M04 spec 5.2, 10, O-91). */
#define CHUNK_WORDS 128u

/* Status polling interval. The device raises the new-data bit at the refresh
 * rate; polling it from every pass of the main loop would put a 100 us
 * transaction on the bus thousands of times a second for an answer that changes
 * eight times. At 5 ms the poll costs about 2 % of the bus and delays the read
 * of a 125 ms subpage by at most 4 % of its interval. */
#define POLL_INTERVAL_MS 5u

/* Reference conditions of the calibration (DS12 section 11.2.2). */
#define TA0  25.0f
#define VDD0 3.3f

/* --- restored calibration (DS12 section 11.1) -----------------------------
 *
 * The per-pixel tables are held in the vendor's packed form rather than as
 * floats: alpha as uint16 against a common power-of-two scale, offset as the
 * int16 it already is, Kta as int8 against a common scale. That is 4608 bytes
 * for the four tables against 12 288 as floats, and the quantization it costs
 * is far below the device's own noise -- alpha keeps ~15 bits of a quantity
 * that enters the result under a fourth root, and Kta trims a correction of a
 * few per cent.
 *
 * Kv is not a table at all: DS12 section 11.1.5 gives it FOUR values selected
 * by the parity of the pixel's row and column, so it is held exactly as four
 * floats instead of 768 quantized copies of them. */
typedef struct {
    float k_vdd, vdd25;
    float kv_ptat, kt_ptat, v_ptat25, alpha_ptat;
    float gain;
    float ks_ta;
    float ks_to[4];
    float ct[4];
    float alpha_corr[4];
    float cp_alpha[2], cp_offset[2];
    float cp_kta, cp_kv;
    float tgc;
    uint8_t resolution_ee;

    uint16_t alpha[MLX90640_PIXELS];
    float alpha_mult;   /* alpha(i,j) = alpha[p] * alpha_mult */
    int16_t offset[MLX90640_PIXELS];
    int8_t kta[MLX90640_PIXELS];
    float kta_mult;     /* Kta(i,j) = kta[p] * kta_mult */
    float kv[4];        /* by parity: [row odd/col odd, even/odd, odd/even, even/even] */
} params_t;

static params_t s_p;
static bool s_restored;
static mlx90640_id_t s_id;

static uint16_t s_defective[MLX90640_MAX_DEFECTIVE];
static uint8_t s_defective_n;
static bool s_defective_overflow;

/* The device RAM image: the subpage just measured, plus the one before it. */
static uint16_t s_ram[RAM_WORDS];
static uint16_t s_ctrl;
static uint8_t s_subpage;
static bool s_seen[2];

static enum { RD_IDLE, RD_DATA, RD_TAIL } s_rd;
static uint16_t s_rd_word;
static uint32_t s_next_poll_ms;

/* Per-frame common values, from mlx90640_frame_common(). */
static float s_ta, s_vdd, s_k_gain;

/* --- transport ------------------------------------------------------------ */

/* 16-bit address, then `n` big-endian words. The words are read straight into
 * the caller's array as bytes and swapped in place: a separate byte buffer
 * would cost 256 bytes of stack per chunk and one more copy of every frame. */
static int read_words(uint16_t addr, uint16_t *out, uint16_t n)
{
    const uint8_t a[2] = {(uint8_t)(addr >> 8), (uint8_t)addr};
    if (i2c_write_read(MLX90640_ADDR, a, sizeof(a), (uint8_t *)out, (size_t)n * 2u) != 0) {
        return -1;
    }
    uint8_t *b = (uint8_t *)out;
    for (uint16_t i = 0; i < n; i++) {
        out[i] = (uint16_t)(((uint16_t)b[i * 2u] << 8) | b[i * 2u + 1u]);
    }
    return 0;
}

static int write_word(uint16_t addr, uint16_t value)
{
    const uint8_t b[4] = {(uint8_t)(addr >> 8), (uint8_t)addr,
                          (uint8_t)(value >> 8), (uint8_t)value};
    return i2c_write(MLX90640_ADDR, b, sizeof(b));
}

/* --- probe and configuration ---------------------------------------------- */

static bool id_plausible(const mlx90640_id_t *id)
{
    for (unsigned i = 0; i < 3; i++) {
        if ((id->word[i] != 0x0000u) && (id->word[i] != 0xFFFFu)) {
            return true; /* at least one word carries something */
        }
    }
    return false;
}

bool mlx90640_present(mlx90640_id_t *out_id)
{
    mlx90640_id_t a, b;
    if (read_words(EE_BASE + 0x07u, a.word, 3) != 0) {
        return false;
    }
    if (read_words(EE_BASE + 0x07u, b.word, 3) != 0) {
        return false;
    }
    /* Two reads, because a bus that answers with the same corrupted words twice
     * is a different fault from one that answers differently each time, and an
     * address ACK identifies nothing (M04 spec 10, V12). */
    if ((memcmp(&a, &b, sizeof(a)) != 0) || !id_plausible(&a)) {
        return false;
    }
    s_id = a;
    if (out_id != NULL) {
        *out_id = a;
    }
    return true;
}

int mlx90640_configure(void)
{
    return write_word(REG_CONTROL1, CTRL1_VALUE);
}

const mlx90640_id_t *mlx90640_id(void)
{
    return &s_id;
}

uint8_t mlx90640_defective_count(void)
{
    return s_defective_n;
}

bool mlx90640_pixel_valid(uint16_t index)
{
    for (uint8_t i = 0; i < s_defective_n; i++) {
        if (s_defective[i] == index) {
            return false;
        }
    }
    return true;
}

bool mlx90640_restored(void)
{
    return s_restored;
}

/* --- EEPROM restore (DS12 section 11.1) ----------------------------------- */

/* Two's complement in `bits` bits, widened. Every EEPROM field is coded this
 * way unless DS12 says unsigned. */
static int32_t sign_extend(uint32_t v, uint8_t bits)
{
    const uint32_t m = 1uL << (bits - 1u);
    return (int32_t)((v ^ m) - m);
}

/* Signed nibble `index` of a run of 4-bit fields starting at word `first`
 * (OCC_row/OCC_column and their ACC counterparts, DS12 11.1.3 and 11.1.4). */
static int32_t nibble_field(const uint16_t *ee, uint16_t first, uint16_t index)
{
    const uint16_t w = ee[first + (index / 4u)];
    return sign_extend((w >> (4u * (index % 4u))) & 0xFu, 4);
}

/* Parity slot of DS12 11.1.5 / 11.1.6: row and column are 1-based there, so an
 * even 0-based row index is an ODD row. */
static uint8_t parity_slot(uint16_t row0, uint16_t col0)
{
    return (uint8_t)(((row0 & 1u) ? 1u : 0u) + ((col0 & 1u) ? 2u : 0u));
}

static float pow2f(int e)
{
    return ldexpf(1.0f, e);
}

int mlx90640_restore(uint16_t *scratch, size_t scratch_words)
{
    if ((scratch == NULL) || (scratch_words < EE_WORDS)) {
        return -1;
    }
    s_restored = false;

    /* One read of the whole calibration block, at start-up, never per frame
     * (M04 spec 6.2). The watchdog is not running yet; this is the one place a
     * long blocking transfer is admissible. */
    for (uint16_t w = 0; w < EE_WORDS; w += CHUNK_WORDS) {
        uint16_t n = (uint16_t)(EE_WORDS - w);
        if (n > CHUNK_WORDS) {
            n = CHUNK_WORDS;
        }
        if (read_words((uint16_t)(EE_BASE + w), &scratch[w], n) != 0) {
            return -2;
        }
    }
    const uint16_t *ee = scratch;

    /* An erased or unanswered block reads as one repeated value. Publishing a
     * temperature computed from it would be worse than publishing nothing. */
    bool varied = false;
    for (uint16_t i = 1; i < EE_WORDS; i++) {
        if (ee[i] != ee[0]) {
            varied = true;
            break;
        }
    }
    if (!varied) {
        return -3;
    }

    for (unsigned i = 0; i < 3; i++) {
        s_id.word[i] = ee[0x07u + i];
    }
    if (!id_plausible(&s_id)) {
        return -4;
    }

    /* VDD sensor, DS12 11.1.1 */
    s_p.k_vdd = (float)(sign_extend((ee[0x33] & 0xFF00u) >> 8, 8) * 32);
    s_p.vdd25 = (float)((((int32_t)(ee[0x33] & 0x00FFu)) - 256) * 32 - 8192);

    /* Ta sensor, DS12 11.1.2 */
    s_p.kv_ptat = (float)sign_extend((ee[0x32] & 0xFC00u) >> 10, 6) / 4096.0f;
    s_p.kt_ptat = (float)sign_extend(ee[0x32] & 0x03FFu, 10) / 8.0f;
    s_p.v_ptat25 = (float)sign_extend(ee[0x31], 16);
    s_p.alpha_ptat = (float)((ee[0x10] & 0xF000u) >> 12) / 4.0f + 8.0f;

    /* Gain, KsTa, TGC, DS12 11.1.7, 11.1.8, 11.1.16 */
    s_p.gain = (float)sign_extend(ee[0x30], 16);
    s_p.ks_ta = (float)sign_extend((ee[0x3C] & 0xFF00u) >> 8, 8) / 8192.0f;
    s_p.tgc = (float)sign_extend(ee[0x3C] & 0x00FFu, 8) / 32.0f;

    if ((s_p.gain == 0.0f) || (s_p.kt_ptat == 0.0f) || (s_p.k_vdd == 0.0f)) {
        return -5;
    }

    /* Corner temperatures and their sensitivity slopes, DS12 11.1.9 to 11.1.11.
     * CT1 and CT2 are fixed at -40 and 0 degrees C and are not in the EEPROM. */
    const int32_t step = (int32_t)((ee[0x3F] & 0x3000u) >> 12) * 10;
    s_p.ct[0] = -40.0f;
    s_p.ct[1] = 0.0f;
    s_p.ct[2] = (float)((int32_t)((ee[0x3F] & 0x00F0u) >> 4) * step);
    s_p.ct[3] = (float)((int32_t)((ee[0x3F] & 0x0F00u) >> 8) * step) + s_p.ct[2];

    const int ks_to_scale = (int)(ee[0x3F] & 0x000Fu) + 8;
    const float ks_to_div = pow2f(-ks_to_scale);
    s_p.ks_to[0] = (float)sign_extend(ee[0x3D] & 0x00FFu, 8) * ks_to_div;
    s_p.ks_to[1] = (float)sign_extend((ee[0x3D] & 0xFF00u) >> 8, 8) * ks_to_div;
    s_p.ks_to[2] = (float)sign_extend(ee[0x3E] & 0x00FFu, 8) * ks_to_div;
    s_p.ks_to[3] = (float)sign_extend((ee[0x3E] & 0xFF00u) >> 8, 8) * ks_to_div;

    s_p.alpha_corr[0] = 1.0f / (1.0f + s_p.ks_to[0] * 40.0f);
    s_p.alpha_corr[1] = 1.0f;
    s_p.alpha_corr[2] = 1.0f + s_p.ks_to[1] * s_p.ct[2];
    s_p.alpha_corr[3] = s_p.alpha_corr[2] * (1.0f + s_p.ks_to[2] * (s_p.ct[3] - s_p.ct[2]));

    /* Compensation pixel, DS12 11.1.12 to 11.1.15. Two of everything: the CP
     * has one value per subpage. */
    const int kv_scale = (int)((ee[0x38] & 0x0F00u) >> 8);
    const int kta_scale1 = (int)((ee[0x38] & 0x00F0u) >> 4) + 8;
    const int kta_scale2 = (int)(ee[0x38] & 0x000Fu);
    s_p.resolution_ee = (uint8_t)((ee[0x38] & 0x3000u) >> 12);

    const int alpha_scale_cp = (int)((ee[0x20] & 0xF000u) >> 12) + 27;
    s_p.cp_alpha[0] = (float)(ee[0x39] & 0x03FFu) * pow2f(-alpha_scale_cp);
    s_p.cp_alpha[1] = s_p.cp_alpha[0] *
                      (1.0f + (float)sign_extend((ee[0x39] & 0xFC00u) >> 10, 6) / 128.0f);
    s_p.cp_offset[0] = (float)sign_extend(ee[0x3A] & 0x03FFu, 10);
    s_p.cp_offset[1] = s_p.cp_offset[0] +
                       (float)sign_extend((ee[0x3A] & 0xFC00u) >> 10, 6);
    s_p.cp_kv = (float)sign_extend((ee[0x3B] & 0xFF00u) >> 8, 8) * pow2f(-kv_scale);
    s_p.cp_kta = (float)sign_extend(ee[0x3B] & 0x00FFu, 8) * pow2f(-kta_scale1);

    /* Kv: four values by parity (DS12 11.1.5). */
    for (uint8_t slot = 0; slot < 4u; slot++) {
        const uint32_t nib = (ee[0x34] >> (12u - 4u * slot)) & 0xFu;
        s_p.kv[slot] = (float)sign_extend(nib, 4) * pow2f(-kv_scale);
    }

    /* Kta reference by parity (DS12 11.1.6), one byte each in 0x2436/0x2437. */
    float kta_rc[4];
    for (uint8_t slot = 0; slot < 4u; slot++) {
        const uint16_t w = ee[0x36u + (slot >> 1)];
        const uint32_t byte = ((slot & 1u) == 0u) ? ((w & 0xFF00u) >> 8) : (w & 0x00FFu);
        kta_rc[slot] = (float)sign_extend(byte, 8);
    }

    /* Offsets and sensitivities, DS12 11.1.3 and 11.1.4. Two passes over the
     * pixel words: the first sizes the alpha and Kta tables against the largest
     * value the device carries, the second quantizes into them.
     *
     * ONE word per pixel at 0x2440 + p carries all four of that pixel's fields:
     * offset in bits 15:10, alpha in 9:4, Kta in 3:1, and the outlier flag in
     * bit 0. DS12's worked example takes pixel (12,16)'s offset and Kta from
     * 0x25AF and its alpha from 0x258F -- a different pixel's cell -- while
     * printing the same content for both. The formulas of 11.1.3 and 11.1.4 and
     * the vendor driver both use the pixel's own cell, which is what this does;
     * following the example instead would shift every sensitivity by one row. */
    const int32_t offset_avg = sign_extend(ee[0x11], 16);
    const int occ_scale_row = (int)((ee[0x10] & 0x0F00u) >> 8);
    const int occ_scale_col = (int)((ee[0x10] & 0x00F0u) >> 4);
    const int occ_scale_rem = (int)(ee[0x10] & 0x000Fu);

    const int32_t alpha_ref = (int32_t)ee[0x21];
    const int alpha_scale = (int)((ee[0x20] & 0xF000u) >> 12) + 30;
    const int acc_scale_row = (int)((ee[0x20] & 0x0F00u) >> 8);
    const int acc_scale_col = (int)((ee[0x20] & 0x00F0u) >> 4);
    const int acc_scale_rem = (int)(ee[0x20] & 0x000Fu);

    int32_t alpha_num_max = 0;
    float kta_max = 0.0f;
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        const uint16_t row = (uint16_t)(p / MLX90640_COLS);
        const uint16_t col = (uint16_t)(p % MLX90640_COLS);
        const uint16_t w = ee[0x40u + p];

        const int32_t num = alpha_ref +
                            nibble_field(ee, 0x22u, row) * (int32_t)(1uL << acc_scale_row) +
                            nibble_field(ee, 0x28u, col) * (int32_t)(1uL << acc_scale_col) +
                            sign_extend((w & 0x03F0u) >> 4, 6) * (int32_t)(1uL << acc_scale_rem);
        if (num > alpha_num_max) {
            alpha_num_max = num;
        }

        const float kta = ((float)kta_rc[parity_slot(row, col)] +
                           (float)sign_extend((w & 0x000Eu) >> 1, 3) *
                               (float)(1uL << kta_scale2)) *
                          pow2f(-kta_scale1);
        const float mag = (kta < 0.0f) ? -kta : kta;
        if (mag > kta_max) {
            kta_max = mag;
        }
    }
    if (alpha_num_max <= 0) {
        return -6;
    }

    int alpha_shift = 0;
    while ((alpha_num_max >> alpha_shift) > 0xFFFF) {
        alpha_shift++;
    }
    s_p.alpha_mult = pow2f(alpha_shift - alpha_scale);

    int kta_bits = 0;
    while ((kta_max * pow2f(kta_bits + 1)) < 127.0f) {
        kta_bits++;
        if (kta_bits > 30) {
            break; /* Kta is identically zero on this device */
        }
    }
    s_p.kta_mult = pow2f(-kta_bits);

    s_defective_n = 0;
    s_defective_overflow = false;
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        const uint16_t row = (uint16_t)(p / MLX90640_COLS);
        const uint16_t col = (uint16_t)(p % MLX90640_COLS);
        const uint16_t w = ee[0x40u + p];

        s_p.offset[p] = (int16_t)(offset_avg +
                                  nibble_field(ee, 0x12u, row) * (int32_t)(1uL << occ_scale_row) +
                                  nibble_field(ee, 0x18u, col) * (int32_t)(1uL << occ_scale_col) +
                                  sign_extend((w & 0xFC00u) >> 10, 6) *
                                      (int32_t)(1uL << occ_scale_rem));

        const int32_t num = alpha_ref +
                            nibble_field(ee, 0x22u, row) * (int32_t)(1uL << acc_scale_row) +
                            nibble_field(ee, 0x28u, col) * (int32_t)(1uL << acc_scale_col) +
                            sign_extend((w & 0x03F0u) >> 4, 6) * (int32_t)(1uL << acc_scale_rem);
        s_p.alpha[p] = (uint16_t)((num > 0) ? (num >> alpha_shift) : 0);

        const float kta = ((float)kta_rc[parity_slot(row, col)] +
                           (float)sign_extend((w & 0x000Eu) >> 1, 3) *
                               (float)(1uL << kta_scale2)) *
                          pow2f(-kta_scale1);
        s_p.kta[p] = (int8_t)lrintf(kta * pow2f(kta_bits));

        /* DS12 section 9: a pixel with no output reads as a zero calibration
         * word, and one out of specification carries the outlier flag in bit 0.
         * Both are excluded from the statistics and interpolated in the served
         * frame (M04 spec 6.2); neither is a bus fault. */
        if ((w == 0x0000u) || ((w & 0x0001u) != 0u)) {
            if (s_defective_n < MLX90640_MAX_DEFECTIVE) {
                s_defective[s_defective_n++] = p;
            } else {
                s_defective_overflow = true;
            }
        }
    }
    /* More than four is not a device DS12 section 9 describes: the list is
     * evidence of a part outside its own specification, not of a bad read. */
    if (s_defective_overflow) {
        return -7;
    }

    s_restored = true;
    return 0;
}

/* --- acquisition ---------------------------------------------------------- */

int mlx90640_read_step(void)
{
    switch (s_rd) {
    case RD_IDLE: {
        if ((int32_t)(millis() - s_next_poll_ms) < 0) {
            return 0;
        }
        s_next_poll_ms = millis() + POLL_INTERVAL_MS;
        uint16_t status;
        if (read_words(REG_STATUS, &status, 1) != 0) {
            return -1;
        }
        if ((status & STATUS_NEW_DATA) == 0u) {
            return 0;
        }
        s_subpage = (uint8_t)(status & 0x0007u);
        /* Cleared before the read, so that a subpage completing during it is
         * visible afterwards as a new flag -- which is how a torn image is
         * detected below (DS12 section 10.5). */
        if (write_word(REG_STATUS, STATUS_CLEAR) != 0) {
            return -2;
        }
        s_rd_word = 0;
        s_rd = RD_DATA;
        return 0;
    }

    case RD_DATA: {
        uint16_t n = (uint16_t)(RAM_WORDS - s_rd_word);
        if (n > CHUNK_WORDS) {
            n = CHUNK_WORDS;
        }
        if (read_words((uint16_t)(RAM_BASE + s_rd_word), &s_ram[s_rd_word], n) != 0) {
            s_rd = RD_IDLE;
            return -3;
        }
        s_rd_word = (uint16_t)(s_rd_word + n);
        if (s_rd_word >= RAM_WORDS) {
            s_rd = RD_TAIL;
        }
        return 0;
    }

    case RD_TAIL:
    default: {
        s_rd = RD_IDLE;
        /* The control register comes from the device rather than from what was
         * written at start-up: the resolution the compensation corrects for is
         * the one the device is actually running (DS12 11.2.2.1), and a part
         * that was reset under us would otherwise be compensated against a
         * setting it no longer holds. */
        if (read_words(REG_CONTROL1, &s_ctrl, 1) != 0) {
            return -4;
        }
        uint16_t status;
        if (read_words(REG_STATUS, &status, 1) != 0) {
            return -5;
        }
        if ((status & STATUS_NEW_DATA) != 0u) {
            /* The next subpage landed while this one was being read: the image
             * now holds parts of two measurements, and the shared Ta, gain and
             * compensation-pixel words belong to the newer one. Half of one
             * frame and half of the next is not a frame (M04 spec 10). */
            return -6;
        }
        s_seen[s_subpage & 1u] = true;
        return 1;
    }
    }
}

bool mlx90640_frame_complete(void)
{
    return s_seen[0] && s_seen[1];
}

void mlx90640_frame_taken(void)
{
    s_seen[0] = false;
    s_seen[1] = false;
}

bool mlx90640_busy(void)
{
    return s_rd != RD_IDLE;
}

/* --- compensation (DS12 section 11.2) ------------------------------------- */

static int16_t ram_signed(uint16_t index)
{
    return (int16_t)s_ram[index];
}

int mlx90640_frame_common(void)
{
    if (!s_restored) {
        return -1;
    }

    /* DS12 11.2.2.1: the resolution correction applies to the VDD word ONLY.
     * Vbe, PTAT and every IR pixel are used as measured. */
    const uint8_t res_reg = (uint8_t)((s_ctrl & 0x0C00u) >> 10);
    const float res_corr = pow2f((int)s_p.resolution_ee - (int)res_reg);

    s_vdd = (res_corr * (float)ram_signed(810) - s_p.vdd25) / s_p.k_vdd + VDD0;

    const float ptat = (float)ram_signed(800);
    const float vbe = (float)ram_signed(768);
    const float den = ptat * s_p.alpha_ptat + vbe;
    if (den == 0.0f) {
        return -2;
    }
    const float ptat_art = (ptat / den) * 262144.0f; /* 2^18 */
    const float scale = 1.0f + s_p.kv_ptat * (s_vdd - VDD0);
    if (scale == 0.0f) {
        return -3;
    }
    s_ta = (ptat_art / scale - s_p.v_ptat25) / s_p.kt_ptat + TA0;

    const float gain_ram = (float)ram_signed(778);
    if (gain_ram == 0.0f) {
        return -4;
    }
    s_k_gain = s_p.gain / gain_ram;

    /* Outside the device's own operating range the frame is not a measurement,
     * whatever the pixels say (DS12 Table 5). */
    if ((s_ta < -45.0f) || (s_ta > 90.0f)) {
        return -5;
    }
    return 0;
}

float mlx90640_ta(void)
{
    return s_ta;
}

float mlx90640_vdd(void)
{
    return s_vdd;
}

/* Fourth root, guarded. The radicands below are positive for every scene this
 * module images; a negative one means the compensation has been handed a frame
 * it cannot describe, and returning a NaN into the statistics would spread that
 * one pixel across every published figure. */
static float quad_root(float x)
{
    return (x > 0.0f) ? sqrtf(sqrtf(x)) : 0.0f;
}

int mlx90640_compute(float emissivity, float reflected_c, mlx90640_pixel_fn fn, void *ctx)
{
    if (!s_restored || (fn == NULL)) {
        return -1;
    }
    if ((emissivity <= 0.0f) || (emissivity > 1.0f)) {
        emissivity = 1.0f;
    }

    const float d_ta = s_ta - TA0;
    const float d_vdd = s_vdd - VDD0;

    /* Compensation pixel, one value per subpage (DS12 11.2.2.6). */
    float cp_os[2];
    for (unsigned sp = 0; sp < 2u; sp++) {
        const float raw = (float)ram_signed((sp == 0u) ? 776u : 808u) * s_k_gain;
        cp_os[sp] = raw - s_p.cp_offset[sp] * (1.0f + s_p.cp_kta * d_ta) *
                              (1.0f + s_p.cp_kv * d_vdd);
    }

    /* Reflected component (DS12 11.2.2.9). Tr is a deployment constant; where
     * none is supplied the caller passes Ta - 8 K (M04 spec 6.2). */
    const float ta_k = s_ta + 273.15f;
    const float tr_k = reflected_c + 273.15f;
    const float ta_k2 = ta_k * ta_k;
    const float tr_k2 = tr_k * tr_k;
    const float ta_k4 = ta_k2 * ta_k2;
    const float tr_k4 = tr_k2 * tr_k2;
    const float ta_r = tr_k4 - (tr_k4 - ta_k4) / emissivity;

    const float ks_ta_term = 1.0f + s_p.ks_ta * d_ta;
    const float basic_term = 1.0f - s_p.ks_to[1] * 273.15f;

    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        const uint16_t row = (uint16_t)(p / MLX90640_COLS);
        const uint16_t col = (uint16_t)(p % MLX90640_COLS);
        /* Chess pattern (DS12 11.2.2.7): the subpage a pixel belongs to, and
         * with it which compensation-pixel value applies to it. */
        const unsigned pattern = ((row & 1u) ^ (col & 1u));

        const float pix_gain = (float)ram_signed(p) * s_k_gain;
        const float pix_os = pix_gain -
                             (float)s_p.offset[p] *
                                 (1.0f + (float)s_p.kta[p] * s_p.kta_mult * d_ta) *
                                 (1.0f + s_p.kv[parity_slot(row, col)] * d_vdd);

        float v_ir = pix_os / emissivity;
        /* The compensation pixel of the pixel's OWN subpage, per DS12 11.2.2.7:
         * (1 - Pattern) * cp_SP0 + Pattern * cp_SP1. The worked example
         * substitutes the other subpage's value here, and separately applies
         * the subpage-1 offset delta with the wrong sign; both are arithmetic
         * slips in the example, not in the formulas above it. */
        v_ir -= s_p.tgc * cp_os[pattern];

        /* TGC is fixed at zero on the BAx ordering codes (DS12 11.1.16 note 1),
         * so the gradient term above and the one below vanish on E0005. Both
         * are computed rather than compiled out: the chain is the datasheet's,
         * and a part whose EEPROM carries a non-zero TGC is compensated with
         * it rather than silently ignoring it. */
        const float alpha_comp = ((float)s_p.alpha[p] * s_p.alpha_mult -
                                  s_p.tgc * s_p.cp_alpha[pattern]) * ks_ta_term;

        const float a2 = alpha_comp * alpha_comp;
        const float a3 = a2 * alpha_comp;
        const float sx = s_p.ks_to[1] * quad_root(a3 * v_ir + a3 * alpha_comp * ta_r);

        float to = -273.15f;
        float den = alpha_comp * basic_term + sx;
        if (den > 0.0f) {
            to = quad_root(v_ir / den + ta_r) - 273.15f;

            /* Extended ranges, DS12 11.2.2.9.1: the first pass only selects
             * which corner temperature and sensitivity slope the second uses. */
            unsigned range = 3u;
            if (to < s_p.ct[1]) {
                range = 0u;
            } else if (to < s_p.ct[2]) {
                range = 1u;
            } else if (to < s_p.ct[3]) {
                range = 2u;
            }
            den = alpha_comp * s_p.alpha_corr[range] *
                  (1.0f + s_p.ks_to[range] * (to - s_p.ct[range]));
            if (den > 0.0f) {
                to = quad_root(v_ir / den + ta_r) - 273.15f;
            }
        }

        fn(p, to + 273.15f, ctx);
    }
    return 0;
}
