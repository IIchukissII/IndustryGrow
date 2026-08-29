/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "image.h"
#include "crc32.h"

bool image_slot_bootable(igrow_slot_t slot)
{
    const igrow_image_header_t *h = image_header(slot);

    /* An erased slot reads 0xFFFFFFFF, so the magic alone rejects one; the
     * length bound then keeps a corrupt header from pointing the CRC at flash
     * the slot does not own. */
    if ((h->magic != IGROW_IMAGE_MAGIC) ||
        (h->header_version != IGROW_IMAGE_HEADER_VERSION) ||
        (h->image_length == 0u) ||
        (h->image_length > IGROW_IMAGE_MAX_BODY) ||
        ((h->image_length % 4u) != 0u)) {
        return false;
    }

    /* d7: a header whose target class is not this hardware is refused. Checked
     * at boot as well as on download, because an image can also arrive over
     * SWD, where nothing checked it. */
    if (h->hardware_class != IGROW_HW_CLASS_E0001) {
        return false;
    }

    return crc32_mpeg2_region((const void *)image_body_addr(slot),
                              h->image_length) == h->body_crc32;
}
