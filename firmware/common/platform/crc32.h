/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_CRC32_H
#define IGROW_PLATFORM_CRC32_H

#include <stdint.h>

/*
 * CRC-32/MPEG-2 from the on-chip unit: poly 0x04C11DB7, init 0xFFFFFFFF, no
 * reflection, no final XOR. The peripheral is fed 32-bit words and processes
 * each word most-significant BYTE first, so a little-endian buffer is covered
 * in byte order b3,b2,b1,b0 per word. Anything computing the same value on a
 * host must byte-swap each word (firmware/tools/mkimage.py does).
 *
 * One definition, three users: the Node-ID record (ADR-0027), the update-state
 * record and the image body check at boot (ADR-0029 d8). No software table --
 * the unit is otherwise unused and costs 4 cycles per word.
 */

/* CRC over `count` words at `words`. */
uint32_t crc32_mpeg2_words(const uint32_t *words, uint32_t count);

/* CRC over a byte range, which must start word-aligned and be a whole number
 * of words. Used for image bodies, which mkimage.py pads to a word. */
uint32_t crc32_mpeg2_region(const void *addr, uint32_t bytes);

/* The same CRC over more than one region. A record that carries its own CRC has
 * a hole where that field sits, so the value covers two ranges rather than one
 * -- M04's served frame header and its flat-field record both do (M04 spec
 * 6.7, 10.2). Each region must start word-aligned and be a whole number of
 * words; the unit holds the running value between calls.
 *
 *   crc32_mpeg2_begin(); crc32_mpeg2_feed(a, na); crc32_mpeg2_feed(b, nb);
 *   uint32_t crc = crc32_mpeg2_end();
 */
void crc32_mpeg2_begin(void);
void crc32_mpeg2_feed(const void *addr, uint32_t bytes);
uint32_t crc32_mpeg2_end(void);

#endif /* IGROW_PLATFORM_CRC32_H */
