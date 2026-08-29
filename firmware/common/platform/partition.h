/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_PARTITION_H
#define IGROW_PLATFORM_PARTITION_H

#include <stdbool.h>
#include <stdint.h>

/*
 * The flash partition of ADR-0029 d1, in C.
 *
 * The STM32F405RG has one 1 MB bank of 12 sectors -- 16 KB x4, 64 KB x1,
 * 128 KB x7 (RM0090 Table 5). Erase granularity is the sector, so every
 * boundary below is a sector edge and no two regions share one.
 *
 *   sectors 0-3    0x08000000   64 KB   bootloader, written over SWD only
 *   sector  4      0x08010000   64 KB   update state
 *   sectors 5-7    0x08020000  384 KB   slot A
 *   sectors 8-10   0x08080000  384 KB   slot B
 *   sector  11     0x080E0000  128 KB   Node-ID store -- common/carrier/identity.h
 *
 * The identity sector is named here for the map to read whole; its constants
 * belong to identity.h and are not repeated. It is outside every update write
 * path, which is what makes ADR-0027 d4 a property of the image rather than of
 * the operator.
 *
 * The linker scripts carry the same three code regions. update_state.c checks
 * its own against the linker's rather than trusting them to stay in step.
 */

#define IGROW_BOOT_ADDR         0x08000000u
#define IGROW_BOOT_SIZE         (64u * 1024u)
#define IGROW_BOOT_SECTOR_FIRST 0u
#define IGROW_BOOT_SECTOR_LAST  3u

#define IGROW_UPDATE_STATE_ADDR   0x08010000u
#define IGROW_UPDATE_STATE_SIZE   (64u * 1024u)
#define IGROW_UPDATE_STATE_SECTOR 4u

#define IGROW_SLOT_SIZE           (384u * 1024u)
#define IGROW_SLOT_A_ADDR         0x08020000u
#define IGROW_SLOT_A_SECTOR_FIRST 5u
#define IGROW_SLOT_A_SECTOR_LAST  7u
#define IGROW_SLOT_B_ADDR         0x08080000u
#define IGROW_SLOT_B_SECTOR_FIRST 8u
#define IGROW_SLOT_B_SECTOR_LAST  10u

/* Which slot, not where it is: the pair is an index everywhere it is stored
 * (update-state record) or reported (diagnostics), and an address only when
 * something is read from it. */
typedef enum {
    IGROW_SLOT_A = 0,
    IGROW_SLOT_B = 1,
} igrow_slot_t;

#define IGROW_SLOT_COUNT 2u

static inline uint32_t partition_slot_addr(igrow_slot_t slot)
{
    return (slot == IGROW_SLOT_B) ? IGROW_SLOT_B_ADDR : IGROW_SLOT_A_ADDR;
}

static inline uint8_t partition_slot_first_sector(igrow_slot_t slot)
{
    return (slot == IGROW_SLOT_B) ? (uint8_t)IGROW_SLOT_B_SECTOR_FIRST
                                  : (uint8_t)IGROW_SLOT_A_SECTOR_FIRST;
}

static inline uint8_t partition_slot_last_sector(igrow_slot_t slot)
{
    return (slot == IGROW_SLOT_B) ? (uint8_t)IGROW_SLOT_B_SECTOR_LAST
                                  : (uint8_t)IGROW_SLOT_A_SECTOR_LAST;
}

static inline igrow_slot_t partition_other_slot(igrow_slot_t slot)
{
    return (slot == IGROW_SLOT_A) ? IGROW_SLOT_B : IGROW_SLOT_A;
}

/* The slot an address falls in. A running application asks this of its own
 * vector table to learn which slot it is executing from -- nothing tells it
 * otherwise, and the answer decides which slot an update may be written to. */
static inline bool partition_slot_of(uint32_t addr, igrow_slot_t *slot)
{
    if ((addr >= IGROW_SLOT_A_ADDR) && (addr < (IGROW_SLOT_A_ADDR + IGROW_SLOT_SIZE))) {
        *slot = IGROW_SLOT_A;
        return true;
    }
    if ((addr >= IGROW_SLOT_B_ADDR) && (addr < (IGROW_SLOT_B_ADDR + IGROW_SLOT_SIZE))) {
        *slot = IGROW_SLOT_B;
        return true;
    }
    return false;
}

static inline const char *partition_slot_str(igrow_slot_t slot)
{
    return (slot == IGROW_SLOT_B) ? "B" : "A";
}

#endif /* IGROW_PLATFORM_PARTITION_H */
