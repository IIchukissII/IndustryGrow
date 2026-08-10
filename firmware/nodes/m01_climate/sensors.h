/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M01_SENSORS_H
#define IGROW_M01_SENSORS_H

/* The M01-CLIMATE personality: probe the three I2C sensors, publish the
 * subjects whose parts responded, and re-probe periodically (ADR-0014 d8).
 *
 * Named per node type because every personality is compiled into the one image
 * and selected by strap at runtime (ADR-0017 d16) -- unprefixed names would
 * collide at link time. Reached through node_personality_t, not called
 * directly; see common/node/node.h and nodes/registry.c. */
void m01_sensors_init(void);
void m01_sensors_spin(void);

#endif /* IGROW_M01_SENSORS_H */
