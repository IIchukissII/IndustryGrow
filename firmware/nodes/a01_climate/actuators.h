/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_A01_ACTUATORS_H
#define IGROW_A01_ACTUATORS_H

/* A01-CLIMATE personality (E0011): takes one demand per element, conditions it
 * on the node's own state and drives it; publishes the four DS18B20
 * temperatures, each element's applied demand and state, and the interlock
 * lines (A01 spec 3, 10).
 *
 * Named per node type because every personality is compiled into the one image
 * and selected by class ID at runtime (ADR-0017 d16). Reached through
 * node_personality_t; see common/node/node.h and nodes/registry.c. */
void a01_actuators_init(void);

/* Advance the 1-Wire cycle, run the 10 Hz conditioning step and the 1 Hz
 * publications. Call from the main loop; cyphal_spin() delivers the demands
 * and flushes what this queues. */
void a01_actuators_spin(void);

#endif /* IGROW_A01_ACTUATORS_H */
