/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M04_FRAME_H
#define IGROW_M04_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mlx90640.h"

/*
 * What M04 produces from the device's frames: a 1 Hz statistics record, and one
 * interval frame per minute served over uavcan.file.Read (ADR-0005 d11, d12;
 * M04 spec 6.4, 6.5, 10.2). The raw device image is never served.
 *
 * Two accumulators run at once over the same pixels:
 *
 *  - the SECOND, four complete frames, whose mean the 1 Hz statistics are
 *    computed on. Averaging four frames takes the per-frame 0.36 K of noise at
 *    8 Hz down to 0.18 K (M04 spec 6.4);
 *  - the INTERVAL, nominally 240 frames, whose per-pixel mean and standard
 *    deviation are the served frame. 240 frames reach the 0.025 K
 *    fixed-integration-time floor (M04 spec 6.5).
 *
 * The variance is accumulated by WELFORD, never as sum-of-squares minus
 * n*mean^2: at canopy temperatures the sum of squares is about 5.2e6 K^2, where
 * a float32 ULP is 0.5 K^2 against the 0.02 K^2 being extracted. That
 * subtraction returns noise, and can return a negative variance (M04 spec 6.5).
 */

#define FRAME_HEADER_BYTES 64u
#define FRAME_MEAN_BYTES   (MLX90640_PIXELS * 4u)
#define FRAME_SIGMA_BYTES  (MLX90640_PIXELS)
#define FRAME_FILE_BYTES   (FRAME_HEADER_BYTES + FRAME_MEAN_BYTES + FRAME_SIGMA_BYTES)

/* The sigma plane is uint8 at 0.02 K per LSB, saturating at 5.10 K -- below the
 * 0.025 K floor it describes and above anything a still scene reaches. */
#define FRAME_SIGMA_LSB 0.02f

typedef struct {
    float t_mean, t_min, t_max, t_sigma;
    float gradient_x, gradient_y; /* K across the full frame width and height */
    uint8_t hotspot_mask[MLX90640_PIXELS / 8u];
    uint16_t hotspot_count;
    uint16_t frames; /* complete frames this second was computed on */
} frame_stats_t;

/* Header values the personality owns; the rest of the header is this module's. */
typedef struct {
    uint64_t start_us, end_us; /* gateway time base, 0 while unsynchronized */
    bool synchronized;
    bool stabilized;
    bool flat_field;
    float emissivity;
    float reflected_k;
    float ta_mean_k;
    uint8_t defective;
} frame_meta_t;

/* The served buffer doubles as the calibration-restore scratch before the first
 * frame exists (M04 spec 10.3). Nothing is served until frame_build() has run
 * once, so the two uses cannot overlap. */
uint16_t *frame_scratch(void);

/* Drop the served frame. The buffer holding it is the restore scratch, so a
 * device that has to be restored mid-run (one that disappeared and came back)
 * costs the frame that was being served rather than being overwritten under a
 * reader. */
void frame_invalidate(void);

void frame_interval_begin(void);

/* One pixel of an accepted frame, in kelvin, with the flat-field trim already
 * subtracted. */
void frame_add_pixel(uint16_t index, float kelvin);

/* Closes one accepted complete frame. */
void frame_add_done(float ta_k);

uint16_t frame_interval_frames(void);
uint16_t frame_second_frames(void);

/* Mean die temperature over the interval so far, kelvin, for the served header.
 * Zero before the interval's first frame. */
float frame_interval_ta_mean(void);

/* Statistics over the frames accumulated since the last call, on their mean.
 * `k` and `delta_min` are the hotspot constants in force. False when no frame
 * has arrived since -- an absent frame is not a frame of zeros. Resets the
 * second's accumulator either way. */
bool frame_second_stats(float k, float delta_min, frame_stats_t *out);

/* Build the interval frame from what has accumulated, and start a new interval.
 * Returns the new sequence number. Defective pixels are interpolated from their
 * neighbours in the served planes, and excluded everywhere else. */
uint32_t frame_build_interval(const frame_meta_t *meta);

/* Arm an event snapshot: the next accepted frame is captured on its own and
 * served with n_frames = 1 and a zero sigma plane (M04 spec 6.5). It is written
 * straight into the served buffer, so arming it commits to replacing what is
 * being served; the interval accumulation is not disturbed. */
void frame_arm_event(void);
bool frame_event_armed(void);
bool frame_event_captured(void);
uint32_t frame_build_event(const frame_meta_t *meta);

uint32_t frame_seq(void);
bool frame_available(void);
size_t frame_size(void);

/* Copy `len` bytes of the served file from `offset`. Returns the number copied,
 * which is short at the end and zero past it. */
uint16_t frame_read(uint32_t offset, uint8_t *out, uint16_t len);

#endif /* IGROW_M04_FRAME_H */
