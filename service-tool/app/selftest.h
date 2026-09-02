/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Bus-less self-test: exercises the whole receive chain with no cabinet, no
 * transceiver and no wiring.
 *
 * The FDCAN peripheral is put in internal loopback, so a frame this module
 * publishes comes back through the same path a real one would: hardware FIFO,
 * the receive interrupt, the software ring, libcanard reassembly, the DSDL
 * decoders, the model, the UI. Everything above the transceiver is covered.
 *
 * That makes the liveness verifications runnable without a bus (spec V2, V3,
 * V14) and separates two failures that otherwise look identical: if loopback
 * passes and the real bus does not, the fault is physical.
 *
 * The data is synthetic. It is published from this node's own ID and the panel
 * annunciates SELF-TEST for as long as it runs, because a fabricated
 * temperature that reads like a measurement is the worst thing this could do.
 * Entering and leaving both clear the model.
 */
#ifndef IGROW_SELFTEST_H
#define IGROW_SELFTEST_H

#include <stdbool.h>
#include <stdint.h>

/* Toggles internal loopback and the synthetic publishers together; neither is
 * useful without the other. Returns the state it left. */
bool selftest_toggle(void);
bool selftest_active(void);

/* Publishes on schedule. Call from the main loop. */
void selftest_spin(void);

/* Signals published while active, and their periods:
 *
 *   air temperature   1 Hz   forever
 *   bus voltage       2 Hz   forever
 *   air humidity      1 Hz   for the first 20 s only
 *
 * The third stops on its own so LATE and then STALE are reached without anyone
 * having to unplug anything -- that is spec V2 and V3 in one run. */
#define SELFTEST_DROP_AFTER_MS 20000U

#endif /* IGROW_SELFTEST_H */
