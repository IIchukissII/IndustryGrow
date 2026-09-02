/* SPDX-License-Identifier: AGPL-3.0-or-later */
#include "ui_main.h"

#include <stdio.h>
#include <string.h>

#include "can_port.h"
#include "cyphal_rx.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "model.h"
#include "protocol.h"
#include "stm32h7xx_hal.h"
#include "selftest.h"
#include "subjects.h"
#include "ui_theme.h"

#define REFRESH_MS 300U

/* 640 x 480. Four tiles across with an 8 px gutter and 8 px page padding. */
#define TILE_W ((PANEL_W - (2 * IG_GAP) - (3 * IG_GAP)) / 4)
#define TILE_H 92

/* --- widgets kept for the refresh pass ------------------------------------ */

static lv_obj_t *s_hdr_dot;
static lv_obj_t *s_hdr_text;

static lv_obj_t *s_tile_frames;
static lv_obj_t *s_tile_transfers;
static lv_obj_t *s_tile_nodes;
static lv_obj_t *s_tile_errors;
static lv_obj_t *s_bus_pins;
static lv_obj_t *s_bus_errline;
static lv_obj_t *s_bus_hunt;
static lv_obj_t *s_profile_dd;
static lv_obj_t *s_mode_btn_label;
static lv_obj_t *s_selftest_label;

static lv_obj_t *s_node_table;
static lv_obj_t *s_node_dd;
static lv_obj_t *s_cmd_status;
static lv_obj_t *s_restart_label;
static bool      s_restart_armed;
static uint32_t  s_restart_armed_ms;

static lv_obj_t          *s_chart;
static lv_chart_series_t *s_series;
static lv_obj_t          *s_signal_dd;
static lv_obj_t          *s_live_name;
static lv_obj_t          *s_live_value;
static lv_obj_t          *s_live_unit;
static lv_obj_t          *s_live_range;

static lv_obj_t *s_value_table;

/* Liveness rows are pre-made and reused: rebuilding widgets every 300 ms would
 * churn the LVGL heap for no reason. 24 covers M01+M02+M05 with room over. */
#define LIVE_ROWS 24U
static struct {
    lv_obj_t *row;
    lv_obj_t *dot;
    lv_obj_t *name;
    lv_obj_t *age;
    lv_obj_t *state;
} s_live_rows[LIVE_ROWS];
static lv_obj_t *s_live_summary;

static uint32_t s_last_generation = 0xFFFFFFFFU;

/* Recomputed every tick so the header can annunciate staleness from any tab. */
static uint16_t s_stale_signals;
static uint16_t s_overdue_signals;

#define MAX_CHOICES (MODEL_MAX_NODES * MODEL_MAX_SIGNALS)
static struct {
    uint8_t  node_id;
    uint16_t subject_id;
} s_choice[MAX_CHOICES];
static uint16_t s_choice_count;
static char     s_choice_options[MAX_CHOICES * 40];
static char     s_choice_options_shown[MAX_CHOICES * 40];

/* --- helpers -------------------------------------------------------------- */

static const char *mode_name(uint8_t m)
{
    switch (m) {
    case 0:
        return "OPERATIONAL";
    case 1:
        return "INIT";
    case 2:
        return "MAINTENANCE";
    case 3:
        return "SW-UPDATE";
    default:
        return "?";
    }
}

