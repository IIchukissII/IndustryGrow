/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "condition.h"

float cond_clamp(float v, float lo, float hi)
{
    if (!(v == v)) {
        return 0.0f; /* NaN */
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float cond_dead_band(float demand, float band)
{
    return ((demand < band) && (demand > -band)) ? 0.0f : demand;
}

float cond_slew(float from, float to, float max_step)
{
    if (to > from + max_step) {
        return from + max_step;
    }
    if (to < from - max_step) {
        return from - max_step;
    }
    return to;
}

static int8_t sign_of(float v)
{
    return (v > 0.0f) ? 1 : ((v < 0.0f) ? -1 : 0);
}

void cond_reversal_init(cond_reversal_t *r)
{
    r->last_sign = 0;
    r->zero_since_us = 0u;
    r->at_zero = true;
}

float cond_reversal_gate(const cond_reversal_t *r, float target, uint64_t now_us, float dwell_s)
{
    const int8_t s = sign_of(target);
    if ((s == 0) || (r->last_sign == 0) || (s == r->last_sign)) {
        return target;
    }
    /* A reversal: the output must reach zero first and then stay there. */
    if (!r->at_zero) {
        return 0.0f;
    }
    const uint64_t dwell_us = (uint64_t)(dwell_s * 1.0e6f);
    return ((now_us - r->zero_since_us) >= dwell_us) ? target : 0.0f;
}

void cond_reversal_track(cond_reversal_t *r, float out, uint64_t now_us)
{
    const int8_t s = sign_of(out);
    if (s == 0) {
        if (!r->at_zero) {
            r->at_zero = true;
            r->zero_since_us = now_us;
        }
    } else {
        r->at_zero = false;
        r->last_sign = s;
    }
}

float cond_block_derate(float block_k, float full_k, float zero_k, float rearm_k, bool *cut)
{
    if (*cut) {
        if (block_k < rearm_k) {
            *cut = false;
        } else {
            return 0.0f;
        }
    }
    if (block_k >= zero_k) {
        *cut = true;
        return 0.0f;
    }
    if (block_k <= full_k) {
        return 1.0f;
    }
    return (zero_k - block_k) / (zero_k - full_k);
}

float cond_floor_derate(float surface_k, float floor_k, float band_k)
{
    if (surface_k <= floor_k) {
        return 0.0f;
    }
    if ((band_k <= 0.0f) || (surface_k >= floor_k + band_k)) {
        return 1.0f;
    }
    return (surface_k - floor_k) / band_k;
}
