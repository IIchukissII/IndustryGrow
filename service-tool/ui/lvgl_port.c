/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "lvgl_port.h"

#include <string.h>

#include "console.h"
#include "lvgl.h"
#include "panel_mem.h"
#include "eval_board.h"
#include "stm32h7xx_hal.h"

static bool          s_display_up;
static bool          s_touch_up;
static lv_display_t *s_disp;
static lv_group_t   *s_group;

bool panel_sdram_init(void)
{
    /* BSP_LCD_Init brings the SDRAM up itself, but the model and the LVGL heap
     * both live there and must survive a display that never initialises. */
    if (BSP_SDRAM_Init(0) != BSP_ERROR_NONE) {
        return false;
    }

    /* The BSP's refresh counter is a constant for a 100 MHz SDRAM clock, and
     * the SDRAM clock is HCLK/2. When the board comes up on the reduced clock
     * set (main.c: a warm boot cannot reach voltage Scale 1) that constant
     * refreshes every row half as often as the part requires -- a slow
     * corruption of the framebuffer, not a failure to initialise. Scale ST's
     * known-good value by the clock that is actually running. */
    const uint32_t sdclk = HAL_RCC_GetHCLKFreq() / 2U;
    if (sdclk != 100000000U) {
        const uint32_t count = (uint32_t)(((uint64_t)REFRESH_COUNT * sdclk) / 100000000U);
        (void)HAL_SDRAM_ProgramRefreshRate(&hsdram[0], count);
        console_printf("SDRAM   : refresh %lu for a %lu Hz SDRAM clock\r\n", (unsigned long)count,
                       (unsigned long)sdclk);
    }
    return true;
}

bool panel_display_init(void)
{
    int32_t rc;
    return panel_display_init_rc(&rc);
}

bool panel_display_init_rc(int32_t *out_rc)
{
    const int32_t rc =
        BSP_LCD_InitEx(0, LCD_ORIENTATION_LANDSCAPE, LCD_PIXEL_FORMAT_RGB565, PANEL_W, PANEL_H);
    if (out_rc != NULL) {
        *out_rc = rc;
    }
    if (rc != BSP_ERROR_NONE) {
        return false;
    }
    (void)BSP_LCD_SetActiveLayer(0, 0);
    (void)BSP_LCD_DisplayOn(0);
    s_display_up = true;
    return true;
}

/* Free a wedged I2C1 bus WITHOUT going through BSP_I2C1_DeInit().
 *
 * That function must not be called: it decrements its reference counter twice
 *
 *     I2c1InitCounter--;
 *     if (--I2c1InitCounter == 0U) { ... }
 *
 * so from a counter of 0 it underflows to 0xFFFFFFFE, and BSP_I2C1_Init() only
 * does anything when the counter is 0 -- after one DeInit, I2C1 can never be
 * initialised again. Calling it here took the display from 3 boots in 5 to 0 in
 * 8. Left in the ST BSP as found; worked around, not patched.
 *
 * So this touches only the pins: park them as GPIO, clock SCL until the slave
 * releases SDA, issue a STOP, and hand them back exactly as I2C1_MspInit had
 * them (AF open-drain, no pull, high speed -- the board has the pull-ups). */
static void i2c1_pins_af(void)
{
    GPIO_InitTypeDef g = {0};
    g.Mode             = GPIO_MODE_AF_OD;
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_HIGH;
    g.Alternate        = BUS_I2C1_SCL_AF;
    g.Pin              = BUS_I2C1_SCL_PIN;
    HAL_GPIO_Init(BUS_I2C1_SCL_GPIO_PORT, &g);
    g.Alternate = BUS_I2C1_SDA_AF;
    g.Pin       = BUS_I2C1_SDA_PIN;
    HAL_GPIO_Init(BUS_I2C1_SDA_GPIO_PORT, &g);
}

