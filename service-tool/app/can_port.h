/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Classic CAN at 500 kbit/s on FDCAN, for a bus the panel joins as a listener
 * first (ADR-0002 d8: 500 kbit/s classic, linear, CAN FD out of scope). The
 * peripheral runs in FDCAN_FRAME_CLASSIC; nothing here ever emits an FD frame,
 * because one FD frame would error-frame every bxCAN node on the bus.
 *
 * Frames are lifted out of the hardware FIFO in the FDCAN interrupt and queued
 * in a software ring, so a slow screen repaint cannot cost a frame. See the ring
 * comment in can_port.c for the timing that makes this necessary.
 *
 * Which pins the board's transceiver hangs off is NOT known from the BSP -- the
 * eval board brings CAN out on a Sub-D and the BSP has no CAN driver. So the
 * pin pair is a runtime choice, and can_hunt_*() sweeps the candidates in bus
 * monitoring mode until frames appear. Monitoring mode never drives the bus,
 * not even an ACK bit, so a sweep cannot disturb a bus that is working.
 */
#ifndef IGROW_CAN_PORT_H
#define IGROW_CAN_PORT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t tx_dropped;   /* TX FIFO was full */
    uint32_t rx_lost;      /* errored frames counted by the peripheral */
    uint32_t rx_lost_ring; /* frames the ISR had to drop: the main loop fell behind */
    uint8_t  last_error;   /* protocol status LEC */
    uint8_t  tx_errors;    /* TEC */
    uint8_t  rx_errors;    /* REC */
    bool     bus_off;
    bool     error_passive;
    bool     error_warning;
} can_stats_t;

/* Bus monitoring never drives the bus, not even an ACK. Internal loopback
 * never reaches the pins at all: a transmitted frame is delivered straight to
 * this node's own receive FIFO, which exercises everything above the
 * transceiver with no cabinet attached. */
typedef enum {
    CAN_MODE_LISTEN = 0, /* FDCAN_MODE_BUS_MONITORING */
    CAN_MODE_NORMAL,     /* on the bus, acking */
    CAN_MODE_LOOPBACK,   /* FDCAN_MODE_INTERNAL_LOOPBACK -- self-test only */
} can_mode_t;

typedef enum {
    CAN_HUNT_IDLE = 0,
    CAN_HUNT_RUNNING,
    CAN_HUNT_FOUND,
    CAN_HUNT_EXHAUSTED,
} can_hunt_state_t;

/* Bring the interface up on `profile` (index into the candidate list).
 * listen_only selects FDCAN_MODE_BUS_MONITORING. Returns false if the
 * peripheral refused the configuration. */
bool      can_init(uint8_t profile, bool listen_only); /* LISTEN or NORMAL */
bool      can_init_mode(uint8_t profile, can_mode_t mode);
can_mode_t can_mode(void);
void can_deinit(void);

/* One received frame, or false when the FIFO is empty. Extended IDs only --
 * Cyphal/CAN uses nothing else, and standard-ID frames are counted and
 * dropped so a foreign bus still shows a frame rate. */
bool can_rx(uint32_t *out_ext_id, uint8_t *out_data, uint8_t *out_len);

/* Frames sitting in the ring right now -- how far behind the main loop is.
 * Steady non-zero means the GUI is not draining fast enough; a climbing
 * rx_lost_ring means it already lost. */
uint16_t can_ring_depth(void);

/* Ring size, so a depth reads as a fraction of what there is. */
uint16_t can_ring_capacity(void);

/* Queue one extended-ID classic frame. Fails in listen-only mode, and when
 * the TX FIFO is full. */
bool can_tx(uint32_t ext_id, const uint8_t *data, uint8_t len);

const can_stats_t *can_stats(void);
void               can_stats_poll(void); /* refreshes the error counters */

uint8_t     can_profile_count(void);
const char *can_profile_name(uint8_t profile);
uint8_t     can_current_profile(void);
bool        can_is_listen_only(void);
bool        can_is_up(void);

/* Non-blocking sweep. Call can_hunt_start() once, then can_hunt_step() from
 * the main loop; it re-initialises the interface on each candidate, gives it
 * `dwell_ms`, and stops at the first one that receives a frame. The interface
 * is left up on whichever profile it stopped at. */
void             can_hunt_start(uint32_t dwell_ms);
can_hunt_state_t can_hunt_step(void);
/* Stop a sweep in progress. The sweep re-initialises the interface on each
 * candidate, so anything else that sets a mode must abort it first or the
 * sweep will overwrite that mode a moment later. */
void             can_hunt_abort(void);
can_hunt_state_t can_hunt_state(void);
uint8_t          can_hunt_current(void);

#endif /* IGROW_CAN_PORT_H */
