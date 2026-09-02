/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The screens. Four tabs, in the order they are needed at a bench:
 *
 *   Bus    -- is the interface up, on which pins, in which mode, and is the
 *             bus healthy. This is the tab that answers "why do I see nothing".
 *   Nodes  -- who is on the bus, and the two manual actions (GetInfo, restart).
 *   Live   -- one signal, plotted over its last 240 samples.
 *   Values -- everything at once, one row per signal.
 *
 * Nothing here reads a peripheral. The UI reads the model and calls into
 * can_port / cyphal_rx; that is the whole coupling.
 */
#ifndef IGROW_UI_MAIN_H
#define IGROW_UI_MAIN_H

void ui_build(void);

#endif /* IGROW_UI_MAIN_H */
