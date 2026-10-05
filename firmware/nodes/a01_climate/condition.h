/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_A01_CONDITION_H
#define IGROW_A01_CONDITION_H

#include <stdbool.h>
#include <stdint.h>

/* Output conditioning for actuator demands (ADR-0015 d18): the node's half of
 * the cascade. Pure functions of their arguments and their own state -- no
 * hardware, no clock reads -- so the rules of A01 spec section 6 can be read
 * here without the plumbing around them. Temperatures are kelvin. */

/* D5: a demand inside the dead band is zero drive. */
float cond_dead_band(float demand, float band);

/* D7: move `from` toward `to` by at most `max_step`. */
float cond_slew(float from, float to, float max_step);

/* D3/D5: no sign reversal until the output has been at zero for the dwell.
 *
 * `out` is the conditioned output of the previous step. Feed every step's
 * output back through cond_reversal_track() so the time at zero is known. The
 * node starts as if it had dwelt, so the first demand after a reset is not
 * held. */
typedef struct {
    int8_t last_sign;        /* sign of the last non-zero output, 0 = none yet */
    uint64_t zero_since_us;  /* when the output last reached zero */
    bool at_zero;
} cond_reversal_t;

void cond_reversal_init(cond_reversal_t *r);

/* `target` itself, or 0 while a reversal is waiting out the dwell. */
float cond_reversal_gate(const cond_reversal_t *r, float target, uint64_t now_us, float dwell_s);

void cond_reversal_track(cond_reversal_t *r, float out, uint64_t now_us);

/* D9: water-block derate. 1 below `full_k`, linear to 0 at `zero_k`; once at 0
 * it stays there until the block falls below `rearm_k`. `*cut` carries that
 * hysteresis between calls. */
float cond_block_derate(float block_k, float full_k, float zero_k, float rearm_k, bool *cut);

/* D10/D11: a cooling demand is held off a temperature floor. 1 at or above
 * floor + band, linear to 0 at the floor, 0 below it. Applied to cooling only;
 * the caller decides which sign that is. */
float cond_floor_derate(float surface_k, float floor_k, float band_k);

/* Clamp to [lo, hi]; NaN becomes 0. */
float cond_clamp(float v, float lo, float hi);

#endif /* IGROW_A01_CONDITION_H */