static void style_table(lv_obj_t *t)
{
    lv_obj_set_style_bg_color(t, IG_PANEL, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(t, IG_GRID, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_radius(t, IG_RADIUS, 0);
    lv_obj_set_style_text_color(t, IG_INK_HI, 0);
    /* Cell separators are hairlines in the grid colour: the rows are the data,
     * the rules between them are not. */
    lv_obj_set_style_bg_color(t, IG_PANEL, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(t, IG_GRID, LV_PART_ITEMS);
    lv_obj_set_style_border_width(t, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_color(t, IG_INK_HI, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(t, 6, LV_PART_ITEMS);
}

/* --- Bus tab -------------------------------------------------------------- */

static void mode_btn_cb(lv_event_t *e)
{
    (void)e;
    const bool want_listen = !can_is_listen_only();
    (void)can_init(can_current_profile(), want_listen);
    lv_label_set_text(s_mode_btn_label, want_listen ? "Go normal" : "Go listen-only");
}

static void selftest_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_label_set_text(s_selftest_label, selftest_toggle() ? "Stop test" : "Self-test");
}

static void hunt_btn_cb(lv_event_t *e)
{
    (void)e;
    can_hunt_start(3000U);
}

static void profile_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    (void)can_init((uint8_t)lv_dropdown_get_selected(dd), can_is_listen_only());
}

static void style_dropdown(lv_obj_t *dd)
{
    lv_obj_set_height(dd, IG_TOUCH_MIN);
    lv_obj_set_style_bg_color(dd, IG_PANEL_HI, 0);
    lv_obj_set_style_border_color(dd, IG_GRID, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, IG_RADIUS, 0);
    lv_obj_set_style_text_color(dd, IG_INK_HI, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list != NULL) {
        lv_obj_set_style_bg_color(list, IG_PANEL_HI, 0);
        lv_obj_set_style_text_color(list, IG_INK_HI, 0);
        lv_obj_set_style_border_color(list, IG_GRID, 0);
    }
}

static void build_bus_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, IG_GAP, 0);

    /* Four headline numbers. These are the whole answer to "is anything
     * arriving", so they are tiles and not a chart. */
    lv_obj_t *tiles = lv_obj_create(tab);
    lv_obj_remove_style_all(tiles);
    lv_obj_set_size(tiles, LV_PCT(100), TILE_H);
    lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tiles, IG_GAP, 0);

    /* Let the row divide itself rather than pinning four widths that happen to
     * add up to exactly the content width -- one border and they would wrap. */
    static const char *const captions[] = {"FRAMES RX", "TRANSFERS", "NODES", "ERRORS"};
    lv_obj_t **const         values[]   = {&s_tile_frames, &s_tile_transfers, &s_tile_nodes,
                                           &s_tile_errors};
    for (unsigned i = 0; i < 4U; i++) {
        lv_obj_t *tile = ig_stat_tile(tiles, TILE_W, TILE_H, captions[i], values[i]);
        lv_obj_set_width(tile, 0);
        lv_obj_set_flex_grow(tile, 1);
    }

    /* The card takes whatever the tiles and the button row leave, so the tab
     * ends at the bottom of the screen instead of part way down it. */
    lv_obj_t *detail = ig_card(tab, LV_PCT(100), 0);
    lv_obj_set_flex_grow(detail, 1);
    lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail, 4, 0);
    s_bus_pins    = ig_label(detail, "", &lv_font_montserrat_16, IG_INK_HI);
    s_bus_errline = ig_label(detail, "", &lv_font_montserrat_14, IG_INK_MID);
    s_bus_hunt    = ig_label(detail, "", &lv_font_montserrat_14, IG_INK_MID);
    lv_label_set_long_mode(s_bus_hunt, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_bus_hunt, PANEL_W - (4 * IG_GAP));

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), IG_TOUCH_MIN + 4);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, IG_GAP, 0);

    ig_button(row, "Go normal", mode_btn_cb, &s_mode_btn_label);
    ig_button(row, "Hunt pins", hunt_btn_cb, NULL);
    ig_button(row, "Self-test", selftest_btn_cb, &s_selftest_label);

    s_profile_dd = lv_dropdown_create(row);
    lv_obj_set_flex_grow(s_profile_dd, 1);
    char opts[192];
    opts[0] = '\0';
    for (uint8_t i = 0; i < can_profile_count(); i++) {
        (void)strncat(opts, can_profile_name(i), sizeof opts - strlen(opts) - 2U);
        if ((i + 1U) < can_profile_count()) {
            (void)strncat(opts, "\n", sizeof opts - strlen(opts) - 1U);
        }
    }
    lv_dropdown_set_options(s_profile_dd, opts);
    style_dropdown(s_profile_dd);
    lv_obj_add_event_cb(s_profile_dd, profile_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void refresh_bus_tab(void)
{
    const can_stats_t *st = can_stats();
    s_overdue_signals      = model_overdue_count(HAL_GetTick(), &s_stale_signals);

    lv_label_set_text_fmt(s_tile_frames, "%lu", (unsigned long)st->rx_frames);
    lv_label_set_text_fmt(s_tile_transfers, "%lu", (unsigned long)cyphal_rx_accepted());
    lv_label_set_text_fmt(s_tile_nodes, "%u", (unsigned)model_node_count());
    lv_label_set_text_fmt(s_tile_errors, "%u", (unsigned)(st->tx_errors + st->rx_errors));

    /* The header chip is the one thing visible from every tab, so it carries
     * the state that decides whether anything else on screen can be trusted. */
    if (selftest_active()) {
        /* Outranks every other state: the values on screen are fabricated and
         * must never be mistaken for a measurement. */
        ig_set_status(s_hdr_dot, s_hdr_text, IG_WARNING, "SELF-TEST - SYNTHETIC DATA");
    } else if (!can_is_up()) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_CRITICAL, "BUS DOWN");
    } else if (st->bus_off) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_CRITICAL, "BUS-OFF");
    } else if (st->error_passive) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_SERIOUS, "ERROR PASSIVE");
    } else if (st->error_warning) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_WARNING, "ERROR WARNING");
    } else if (st->rx_frames == 0U) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_WARNING, "NO TRAFFIC");
    } else if (s_stale_signals > 0U) {
        /* Frames are flowing but something stopped publishing -- the failure a
         * healthy-looking node hides. It outranks the calm states below. */
        lv_label_set_text_fmt(s_hdr_text, "%u STALE", (unsigned)s_stale_signals);
        lv_obj_set_style_bg_color(s_hdr_dot, IG_CRITICAL, 0);
    } else if (s_overdue_signals > 0U) {
        lv_label_set_text_fmt(s_hdr_text, "%u LATE", (unsigned)s_overdue_signals);
        lv_obj_set_style_bg_color(s_hdr_dot, IG_WARNING, 0);
    } else if (can_is_listen_only()) {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_GOOD, "LISTENING");
    } else {
        ig_set_status(s_hdr_dot, s_hdr_text, IG_GOOD, "ON BUS");
    }

    lv_label_set_text_fmt(s_bus_pins, "%s   500 kbit/s classic   %s",
                          can_is_up() ? can_profile_name(can_current_profile()) : "down",
                          can_is_listen_only() ? "listen-only" : "NORMAL - acking");
    lv_label_set_text_fmt(s_bus_errline,
                          "tx %lu   dropped %lu   unknown %lu   TEC %u  REC %u  LEC %u\n"
                          "rx queue %u/512   lost to a slow UI %lu",
                          (unsigned long)st->tx_frames, (unsigned long)st->tx_dropped,
                          (unsigned long)cyphal_rx_unknown(), (unsigned)st->tx_errors,
                          (unsigned)st->rx_errors, (unsigned)st->last_error,
                          (unsigned)can_ring_depth(), (unsigned long)st->rx_lost_ring);

    switch (can_hunt_state()) {
    case CAN_HUNT_RUNNING:
        lv_label_set_text_fmt(s_bus_hunt, "Hunting: trying %s", can_profile_name(can_hunt_current()));
        break;
    case CAN_HUNT_FOUND:
        lv_label_set_text_fmt(s_bus_hunt, "Hunt found frames on %s",
                              can_profile_name(can_hunt_current()));
        break;
    case CAN_HUNT_EXHAUSTED:
        lv_label_set_text(s_bus_hunt, "Hunt found nothing on any pin pair. Check the transceiver "
                                      "enable, the wiring, and that the bus is live.");
        break;
    default:
        lv_label_set_text(s_bus_hunt, "");
        break;
    }
}

/* --- Nodes tab ------------------------------------------------------------ */

static void getinfo_btn_cb(lv_event_t *e)
{
    (void)e;
    model_node_t *n = model_node_at((uint8_t)lv_dropdown_get_selected(s_node_dd));
    if (n == NULL) {
        return;
    }
    lv_label_set_text(s_cmd_status, cyphal_request_getinfo(n->node_id)
                                        ? "GetInfo sent"
                                        : "Not sent - the panel is in listen-only mode");
}

