/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_BOOT_UPDATE_H
#define IGROW_BOOT_UPDATE_H

#include <stdbool.h>

/*
 * The download half of ADR-0029: the bootloader as a Cyphal node (d4) pulling
 * an artifact with uavcan.file.Read (d5), verifying it (d6) and marking its
 * slot bootable on trial (d8).
 *
 * Both entry points below bring the bus up themselves and never return with it
 * running: the node either restarts into the image it just wrote, or hands the
 * bus back and boots what it already had.
 */

/* Act on the request recorded in the update-state block. Returns true when a
 * new slot was written, verified and marked -- the caller restarts. Returns
 * false when the update failed, having cleared the request so the failure does
 * not repeat on every boot; the previous image is untouched either way. */
bool update_run_request(void);

/* No slot holds a bootable image. Join the bus and wait to be given one.
 *
 * The alternative is halting, which needs an operator with an SWD probe at the
 * node. The bootloader is not itself an update target over the bus (d10), but
 * nothing stops it accepting an image for a slot -- and a node that answers
 * GetInfo while it waits can at least be found. Never returns: the update
 * request arrives as an ExecuteCommand and restarts the node. */
void update_await_image(void);

#endif /* IGROW_BOOT_UPDATE_H */
