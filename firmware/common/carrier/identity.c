/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "identity.h"
#include "crc32.h"
#include "flash.h"
#include "e0001.h" /* CMSIS device header */

/* The record. Four words, so the layout reads as itself rather than as a
 * bit-packed word; 16 bytes of a 128 KB sector costs nothing.
 *
 * A partially written sector must be detectable (ADR-0027, Consequences). The
 * magic already rejects a blank sector -- erased flash reads 0xFFFFFFFF -- and
 * the CRC covers an interrupted program, so a commit cut short by a power loss
 * reads back as UNPROVISIONED and never as some other node's identity. */
#define IGROW_IDENTITY_MAGIC   0x49474E44u /* 'IGND' */
#define IGROW_IDENTITY_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t node_id;
    uint32_t crc; /* CRC-32/MPEG-2 over the three words above */
} identity_record_t;

/* Reserved by the linker script. Checked against IGROW_IDENTITY_FLASH_ADDR at
 * init: the two describe one range, and drifting apart would put the store
 * inside the application image. */
extern uint32_t _identity_start;

static uint8_t s_running = IGROW_NODE_ID_UNPROVISIONED;
static uint8_t s_committed = IGROW_NODE_ID_UNPROVISIONED;
static bool s_range_conflict;

/* The Node-ID the sector holds, or IGROW_NODE_ID_UNPROVISIONED if it holds no
 * valid record. Reads flash and computes a CRC, so callers use the cached
 * accessors below rather than this. */
static uint8_t read_stored(void)
{
    if (s_range_conflict) {
        return IGROW_NODE_ID_UNPROVISIONED;
    }
    const identity_record_t *r = (const identity_record_t *)IGROW_IDENTITY_FLASH_ADDR;
    const uint32_t body[3] = {r->magic, r->version, r->node_id};
    if ((r->magic == IGROW_IDENTITY_MAGIC) &&
        (r->version == IGROW_IDENTITY_VERSION) &&
        (r->node_id <= IGROW_NODE_ID_MAX_PROVISIONABLE) &&
        (r->crc == crc32_mpeg2_words(body, 3u))) {
        return (uint8_t)r->node_id;
    }
    return IGROW_NODE_ID_UNPROVISIONED;
}

void identity_init(void)
{
    s_range_conflict = ((uint32_t)&_identity_start != IGROW_IDENTITY_FLASH_ADDR);
    s_committed = read_stored();
    /* Latched: the Node-ID the transport is brought up with cannot change while
     * the node runs, whatever a later commit writes (ADR-0027 d5). */
    s_running = s_committed;
}

uint8_t identity_node_id(void)
{
    return s_running;
}

bool identity_provisioned(void)
{
    return s_running != IGROW_NODE_ID_UNPROVISIONED;
}

uint8_t identity_committed_node_id(void)
{
    return s_committed;
}

bool identity_commit_pending(void)
{
    return s_committed != s_running;
}

int identity_commit(uint8_t node_id)
{
    if (s_range_conflict || (node_id > IGROW_NODE_ID_UNPROVISIONED)) {
        return -1;
    }
    if (flash_erase_sector(IGROW_IDENTITY_FLASH_SECTOR) != 0) {
        s_committed = read_stored();
        return -1;
    }
    if (node_id != IGROW_NODE_ID_UNPROVISIONED) {
        identity_record_t rec;
        rec.magic = IGROW_IDENTITY_MAGIC;
        rec.version = IGROW_IDENTITY_VERSION;
        rec.node_id = node_id;
        rec.crc = crc32_mpeg2_words((const uint32_t *)&rec, 3u);
        (void)flash_program_words(IGROW_IDENTITY_FLASH_ADDR, (const uint32_t *)&rec, 4u);
    }
    /* Read back through the store's own validation, so a commit reports success
     * only when the next boot will accept what was written. An erased sector is
     * the empty store, which is what IGROW_NODE_ID_UNPROVISIONED asks for. */
    s_committed = read_stored();
    return (s_committed == node_id) ? 0 : -1;
}

const char *identity_state_str(void)
{
    if (s_range_conflict) {
        return "STORE RANGE CONFLICT (linker script vs identity.h)";
    }
    return identity_provisioned() ? "provisioned"
                                  : "UNPROVISIONED (no Node-ID in flash)";
}