static void restart_btn_cb(lv_event_t *e)
{
    (void)e;
    model_node_t *n = model_node_at((uint8_t)lv_dropdown_get_selected(s_node_dd));
    if (n == NULL) {
        return;
    }
    if (!s_restart_armed) {
        s_restart_armed    = true;
        s_restart_armed_ms = HAL_GetTick();
        lv_label_set_text(s_restart_label, "Confirm restart");
        lv_label_set_text_fmt(s_cmd_status, "Restart of node %u armed for 5 s",
                              (unsigned)n->node_id);
        return;
    }
    s_restart_armed = false;
    lv_label_set_text(s_restart_label, "Restart node");
    lv_label_set_text(s_cmd_status, cyphal_send_restart(n->node_id)
                                        ? "Restart sent"
                                        : "Not sent - the panel is in listen-only mode");
}

static void build_nodes_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, IG_GAP, 0);

    s_node_table = lv_table_create(tab);
    lv_obj_set_size(s_node_table, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_node_table, 1);
    style_table(s_node_table);
    lv_table_set_column_count(s_node_table, 5);
    lv_table_set_column_width(s_node_table, 0, 58);
    lv_table_set_column_width(s_node_table, 1, 190);
    lv_table_set_column_width(s_node_table, 2, 78);
    lv_table_set_column_width(s_node_table, 3, 130);
    lv_table_set_column_width(s_node_table, 4, 140);
    lv_table_set_cell_value(s_node_table, 0, 0, "Node");
    lv_table_set_cell_value(s_node_table, 0, 1, "Name");
    lv_table_set_cell_value(s_node_table, 0, 2, "Uptime");
    lv_table_set_cell_value(s_node_table, 0, 3, "Health");
    lv_table_set_cell_value(s_node_table, 0, 4, "Mode");

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), IG_TOUCH_MIN + 4);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, IG_GAP, 0);

    s_node_dd = lv_dropdown_create(row);
    lv_obj_set_width(s_node_dd, 150);
    lv_dropdown_set_options(s_node_dd, "(no nodes)");
    style_dropdown(s_node_dd);

    ig_button(row, "Ask GetInfo", getinfo_btn_cb, NULL);
    ig_button(row, "Restart node", restart_btn_cb, &s_restart_label);

    s_cmd_status = ig_label(tab, "", &lv_font_montserrat_14, IG_INK_MID);
}

static void refresh_nodes_tab(void)
{
    if (s_restart_armed && ((HAL_GetTick() - s_restart_armed_ms) > 5000U)) {
        s_restart_armed = false;
        lv_label_set_text(s_restart_label, "Restart node");
    }

    const uint8_t count = model_node_count();
    lv_table_set_row_count(s_node_table, (uint32_t)count + 1U);

    static char node_opts_shown[MODEL_MAX_NODES * 12];
    char        node_opts[MODEL_MAX_NODES * 12];
    node_opts[0] = '\0';

    uint8_t sel_node_id  = 0;
    bool    had_node_sel = false;
    {
        model_node_t *cur = model_node_at((uint8_t)lv_dropdown_get_selected(s_node_dd));
        if (cur != NULL) {
            sel_node_id  = cur->node_id;
            had_node_sel = true;
        }
    }

    for (uint8_t i = 0; i < count; i++) {
        model_node_t *n = model_node_at(i);
        if (n == NULL) {
            break;
        }
        const uint32_t r = (uint32_t)i + 1U;
        lv_table_set_cell_value_fmt(s_node_table, r, 0, "%u", (unsigned)n->node_id);
        lv_table_set_cell_value(s_node_table, r, 1, n->have_name ? n->name : "(ask GetInfo)");
        lv_table_set_cell_value_fmt(s_node_table, r, 2, "%lu s", (unsigned long)n->uptime);
        /* Health as a word. The colour lives on the header chip and the Bus
         * tab; a table cell that meant something only by its colour would be
         * unreadable to a colourblind operator and unprintable. */
        lv_table_set_cell_value(s_node_table, r, 3, ig_health_word(n->health));
        lv_table_set_cell_value(s_node_table, r, 4, mode_name(n->mode));

        char one[12];
        (void)snprintf(one, sizeof one, "%s%u", (i == 0) ? "" : "\n", (unsigned)n->node_id);
        (void)strncat(node_opts, one, sizeof node_opts - strlen(node_opts) - 1U);
    }
    /* Same trap as the signal selector: rewrite only on a real change. */
    if ((count > 0U) && (strcmp(node_opts, node_opts_shown) != 0)) {
        (void)strncpy(node_opts_shown, node_opts, sizeof node_opts_shown - 1U);
        node_opts_shown[sizeof node_opts_shown - 1U] = '\0';
        lv_dropdown_set_options(s_node_dd, node_opts);
        if (had_node_sel) {
            for (uint8_t i = 0; i < count; i++) {
                model_node_t *nd = model_node_at(i);
                if ((nd != NULL) && (nd->node_id == sel_node_id)) {
                    lv_dropdown_set_selected(s_node_dd, i);
                    break;
                }
            }
        }
    }

    const cyphal_cmd_result_t *c = cyphal_last_command();
    if (c->answered) {
        lv_label_set_text_fmt(s_cmd_status, "Node %u answered command %u with status %u",
                              (unsigned)c->target, (unsigned)c->command, (unsigned)c->status);
    } else if (c->pending) {
        lv_label_set_text_fmt(s_cmd_status, "Command %u to node %u: waiting", (unsigned)c->command,
                              (unsigned)c->target);
    }
}

/* --- Live tab ------------------------------------------------------------- */

