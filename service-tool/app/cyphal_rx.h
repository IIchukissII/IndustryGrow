/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Cyphal/CAN receive path for the panel: libcanard reassembles transfers off
 * can_port, the subject table decodes them and the model records them.
 *
 * The panel is a listener by design. It subscribes to uavcan.node.Heartbeat
 * and to every subject in subjects.c, and it publishes NOTHING on its own --
 * no heartbeat, no diagnostics. That keeps it invisible to the rest of the bus
 * until someone presses a button, and it means the panel cannot be mistaken
 * for a node the gateway should be managing.
 *
 * The request side is deliberately narrow: GetInfo, and ExecuteCommand at
 * version 1.0, which is the version the node firmware implements
 * (firmware/common/cyphal/cyphal.c). Sending 1.1 would not deserialize there.
 */
#ifndef IGROW_CYPHAL_RX_H
#define IGROW_CYPHAL_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* `node_id` is only needed to send requests; a listener never uses it.
 * 1 is the gateway time master and 96/97 are the nodes on the bench bus, so
 * the default is clear of all three. */
#define PANEL_DEFAULT_NODE_ID 10U

void cyphal_rx_init(uint8_t local_node_id);

/* Drain the CAN receive FIFO, reassemble, decode, record. Call as often as
 * the main loop can: at 500 kbit/s a full bus is ~3800 frames/s and the FIFO
 * holds 64. */
void cyphal_rx_spin(void);

/* Frames that reassembled into a transfer the panel understands, and ones it
 * saw but had no subscription for. A high `unknown` count with a healthy
 * `accepted` count means the bus carries subjects this build does not know. */
uint32_t cyphal_rx_accepted(void);
uint32_t cyphal_rx_unknown(void);

/* Manual actions. Each returns false if the transmit queue could not take the
 * request -- in listen-only mode that is always. */
bool cyphal_request_getinfo(uint8_t target_node_id);
bool cyphal_send_command(uint8_t target_node_id, uint16_t command);
bool cyphal_send_restart(uint8_t target_node_id);

/* Publish a pre-serialized message. The caller owns `transfer_id`, one counter
 * per subject. Used only by the self-test: in normal operation the panel
 * publishes nothing at all (ADR-0030 d3). */
bool cyphal_publish(uint16_t subject_id, const uint8_t *payload, size_t size,
                    uint8_t *transfer_id);

/* Result of the last ExecuteCommand, for the UI to show. `pending` clears when
 * a response arrives or the request times out. */
typedef struct {
    bool     pending;
    bool     answered;
    uint8_t  target;
    uint16_t command;
    uint8_t  status;      /* uavcan.node.ExecuteCommand.Response status */
    uint32_t sent_ms;
} cyphal_cmd_result_t;

const cyphal_cmd_result_t *cyphal_last_command(void);

#endif /* IGROW_CYPHAL_RX_H */
