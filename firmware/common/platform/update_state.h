/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_UPDATE_STATE_H
#define IGROW_PLATFORM_UPDATE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "partition.h"

/*
 * The update-state block: sector 4, the one thing both the bootloader and the
 * application write (ADR-0029 d1).
 *
 * It answers two questions and nothing else -- which slot boots, and whether
 * an update was asked for. The application never writes a slot (d3); it
 * records a request here and restarts into the bootloader.
 *
 * WHY A LOG AND NOT A RECORD. Flash cannot rewrite a word without erasing its
 * sector, and a 128 KB-class erase stalls the core for the order of a second
 * (common/platform/flash.h). Rewriting one record per boot would mean an erase
 * per boot. So the sector is an append log of 256-byte records, the highest
 * valid `seq` wins, and the erase comes once every 256 writes. An interrupted
 * append fails its CRC and leaves the previous record in force -- which is the
 * property that matters: a power loss during a state write cannot leave the
 * node without a boot decision.
 */

/* Trial-boot budget of d8: a slot that never confirms is reverted, and this
 * bounds how many times it may try before that happens. */
#define IGROW_UPDATE_TRIAL_ATTEMPTS 3u

/* uavcan.file.Path is 255 bytes; what fits beside the boot fields in one
 * record is 223 plus a terminator. Longer artifact paths are refused when the
 * request is recorded rather than truncated into a wrong path. */
#define IGROW_UPDATE_PATH_MAX 223u

typedef enum {
    IGROW_BOOT_CONFIRMED = 0, /* the slot has proven itself; boot it */
    IGROW_BOOT_TRIAL = 1,     /* newly written; revert if it does not confirm */
} igrow_boot_state_t;

typedef struct {
    igrow_slot_t boot_slot;      /* the slot the bootloader runs */
    igrow_slot_t prev_slot;      /* where a failed trial reverts to */
    igrow_boot_state_t state;
    uint8_t attempts;            /* trial boots left before the revert */
    bool request_pending;        /* an update was asked for (d3) */
    igrow_slot_t request_slot;   /* the slot it must be written into */
    uint8_t server_node_id;      /* who serves the artifact: the caller */
    char path[IGROW_UPDATE_PATH_MAX + 1u]; /* artifact path, NUL-terminated */
} update_state_t;

/* Read the block. Call once at boot, after clock_init(). A blank or unreadable
 * block yields the first-flash default: slot A, confirmed, no request -- which
 * is what an SWD-flashed node has, and why a first flash needs to write only
 * the bootloader and one slot. */
void update_state_load(void);

/* The state this boot is running under. Never NULL after update_state_load(). */
const update_state_t *update_state(void);

/* Confirm the trial image running in `running_slot` (ADR-0029 d12). Returns 1
 * if it confirmed, 0 if there was nothing to confirm -- the ordinary case, and
 * why this is cheap to call on every boot -- and -1 on a flash failure.
 *
 * Called by the application once bring-up has completed. An image that never
 * gets that far never calls it, which is what the trial state is for. */
int update_state_confirm(igrow_slot_t running_slot);

/* Record an update request and leave the boot decision alone: the target is
 * the slot that is NOT running, the server is the node that asked, and the
 * path is the artifact it will serve (ADR-0029 d3, d5). The caller restarts;
 * the bootloader acts on it.
 *
 * The server is the requester rather than a configured address because the
 * node that issues COMMAND_BEGIN_SOFTWARE_UPDATE is by definition reachable
 * and holds the artifact it named. Returns 0, or -1 as for the store below. */
int update_state_request(uint8_t server_node_id, const char *path);

/* Append `st` as the newest record. Returns 0, or -1 on a flash failure or a
 * path longer than IGROW_UPDATE_PATH_MAX.
 *
 * Blocks for a sector erase -- of the order of a second, interrupts masked --
 * on the one write in 256 that fills the log. */
int update_state_store(const update_state_t *st);

#endif /* IGROW_PLATFORM_UPDATE_STATE_H */