static void rebuild_choices(void)
{
    /* What is selected right now, by identity, before the list is rebuilt. */
    uint8_t  sel_node    = 0;
    uint16_t sel_subject = 0;
    bool     had_selection = false;
    {
        const uint32_t sel = lv_dropdown_get_selected(s_signal_dd);
        if (sel < s_choice_count) {
            sel_node      = s_choice[sel].node_id;
            sel_subject   = s_choice[sel].subject_id;
            had_selection = true;
        }
    }

    s_choice_count      = 0;
    s_choice_options[0] = '\0';

    for (uint8_t i = 0;; i++) {
        model_node_t *n = model_node_at(i);
        if (n == NULL) {
            break;
        }
        for (uint8_t k = 0; k < n->n_signals; k++) {
            const igrow_subject_t *sub = igrow_subject_by_id(n->sig[k].subject_id);
            if ((sub == NULL) || (s_choice_count >= MAX_CHOICES)) {
                continue;
            }
            if (sub->kind == IGROW_SIG_SWEEP) {
                continue; /* a sweep has no single value for a time axis */
            }
            s_choice[s_choice_count].node_id    = n->node_id;
            s_choice[s_choice_count].subject_id = sub->subject_id;

            char one[48];
            (void)snprintf(one, sizeof one, "%snode %u  %s", (s_choice_count == 0) ? "" : "\n",
                           (unsigned)n->node_id, sub->name);
            (void)strncat(s_choice_options, one,
                          sizeof s_choice_options - strlen(s_choice_options) - 1U);
            s_choice_count++;
        }
    }
    /* lv_dropdown_set_options() resets the selection to the first entry. Called
     * on every refresh it silently overwrites the operator's choice a few times
     * a second, which looks like a dropdown that will not switch. Rewrite the
     * list only when it actually changed, and put the same signal back. */
    const char *want = (s_choice_count > 0U) ? s_choice_options : "(nothing yet)";
    if (strcmp(want, s_choice_options_shown) == 0) {
        return;
    }
    (void)strncpy(s_choice_options_shown, want, sizeof s_choice_options_shown - 1U);
    s_choice_options_shown[sizeof s_choice_options_shown - 1U] = '\0';

    lv_dropdown_set_options(s_signal_dd, want);

    /* Restore by identity, not by index: the list may have grown or reordered. */
    if (had_selection) {
        for (uint16_t i = 0; i < s_choice_count; i++) {
            if ((s_choice[i].node_id == sel_node) && (s_choice[i].subject_id == sel_subject)) {
                lv_dropdown_set_selected(s_signal_dd, i);
                break;
            }
        }
    }
}

static void build_live_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, IG_GAP, 0);

    /* The selector sits in one row above the chart, not beside it. */
    s_signal_dd = lv_dropdown_create(tab);
    lv_obj_set_width(s_signal_dd, LV_PCT(100));
    lv_dropdown_set_options(s_signal_dd, "(nothing yet)");
    style_dropdown(s_signal_dd);

    /* Hero number. One series is plotted, so there is no legend: this names it,
     * with a cyan rule beside the name to tie it to the line below. */
    lv_obj_t *hero = ig_card(tab, LV_PCT(100), 74);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(hero, 8, 0);

    lv_obj_t *swatch = lv_obj_create(hero);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 4, 34);
    lv_obj_set_style_radius(swatch, 2, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(swatch, IG_SERIES, 0);

    lv_obj_t *col = lv_obj_create(hero);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    s_live_name  = ig_label(col, "--", &lv_font_montserrat_14, IG_INK_LOW);
    s_live_value = ig_label(col, "--", &lv_font_montserrat_28, IG_INK_HI);

    s_live_unit = ig_label(hero, "", &lv_font_montserrat_16, IG_INK_MID);

    s_chart = lv_chart_create(tab);
    lv_obj_set_size(s_chart, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_chart, 1);
    lv_obj_set_style_bg_color(s_chart, IG_PANEL, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_chart, IG_GRID, 0);
    lv_obj_set_style_border_width(s_chart, 1, 0);
    lv_obj_set_style_radius(s_chart, IG_RADIUS, 0);
    lv_obj_set_style_pad_all(s_chart, 6, 0);
    /* Recessive grid: it is scaffolding, not data. */
    lv_obj_set_style_line_color(s_chart, IG_GRID, LV_PART_MAIN);
    lv_obj_set_style_line_width(s_chart, 1, LV_PART_MAIN);
    /* A 2 px line and no point markers -- 240 markers would be noise, not data. */
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);

    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, MODEL_HISTORY);
    lv_chart_set_div_line_count(s_chart, 4, 6);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    s_series = lv_chart_add_series(s_chart, IG_SERIES, LV_CHART_AXIS_PRIMARY_Y);

    s_live_range = ig_label(tab, "", &lv_font_montserrat_14, IG_INK_LOW);
}

static void refresh_live_tab(void)
{
    const uint32_t sel = lv_dropdown_get_selected(s_signal_dd);
    if ((s_choice_count == 0U) || (sel >= s_choice_count)) {
        lv_label_set_text(s_live_name, "no telemetry yet");
        lv_label_set_text(s_live_value, "--");
        lv_label_set_text(s_live_unit, "");
        lv_label_set_text(s_live_range, "");
        return;
    }
    model_node_t          *n   = model_node(s_choice[sel].node_id);
    model_signal_t        *s   = model_signal(n, s_choice[sel].subject_id);
    const igrow_subject_t *sub = igrow_subject_by_id(s_choice[sel].subject_id);
    if ((s == NULL) || (sub == NULL)) {
        return;
    }

    lv_label_set_text_fmt(s_live_name, "node %u  ~  %s", (unsigned)n->node_id, sub->name);
    lv_label_set_text(s_live_value, s->last.text);
    lv_label_set_text(s_live_unit, sub->unit);

    float          hist[MODEL_HISTORY];
    const uint16_t n_pts = model_history(s, hist, MODEL_HISTORY);
    if (n_pts == 0U) {
        lv_label_set_text(s_live_range, "no samples yet");
        return;
    }

    /* The chart carries integers, so scale to keep resolution across the
     * window and put the real numbers in the caption. One axis, never two. */
    float span = s->hist_max - s->hist_min;
    if (span < 1e-6f) {
        span = 1.0f;
    }
    const float   scale = 1000.0f / span;
    const int32_t lo    = (int32_t)(s->hist_min * scale);
    const int32_t hi    = (int32_t)(s->hist_max * scale);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, lo, (hi > lo) ? hi : (lo + 1));

    lv_chart_set_point_count(s_chart, n_pts);
    for (uint16_t i = 0; i < n_pts; i++) {
        lv_chart_set_value_by_id(s_chart, s_series, i, (int32_t)(hist[i] * scale));
    }

    /* Formatted with the C library, not lv_label_set_text_fmt.
     *
     * LVGL's built-in formatter does not implement %f. Given one it desynchronises
     * from the argument list, and the next %s dereferences whatever follows --
     * a precise bus fault inside lv_vsnprintf_inner, which reads as an LVGL bug
     * and is not one. No float may reach an lv_*_fmt() call. */
    char line[128];
    (void)snprintf(line, sizeof line, "%u samples   min %.3f   max %.3f %s   age %lu ms%s",
                   (unsigned)n_pts, (double)s->hist_min, (double)s->hist_max, sub->unit,
                   (unsigned long)(HAL_GetTick() - s->last_ms),
                   s->last.valid ? "" : "   INVALID");
    lv_label_set_text(s_live_range, line);
}