bool panel_i2c_recover(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode             = GPIO_MODE_OUTPUT_OD; /* open drain: a released line pulls up */
    g.Pull             = GPIO_NOPULL;
    g.Speed            = GPIO_SPEED_FREQ_LOW;
    g.Pin              = BUS_I2C1_SCL_PIN;
    HAL_GPIO_Init(BUS_I2C1_SCL_GPIO_PORT, &g);
    g.Pin = BUS_I2C1_SDA_PIN;
    HAL_GPIO_Init(BUS_I2C1_SDA_GPIO_PORT, &g);

    HAL_GPIO_WritePin(BUS_I2C1_SDA_GPIO_PORT, BUS_I2C1_SDA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(BUS_I2C1_SCL_GPIO_PORT, BUS_I2C1_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    /* Nine clocks is one byte plus the ACK: enough for a slave stuck part way
     * through a transfer to finish it and let SDA go. */
    for (unsigned i = 0; i < 9U; i++) {
        if (HAL_GPIO_ReadPin(BUS_I2C1_SDA_GPIO_PORT, BUS_I2C1_SDA_PIN) == GPIO_PIN_SET) {
            break;
        }
        HAL_GPIO_WritePin(BUS_I2C1_SCL_GPIO_PORT, BUS_I2C1_SCL_PIN, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(BUS_I2C1_SCL_GPIO_PORT, BUS_I2C1_SCL_PIN, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    /* STOP: SDA low->high while SCL is high, so anything still listening goes
     * back to idle instead of into the middle of the next transfer. */
    HAL_GPIO_WritePin(BUS_I2C1_SDA_GPIO_PORT, BUS_I2C1_SDA_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(BUS_I2C1_SCL_GPIO_PORT, BUS_I2C1_SCL_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(BUS_I2C1_SDA_GPIO_PORT, BUS_I2C1_SDA_PIN, GPIO_PIN_SET);
    HAL_Delay(1);

    const bool freed =
        (HAL_GPIO_ReadPin(BUS_I2C1_SDA_GPIO_PORT, BUS_I2C1_SDA_PIN) == GPIO_PIN_SET);

    i2c1_pins_af();
    return freed;
}

void panel_display_deinit(void)
{
    (void)BSP_LCD_DeInit(0);
    s_display_up = false;
}

bool panel_touch_init(void)
{
    TS_Init_t init;
    init.Width       = PANEL_W;
    init.Height      = PANEL_H;
    init.Orientation = TS_SWAP_NONE;
    init.Accuracy    = 5;
    s_touch_up       = (BSP_TS_Init(0, &init) == BSP_ERROR_NONE);
    return s_touch_up;
}

/* The five-way on the motherboard, as a second way to drive the UI.
 *
 * It is not a fallback for a missing touch panel -- it is the input that is on
 * the board itself rather than on the removable daughterboard, so it works
 * with any panel fitted and with none. It hangs off the MFX IO expander on
 * I2C1, the same bus the touch controller uses. */
static bool     s_joy_up;
static uint32_t s_joy_idle; /* the pin pattern with nothing pressed */

/* THE PINS ARE READ RAW, NOT THROUGH BSP_JOY_GetState().
 *
 * That function tests `(pins | SEL_PIN) == mask` first, and with nothing
 * pressed every pin is at its pull-up so `pins` is already the whole mask --
 * which satisfies that test. Polled, it therefore reports SEL held down
 * forever; observed as a permanent ENTER into LVGL on the first boot with the
 * joystick attached. ST's own examples drive it from an EXTI callback, where a
 * state that never changes never fires, so the ordering never shows there.
 *
 * The idle pattern is sampled once at init and a press is a DEVIATION from it,
 * which needs no assumption about polarity -- the same lesson as
 * BSP_PB_GetState, where a level alone never means "pressed" and only a change
 * does. */
bool panel_joy_init(void)
{
    if (BSP_JOY_Init(JOY1, JOY_MODE_GPIO, JOY_ALL) != BSP_ERROR_NONE) {
        s_joy_up = false;
        return false;
    }
    const int32_t idle = BSP_IO_ReadPin(0, JOY1_ALL_PIN);
    if (idle < 0) {
        s_joy_up = false;
        return false;
    }
    s_joy_idle = (uint32_t)idle & JOY1_ALL_PIN;
    s_joy_up   = true;
    return true;
}

static bool s_display_on = true;

void panel_display_set(bool on)
{
    s_display_on = on;
    if (on) {
        (void)BSP_LCD_DisplayOn(0);
    } else {
        (void)BSP_LCD_DisplayOff(0);
    }
}

bool panel_display_is_on(void)
{
    return s_display_on;
}

/* --- LVGL glue ------------------------------------------------------------ */

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t x1 = area->x1;
    const int32_t y1 = area->y1;
    const int32_t w  = area->x2 - area->x1 + 1;
    const int32_t h  = area->y2 - area->y1 + 1;

    uint16_t       *fb  = (uint16_t *)PANEL_FB0_ADDR;
    const uint16_t *src = (const uint16_t *)px_map;

    for (int32_t row = 0; row < h; row++) {
        memcpy(&fb[(size_t)(y1 + row) * PANEL_W + (size_t)x1], &src[(size_t)row * w],
               (size_t)w * sizeof(uint16_t));
    }
    lv_display_flush_ready(disp);
}

/* --- touch filtering ------------------------------------------------------
 * The controller is read once per LVGL cycle (~20 ms) and is noisy enough that
 * single-sample glitches become real button presses. Two filters fix that, and
 * both live here rather than in the BSP so they can be tuned by eye:
 *
 *   DEBOUNCE  consecutive samples that must agree before the pressed/released
 *             state flips.
 *   DEADBAND  pixels of movement ignored while the finger is down, so a still
 *             finger reports a still point. Without it a held press wanders and
 *             LVGL reads the wander as a scroll instead of a click.
 *
 * DEBOUNCE costs latency at BOTH edges, and the one that is felt is the release:
 * LVGL fires the click when the finger lifts, so a tap costs DEBOUNCE cycles of
 * press plus DEBOUNCE of release. At 20 ms a cycle, 2 is ~40 ms each way and
 * reads as instant; 3 was ~60 ms each way and read as sluggish. Two samples is
 * still enough to reject the single-sample glitches, which is what made it feel
 * twitchy in the first place.
 *
 * Tuning, one constant at a time: twitchy or double-firing -> DEBOUNCE 3.
 * Sluggish -> DEBOUNCE 1 (and accept that a momentary dropout mid-press then
 * reads as two taps). Taps that scroll instead of clicking -> DEADBAND up. */
#define TOUCH_DEBOUNCE 2
#define TOUCH_DEADBAND 8

/* Counted so the board's two input paths can be told apart from the console
 * without watching the glass. */
static uint32_t s_touch_presses;
static uint32_t s_joy_presses;

static const char *nav_name(panel_nav_t ev)
{
    switch (ev) {
    case PANEL_NAV_LEFT:
        return "LEFT";
    case PANEL_NAV_RIGHT:
        return "RIGHT";
    case PANEL_NAV_UP:
        return "UP";
    case PANEL_NAV_DOWN:
        return "DOWN";
    case PANEL_NAV_ENTER:
        return "ENTER";
    default:
        return "BACK";
    }
}

/* The LVGL key a declined event falls back to. */
static uint32_t nav_key(panel_nav_t ev)
{
    switch (ev) {
    case PANEL_NAV_LEFT:
        return LV_KEY_LEFT;
    case PANEL_NAV_RIGHT:
        return LV_KEY_RIGHT;
    case PANEL_NAV_UP:
        return LV_KEY_UP;
    case PANEL_NAV_DOWN:
        return LV_KEY_DOWN;
    case PANEL_NAV_ENTER:
        return LV_KEY_ENTER;
    default:
        return LV_KEY_ESC;
    }
}

void panel_input_counts(uint32_t *touch, uint32_t *joy)
{
    *touch = s_touch_presses;
    *joy   = s_joy_presses;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int32_t last_x;
    static int32_t last_y;
    static bool    pressed;     /* the debounced state LVGL is told about */
    static bool    was_pressed; /* the previous one, so a press edge is visible */
    static uint8_t disagree;    /* consecutive raw samples differing from it */

    if (!s_touch_up) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    /* Static, not automatic: the BSP keeps the pointer it is handed past the
     * return (GCC flags it as dangling), so the storage has to outlive the
     * call. Only ever touched from this callback, which the main loop drives. */
    static TS_State_t st;
    if (BSP_TS_GetState(0, &st) != BSP_ERROR_NONE) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    const bool raw = (st.TouchDetected != 0U);

    if (raw != pressed) {
        disagree++;
        if (disagree >= TOUCH_DEBOUNCE) {
            pressed  = raw;
            disagree = 0;
        }
    } else {
        disagree = 0;
    }

    if (raw) {
        int32_t x = (int32_t)st.TouchX;
        int32_t y = (int32_t)st.TouchY;
        /* The controller can report outside the panel; an off-screen
         * coordinate would target no widget at all. */
        if (x < 0) {
            x = 0;
        } else if (x >= PANEL_W) {
            x = PANEL_W - 1;
        }
        if (y < 0) {
            y = 0;
        } else if (y >= PANEL_H) {
            y = PANEL_H - 1;
        }
        const int32_t dx = (x > last_x) ? (x - last_x) : (last_x - x);
        const int32_t dy = (y > last_y) ? (y - last_y) : (last_y - y);
        /* Take the new point on the press itself, or once the finger has
         * genuinely moved -- otherwise hold the old one still. */
        if (!pressed || (dx > TOUCH_DEADBAND) || (dy > TOUCH_DEADBAND)) {
            last_x = x;
            last_y = y;
        }
    }

    /* Report the press edge. Whether a given daughterboard has a working touch
     * panel is a question about the hardware in front of you, and a line per
     * press answers it in one boot -- BSP_TS_Init succeeding only says the
     * controller answered on I2C, not that the glass registers a finger. */
    if (pressed && !was_pressed) {
        s_touch_presses++;
        console_printf("touch   : press %lu at (%ld,%ld)\r\n", (unsigned long)s_touch_presses,
                       (long)last_x, (long)last_y);
    }
    was_pressed = pressed;

    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    /* LVGL wants the last known coordinate on release too, so the widget
     * under the finger gets the click. */
    data->point.x = last_x;
    data->point.y = last_y;
}

/* --- joystick -------------------------------------------------------------
 * The five-way produces navigation EVENTS, not LVGL keys.
 *
 * LVGL navigates a keypad by group ORDER, which is creation order, and it
 * delivers LV_KEY_LEFT/RIGHT to the focused widget, where most of this UI
 * ignores them. Mapped straight onto keys, a five-way therefore behaves as two
 * unrelated controls -- one axis moving focus in a sequence nobody can predict
 * from looking at the screen, the other doing nothing. It reads as broken even
 * with every direction arriving correctly.
 *
 * So each push becomes a panel_nav_t and goes to the UI, which resolves it
 * against the layout. What the UI declines comes back here and is passed on as
 * the matching LV_KEY, so a widget that does want the raw key -- an open
 * dropdown, a text area -- still gets it.
 *
 * One direction at a time: the five-way cannot make a diagonal. */
static bool (*s_nav_cb)(panel_nav_t);

void lvgl_port_set_nav_cb(bool (*cb)(panel_nav_t))
{
    s_nav_cb = cb;
}

/* The pending key a declined event left for LVGL, and the state machine that
 * delivers it as one press followed by one release. A keypad indev that stays
 * pressed while the stick is held would repeat, and a repeat is wrong for
 * navigation: one push, one move. */
static uint32_t s_pending_key;
static bool     s_pending_down;

/* Shared by the joystick and by the BACK button, which is a board push-button
 * on the other side of the panel and not part of the five-way at all. */
void lvgl_port_nav(panel_nav_t ev)
{
    s_joy_presses++;
    console_printf("nav     : %s\r\n", nav_name(ev));
    if ((s_nav_cb != NULL) && s_nav_cb(ev)) {
        return;
    }
    s_pending_key = nav_key(ev);
}

/* Consecutive reads that must agree before the direction is believed.
 *
 * The stick is a mechanical five-way and its contacts bounce; it is read once
 * per LVGL cycle, so a bounce landing on a sample is a whole spurious push --
 * and here a spurious push is not a flicker, it is a tab change or a button
 * activation. Debouncing BOTH edges also stops a momentary dropout mid-push
 * reading as release-then-press, which is what turns one deliberate press into
 * two moves.
 *
 * The cost is latency at both edges, and only the press edge is felt: at ~30 ms
 * a cycle, 2 is about 60 ms and reads as immediate. Raise it if a single push
 * ever moves twice; lower it only if the control feels late. */
#define JOY_DEBOUNCE 2

/* How long SEL must be held to mean BACK rather than ENTER. Long enough that a
 * deliberate click never reaches it, short enough that the hold does not feel
 * like waiting: a click is well under 200 ms, a hold is a decision. */
#define JOY_HOLD_MS 500U

static uint32_t s_sel_down_ms;
static bool     s_sel_fired; /* the hold already produced a BACK */

static void joy_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint32_t held;      /* the debounced direction, 0 for none */
    static uint32_t candidate; /* the raw reading that disagrees with it */
    static uint8_t  agree;     /* consecutive samples that have shown it */

    (void)indev;

    /* Finish delivering a declined key before reading the stick again. */
    if (s_pending_key != 0U) {
        data->key = s_pending_key;
        if (!s_pending_down) {
            s_pending_down = true;
            data->state    = LV_INDEV_STATE_PRESSED;
        } else {
            s_pending_down = false;
            s_pending_key  = 0;
            data->state    = LV_INDEV_STATE_RELEASED;
        }
        return;
    }

    data->key   = 0;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!s_joy_up) {
        return;
    }

    const int32_t raw = BSP_IO_ReadPin(0, JOY1_ALL_PIN);
    if (raw < 0) {
        return;
    }
    /* Whichever pins left their idle level. */
    const uint32_t changed = ((uint32_t)raw ^ s_joy_idle) & JOY1_ALL_PIN;

    uint32_t    pin = 0;
    panel_nav_t ev  = PANEL_NAV_ENTER;
    if ((changed & JOY1_SEL_PIN) != 0U) {
        pin = JOY1_SEL_PIN;
        ev  = PANEL_NAV_ENTER;
    } else if ((changed & JOY1_UP_PIN) != 0U) {
        pin = JOY1_UP_PIN;
        ev  = PANEL_NAV_UP;
    } else if ((changed & JOY1_DOWN_PIN) != 0U) {
        pin = JOY1_DOWN_PIN;
        ev  = PANEL_NAV_DOWN;
    } else if ((changed & JOY1_LEFT_PIN) != 0U) {
        pin = JOY1_LEFT_PIN;
        ev  = PANEL_NAV_LEFT;
    } else if ((changed & JOY1_RIGHT_PIN) != 0U) {
        pin = JOY1_RIGHT_PIN;
        ev  = PANEL_NAV_RIGHT;
    }

    /* Debounce: the direction has to be read the same way JOY_DEBOUNCE times
     * running before it is believed, on both edges. */
    if (pin == held) {
        agree = 0;
    } else if (pin != candidate) {
        candidate = pin;
        agree     = 1;
    } else if (++agree >= JOY_DEBOUNCE) {
        agree = 0;
        const uint32_t was = held;
        held               = pin;

        if (pin == JOY1_SEL_PIN) {
            /* SEL is not decided yet -- see the hold check below. */
            s_sel_down_ms = HAL_GetTick();
            s_sel_fired   = false;
        } else if (was == JOY1_SEL_PIN) {
            /* Released, or pushed off to a direction, without ever reaching the
             * hold time: that was a click. */
            if (!s_sel_fired) {
                lvgl_port_nav(PANEL_NAV_ENTER);
            }
        }
        /* Directions act on the press edge. Holding one must not walk the focus
         * across the screen -- on a service panel that is how an operator ends
         * up somewhere they did not choose. */
        if ((pin != 0U) && (pin != JOY1_SEL_PIN)) {
            lvgl_port_nav(ev);
        }
    }

    /* BACK is SEL held down. There is no sixth direction on a five-way and no
     * navigation model is usable without a way out, so the one button that is
     * not a direction carries both: a click enters, a hold leaves. It fires the
     * moment the hold time is reached rather than on release, so the screen
     * changes while the finger is still down and the length of the press does
     * not have to be guessed at. */
    if ((held == JOY1_SEL_PIN) && !s_sel_fired &&
        ((HAL_GetTick() - s_sel_down_ms) >= JOY_HOLD_MS)) {
        s_sel_fired = true;
        lvgl_port_nav(PANEL_NAV_BACK);
    }
}

void lvgl_port_init(void)
{
    lv_init();
    lv_tick_set_cb(HAL_GetTick);

    s_disp = lv_display_create(PANEL_W, PANEL_H);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);

    /* Partial rendering: LVGL draws a slice at a time into these, and
     * flush_cb copies each slice into the LTDC framebuffer. Both buffers are
     * in SDRAM (see panel_mem.h) -- 640x480 will not fit twice in AXI SRAM
     * next to everything else. */
    lv_display_set_buffers(s_disp, (void *)PANEL_DRAWBUF_1_ADDR, (void *)PANEL_DRAWBUF_2_ADDR,
                           (uint32_t)(PANEL_W * PANEL_DRAWBUF_LINES * 2),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, s_disp);
    /* A fingertip is not a mouse: it always slides a little. Default is 10 px
     * before a press becomes a scroll, which turns some taps into scrolls on a
     * touch panel. 24 px means a tap stays a tap. */
    lv_indev_set_scroll_limit(indev, 24);

    /* The joystick's focus group. Made the DEFAULT group before the UI is
     * built, because that is what makes LVGL add each interactive widget to it
     * as it is created -- buttons, dropdowns and the tab bar. Built after the
     * UI instead, it would be empty and the joystick would do nothing. */
    s_group = lv_group_create();
    lv_group_set_default(s_group);

    lv_indev_t *joy = lv_indev_create();
    lv_indev_set_type(joy, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(joy, joy_read_cb);
    lv_indev_set_display(joy, s_disp);
    lv_indev_set_group(joy, s_group);
}

static bool s_repaint_storm;

/* SERVICE-TOOL V1 needs the display doing its worst while the bus is saturated,
 * because S1 is a claim about the receive path surviving a repaint -- not about
 * an idle panel. Invalidating the whole screen every pass makes LVGL redraw all
 * 640x480 continuously, which is the heaviest thing the UI can ask of the CPU
 * and the SDRAM the framebuffer sits in. */
void lvgl_port_repaint_storm(bool on)
{
    s_repaint_storm = on;
}

bool lvgl_port_repaint_storm_is_on(void)
{
    return s_repaint_storm;
}

void lvgl_port_spin(void)
{
    if (s_display_up) {
        if (s_repaint_storm) {
            lv_obj_invalidate(lv_screen_active());
        }
        (void)lv_timer_handler();
    }
}

void lvgl_port_tick_inc(uint32_t ms)
{
    (void)ms; /* lv_tick_set_cb(HAL_GetTick) means LVGL reads the tick itself */
}
