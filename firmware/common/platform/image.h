/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_IMAGE_H
#define IGROW_PLATFORM_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "partition.h"

/*
 * The slot image: a 512-byte header followed by the application binary.
 * ADR-0029 d7 fixes what the header carries and leaves the byte layout to
 * implementation; this file is that specification, and firmware/tools/
 * mkimage.py is its only writer.
 *
 * 512 bytes is not padding for its own sake. The application's vector table
 * follows the header, and VTOR ignores its low 9 bits: a table of 98 entries
 * needs 512-byte alignment, so the header is exactly one alignment unit and
 * the table lands at slot base + 512 with nothing between.
 *
 * What is covered by what:
 *
 *   digest    = SHA-256 over the body, `image_length` bytes at IMAGE_BODY(slot)
 *   signature = ECDSA P-256 over SHA-256 of the header up to `signature`
 *
 * The signed region therefore contains the digest, which binds the body
 * without the header having to sign itself (ADR-0029 d6). `body_crc32` is a
 * separate, cheap integrity check: the signature is verified once on download,
 * the CRC on every boot (d8), and the two answer different questions --
 * whether the artifact is ours, and whether the flash still holds it.
 */

#define IGROW_IMAGE_MAGIC          0x4947494Du /* 'IGIM' */
#define IGROW_IMAGE_HEADER_VERSION 1u
#define IGROW_IMAGE_HEADER_SIZE    512u
#define IGROW_IMAGE_DIGEST_LEN     32u
#define IGROW_IMAGE_SIGNATURE_LEN  64u /* P-256 r||s, big-endian */

/* Target hardware class (d7). The carrier is the hardware the image runs on --
 * one image serves every sensor module because the module is selected at
 * runtime by the strap (ADR-0017 d16), so the class names the carrier and
 * matches the GetInfo hardware_version.major the node reports. */
#define IGROW_HW_CLASS_E0001 1u

/* Largest body a slot can hold. */
#define IGROW_IMAGE_MAX_BODY (IGROW_SLOT_SIZE - IGROW_IMAGE_HEADER_SIZE)

typedef struct {
    uint32_t magic;          /* 0x00 IGROW_IMAGE_MAGIC */
    uint32_t header_version; /* 0x04 IGROW_IMAGE_HEADER_VERSION */
    uint32_t image_length;   /* 0x08 body bytes, multiple of 4 */
    uint32_t hardware_class; /* 0x0C IGROW_HW_CLASS_E0001 */
    uint16_t version_major;  /* 0x10 image version, as GetInfo reports it */
    uint16_t version_minor;  /* 0x12 */
    uint32_t reserved0;      /* 0x14 zero; keeps vcs_revision_id 8-aligned */
    uint64_t vcs_revision_id;/* 0x18 git commit, 0 when not a released build */
    uint32_t body_crc32;     /* 0x20 CRC-32/MPEG-2 over the body (crc32.h) */
    uint32_t reserved1;      /* 0x24 zero */
    uint8_t  digest[IGROW_IMAGE_DIGEST_LEN];       /* 0x28 SHA-256 of the body */
    uint8_t  signature[IGROW_IMAGE_SIGNATURE_LEN]; /* 0x48 detached, see above */
    uint8_t  pad[IGROW_IMAGE_HEADER_SIZE - 0x88u]; /* 0x88 zero to 512 */
} igrow_image_header_t;

_Static_assert(sizeof(igrow_image_header_t) == IGROW_IMAGE_HEADER_SIZE,
               "image header must be exactly one VTOR alignment unit");

/* The bytes the signature covers: the header up to the signature field. */
#define IGROW_IMAGE_SIGNED_LEN 0x48u

/* Where a slot's header and body sit. */
static inline const igrow_image_header_t *image_header(igrow_slot_t slot)
{
    return (const igrow_image_header_t *)partition_slot_addr(slot);
}

static inline uint32_t image_body_addr(igrow_slot_t slot)
{
    return partition_slot_addr(slot) + IGROW_IMAGE_HEADER_SIZE;
}

/* Whether the slot holds an image this node may run: header well-formed, body
 * length sane, hardware class ours, and the body matching its CRC. The
 * signature is NOT checked here -- it is checked once, on download, before the
 * slot is marked bootable (ADR-0029 d6, d8). */
bool image_slot_bootable(igrow_slot_t slot);

#endif /* IGROW_PLATFORM_IMAGE_H */