/* --- Values tab ----------------------------------------------------------- */

static void build_values_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    s_value_table = lv_table_create(tab);
    lv_obj_set_size(s_value_table, LV_PCT(100), LV_PCT(100));
    style_table(s_value_table);
    lv_table_set_column_count(s_value_table, 5);
    lv_table_set_column_width(s_value_table, 0, 58);
    lv_table_set_column_width(s_value_table, 1, 58);
    lv_table_set_column_width(s_value_table, 2, 168);
    lv_table_set_column_width(s_value_table, 3, 180);
    lv_table_set_column_width(s_value_table, 4, 110);
    lv_table_set_cell_value(s_value_table, 0, 0, "Node");
    lv_table_set_cell_value(s_value_table, 0, 1, "Subj");
    lv_table_set_cell_value(s_value_table, 0, 2, "Signal");
    lv_table_set_cell_value(s_value_table, 0, 3, "Value");
    lv_table_set_cell_value(s_value_table, 0, 4, "Age");
}

static void refresh_values_tab(void)
{
    uint32_t row = 1;
    for (uint8_t i = 0;; i++) {
        model_node_t *n = model_node_at(i);
        if (n == NULL) {
            break;
        }
        for (uint8_t k = 0; k < n->n_signals; k++) {
            const model_signal_t  *s   = &n->sig[k];
            const igrow_subject_t *sub = igrow_subject_by_id(s->subject_id);
            if (sub == NULL) {
                continue;
            }
            lv_table_set_row_count(s_value_table, row + 1U);
            lv_table_set_cell_value_fmt(s_value_table, row, 0, "%u", (unsigned)n->node_id);
            lv_table_set_cell_value_fmt(s_value_table, row, 1, "%u", (unsigned)s->subject_id);
            lv_table_set_cell_value_fmt(s_value_table, row, 2, "%s  %s",
                                        igrow_subject_module(s->subject_id), sub->name);
            lv_table_set_cell_value_fmt(s_value_table, row, 3, "%s %s%s", s->last.text, sub->unit,
                                        s->last.valid ? "" : "  INVALID");
            const model_liveness_t l = model_liveness(s, HAL_GetTick());
            lv_table_set_cell_value_fmt(
                s_value_table, row, 4, "%lu ms%s", (unsigned long)(HAL_GetTick() - s->last_ms),
                (l == MODEL_LIVE) ? "" : ((l == MODEL_LATE) ? "  LATE" : "  STALE"));
            row++;
        }
    }
    if (row == 1U) {
        lv_table_set_row_count(s_value_table, 2);
        lv_table_set_cell_value(s_value_table, 1, 2, "no telemetry yet");
    }
}

/* --- Liveness tab ---------------------------------------------------------
 * Every signal, ranked worst first. The ranking IS the content: if anything has
 * gone quiet it is on the top row, and if nothing has, the top row is green.
 *
 * Judged against each signal's own observed rate rather than a fixed timeout,
 * and annunciated in the header so it shows from any tab -- O-75 was a node
 * reporting NOMINAL health while publishing nothing for 37 minutes. */

static lv_color_t liveness_colour(model_liveness_t l)
{
    switch (l) {
    case MODEL_LIVE:
        return IG_GOOD;
    case MODEL_LATE:
        return IG_WARNING;
    default:
        return IG_CRITICAL;
    }
}

static const char *liveness_word(model_liveness_t l)
{
    switch (l) {
    case MODEL_LIVE:
        return "live";
    case MODEL_LATE:
        return "LATE";
    default:
        return "STALE";
    }
}

