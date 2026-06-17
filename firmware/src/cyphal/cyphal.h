/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_CYPHAL_CYPHAL_H
#define IGROW_CYPHAL_CYPHAL_H

#include <stdint.h>

/* Minimal Cyphal node skeleton over libcanard (ADR-0005 d5): publishes
 * uavcan.node.Heartbeat at 1 Hz and answers uavcan.node.GetInfo, which is
 * what makes the node enumerate on the gateway console (roadmap stage 1).
 * register Access/List + ExecuteCommand are the next slice.
 *
 * `node_id` is static for bring-up; ADR-0005 d6 makes it register-provisioned
 * later. Call cyphal_init() once after can_init_normal(), then cyphal_spin()
 * as often as possible from the main loop. */
void cyphal_init(uint8_t node_id);
void cyphal_spin(void);

#endif /* IGROW_CYPHAL_CYPHAL_H */
