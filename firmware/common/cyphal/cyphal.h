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
 * as often as possible from the main loop.
 *
 * `node_name` is the reverse-DNS uavcan.node.GetInfo name and `description` the
 * uavcan.node.description register. Both belong to the strap-selected
 * personality, not to the image -- one image serves every module class
 * (ADR-0017 d16), so these are how the gateway tells an E0002 from an E0006 on
 * the wire. Both must outlive the call; string literals are the intended source. */
void cyphal_init(uint8_t node_id, const char *node_name, const char *description);
void cyphal_spin(void);

/* Publish a pre-serialized message on `subject_id` (a Cyphal port-ID). The
 * caller owns `transfer_id` (one counter per subject) — it is post-incremented
 * and wrapped. Used by the node personality to emit sensor telemetry. */
void cyphal_publish(uint16_t subject_id, uint8_t *transfer_id,
                    const uint8_t *payload, size_t size);

#endif /* IGROW_CYPHAL_CYPHAL_H */