static void build_liveness_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 4, 0);

    s_live_summary = ig_label(tab, "", &lv_font_montserrat_16, IG_INK_HI);

    lv_obj_t *list = lv_obj_create(tab);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_color(list, IG_PANEL, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(list, IG_GRID, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, IG_RADIUS, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 2, 0);

    for (unsigned i = 0; i < LIVE_ROWS; i++) {
        lv_obj_t *r = lv_obj_create(list);
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, LV_PCT(100), 22);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(r, 8, 0);

        lv_obj_t *dot = lv_obj_create(r);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

        lv_obj_t *name = ig_label(r, "", &lv_font_montserrat_14, IG_INK_HI);
        lv_obj_set_width(name, 290);
        lv_obj_t *age = ig_label(r, "", &lv_font_montserrat_14, IG_INK_MID);
        lv_obj_set_width(age, 110);
        lv_obj_t *state = ig_label(r, "", &lv_font_montserrat_14, IG_INK_MID);

        s_live_rows[i].row   = r;
        s_live_rows[i].dot   = dot;
        s_live_rows[i].name  = name;
        s_live_rows[i].age   = age;
        s_live_rows[i].state = state;
        lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_liveness_tab(void)
{
    const uint32_t now = HAL_GetTick();

    struct entry {
        uint8_t          node_id;
        uint16_t         subject_id;
        uint32_t         age;
        uint32_t         period;
        model_liveness_t live;
    } list[MODEL_MAX_NODES * MODEL_MAX_SIGNALS];
    uint16_t n = 0;

    for (uint8_t i = 0;; i++) {
        model_node_t *nd = model_node_at(i);
        if (nd == NULL) {
            break;
        }
        for (uint8_t k = 0;
             (k < nd->n_signals) && (n < (uint16_t)(sizeof list / sizeof list[0])); k++) {
            const model_signal_t *sg = &nd->sig[k];
            list[n].node_id    = nd->node_id;
            list[n].subject_id = sg->subject_id;
            list[n].age        = now - sg->last_ms;
            list[n].period     = sg->period_ms;
            list[n].live       = model_liveness(sg, now);
            n++;
        }
    }

    /* Worst first, oldest first within a state. Insertion sort: n is about 21
     * and this runs three times a second. */
    for (uint16_t i = 1; i < n; i++) {
        const struct entry key = list[i];
        int32_t            j   = (int32_t)i - 1;
        while ((j >= 0) && ((list[j].live < key.live) ||
                            ((list[j].live == key.live) && (list[j].age < key.age)))) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }

    uint16_t       stale   = 0;
    const uint16_t overdue = model_overdue_count(now, &stale);
    if (n == 0U) {
        lv_label_set_text(s_live_summary, "No signals seen yet");
    } else if (stale > 0U) {
        lv_label_set_text_fmt(s_live_summary, "%u of %u signals STALE, %u late", (unsigned)stale,
                              (unsigned)n, (unsigned)(overdue - stale));
    } else if (overdue > 0U) {
        lv_label_set_text_fmt(s_live_summary, "%u of %u signals late", (unsigned)overdue,
                              (unsigned)n);
    } else {
        lv_label_set_text_fmt(s_live_summary, "All %u signals arriving on time", (unsigned)n);
    }

    for (unsigned i = 0; i < LIVE_ROWS; i++) {
        if (i >= n) {
            lv_obj_add_flag(s_live_rows[i].row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_live_rows[i].row, LV_OBJ_FLAG_HIDDEN);

        const igrow_subject_t *sub = igrow_subject_by_id(list[i].subject_id);
        lv_obj_set_style_bg_color(s_live_rows[i].dot, liveness_colour(list[i].live), 0);
        lv_label_set_text_fmt(s_live_rows[i].name, "node %u  %s  %s", (unsigned)list[i].node_id,
                              igrow_subject_module(list[i].subject_id),
                              (sub != NULL) ? sub->name : "?");
        if (list[i].age < 10000U) {
            lv_label_set_text_fmt(s_live_rows[i].age, "%lu ms", (unsigned long)list[i].age);
        } else {
            lv_label_set_text_fmt(s_live_rows[i].age, "%lu s",
                                  (unsigned long)(list[i].age / 1000U));
        }
        /* The word, not just the dot: a colour alone is unreadable to a
         * colourblind operator and does not survive a photograph. */
        lv_label_set_text_fmt(s_live_rows[i].state, "%s  (every %lu ms)",
                              liveness_word(list[i].live), (unsigned long)list[i].period);
        lv_obj_set_style_text_color(s_live_rows[i].state,
                                    (list[i].live == MODEL_LIVE) ? IG_INK_LOW : IG_INK_HI, 0);
    }
}

/* --- protocol tab (SERVICE-TOOL F3) ---------------------------------------
 *
 * Presents the step that is due and offers exactly two outcomes for it. There
 * is no control that jumps to another step, revises one already recorded, or
 * edits any text: S11 is not enforced here so much as not offered here, and the
 * executor refuses those regardless of what a caller asks (protocol_record).
 * What this tab adds over the console is that a refusal reaches the person
 * holding the tool (S10). */

static lv_obj_t *s_proto_title;
static lv_obj_t *s_proto_progress;
static lv_obj_t *s_proto_section;
static lv_obj_t *s_proto_step;
static lv_obj_t *s_proto_criterion;
static lv_obj_t *s_proto_status;
static lv_obj_t *s_proto_pass_btn;
static lv_obj_t *s_proto_fail_btn;

static void refresh_protocol_tab(void);

static void proto_outcome(bool passed)
{
    const protocol_t *p   = protocol_get();
    const char       *why = NULL;
    if (protocol_record(p->cursor, passed, &why)) {
        lv_label_set_text(s_proto_status, passed ? "recorded: pass" : "recorded: FAIL");
        lv_obj_set_style_text_color(s_proto_status, passed ? IG_GOOD : IG_CRITICAL, 0);
    } else {
        char line[180];
        (void)snprintf(line, sizeof line, "refused - %s", (why != NULL) ? why : "unknown");
        lv_label_set_text(s_proto_status, line);
        lv_obj_set_style_text_color(s_proto_status, IG_WARNING, 0);
    }
    refresh_protocol_tab();
}

static void proto_pass_cb(lv_event_t *e)
{
    (void)e;
    proto_outcome(true);
}

static void proto_fail_cb(lv_event_t *e)
{
    (void)e;
    proto_outcome(false);
}

static void proto_restart_cb(lv_event_t *e)
{
    (void)e;
    protocol_restart();
    lv_label_set_text(s_proto_status, "run restarted from step one");
    lv_obj_set_style_text_color(s_proto_status, IG_INK_MID, 0);
    refresh_protocol_tab();
}

static void build_protocol_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, IG_GAP, 0);
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, IG_GAP, 0);

    s_proto_title    = ig_label(tab, "", &lv_font_montserrat_14, IG_INK_MID);
    s_proto_progress = ig_label(tab, "", &lv_font_montserrat_16, IG_INK_HI);
    s_proto_section  = ig_label(tab, "", &lv_font_montserrat_14, IG_SERIES);

    s_proto_step = ig_label(tab, "", &lv_font_montserrat_20, IG_INK_HI);
    lv_obj_set_width(s_proto_step, LV_PCT(100));
    lv_label_set_long_mode(s_proto_step, LV_LABEL_LONG_WRAP);

    s_proto_criterion = ig_label(tab, "", &lv_font_montserrat_16, IG_INK_MID);
    lv_obj_set_width(s_proto_criterion, LV_PCT(100));
    lv_label_set_long_mode(s_proto_criterion, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(s_proto_criterion, 1);

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), IG_TOUCH_MIN + 4);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, IG_GAP, 0);

    s_proto_pass_btn = ig_button(row, "Pass", proto_pass_cb, NULL);
    s_proto_fail_btn = ig_button(row, "Fail", proto_fail_cb, NULL);
    (void)ig_button(row, "Restart run", proto_restart_cb, NULL);

    s_proto_status = ig_label(tab, "", &lv_font_montserrat_14, IG_INK_MID);
    lv_obj_set_width(s_proto_status, LV_PCT(100));
    lv_label_set_long_mode(s_proto_status, LV_LABEL_LONG_WRAP);

    refresh_protocol_tab();
}

