/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * IndustryGrow panel design tokens.
 *
 * The ground and the accents are taken from the project's own logo assets
 * (img/industrygrow-logo.svg, img/industrygrow-mark.svg): navy #0A1428, with
 * the cyan/blue/violet/green family the mark uses. Nothing here is invented.
 *
 * The status four are NOT brand colours and are deliberately not themed -- they
 * are reserved for state and never reused as a series colour. They map one to
 * one onto uavcan.node.Health, which also has exactly four levels, and every
 * use pairs the colour with a word so the meaning never rides on colour alone.
 *
 * Every mark colour below clears 3:1 against the navy ground, and every text
 * colour clears 4.5:1. Grid and panel are far below that on purpose: they are
 * recessive surfaces, not marks.
 *
 *   ink_hi   15.7:1   series  10.2:1   good     5.5:1
 *   ink_mid   8.3:1   warning 10.0:1   serious  7.0:1
 *   ink_low   4.3:1   critical 3.8:1 (mark floor; always carries a label)
 */
#ifndef IGROW_UI_THEME_H
#define IGROW_UI_THEME_H

#include "lvgl.h"

/* --- surfaces ------------------------------------------------------------- */
#define IG_GROUND   lv_color_hex(0x0A1428) /* brand navy, the page */
#define IG_PANEL    lv_color_hex(0x101E38) /* raised card */
#define IG_PANEL_HI lv_color_hex(0x16294A) /* header, tab bar */
#define IG_GRID     lv_color_hex(0x1E2A42) /* chart grid, hairlines */

/* --- ink ------------------------------------------------------------------ */
#define IG_INK_HI  lv_color_hex(0xE8EEF7)
#define IG_INK_MID lv_color_hex(0x9FB0C7)
#define IG_INK_LOW lv_color_hex(0x6B7C96)

/* --- the one series colour ------------------------------------------------
 * One signal is plotted at a time, so there is no categorical set and no
 * legend: the tile title names the series. Cyan is the mark's own accent and
 * is distinct from all four status colours. */
#define IG_SERIES lv_color_hex(0x22D3EE)

/* --- brand accent, for the wordmark only ---------------------------------- */
#define IG_BRAND lv_color_hex(0x4ADE80)

/* --- status: reserved, never a series colour ------------------------------ */
#define IG_GOOD     lv_color_hex(0x0CA30C)
#define IG_WARNING  lv_color_hex(0xFAB219)
#define IG_SERIOUS  lv_color_hex(0xEC835A)
#define IG_CRITICAL lv_color_hex(0xD03B3B)

/* --- geometry ------------------------------------------------------------- */
#define IG_HEADER_H 42
/* Spec P2's 9 mm floor, in pixels. Nothing interactive may be shorter. This is
 * a PHYSICAL size, so it is not the same pixel count on the two panels:
 * MB1063's 5.7 inch 640x480 is 0.181 mm/px, MB1166's 4.3 inch 800x480 is
 * 0.1175 mm/px -- a finer pitch, so the same finger costs more pixels. */
#if defined(IGROW_BOARD_H757)
#define IG_TOUCH_MIN 77
#else
#define IG_TOUCH_MIN 50
#endif
#define IG_TABBAR_H  52
#define IG_RADIUS   6
#define IG_GAP      8

/* The IndustryGrow mark, rasterised from img/industrygrow-mark.svg by
 * tools/mkmark.py. Generated, so regenerate rather than edit. */
extern const lv_image_dsc_t ig_mark_img;

/* A card: panel fill, hairline border, rounded. Everything sits in one. */
lv_obj_t *ig_card(lv_obj_t *parent, int32_t w, int32_t h);

/* A stat tile -- a caption in muted ink over a hero number in primary ink.
 * The number is the point, so it gets the largest font on the screen. Returns
 * the tile; `out_value` receives the label to update. */
lv_obj_t *ig_stat_tile(lv_obj_t *parent, int32_t w, int32_t h, const char *caption,
                       lv_obj_t **out_value);

/* Colour + word, never colour alone. `out_dot` and `out_text` are the two
 * pieces to update; set the dot with ig_set_status(). */
lv_obj_t *ig_status_chip(lv_obj_t *parent, lv_obj_t **out_dot, lv_obj_t **out_text);
void      ig_set_status(lv_obj_t *dot, lv_obj_t *text, lv_color_t colour, const char *word);

/* uavcan.node.Health -> the reserved status colour and its word. */
lv_color_t  ig_health_colour(uint8_t health);
const char *ig_health_word(uint8_t health);

lv_obj_t *ig_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t colour);
lv_obj_t *ig_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **out_label);

/* Page background, fonts and the flat dark look, applied once at build time. */
void ig_theme_apply(void);

#endif /* IGROW_UI_THEME_H */
