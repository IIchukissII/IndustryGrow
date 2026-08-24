/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_BME68X_H
#define IGROW_M01_BME68X_H

#include <stdbool.h>
#include <stdint.h>

/*
 * U2 -- Bosch BME688 (M01 spec 4): gas/VOC resistance, barometric pressure, and
 * a secondary T/RH that must not reach the VPD computation. Address 0x76, set
 * by the SDO strap; 0x77 is unused on this board.
 *
 * VDD comes from U4's 1.8 V rail, VDDIO from the header 3.3 V (spec 7.4). The
 * part is characterized at VDD <= 1.8 V and the rail exists for that reason, so
 * the datasheet currents this driver's duty cycle assumes apply as written.
 *
 * Two roles, and the second one is the load-bearing one:
 *
 *  - VOC trend. Uncalibrated resistance against a per-device baseline, never an
 *    absolute threshold (spec 6.4). No IAQ, no CO2-equivalent, no bVOC -- those
 *    are BSEC outputs, and BSEC is a closed-source binary incompatible with the
 *    AGPL firmware (ADR-0002 d5). Humidity compensation and baseline tracking
 *    of the gas signal are BSEC functions too, and are therefore absent here.
 *  - Barometric pressure, which is what compensates the SCD41 (spec 6.3).
 *    Uncompensated pressure error propagates into CO2 at ~1:1, and this part's
 *    +/-0.6 hPa is ~0.25 ppm at 400 ppm against U3's +/-50 ppm floor -- two
 *    orders of margin, which is why the on-board barometer won over M07's.
 *    Pressure is oversampled x16 for that reason and no other.
 *
 * VARIANT DISCRIMINATION (spec 10). chip_id is 0x61 on BOTH the BME688 and the
 * approved BME680 alternative and cannot tell them apart. variant_id at 0xF0
 * can: 0x01 = BME688, 0x00 = BME680. It is not cosmetic -- the run_gas enable
 * bit and the gas-resistance conversion differ between the two, so a BME680
 * read with BME688 constants returns a plausible wrong number rather than an
 * error. Both variants are handled; spec 4.2's substitution stays a BOM line.
 */

#define BME68X_ADDR 0x76u

/* Hotplate dwell per shot. The setpoints themselves are the node's business --
 * a sweep is a list of them (spec 6.4) and each is published alongside its
 * resistance, because a reading is comparable only against the same setpoint. */
#define BME68X_HEATER_MS 150u

typedef struct {
    float celsius;      /* secondary -- NOT a VPD input (spec 4) */
    float pressure_pa;
    float rh_ratio;     /* secondary -- NOT a VPD input */
    float gas_ohm;
    bool gas_valid;     /* conversion completed AND the heater reached stability */
} bme68x_data_t;

/* Verify chip_id, latch variant_id, read the calibration block, and program the
 * oversampling and filter configuration. Returns 0 on success. */
int bme68x_init(void);

/* True when the fitted part is a BME688 rather than the BME680 alternative. */
bool bme68x_is_bme688(void);

/* Start one forced-mode conversion at `heater_celsius`. `ambient_celsius` sets
 * the heater resistance for that setpoint -- the hotplate is driven to a
 * temperature ABOVE ambient, so the code that reaches the target depends on
 * where it starts. Feed it U1's reading; 25 C is the datasheet reference if
 * nothing better is known.
 *
 * `heater_celsius` = 0 takes the pressure and secondary T/RH WITHOUT lighting
 * the hotplate: no heater profile is programmed and the gas step is disabled.
 * That is the configuration the barometer alone needs, and the barometer is the
 * role this part is load-bearing for (spec 6.3). `ambient_celsius` is then
 * unused.
 *
 * A sweep is this call repeated per setpoint (spec 6.4). Forced mode is what
 * makes that portable: the parallel mode that would sequence a profile in
 * hardware exists only on the BME688, while forced mode and per-shot setpoints
 * exist on the spec 4.2 alternative too. */
int bme68x_trigger(float ambient_celsius, uint16_t heater_celsius);

/* Milliseconds from bme68x_trigger() to a completed conversion, from the
 * datasheet timing of the configured oversampling, plus the gas step and heater
 * dwell when a setpoint was given. The caller waits this out in its own loop
 * rather than blocking here: it is ~190 ms with the heater against a 1 s publish
 * cycle, and the Cyphal TX queue must keep flushing. Pass the same
 * `heater_celsius` the trigger was given -- without the hotplate the conversion
 * is ~20 ms. */
uint32_t bme68x_meas_duration_ms(uint16_t heater_celsius);

/* Read and compensate the completed conversion. Returns 0 on success, <0 on a
 * bus error or if the device has not finished. */
int bme68x_read(bme68x_data_t *out);

#endif /* IGROW_M01_BME68X_H */