static void refresh_protocol_tab(void)
{
    const protocol_t *p = protocol_get();
    char              line[220];

    if (!p->loaded) {
        lv_label_set_text(s_proto_title, "no protocol carried");
        lv_label_set_text(s_proto_progress, "");
        lv_label_set_text(s_proto_section, "");
        lv_label_set_text(s_proto_step, "");
        lv_label_set_text(s_proto_criterion, "");
        return;
    }

    lv_label_set_text(s_proto_title, p->identity);
    (void)snprintf(line, sizeof line, "%u of %u steps - %u passed, %u failed",
                   (unsigned)p->cursor, (unsigned)p->count, (unsigned)protocol_passed_count(),
                   (unsigned)protocol_failed_count());
    lv_label_set_text(s_proto_progress, line);

    const proto_step_t *cur = protocol_current();
    if (cur == NULL) {
        lv_label_set_text(s_proto_section, "");
        lv_label_set_text(s_proto_step, (protocol_failed_count() > 0U)
                                            ? "Run stopped at a failed step."
                                            : "Run complete.");
        lv_label_set_text(s_proto_criterion,
                          "The performance produces a record this tool does not keep.");
        lv_obj_add_state(s_proto_pass_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_proto_fail_btn, LV_STATE_DISABLED);
        return;
    }

    lv_obj_remove_state(s_proto_pass_btn, LV_STATE_DISABLED);
    lv_obj_remove_state(s_proto_fail_btn, LV_STATE_DISABLED);

    lv_label_set_text(s_proto_section, cur->section);
    (void)snprintf(line, sizeof line, "Step %u.  %s", (unsigned)cur->number, cur->text);
    lv_label_set_text(s_proto_step, line);
    (void)snprintf(line, sizeof line, "Pass when: %s", cur->criterion);
    lv_label_set_text(s_proto_criterion, line);
}

/* --- refresh -------------------------------------------------------------- */

static void refresh_cb(lv_timer_t *t)
{
    (void)t;

    /* The bus tab updates even when nothing arrives: an idle bus is exactly
     * what the operator needs to see, and a frozen screen does not say it. */
    refresh_bus_tab();

    const uint32_t gen = model_generation();
    if (gen == s_last_generation) {
        return;
    }
    s_last_generation = gen;

    rebuild_choices();
    refresh_liveness_tab();
    refresh_nodes_tab();
    refresh_live_tab();
    refresh_values_tab();
}

/* --- build ---------------------------------------------------------------- */

static void build_header(lv_obj_t *parent)
{
    lv_obj_t *hdr = lv_obj_create(parent);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, PANEL_W, IG_HEADER_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, IG_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(hdr, IG_GAP, 0);
    lv_obj_set_style_pad_right(hdr, IG_GAP, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hdr, 8, 0);

    /* The real mark, from the repo's own SVG. */
    lv_obj_t *mark = lv_image_create(hdr);
    lv_image_set_src(mark, &ig_mark_img);

    ig_label(hdr, "IndustryGrow", &lv_font_montserrat_20, IG_INK_HI);
    ig_label(hdr, "cabinet bus", &lv_font_montserrat_14, IG_INK_LOW);

    lv_obj_t *spacer = lv_obj_create(hdr);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);

    ig_status_chip(hdr, &s_hdr_dot, &s_hdr_text);
}

/* --- navigation ------------------------------------------------------------
 *
 * Four directions, ENTER and BACK, and each one means the same thing wherever
 * you are. Two levels, because the screen has two: a row of tabs, and what is
 * inside the selected tab.
 *
 *   TABS     LEFT / RIGHT   previous / next tab, wrapping
 *            DOWN or ENTER  go into the tab
 *            UP, BACK       nothing, this is the top
 *
 *   CONTENT  any direction  move to the nearest widget THAT WAY
 *            ENTER          activate the focused widget
 *            BACK           back out to the tabs
 *            UP with nothing above it also backs out, because the tab row is
 *            what is above the content on the screen
 *
 * Direction is resolved against the LAYOUT, not against creation order. LVGL's
 * own keypad navigation walks the group in the order widgets were built, which
 * on any real screen is not the order they are arranged in -- pressing DOWN
 * lands somewhere to the left and up, and the control stops being predictable.
 * A person pressing RIGHT means the thing to the right of what is lit up. */

typedef enum { NAV_TABS = 0, NAV_CONTENT } nav_level_t;

static lv_obj_t   *s_tv;
static nav_level_t s_level;

#define NAV_MAX 48
static lv_obj_t *s_nav[NAV_MAX];
static uint32_t  s_nav_count;

static void nav_collect(lv_obj_t *parent)
{
    const uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, i);
        if (lv_obj_is_group_def(c) && (s_nav_count < NAV_MAX)) {
            /* Focus has to be VISIBLE. Without a ring, every push looks like it
             * did nothing and the operator presses it again. */
            lv_obj_set_style_outline_color(c, IG_SERIES, LV_STATE_FOCUSED);
            lv_obj_set_style_outline_width(c, 3, LV_STATE_FOCUSED);
            lv_obj_set_style_outline_opa(c, LV_OPA_COVER, LV_STATE_FOCUSED);
            s_nav[s_nav_count++] = c;
            lv_group_add_obj(lv_group_get_default(), c);
        }
        nav_collect(c);
    }
}

/* Rebuilt from the VISIBLE TAB on every tab change. A tabview's inactive pages
 * are scrolled aside rather than hidden, so one group built once walks the
 * focus off the screen into widgets nobody can see. */
static void nav_rebuild(void)
{
    lv_group_t *g = lv_group_get_default();
    if ((g == NULL) || (s_tv == NULL)) {
        return;
    }
    lv_group_remove_all_objs(g);
    s_nav_count = 0;

    lv_obj_t *page = lv_obj_get_child(lv_tabview_get_content(s_tv),
                                      (int32_t)lv_tabview_get_tab_active(s_tv));
    if (page != NULL) {
        nav_collect(page);
    }
}

/* The tab row is lit while it holds the navigation, so the two levels are told
 * apart at a glance rather than by remembering which one you are in. */
