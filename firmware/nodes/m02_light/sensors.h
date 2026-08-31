/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M02_SENSORS_H
#define IGROW_M02_SENSORS_H

/* The M02-LIGHT personality: bring up the bus switch, probe the two sensors on
 * their separate channels, autorange the spectral engine and publish the
 * subjects whose parts responded (ADR-0014 d8, M02 spec 10).
 *
 * Named per node type because every personality is compiled into the one image
 * and selected by strap at runtime (ADR-0017 d16) -- unprefixed names would
 * collide at link time. Reached through node_personality_t, not called
 * directly; see common/node/node.h and nodes/registry.c. */
void m02_sensors_init(void);
void m02_sensors_spin(void);

#endif /* IGROW_M02_SENSORS_H */
