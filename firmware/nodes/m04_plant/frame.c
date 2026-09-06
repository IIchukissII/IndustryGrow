/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "frame.h"

#include "crc32.h"

#include <math.h>
#include <string.h>

/* Served-frame header (M04 spec 10.2), little-endian throughout:
 *
 *   0  magic "IGP1"       28  n_frames accumulated, 1 = event snapshot
 *   4  format version 1   30  defective-pixel count
 *   5  planes present     31  flags
 *   6  columns 32         32  device ID, 3 x u16
 *   7  rows 24            38  reserved
 *   8  frame_seq          40  emissivity, float32
 *  12  interval start us  44  reflected temperature, float32 K
 *  20  interval end us    48  interval mean Ta, float32 K
 *                         52  mean-plane format, 1 = float32 kelvin
 *                         56  sigma-plane scale, float32 K per LSB
 *                         60  CRC-32 over bytes 0..59 and 64..3903
 */
#define OFF_MAGIC     0u
#define OFF_VERSION   4u
#define OFF_PLANES    5u
#define OFF_COLS      6u
#define OFF_ROWS      7u
#define OFF_SEQ       8u
#define OFF_START     12u
#define OFF_END       20u
#define OFF_NFRAMES   28u
#define OFF_DEFECTIVE 30u
#define OFF_FLAGS     31u
#define OFF_DEVICE_ID 32u
#define OFF_EMISSIVITY 40u
#define OFF_REFLECTED 44u
#define OFF_TA        48u
#define OFF_MEAN_FMT  52u
#define OFF_SIGMA_LSB 56u
#define OFF_CRC       60u

#define FRAME_MAGIC 0x31504749u /* "IGP1", little-endian */

#define PLANE_MEAN  0x01u
#define PLANE_SIGMA 0x02u

#define FLAG_STABILIZED   0x01u
#define FLAG_EVENT        0x02u
#define FLAG_UNSYNCED     0x04u
#define FLAG_FLAT_FIELD   0x08u

#define MEAN_FORMAT_FLOAT32_K 1u

/* The served file. Word-aligned: the CRC unit is fed words, the mean plane is
 * written as float32 in place, and before the first frame exists this buffer is
 * lent to the calibration restore as scratch (M04 spec 10.3). */
static union {
    uint32_t words[FRAME_FILE_BYTES / 4u];
    uint8_t bytes[FRAME_FILE_BYTES];
} s_file;

static uint32_t s_seq;
static bool s_available;

/* Interval accumulators, Welford (M04 spec 6.5). */
static float s_mean[MLX90640_PIXELS];
static float s_m2[MLX90640_PIXELS];
static uint16_t s_interval_n;
static float s_ta_sum;

/* The second's accumulator, whose mean the 1 Hz statistics run on. */
static float s_sum[MLX90640_PIXELS];
static uint16_t s_second_n;

static bool s_event_armed;
static bool s_event_captured;

static void wr_u8(uint16_t off, uint8_t v) { s_file.bytes[off] = v; }
static void wr_u16(uint16_t off, uint16_t v) { memcpy(&s_file.bytes[off], &v, sizeof(v)); }
static void wr_u32(uint16_t off, uint32_t v) { memcpy(&s_file.bytes[off], &v, sizeof(v)); }
static void wr_u64(uint16_t off, uint64_t v) { memcpy(&s_file.bytes[off], &v, sizeof(v)); }
static void wr_f32(uint16_t off, float v) { memcpy(&s_file.bytes[off], &v, sizeof(v)); }

static float *mean_plane(void)
{
    return (float *)(void *)&s_file.bytes[FRAME_HEADER_BYTES];
}

static uint8_t *sigma_plane(void)
{
    return &s_file.bytes[FRAME_HEADER_BYTES + FRAME_MEAN_BYTES];
}

uint16_t *frame_scratch(void)
{
    return (uint16_t *)(void *)s_file.words;
}

void frame_invalidate(void)
{
    s_available = false;
}

void frame_interval_begin(void)
{
    memset(s_mean, 0, sizeof(s_mean));
    memset(s_m2, 0, sizeof(s_m2));
    s_interval_n = 0u;
    s_ta_sum = 0.0f;
}

void frame_add_pixel(uint16_t index, float kelvin)
{
    if (index >= MLX90640_PIXELS) {
        return;
    }
    /* Welford, with the count of the frame being closed: s_interval_n is
     * incremented in frame_add_done(), after every pixel of the frame has been
     * folded in, so the n used here is this frame's own ordinal. */
    const float n = (float)(s_interval_n + 1u);
    const float d = kelvin - s_mean[index];
    s_mean[index] += d / n;
    s_m2[index] += d * (kelvin - s_mean[index]);

    s_sum[index] += kelvin;

    if (s_event_armed) {
        mean_plane()[index] = kelvin;
    }
}

void frame_add_done(float ta_k)
{
    s_interval_n++;
    s_second_n++;
    s_ta_sum += ta_k;
    if (s_event_armed) {
        s_event_armed = false;
        s_event_captured = true;
    }
}