static void nav_show_level(void)
{
    if (s_tv == NULL) {
        return;
    }
    lv_obj_t *bar = lv_tabview_get_tab_bar(s_tv);
    const bool on = (s_level == NAV_TABS);
    lv_obj_set_style_outline_color(bar, IG_SERIES, 0);
    lv_obj_set_style_outline_width(bar, on ? 3 : 0, 0);
    lv_obj_set_style_outline_opa(bar, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

static void nav_center(lv_obj_t *o, int32_t *cx, int32_t *cy)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    *cx = (a.x1 + a.x2) / 2;
    *cy = (a.y1 + a.y2) / 2;
}

/* The nearest candidate in the requested direction, or NULL.
 *
 * Score is the travel along the axis being moved plus twice the drift across
 * it: a widget straight ahead beats a nearer one off to the side, which is what
 * "down" has to mean on a screen laid out in columns. */
static lv_obj_t *nav_nearest(lv_obj_t *from, panel_nav_t dir)
{
    int32_t fx;
    int32_t fy;
    nav_center(from, &fx, &fy);

    lv_obj_t *best       = NULL;
    int32_t   best_score = INT32_MAX;

    for (uint32_t i = 0; i < s_nav_count; i++) {
        lv_obj_t *c = s_nav[i];
        if (c == from) {
            continue;
        }
        int32_t cx;
        int32_t cy;
        nav_center(c, &cx, &cy);

        const int32_t dx = cx - fx;
        const int32_t dy = cy - fy;

        int32_t along;
        int32_t across;
        switch (dir) {
        case PANEL_NAV_LEFT:
            along  = -dx;
            across = (dy < 0) ? -dy : dy;
            break;
        case PANEL_NAV_RIGHT:
            along  = dx;
            across = (dy < 0) ? -dy : dy;
            break;
        case PANEL_NAV_UP:
            along  = -dy;
            across = (dx < 0) ? -dx : dx;
            break;
        default:
            along  = dy;
            across = (dx < 0) ? -dx : dx;
            break;
        }
        /* Strictly that way, and no more sideways than forwards -- otherwise
         * "up" reaches something that is really beside you. */
        if ((along <= 0) || (across > along)) {
            continue;
        }
        const int32_t score = along + (2 * across);
        if (score < best_score) {
            best_score = score;
            best       = c;
        }
    }
    return best;
}

static void nav_enter_content(void)
{
    if (s_nav_count == 0U) {
        return; /* nothing to focus: stay on the tabs rather than trap the user */
    }
    s_level = NAV_CONTENT;
    /* Topmost, then leftmost -- where reading starts. */
    lv_obj_t *first = s_nav[0];
    int32_t   bx;
    int32_t   by;
    nav_center(first, &bx, &by);
    for (uint32_t i = 1; i < s_nav_count; i++) {
        int32_t cx;
        int32_t cy;
        nav_center(s_nav[i], &cx, &cy);
        if ((cy < by) || ((cy == by) && (cx < bx))) {
            first = s_nav[i];
            bx    = cx;
            by    = cy;
        }
    }
    lv_group_focus_obj(first);
    nav_show_level();
}

static void nav_leave_content(void)
{
    s_level = NAV_TABS;
    lv_group_focus_obj(NULL);
    nav_show_level();
}

static void nav_tab_step(int32_t dir)
{
    const uint32_t n = lv_tabview_get_tab_count(s_tv);
    if (n == 0U) {
        return;
    }
    /* Wraps, so the far tab is one push away rather than five. */
    const int32_t next = ((int32_t)lv_tabview_get_tab_active(s_tv) + dir + (int32_t)n) % (int32_t)n;
    lv_tabview_set_active(s_tv, (uint32_t)next, LV_ANIM_OFF);
    nav_rebuild();
}

/* Returns true when the UI consumed the event. Declining hands the matching
 * LV_KEY back to LVGL, which is what lets an OPEN dropdown keep its own
 * up/down/enter -- inside a list, "down" means the next option, and taking that
 * away would be the same mistake as ignoring the direction in the first
 * place. */
static bool nav_event(panel_nav_t ev)
{
    if (s_tv == NULL) {
        return false;
    }

    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
    if ((focused != NULL) && lv_obj_check_type(focused, &lv_dropdown_class) &&
        lv_dropdown_is_open(focused)) {
        /* BACK closes it; everything else belongs to the open list. */
        if (ev == PANEL_NAV_BACK) {
            lv_dropdown_close(focused);
            return true;
        }
        return false;
    }

    if (s_level == NAV_TABS) {
        switch (ev) {
        case PANEL_NAV_LEFT:
            nav_tab_step(-1);
            return true;
        case PANEL_NAV_RIGHT:
            nav_tab_step(1);
            return true;
        case PANEL_NAV_DOWN:
        case PANEL_NAV_ENTER:
            nav_enter_content();
            return true;
        default:
            return true; /* UP and BACK: already at the top */
        }
    }

    switch (ev) {
    case PANEL_NAV_BACK:
        nav_leave_content();
        return true;
    case PANEL_NAV_ENTER:
        return false; /* LVGL activates whatever is focused */
    default:
        break;
    }

    if (focused == NULL) {
        nav_enter_content();
        return true;
    }
    lv_obj_t *target = nav_nearest(focused, ev);
    if (target != NULL) {
        lv_group_focus_obj(target);
        lv_obj_scroll_to_view(target, LV_ANIM_OFF);
        return true;
    }
    /* Nothing that way. Off the top is the tab row, which is what is physically
     * above the content; the other three edges just hold. */
    if (ev == PANEL_NAV_UP) {
        nav_leave_content();
    }
    return true;
}

void ui_build(void)
{
    ig_theme_apply();

    lv_obj_t *scr = lv_screen_active();
    build_header(scr);

    lv_obj_t *tv = lv_tabview_create(scr);
    s_tv         = tv;
    lv_tabview_set_tab_bar_size(tv, IG_TABBAR_H);
    lv_obj_set_size(tv, PANEL_W, PANEL_H - IG_HEADER_H);
    lv_obj_set_pos(tv, 0, IG_HEADER_H);
    lv_obj_set_style_bg_color(tv, IG_GROUND, 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);

    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, IG_PANEL_HI, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(bar, IG_INK_MID, 0);
    lv_obj_set_style_text_font(bar, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(bar, IG_INK_HI, LV_STATE_CHECKED);

    lv_obj_t *content = lv_tabview_get_content(tv);
    lv_obj_set_style_bg_color(content, IG_GROUND, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);

    build_bus_tab(lv_tabview_add_tab(tv, "Bus"));
    build_liveness_tab(lv_tabview_add_tab(tv, "Liveness"));
    build_nodes_tab(lv_tabview_add_tab(tv, "Nodes"));
    build_live_tab(lv_tabview_add_tab(tv, "Plot"));
    build_values_tab(lv_tabview_add_tab(tv, "Values"));
    build_protocol_tab(lv_tabview_add_tab(tv, "Protocol"));

    lvgl_port_set_nav_cb(nav_event);
    nav_rebuild();
    nav_leave_content();

    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
}
