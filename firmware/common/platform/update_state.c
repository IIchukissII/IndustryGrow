/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "update_state.h"
#include "crc32.h"
#include "flash.h"

#include <string.h>

#define IGROW_UPDATE_MAGIC   0x49475553u /* 'IGUS' */
#define IGROW_UPDATE_VERSION 1u

/* One record, 256 bytes: 64 words, the last of which is the CRC over the other
 * 63. The size is a divisor of the sector, so the log holds a whole number of
 * records and the scan needs no end marker. */
#define IGROW_UPDATE_RECORD_SIZE 256u
#define IGROW_UPDATE_RECORD_WORDS (IGROW_UPDATE_RECORD_SIZE / 4u)
#define IGROW_UPDATE_RECORD_COUNT (IGROW_UPDATE_STATE_SIZE / IGROW_UPDATE_RECORD_SIZE)

typedef struct {
    uint32_t magic;        /* 0x00 */
    uint32_t version;      /* 0x04 */
    uint32_t seq;          /* 0x08 monotonic; the highest valid record wins */
    uint8_t boot_slot;     /* 0x0C */
    uint8_t prev_slot;     /* 0x0D */
    uint8_t state;         /* 0x0E igrow_boot_state_t */
    uint8_t attempts;      /* 0x0F */
    uint8_t request;       /* 0x10 0 or 1 */
    uint8_t request_slot;  /* 0x11 */
    uint16_t path_len;     /* 0x12 bytes of path, excluding the terminator */
    char path[IGROW_UPDATE_PATH_MAX + 1u]; /* 0x14 */
    uint32_t reserved0;    /* 0xF4 */
    uint32_t reserved1;    /* 0xF8 */
    uint32_t crc;          /* 0xFC CRC-32/MPEG-2 over the 63 words above */
} update_record_t;

_Static_assert(sizeof(update_record_t) == IGROW_UPDATE_RECORD_SIZE,
               "update record must divide the sector");

static update_state_t s_state;
static uint32_t s_seq;   /* seq of the record in force, 0 if none */
static uint32_t s_index; /* its position in the log */

static const update_record_t *record_at(uint32_t i)
{
    return (const update_record_t *)(IGROW_UPDATE_STATE_ADDR +
                                     (i * IGROW_UPDATE_RECORD_SIZE));
}

/* An erased record: 256 bytes of 0xFF. Programming a word that is not erased
 * fails on the F4, so the append site is checked rather than assumed -- a
 * sector left half-written by an interrupted erase must not wedge the log. */
static bool record_erased(uint32_t i)
{
    const uint32_t *w = (const uint32_t *)record_at(i);
    for (uint32_t k = 0u; k < IGROW_UPDATE_RECORD_WORDS; k++) {
        if (w[k] != 0xFFFFFFFFu) {
            return false;
        }
    }
    return true;
}

static bool record_valid(const update_record_t *r)
{
    return (r->magic == IGROW_UPDATE_MAGIC) &&
           (r->version == IGROW_UPDATE_VERSION) &&
           (r->boot_slot < IGROW_SLOT_COUNT) &&
           (r->prev_slot < IGROW_SLOT_COUNT) &&
           (r->request_slot < IGROW_SLOT_COUNT) &&
           (r->path_len <= IGROW_UPDATE_PATH_MAX) &&
           (r->crc == crc32_mpeg2_words((const uint32_t *)r,
                                        IGROW_UPDATE_RECORD_WORDS - 1u));
}

void update_state_load(void)
{
    /* First-flash default. A node that has never been updated has a blank
     * sector 4 and runs slot A confirmed. */
    s_state.boot_slot = IGROW_SLOT_A;
    s_state.prev_slot = IGROW_SLOT_A;
    s_state.state = IGROW_BOOT_CONFIRMED;
    s_state.attempts = 0u;
    s_state.request_pending = false;
    s_state.request_slot = IGROW_SLOT_B;
    s_state.path[0] = '\0';
    s_seq = 0u;
    s_index = 0u;

    const update_record_t *newest = NULL;
    for (uint32_t i = 0u; i < IGROW_UPDATE_RECORD_COUNT; i++) {
        const update_record_t *r = record_at(i);
        if (record_valid(r) && ((newest == NULL) || (r->seq > newest->seq))) {
            newest = r;
            s_index = i;
        }
    }
    if (newest == NULL) {
        return;
    }

    s_seq = newest->seq;
    s_state.boot_slot = (igrow_slot_t)newest->boot_slot;
    s_state.prev_slot = (igrow_slot_t)newest->prev_slot;
    s_state.state = (newest->state == (uint8_t)IGROW_BOOT_TRIAL) ? IGROW_BOOT_TRIAL
                                                                 : IGROW_BOOT_CONFIRMED;
    s_state.attempts = newest->attempts;
    s_state.request_pending = (newest->request != 0u);
    s_state.request_slot = (igrow_slot_t)newest->request_slot;
    memcpy(s_state.path, newest->path, newest->path_len);
    s_state.path[newest->path_len] = '\0';
}

const update_state_t *update_state(void)
{
    return &s_state;
}

int update_state_store(const update_state_t *st)
{
    const size_t len = strlen(st->path);
    if (len > IGROW_UPDATE_PATH_MAX) {
        return -1;
    }

    update_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic = IGROW_UPDATE_MAGIC;
    rec.version = IGROW_UPDATE_VERSION;
    rec.seq = s_seq + 1u;
    rec.boot_slot = (uint8_t)st->boot_slot;
    rec.prev_slot = (uint8_t)st->prev_slot;
    rec.state = (uint8_t)st->state;
    rec.attempts = st->attempts;
    rec.request = st->request_pending ? 1u : 0u;
    rec.request_slot = (uint8_t)st->request_slot;
    rec.path_len = (uint16_t)len;
    memcpy(rec.path, st->path, len);
    rec.crc = crc32_mpeg2_words((const uint32_t *)&rec, IGROW_UPDATE_RECORD_WORDS - 1u);

    /* Append after the record in force. The log is full when that would run
     * off the end -- then, and only then, the sector is erased and the new
     * record starts it again. The record in force is the one being replaced,
     * so nothing is lost by the erase. */
    uint32_t index = (s_seq == 0u) ? 0u : (s_index + 1u);
    if ((index >= IGROW_UPDATE_RECORD_COUNT) || !record_erased(index)) {
        if (flash_erase_sector(IGROW_UPDATE_STATE_SECTOR) != 0) {
            return -1;
        }
        index = 0u;
    }

    const uint32_t addr = IGROW_UPDATE_STATE_ADDR + (index * IGROW_UPDATE_RECORD_SIZE);
    if (flash_program_words(addr, (const uint32_t *)&rec,
                            IGROW_UPDATE_RECORD_WORDS) != 0) {
        return -1;
    }

    s_seq = rec.seq;
    s_index = index;
    s_state = *st;
    return 0;
}
