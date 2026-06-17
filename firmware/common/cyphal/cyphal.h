/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_CYPHAL_CYPHAL_H
#define IGROW_CYPHAL_CYPHAL_H

#include <stddef.h>
#include <stdint.h>

/* Cyphal node skeleton over libcanard (ADR-0005 d5): publishes
 * uavcan.node.Heartbeat at 1 Hz, answers uavcan.node.GetInfo + the register
 * interface + ExecuteCommand. That makes the node enumerate and be configurable
 * on the gateway. The per-node personality (sensor publications) sits on top
 * via cyphal_publish().
 *
 * `node_id` is static for bring-up; ADR-0005 d6 makes it register-provisioned
 * later. Call cyphal_init() once after can_init_normal(), then cyphal_spin()
 * as often as possible from the main loop. */
void cyphal_init(uint8_t node_id);
void cyphal_spin(void);

/* Publish a pre-serialized message on `subject_id` (a Cyphal port-ID). The
 * caller owns `transfer_id` (one counter per subject) — it is post-incremented
 * and wrapped. Used by the node personality to emit sensor telemetry. */
void cyphal_publish(uint16_t subject_id, uint8_t *transfer_id,
                    const uint8_t *payload, size_t size);

#endif /* IGROW_CYPHAL_CYPHAL_H */
