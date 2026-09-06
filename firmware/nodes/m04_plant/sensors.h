/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_M04_SENSORS_H
#define IGROW_M04_SENSORS_H

/* The M04-PLANT personality: bring up the imager and its store, read a subpage
 * at a time without holding the main loop, publish the 1 Hz canopy statistics
 * and serve one interval frame per minute (M04 spec 10, ADR-0005 d11, d12).
 *
 * Named per node type because every personality is compiled into the one image
 * and selected by strap at runtime (ADR-0017 d16) -- unprefixed names would
 * collide at link time. Reached through node_personality_t, not called
 * directly; see common/node/node.h and nodes/registry.c. */
void m04_sensors_init(void);
void m04_sensors_spin(void);

#endif /* IGROW_M04_SENSORS_H */
