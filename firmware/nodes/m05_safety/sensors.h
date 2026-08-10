/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M05_SENSORS_H
#define IGROW_M05_SENSORS_H

/* M05-SAFETY personality: probe the sensor set, then publish telemetry on the
 * Cyphal bus. I2C sensors are presence-probed at boot and re-probed every 60 s,
 * so a partial population publishes only what responds (ADR-0014 d8).
 *
 * Named per node type because every personality is compiled into the one image
 * and selected by strap at runtime (ADR-0017 d16) -- unprefixed names would
 * collide at link time. Reached through node_personality_t, not called
 * directly; see common/node/node.h and nodes/registry.c. */
void m05_sensors_init(void);

/* Drive periodic publication (1 Hz) and re-probing. Call from the main loop;
 * cyphal_spin() flushes what this queues. */
void m05_sensors_spin(void);

#endif /* IGROW_M05_SENSORS_H */
