/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * Display and touch, and LVGL bound to them.
 *
 * Everything board-specific about the glass is behind this file. Which panel
 * that is comes from eval_board.h: MB1063 is 640x480 over LTDC parallel RGB
 * with an EXC7200, MB1166 is 800x480 over DSI with an OTM8009A and an FT6x06.
 * The BSP API is the same for both, so nothing outside this file knows which
 * is fitted -- or that a panel exists at all.
 *
 * Both init calls report failure rather than hanging. A panel that will not
 * come up must not take the CAN side down with it.
 */
#ifndef IGROW_LVGL_PORT_H
#define IGROW_LVGL_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "eval_board.h"

#define PANEL_W ((int32_t)IGROW_PANEL_W)
#define PANEL_H ((int32_t)IGROW_PANEL_H)

bool panel_sdram_init(void);
bool panel_display_init(void);
/* Same, but hands back the BSP return code so a failure can be told apart:
 * -4 peripheral (LTDC/SDRAM), -5 component, -7 unknown component (the panel
 * controller did not answer -- a different daughterboard). */
bool panel_display_init_rc(int32_t *out_rc);
/* Unwedge I2C1 before an LCD init attempt.
 *
 * BSP_LCD_InitEx identifies the daughterboard by READING THE TOUCH CONTROLLER'S
 * ID over I2C, so an I2C fault surfaces as BSP_ERROR_COMPONENT_FAILURE (-5) from
 * the LCD, which reads like a display problem and is not one. Observed on this
 * board: 2 boots in 5 failed that read, and retrying inside the same boot never
 * helped -- but a reset always did, because a reset re-initialises I2C1.
 *
 * The remedy is the standard one for a wedged I2C bus: if a slave is holding SDA
 * low mid-transfer, clock SCL by hand until it lets go, issue a STOP, and bring
 * the peripheral back up. Returns true if SDA was free at the end. */
bool panel_i2c_recover(void);

/* Tear the LCD back down so a failed init can be retried from a known
 * state rather than on top of a half-configured LTDC. */
void panel_display_deinit(void);

/* Blanks or restores the panel: LTDC, the DISP_EN pin and the backlight, all
 * through the BSP, which drives LCD_BL_CTRL (PA6) as a plain GPIO.
 *
 * There is deliberately no brightness control. PA6 is also TIM3_CH1, so PWM
 * dimming works -- but on this daughterboard the backlight driver is audible
 * when dimmed, worst at low duty, and no carrier from 1 kHz to 200 kHz silenced
 * it. That is the driver's boost converter bursting, which is a property of the
 * hardware and not fixable from here. Dimming was never a requirement, so it is
 * gone rather than tuned around. */
void panel_display_set(bool on);
bool panel_display_is_on(void);

bool panel_touch_init(void);
/* The motherboard's five-way, driving LVGL by focus. Call it before
 * lvgl_port_init(); the group the joystick steers has to be the default group
 * while ui_build() runs, or it ends up empty. */
bool panel_joy_init(void);
/* One vocabulary for every navigation input: the five-way, and the BACK button
 * next to it. Four directions that mean directions, one that means "do this",
 * one that means "undo the last one" -- and nothing that means two different
 * things depending on the axis.
 *
 * Routed through a hook rather than straight into LVGL as keys, because LVGL
 * navigates by group ORDER (creation order) and a person navigates by WHERE
 * THINGS ARE. The UI installs a handler that resolves a direction against the
 * layout; what it does not want, it declines and the port passes on as the
 * matching LV_KEY so LVGL's own widget handling still works. The port knows
 * nothing about tabs or screens. */
typedef enum {
    PANEL_NAV_LEFT,
    PANEL_NAV_RIGHT,
    PANEL_NAV_UP,
    PANEL_NAV_DOWN,
    PANEL_NAV_ENTER,
    PANEL_NAV_BACK
} panel_nav_t;

/* Return true if the UI consumed it, false to let LVGL have the key.
 *
 * All six come from the joystick alone: four directions, a CLICK on SEL for
 * ENTER, and a HOLD on SEL for BACK. Nothing needs a board push-button, so the
 * panel does not depend on an operator finding one. */
void lvgl_port_set_nav_cb(bool (*cb)(panel_nav_t));
/* Raise one by hand, for a test or another input. */
void lvgl_port_nav(panel_nav_t ev);
/* Presses seen since boot, per input. Reported by the console 'c' command so
 * that "does the fitted panel actually register a finger" is answered by a
 * number rather than by whether BSP_TS_Init returned OK. */
void panel_input_counts(uint32_t *touch, uint32_t *joy);

/* Only meaningful once panel_display_init() has returned true. */
void lvgl_port_init(void);
void lvgl_port_tick_inc(uint32_t ms); /* from the SysTick handler */
void lvgl_port_spin(void);            /* from the main loop */

/* Redraw the whole screen every pass: the worst-case repaint SERVICE-TOOL V1
 * requires the receive path to survive. Costs most of the CPU; leave it off. */
void lvgl_port_repaint_storm(bool on);
bool lvgl_port_repaint_storm_is_on(void);

#endif /* IGROW_LVGL_PORT_H */
