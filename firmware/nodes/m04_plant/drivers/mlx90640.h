/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M04_MLX90640_H
#define IGROW_M04_MLX90640_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Melexis MLX90640 32x24 far-infrared array, U1 on E0005 (M04 spec 4).
 * Datasheet rev 12, 3901090640, 2019-12-03 -- cited below as DS12.
 *
 * The device delivers half an image at a time: one SUBPAGE every refresh
 * interval, the two subpages interleaved over the array in the chess pattern it
 * is factory calibrated in. A complete frame is therefore two subpage reads,
 * and the RAM read that fetches one of them fetches the whole 832-word image --
 * the half that was just measured, and the half that was measured before it.
 *
 * Everything here is single precision. The STM32F405's FPU is single precision
 * only; the same chain in `double` costs ~21 ms per frame against ~1 ms, which
 * M04 spec 10 forbids outright. `float32`'s ULP at 300 K is 0.00003 K against a
 * 0.025 K interval noise floor, so the arithmetic carries more digits than the
 * instrument produces.
 */

#define MLX90640_ADDR   0x33u  /* factory default; M04 spec 4 requires it unchanged */
#define MLX90640_COLS   32u
#define MLX90640_ROWS   24u
#define MLX90640_PIXELS (MLX90640_COLS * MLX90640_ROWS)

/* DS12 section 9: up to four defective pixels per device, identified in its own
 * EEPROM. More than that is not a device this specification covers. */
#define MLX90640_MAX_DEFECTIVE 4u

/* The 48-bit per-part identifier at EEPROM 0x2407..0x2409 (M04 spec 10). It is
 * the instance evidence a bring-up record cites, and what a flat-field trim is
 * bound to (M04 spec 6.7). An address ACK is not identification. */
typedef struct {
    uint16_t word[3];
} mlx90640_id_t;

/* Probe: ACK at 0x33 plus device-ID words that are non-zero, not all ones, and
 * identical across two reads. Fills `out_id` when it succeeds. */
bool mlx90640_present(mlx90640_id_t *out_id);

/* Write control register 1 explicitly rather than inheriting the device's
 * EEPROM defaults (M04 spec 10): chess pattern, 19-bit ADC, 8 Hz refresh,
 * alternating subpages, overwrite enabled. Returns 0, or <0 on a bus failure. */
int mlx90640_configure(void);

/* Read the 832-word calibration EEPROM once and restore the per-pixel and
 * common parameters per DS12 section 11.1 (M04 spec 6.2). Returns 0 on success,
 * <0 on a bus failure or an implausible block -- publication does not start on
 * a failed restore, because uncompensated data is not a measurement.
 *
 * `scratch` holds the raw block while it is being unpacked and needs
 * MLX90640_RESTORE_SCRATCH_WORDS of it. It is a borrowed buffer and not kept:
 * the restore runs once, before the first frame exists, so the caller lends the
 * served-frame buffer rather than the node carrying 1664 bytes for the life of
 * the boot (M04 spec 10.3, transient at start-up). */
#define MLX90640_RESTORE_SCRATCH_WORDS 832u
int mlx90640_restore(uint16_t *scratch, size_t scratch_words);
bool mlx90640_restored(void);

const mlx90640_id_t *mlx90640_id(void);
uint8_t mlx90640_defective_count(void);

/* False for a pixel the device's EEPROM marks broken or as an outlier. Such a
 * pixel is excluded from every statistic and interpolated in the served frame
 * (M04 spec 6.2), so both users need to ask. */
bool mlx90640_pixel_valid(uint16_t index);

/* One step of the chunked subpage acquisition, called from the main loop.
 *
 * A whole RAM image is 1668 bytes, 37.5 ms of blocking I2C at 400 kHz, eight
 * times a second (M04 spec 5.2). Read in one call it would hold the loop away
 * from the heartbeat and the file service for that long; read in chunks, no
 * single call costs more than a few milliseconds and the node keeps answering
 * throughout (M04 spec 10, O-91).
 *
 * Returns 1 when a subpage has completed and the RAM image is coherent, 0 while
 * idle or mid-read, and <0 when the read was abandoned. A read the device
 * overwrote before it finished returns <0 and the subpage is dropped: half of
 * one frame and half of the next is not a frame.
 */
int mlx90640_read_step(void);

/* True once both subpages have been read since the last mlx90640_frame_taken().
 * That, and not a single subpage read, is one complete frame. */
bool mlx90640_frame_complete(void);
void mlx90640_frame_taken(void);

/* True while a chunked read is part-way through. A presence probe issued then
 * would interleave a transaction between two chunks of one image; it is legal
 * on the bus and costs the read the probe's time, so the re-probe waits. */
bool mlx90640_busy(void);

/* The per-frame quantities every pixel shares: supply voltage, die temperature
 * and the gain factor (DS12 11.2.2.2 to 11.2.2.4). Run it on a complete frame
 * BEFORE mlx90640_compute(), which uses what it leaves behind.
 *
 * Split from the pixel loop because the frame-validity rule of M04 spec 10 is
 * decided on Ta -- a frame whose Ta jumped is not accumulated -- and that
 * decision has to be reachable before 768 pixels have been streamed to a
 * consumer that would then have to unpick them.
 *
 * Returns 0, or <0 when the frame cannot be compensated at all. */
int mlx90640_frame_common(void);

/* Called once per pixel by mlx90640_compute(), in row-major index order. */
typedef void (*mlx90640_pixel_fn)(uint16_t index, float kelvin, void *ctx);

/* The rest of the DS12 section 11.2 chain over the current RAM image: gain,
 * offset/Ta/VDD compensation, emissivity, gradient compensation, normalization
 * to sensitivity, and the range-dependent sensitivity correction.
 *
 * `emissivity` and `reflected_c` are deployment constants (M04 spec 6.2), not
 * device properties -- reflected apparent temperature in degrees Celsius.
 * Results reach the caller through `fn` in kelvin, one pixel at a time, which
 * is what keeps the compensated field out of RAM: no consumer of it needs the
 * whole field at once.
 *
 * Returns 0, or <0 when there is nothing to compensate with.
 */
int mlx90640_compute(float emissivity, float reflected_c, mlx90640_pixel_fn fn, void *ctx);

/* Die temperature and supply, from the last mlx90640_frame_common(). Ta is the
 * DIE temperature: the air around the device is about 8 K below it, and it is
 * not air temperature (M04 spec 8 T1). */
float mlx90640_ta(void);
float mlx90640_vdd(void);

#endif /* IGROW_M04_MLX90640_H */
