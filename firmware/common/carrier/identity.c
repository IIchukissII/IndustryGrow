/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "identity.h"
#include "crc32.h"
#include "e0001.h" /* CMSIS device header */
#if IGROW_CARRIER_000100
#include "clock.h"
#include "i2c.h"
#include <string.h>
#else
#include "flash.h"
#endif

/* The record. Four words, so the layout reads as itself rather than as a
 * bit-packed word; 16 bytes of a 128 KB sector costs nothing.
 *
 * A partially written sector must be detectable (ADR-0027, Consequences). The
 * magic already rejects a blank sector -- erased flash reads 0xFFFFFFFF -- and
 * the CRC covers an interrupted program, so a commit cut short by a power loss
 * reads back as UNPROVISIONED and never as some other node's identity. The same
 * holds for the EEPROM of E0001-000100, which also reads 0xFF when blank. */
#define IGROW_IDENTITY_MAGIC   0x49474E44u /* 'IGND' */
#define IGROW_IDENTITY_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t node_id;
    uint32_t crc; /* CRC-32/MPEG-2 over the three words above */
} identity_record_t;

static uint8_t s_running = IGROW_NODE_ID_UNPROVISIONED;
static uint8_t s_committed = IGROW_NODE_ID_UNPROVISIONED;

/* The Node-ID a record names, or IGROW_NODE_ID_UNPROVISIONED if it is not a
 * valid record. */
static uint8_t validate(const identity_record_t *r)
{
    const uint32_t body[3] = {r->magic, r->version, r->node_id};
    if ((r->magic == IGROW_IDENTITY_MAGIC) &&
        (r->version == IGROW_IDENTITY_VERSION) &&
        (r->node_id <= IGROW_NODE_ID_MAX_PROVISIONABLE) &&
        (r->crc == crc32_mpeg2_words(body, 3u))) {
        return (uint8_t)r->node_id;
    }
    return IGROW_NODE_ID_UNPROVISIONED;
}

#if IGROW_CARRIER_000100

/* U4 on the header I2C. The record is stored as the struct's bytes, little-
 * endian as the core lays it out, so one validate() serves both stores.
 *
 * The bus is shared with the module, and a module that holds it leaves the
 * read failing. That reads as UNPROVISIONED -- the node joins as 127 and says
 * so -- rather than as any number it would have to guess. A few attempts cover
 * a transfer lost to a glitch without hiding a bus that stays down. */
#define EEPROM_ATTEMPTS     3u
#define EEPROM_WRITE_POLL_MS 10u /* M24C64 tW is 5 ms max */

static bool s_read_failed;

static uint8_t read_stored(void)
{
    const uint8_t ptr[2] = {(uint8_t)(IGROW_IDENTITY_EEPROM_WORD >> 8),
                            (uint8_t)IGROW_IDENTITY_EEPROM_WORD};
    identity_record_t r;
    for (uint32_t i = 0u; i < EEPROM_ATTEMPTS; i++) {
        if (i2c_write_read(IGROW_IDENTITY_EEPROM_ADDR, ptr, sizeof(ptr),
                           (uint8_t *)&r, sizeof(r)) == 0) {
            s_read_failed = false;
            return validate(&r);
        }
    }
    s_read_failed = true;
    return IGROW_NODE_ID_UNPROVISIONED;
}

/* One page write, then the acknowledge poll that ends the write cycle. Clearing
 * writes an erased-looking record, which the magic rejects. */
static int write_stored(uint8_t node_id)
{
    uint8_t buf[2u + sizeof(identity_record_t)];
    buf[0] = (uint8_t)(IGROW_IDENTITY_EEPROM_WORD >> 8);
    buf[1] = (uint8_t)IGROW_IDENTITY_EEPROM_WORD;
    if (node_id == IGROW_NODE_ID_UNPROVISIONED) {
        memset(&buf[2], 0xFF, sizeof(identity_record_t));
    } else {
        identity_record_t rec;
        rec.magic = IGROW_IDENTITY_MAGIC;
        rec.version = IGROW_IDENTITY_VERSION;
        rec.node_id = node_id;
        rec.crc = crc32_mpeg2_words((const uint32_t *)&rec, 3u);
        memcpy(&buf[2], &rec, sizeof(rec));
    }
    if (i2c_write(IGROW_IDENTITY_EEPROM_ADDR, buf, sizeof(buf)) != 0) {
        return -1;
    }
    for (uint32_t ms = 0u; ms < EEPROM_WRITE_POLL_MS; ms++) {
        delay_ms(1u);
        if (i2c_probe(IGROW_IDENTITY_EEPROM_ADDR)) {
            return 0;
        }
    }
    return -1;
}

void identity_init(void)
{
    s_committed = read_stored();
    /* Latched: the Node-ID the transport is brought up with cannot change while
     * the node runs, whatever a later commit writes (ADR-0027 d5). */
    s_running = s_committed;
}

int identity_commit(uint8_t node_id)
{
    if (node_id > IGROW_NODE_ID_UNPROVISIONED) {
        return -1;
    }
    (void)write_stored(node_id);
    /* Read back through the store's own validation, so a commit reports success
     * only when the next boot will accept what was written. */
    s_committed = read_stored();
    return (!s_read_failed && (s_committed == node_id)) ? 0 : -1;
}

const char *identity_state_str(void)
{
    if (s_read_failed) {
        return "UNPROVISIONED (Node-ID store U4 at 0x57 not answering)";
    }
    return identity_provisioned() ? "provisioned"
                                  : "UNPROVISIONED (no Node-ID in U4)";
}

#else /* E0001-000003: the last flash sector */

/* Reserved by the linker script. Checked against IGROW_IDENTITY_FLASH_ADDR at
 * init: the two describe one range, and drifting apart would put the store
 * inside the application image. */
extern uint32_t _identity_start;

static bool s_range_conflict;

/* The Node-ID the sector holds, or IGROW_NODE_ID_UNPROVISIONED if it holds no
 * valid record. Reads flash and computes a CRC, so callers use the cached
 * accessors below rather than this. */
static uint8_t read_stored(void)
{
    if (s_range_conflict) {
        return IGROW_NODE_ID_UNPROVISIONED;
    }
    return validate((const identity_record_t *)IGROW_IDENTITY_FLASH_ADDR);
}

void identity_init(void)
{
    s_range_conflict = ((uint32_t)&_identity_start != IGROW_IDENTITY_FLASH_ADDR);
    s_committed = read_stored();
    /* Latched: the Node-ID the transport is brought up with cannot change while
     * the node runs, whatever a later commit writes (ADR-0027 d5). */
    s_running = s_committed;
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

#endif /* IGROW_CARRIER_000100 */

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