uint16_t frame_interval_frames(void) { return s_interval_n; }
uint16_t frame_second_frames(void) { return s_second_n; }

float frame_interval_ta_mean(void)
{
    return (s_interval_n > 0u) ? (s_ta_sum / (float)s_interval_n) : 0.0f;
}

void frame_arm_event(void)
{
    s_event_armed = true;
    s_event_captured = false;
}

bool frame_event_armed(void) { return s_event_armed; }
bool frame_event_captured(void) { return s_event_captured; }

uint32_t frame_seq(void) { return s_seq; }
bool frame_available(void) { return s_available; }
size_t frame_size(void) { return FRAME_FILE_BYTES; }

uint16_t frame_read(uint32_t offset, uint8_t *out, uint16_t len)
{
    if (!s_available || (out == NULL) || (offset >= FRAME_FILE_BYTES)) {
        return 0u;
    }
    uint32_t left = FRAME_FILE_BYTES - offset;
    if (left > len) {
        left = len;
    }
    memcpy(out, &s_file.bytes[offset], left);
    return (uint16_t)left;
}

/* --- 1 Hz statistics (M04 spec 6.4) --------------------------------------- */

bool frame_second_stats(float k, float delta_min, frame_stats_t *out)
{
    if (out == NULL) {
        return false;
    }
    const uint16_t n = s_second_n;
    if (n == 0u) {
        return false;
    }
    const float inv_n = 1.0f / (float)n;

    memset(out, 0, sizeof(*out));
    out->frames = n;

    /* Pass one: mean, extremes, and the sums of the plane fit. Only the valid
     * pixels: a defective pixel is excluded from every statistic, never
     * interpolated into one (M04 spec 6.2). */
    float sum = 0.0f, sum_x = 0.0f, sum_y = 0.0f;
    float t_min = 0.0f, t_max = 0.0f;
    uint16_t valid = 0u;
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        if (!mlx90640_pixel_valid(p)) {
            continue;
        }
        const float t = s_sum[p] * inv_n;
        if ((valid == 0u) || (t < t_min)) {
            t_min = t;
        }
        if ((valid == 0u) || (t > t_max)) {
            t_max = t;
        }
        sum += t;
        sum_x += (float)(p % MLX90640_COLS);
        sum_y += (float)(p / MLX90640_COLS);
        valid++;
    }
    if (valid == 0u) {
        s_second_n = 0u;
        memset(s_sum, 0, sizeof(s_sum));
        return false;
    }
    const float inv_valid = 1.0f / (float)valid;
    const float t_bar = sum * inv_valid;
    const float x_bar = sum_x * inv_valid;
    const float y_bar = sum_y * inv_valid;

    /* Pass two: variance, and the normal equations of the least-squares plane.
     * Solved rather than assumed separable, because excluding a defective pixel
     * breaks the orthogonality a full grid would have given for free. */
    float sq = 0.0f, sxx = 0.0f, syy = 0.0f, sxy = 0.0f, sxt = 0.0f, syt = 0.0f;
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        if (!mlx90640_pixel_valid(p)) {
            continue;
        }
        const float t = s_sum[p] * inv_n;
        const float dt = t - t_bar;
        const float dx = (float)(p % MLX90640_COLS) - x_bar;
        const float dy = (float)(p / MLX90640_COLS) - y_bar;
        sq += dt * dt;
        sxx += dx * dx;
        syy += dy * dy;
        sxy += dx * dy;
        sxt += dx * dt;
        syt += dy * dt;
    }
    out->t_mean = t_bar;
    out->t_min = t_min;
    out->t_max = t_max;
    out->t_sigma = sqrtf(sq * inv_valid);

    const float det = sxx * syy - sxy * sxy;
    if (det != 0.0f) {
        const float bx = (syy * sxt - sxy * syt) / det;
        const float by = (sxx * syt - sxy * sxt) / det;
        out->gradient_x = bx * (float)(MLX90640_COLS - 1u);
        out->gradient_y = by * (float)(MLX90640_ROWS - 1u);
    }

    /* Pass three: the hotspot mask, against the threshold in force. */
    float threshold = k * out->t_sigma;
    if (threshold < delta_min) {
        threshold = delta_min;
    }
    threshold += t_bar;
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        if (!mlx90640_pixel_valid(p)) {
            continue;
        }
        if ((s_sum[p] * inv_n) > threshold) {
            out->hotspot_mask[p / 8u] |= (uint8_t)(1u << (p % 8u));
            out->hotspot_count++;
        }
    }

    s_second_n = 0u;
    memset(s_sum, 0, sizeof(s_sum));
    return true;
}

/* --- the served frame (M04 spec 10.2) ------------------------------------- */

/* A defective pixel has no output of its own, so the served plane carries the
 * mean of its valid orthogonal neighbours in its place (M04 spec 6.2). It is
 * still absent from every statistic. */
