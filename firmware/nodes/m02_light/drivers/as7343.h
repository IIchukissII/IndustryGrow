/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_DRIVERS_AS7343_H
#define IGROW_DRIVERS_AS7343_H

#include <stdbool.h>
#include <stdint.h>

/* ams OSRAM AS7343, M02's primary photic sensor: eleven filtered bands plus
 * NIR, an unfiltered clear channel and a flicker ADC (M02 spec 4).
 *
 * Address 0x39, fixed and shared with the TSL2585 -- reach it only with the
 * bus switch on channel TCA9543A_CH_U4 (M02 spec 5.2). Nothing in this file
 * touches the switch; the personality owns which channel is live, because it
 * is the only thing that knows which device it is about to talk to.
 *
 * Supplied at 1.8 V behind the series filter of M02 spec 7.3. */
#define AS7343_ADDR 0x39u

/* Band count and their order in `band[]`: ASCENDING PEAK WAVELENGTH, the order
 * M02 spec 6.3 tables. The device does not read them out in this order -- it
 * interleaves three integration cycles -- so the driver reorders once, here,
 * rather than leaving every consumer to. */
#define AS7343_BANDS 12u

/* The PAR window of M02 spec 6.2 is a PREFIX of that order: F1 (405 nm)
 * through F7 (690 nm) are the first ten entries, and F8 (745 nm) and NIR
 * (855 nm) are the two that follow and must not enter the PPFD sum. */
#define AS7343_PAR_BANDS 10u

/* Spectral gain, CFG1 AGAIN[4:0]: 0 = 0.5x, then 1x doubling per step to
 * 12 = 2048x. Thirteen steps, and the whole autorange authority of spec 6.1
 * apart from the integration time below. */
#define AS7343_AGAIN_MIN 0u
#define AS7343_AGAIN_MAX 12u

/* The two integration-time working points (spec 6.1). Both are (ATIME, ASTEP)
 * pairs; ASTEP is fixed at 999 = 2.78 ms per step, which is the device default
 * and the step the datasheet's responsivity anchor is stated at.
 *
 * NORMAL is ten steps: 27.8 ms per cycle, 83 ms for the three-cycle set, ADC
 * full scale 10 000 counts. LONG is 65 steps: 181 ms per cycle, 542 ms for the
 * set, full scale 65 535 -- the ceiling, since the device clamps there.
 *
 * LONG exists for the bottom of the 30 min ramp, where AGAIN is already at
 * 2048x: a longer integration raises signal and full scale together, which is
 * the only handle left once gain is exhausted. It is not the working point --
 * a 542 ms set leaves no margin in a 1 s publication period. */
#define AS7343_ATIME_NORMAL 9u
#define AS7343_ATIME_LONG   64u

typedef struct {
    uint16_t band[AS7343_BANDS]; /* F1 F2 FZ F3 F4 F5 FY FXL F6 F7 F8 NIR */
    uint16_t clear;              /* unfiltered silicon (VIS) */
    uint16_t full_scale;         /* (ATIME+1)*(ASTEP+1), clamped at 65535 */
    float gain;                  /* multiplier in force, 0.5 .. 2048 */
    float integration_time;      /* second, per band per cycle */
    bool saturated;              /* analog or digital saturation this set */
} as7343_sample_t;

typedef struct {
    bool valid;          /* the flicker measurement completed */
    bool saturated;      /* the flicker ADC saturated */
    bool valid_100hz;    /* the 100 Hz calculation converged */
    bool valid_120hz;
    bool detected_100hz;
    bool detected_120hz;
} as7343_flicker_t;

/* Identify the part: ID (0x5A) must read 0x81 (M02 spec 10). An address ACK is
 * not identification. Costs a register-bank round trip; see the note in the
 * implementation. */
bool as7343_present(void);

/* Power up and start free-running acquisition at (atime, again). Configures
 * the automatic 18-channel readout, the auto-zero interval and flicker
 * detection, in the order the datasheet requires (PON, configure, then
 * SP_EN). Returns 0 on success. */
int as7343_init(uint8_t atime, uint8_t again);

/* Change the working point. Stops the spectral engine, writes, restarts it --
 * the device states that changing configuration while SP_EN is set yields
 * invalid results. The next completed set is the first one taken at the new
 * setting. Returns 0 on success. */
int as7343_set_range(uint8_t atime, uint8_t again);

/* The most recently completed channel set, reordered and tagged with the
 * settings it was taken under. Returns 0 on success, <0 on a bus failure or
 * before the first set has completed. */
int as7343_read(as7343_sample_t *out);

/* Flicker flags, and clear them so the next read describes the next
 * measurement rather than every one since boot. Returns 0 on success. */
int as7343_read_flicker(as7343_flicker_t *out);

/* The multiplier an AGAIN code denotes: 0.5x at code 0, doubling thereafter.
 * Exposed because the autorange is the personality's, not the driver's. */
float as7343_gain_of(uint8_t again);

#endif /* IGROW_DRIVERS_AS7343_H */
