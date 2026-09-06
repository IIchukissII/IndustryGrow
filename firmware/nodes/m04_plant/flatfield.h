/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M04_FLATFIELD_H
#define IGROW_M04_FLATFIELD_H

#include <stdbool.h>
#include <stdint.h>

#include "mlx90640.h"

/*
 * The flat-field trim: one offset per pixel, correcting the OFFSET component of
 * the imager's fixed-pattern non-uniformity (M04 spec 6.7). That term is
 * +-0.5 K in zone 1, twenty times the temporal noise floor of a one-minute
 * average, and it does not average away.
 *
 * It lives in U2 and not in U1, because the MLX90640 has no user cell for 768
 * corrections (ADR-0028 d10), and it is BOUND to U1's 48-bit device ID: a trim
 * describes the module it was determined on, and a field whose ID does not
 * match the imager in the socket is refused rather than applied to another
 * part's non-uniformity. That is the module-swap failure ADR-0028 alternative A
 * names, closed by construction.
 *
 * A missing, corrupt or foreign field is REPORTED and publication continues
 * uncorrected (ADR-0028 d5). Firmware writes no default back.
 *
 * Determination is not implemented and is not implementable here: the project
 * owns no uniform reference target and the one- against two-point question is
 * unsettled (M04 spec O-99). What exists is the store, the binding and the
 * write path.
 */

/* On-device layout (M04 spec 4.2, 6.7): byte 0 of U2 is the module class ID,
 * bytes 1-15 are reserved, and the record starts at byte 16. */
#define FLATFIELD_U2_OFFSET   16u
#define FLATFIELD_HEADER_BYTES 32u
#define FLATFIELD_TABLE_BYTES  (MLX90640_PIXELS * 2u)
#define FLATFIELD_RECORD_BYTES (FLATFIELD_HEADER_BYTES + FLATFIELD_TABLE_BYTES)

#define FLATFIELD_MAGIC   0x46464749u /* "IGFF", little-endian */
#define FLATFIELD_VERSION 1u
#define FLATFIELD_KIND_ONE_POINT 1u

/* Read U2, validate, and bind against U1's device ID. Returns 0 when a field is
 * in force; <0 otherwise, with flatfield_state_str() naming which of absent,
 * corrupt and foreign it was -- three different things an operator acts on
 * differently. */
int flatfield_load(void);
bool flatfield_applied(void);
const char *flatfield_state_str(void);

/* The correction for one pixel, in kelvin, SUBTRACTED after the compensation
 * chain and before the statistics and the interval accumulation (M04 spec 6.7).
 * Zero when no field is in force. */
float flatfield_correction(uint16_t index);

/* --- the write path (uavcan.file.Write, M04 spec 6.7) ---------------------
 *
 * An offered record is staged in the same buffer the applied one occupies, so
 * the correction is out of force from the first block until the record either
 * validates or is reloaded from U2. That is reported through the record's
 * `flat_field` flag while it happens, and it is why a refused offer costs a
 * re-read rather than the previous field. */
int flatfield_stage(uint32_t offset, const uint8_t *data, uint16_t len);

/* Validate the staged record and begin committing it to U2. Returns 0 when the
 * commit is under way, <0 when the record was refused -- in which case the
 * previous field has been reloaded and nothing was written. */
int flatfield_commit_begin(void);

/* One page of the commit, from the main loop: 0 while under way, 1 when the
 * whole record is in U2 and applied, <0 on a write failure. */
int flatfield_commit_step(void);
bool flatfield_commit_active(void);

#endif /* IGROW_M04_FLATFIELD_H */
