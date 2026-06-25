/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M05_SENSORS_H
#define IGROW_M05_SENSORS_H

/* M05-SAFETY personality: probe the sensor set, then publish telemetry on the
 * Cyphal bus. I2C sensors are presence-probed at boot and re-probed every 60 s,
 * so a partial population publishes only what responds (ADR-0014 d8). */
void sensors_init(void);

/* Drive periodic publication (1 Hz) and re-probing. Call from the main loop;
 * cyphal_spin() flushes what this queues. */
void sensors_spin(void);

#endif /* IGROW_M05_SENSORS_H */
