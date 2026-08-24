/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_SCD4X_H
#define IGROW_M01_SCD4X_H

#include <stdbool.h>
#include <stdint.h>

/*
 * U3 -- Sensirion SCD41, photoacoustic NDIR CO2 (M01 spec 4). Address 0x62,
 * fixed. Runs on its own 2.8 V rail from U5 (spec 7.4), so an unpopulated U3, a
 * failed U5 and an unpowered board are all one thing on this bus: no ACK. The
 * boot probe cannot tell them apart -- spec O-37.
 *
 * The device is NOT free-running. Periodic mode has one interval, 5 s, and no
 * other exists (spec 3), so the node polls readiness and publishes when the
 * device has something new rather than on its own 1 s cadence.
 *
 * Two operations this driver deliberately does NOT expose:
 *
 *  - Forced recalibration. Spec 6.3.1 bars it until five days after the part is
 *    soldered and requires it at the 2.8 V application rail, and it needs a
 *    reference concentration nobody can supply at boot. It is a commanded
 *    bench operation, and it gets an entry point when there is something to
 *    command it from -- not a background one.
 *  - Writing the temperature offset. Spec O-45: the offset belongs to this
 *    board at thermal equilibrium and has not been measured (V7). Writing the
 *    4 C default back would be a value pretending to be a calibration.
 *    The offset is read during configuration so the log records what is
 *    actually in the device.
 *
 * Until V7 runs, this part's T and RH carry an uncalibrated offset. They are
 * published anyway, on their own subjects: the error is a bias, not an
 * invalidity, and the primary T/RH for every derived quantity is U1 (spec 4).
 */

#define SCD4X_ADDR 0x62u

/* Bring the device to M01's operating configuration and leave it measuring:
 * stop periodic mode, read the identity and offset the accessors below report,
 * disable automatic self-calibration if it is on, seed the ambient pressure,
 * restart periodic mode.
 *
 * ASC is disabled because it needs weekly exposure to ~400 ppm, which a closed
 * cabinet in photoperiod need not ever reach (spec 6.3). Disabling it makes FRC
 * mandatory -- see above.
 *
 * persist_settings is issued ONLY when ASC was actually found enabled AND
 * `allow_persist` is set, never unconditionally: the EEPROM is rated for 2000
 * cycles and a boot loop that persisted every time would spend that budget in a
 * day (spec 10). On a device already configured this call writes no EEPROM at
 * all; scd4x_asc_status() says which happened.
 *
 * `allow_persist` is the caller's statement about its timing budget, not about
 * its intent. The call blocks ~510 ms for the mandatory stop, and 800 ms more
 * if it persists; the second figure does not fit inside the watchdog window, so
 * only the pre-watchdog boot call passes true. A device that appears later on a
 * re-probe still gets ASC cleared in RAM for that run -- and if it was
 * configured at boot the setting is already in its EEPROM, so the branch does
 * not arise in the first place. */
int scd4x_configure(uint16_t ambient_hpa, bool allow_persist);

/* What the last configuration found and did about ASC: `found_on` is the state
 * read out of the device before anything was written, `persisted` whether an
 * EEPROM cycle was spent turning it off. Returns <0 until a configuration has
 * got as far as reading it.
 *
 * Worth logging on every boot, not once: `found_on` true on a device this
 * firmware has already configured means the persist is not sticking, and that
 * is a 2000-cycle budget draining one boot at a time. */
int scd4x_asc_status(bool *found_on, bool *persisted);

/* Commanded self-test. The device takes 10 s to answer, which is several times
 * the watchdog window, so the wait belongs to the caller's loop and not to this
 * driver: stop the device, wait SCD4X_STOP_MS, begin, wait SCD4X_SELF_TEST_MS,
 * read the result, then scd4x_configure() to put it back to work. Nothing else
 * may address U3 in between. */
#define SCD4X_STOP_MS      500u
#define SCD4X_SELF_TEST_MS 10000u
int scd4x_stop(void);
int scd4x_self_test_begin(void);
int scd4x_self_test_result(bool *malfunction_free);

/* True once a new 5 s sample is available. Cheap; poll it. */
int scd4x_data_ready(bool *ready);

/* CO2 as a mole fraction (400 ppm -> 400e-6), plus the device's own T/RH. */
int scd4x_read_measurement(float *co2_mole_fraction, float *celsius, float *rh_ratio);

/* Ambient-pressure compensation. Uncompensated pressure error propagates into
 * reported concentration at roughly 1:1 (spec 6.3), which is why this is fed
 * from U2 every cycle rather than set once. Accepted during periodic
 * measurement -- one of the few commands that is -- and RAM-only, so it costs
 * no EEPROM cycle. */
int scd4x_set_ambient_pressure_hpa(uint16_t hpa);

/* Identity and offset as scd4x_configure() found them, with no bus traffic of
 * their own: both are read there, while the device is stopped, because a
 * measuring SCD41 rejects both commands. They return <0 until a configure has
 * succeeded far enough to read them, and again if the device disappears and is
 * re-configured without answering. */
int scd4x_get_temperature_offset(float *celsius);
int scd4x_serial(uint64_t *out);

#endif /* IGROW_M01_SCD4X_H */
