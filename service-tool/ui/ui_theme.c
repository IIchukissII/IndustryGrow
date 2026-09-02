/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "ui_theme.h"

void ig_theme_apply(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, IG_GROUND, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(scr, IG_INK_HI, 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
}

lv_obj_t *ig_card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, IG_PANEL, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, IG_GRID, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, IG_RADIUS, 0);
    lv_obj_set_style_pad_all(c, IG_GAP, 0);
    lv_obj_set_style_text_color(c, IG_INK_HI, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *ig_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t colour)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, colour, 0);
    return l;
}

lv_obj_t *ig_stat_tile(lv_obj_t *parent, int32_t w, int32_t h, const char *caption,
                       lv_obj_t **out_value)
{
    lv_obj_t *c = ig_card(parent, w, h);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 2, 0);

    /* Caption in muted ink, number in primary. The number carries the meaning,
     * so it is the largest thing in the tile and never wears a series colour. */
    ig_label(c, caption, &lv_font_montserrat_14, IG_INK_LOW);
    *out_value = ig_label(c, "--", &lv_font_montserrat_28, IG_INK_HI);
    return c;
}

lv_obj_t *ig_status_chip(lv_obj_t *parent, lv_obj_t **out_dot, lv_obj_t **out_text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    /* The dot is 10 px so it is still a mark, not a speck, at arm's length. */
    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, IG_INK_LOW, 0);

    *out_dot  = dot;
    *out_text = ig_label(row, "--", &lv_font_montserrat_16, IG_INK_HI);
    return row;
}

void ig_set_status(lv_obj_t *dot, lv_obj_t *text, lv_color_t colour, const char *word)
{
    /* Colour and word together, always. The word is the accessible channel;
     * the dot is the one that catches the eye across the room. */
    if (dot != NULL) {
        lv_obj_set_style_bg_color(dot, colour, 0);
    }
    if (text != NULL) {
        lv_label_set_text(text, word);
    }
}

lv_color_t ig_health_colour(uint8_t health)
{
    switch (health) {
    case 0:
        return IG_GOOD; /* NOMINAL */
    case 1:
        return IG_WARNING; /* ADVISORY */
    case 2:
        return IG_SERIOUS; /* CAUTION */
    case 3:
        return IG_CRITICAL; /* WARNING */
    default:
        return IG_INK_LOW;
    }
}

const char *ig_health_word(uint8_t health)
{
    switch (health) {
    case 0:
        return "NOMINAL";
    case 1:
        return "ADVISORY";
    case 2:
        return "CAUTION";
    case 3:
        return "WARNING";
    default:
        return "UNKNOWN";
    }
}

lv_obj_t *ig_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_obj_t **out_label)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_height(b, IG_TOUCH_MIN);
    lv_obj_set_style_bg_color(b, IG_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, IG_GRID, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, IG_RADIUS, 0);
    lv_obj_set_style_text_color(b, IG_INK_HI, 0);
    lv_obj_set_style_bg_color(b, IG_GRID, LV_STATE_PRESSED);
    if (cb != NULL) {
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    }
    lv_obj_t *l = ig_label(b, text, &lv_font_montserrat_16, IG_INK_HI);
    lv_obj_center(l);
    if (out_label != NULL) {
        *out_label = l;
    }
    return b;
}