static void interpolate_defective(void)
{
    float *plane = mean_plane();
    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        if (mlx90640_pixel_valid(p)) {
            continue;
        }
        const uint16_t row = (uint16_t)(p / MLX90640_COLS);
        const uint16_t col = (uint16_t)(p % MLX90640_COLS);
        float sum = 0.0f;
        uint8_t n = 0u;
        const int16_t neighbour[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (unsigned i = 0; i < 4u; i++) {
            const int16_t r = (int16_t)(row + neighbour[i][0]);
            const int16_t c = (int16_t)(col + neighbour[i][1]);
            if ((r < 0) || (c < 0) || (r >= (int16_t)MLX90640_ROWS) ||
                (c >= (int16_t)MLX90640_COLS)) {
                continue;
            }
            const uint16_t q = (uint16_t)(r * (int16_t)MLX90640_COLS + c);
            if (!mlx90640_pixel_valid(q)) {
                continue;
            }
            sum += plane[q];
            n++;
        }
        if (n > 0u) {
            plane[p] = sum / (float)n;
        }
    }
}

static void write_header(const frame_meta_t *meta, uint16_t n_frames, bool event)
{
    wr_u32(OFF_MAGIC, FRAME_MAGIC);
    wr_u8(OFF_VERSION, 1u);
    wr_u8(OFF_PLANES, (uint8_t)(PLANE_MEAN | PLANE_SIGMA));
    wr_u8(OFF_COLS, (uint8_t)MLX90640_COLS);
    wr_u8(OFF_ROWS, (uint8_t)MLX90640_ROWS);
    wr_u32(OFF_SEQ, s_seq);
    wr_u64(OFF_START, meta->synchronized ? meta->start_us : 0u);
    wr_u64(OFF_END, meta->synchronized ? meta->end_us : 0u);
    wr_u16(OFF_NFRAMES, n_frames);
    wr_u8(OFF_DEFECTIVE, meta->defective);

    uint8_t flags = 0u;
    if (meta->stabilized) {
        flags |= FLAG_STABILIZED;
    }
    if (event) {
        flags |= FLAG_EVENT;
    }
    if (!meta->synchronized) {
        flags |= FLAG_UNSYNCED;
    }
    if (meta->flat_field) {
        flags |= FLAG_FLAT_FIELD;
    }
    wr_u8(OFF_FLAGS, flags);

    /* The device ID and the constants in force are repeated here so that a
     * stored frame is interpretable without the record that announced it, and
     * because the transfer CRC does not survive storage (M04 spec 10.2). */
    const mlx90640_id_t *id = mlx90640_id();
    for (unsigned i = 0; i < 3u; i++) {
        wr_u16((uint16_t)(OFF_DEVICE_ID + 2u * i), id->word[i]);
    }
    wr_u16(38u, 0u); /* reserved */
    wr_f32(OFF_EMISSIVITY, meta->emissivity);
    wr_f32(OFF_REFLECTED, meta->reflected_k);
    wr_f32(OFF_TA, meta->ta_mean_k);
    wr_u32(OFF_MEAN_FMT, MEAN_FORMAT_FLOAT32_K);
    wr_f32(OFF_SIGMA_LSB, FRAME_SIGMA_LSB);

    crc32_mpeg2_begin();
    crc32_mpeg2_feed(&s_file.bytes[0], OFF_CRC);
    crc32_mpeg2_feed(&s_file.bytes[FRAME_HEADER_BYTES],
                     FRAME_MEAN_BYTES + FRAME_SIGMA_BYTES);
    wr_u32(OFF_CRC, crc32_mpeg2_end());
}

uint32_t frame_build_interval(const frame_meta_t *meta)
{
    const uint16_t n = s_interval_n;
    float *plane = mean_plane();
    uint8_t *sigma = sigma_plane();

    for (uint16_t p = 0; p < MLX90640_PIXELS; p++) {
        plane[p] = s_mean[p];
        uint32_t q = 0u;
        if (n > 1u) {
            /* Population standard deviation over the frames of the interval:
             * the discriminator between a pixel that held still and one that
             * moved (M04 spec 6.5). */
            const float var = s_m2[p] / (float)n;
            q = (uint32_t)lrintf(sqrtf((var > 0.0f) ? var : 0.0f) / FRAME_SIGMA_LSB);
        }
        sigma[p] = (uint8_t)((q > 255u) ? 255u : q);
    }
    interpolate_defective();

    s_seq++;
    write_header(meta, n, false);
    s_available = true;

    frame_interval_begin();
    return s_seq;
}

uint32_t frame_build_event(const frame_meta_t *meta)
{
    /* The plane already holds the captured frame -- frame_add_pixel() wrote it
     * there while the capture was armed. n_frames = 1, and a single frame has
     * no spread of its own. */
    memset(sigma_plane(), 0, FRAME_SIGMA_BYTES);
    interpolate_defective();

    s_event_captured = false;
    s_seq++;
    write_header(meta, 1u, true);
    s_available = true;
    return s_seq;
}
