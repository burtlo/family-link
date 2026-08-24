/*
 * h19 — Scrollable inbox list + screen transition manager.
 *
 * Client-side only. Hard-coded audio-message records (same shape as h18:
 * sender, sent_at, duration_ms, position_ms, read). Tap a row to slide to
 * a detail view; Back slides to the list. No server, no codec.
 *
 * -- PASS h19 after one list→detail and one Back.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"

static const char *TAG = "h19";

typedef struct {
    const char *id;
    const char *sender;
    const char *sent_at;
    int duration_ms;
    int position_ms;
    bool read;
} audio_msg_t;

static const audio_msg_t s_msgs[] = {
    { "m1", "Dad", "Sun 3:04 PM", 4500, 800, false },
    { "m2", "Mom", "Sun 11:20 AM", 8200, 0, true },
    { "m3", "Dad", "Sat 8:41 PM", 2100, 0, false },
    { "m4", "Mom", "Sat 7:02 PM", 15300, 4000, true },
    { "m5", "Dad", "Fri 6:15 PM", 9800, 0, false },
    { "m6", "Mom", "Fri 12:03 PM", 3300, 1200, true },
    { "m7", "Dad", "Thu 9:50 PM", 6400, 0, false },
    { "m8", "Mom", "Thu 4:22 PM", 12100, 0, true },
};

#define MSG_N ((int)(sizeof(s_msgs) / sizeof(s_msgs[0])))

static lv_obj_t *s_list_scr;
static lv_obj_t *s_detail_scr;
static int s_open = -1;
static bool s_saw_detail;
static bool s_saw_back;
static bool s_passed;
static lv_obj_t *s_d_sender;
static lv_obj_t *s_d_when;
static lv_obj_t *s_d_meta;
static lv_obj_t *s_d_bar;
static lv_obj_t *s_d_clock;
static lv_obj_t *s_d_play;
static int s_detail_pos;

static void fmt_clock(char *out, size_t cap, int pos_ms, int dur_ms)
{
    int p = pos_ms / 1000;
    int d = dur_ms / 1000;
    snprintf(out, cap, "%d:%02d / %d:%02d", p / 60, p % 60, d / 60, d % 60);
}

static void nav_load(lv_obj_t *scr, bool push)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_screen_load_anim(scr, push ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, 260,
                        0, false);
#else
    lv_scr_load_anim(scr, push ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, 260, 0,
                     false);
#endif
}

static void maybe_pass(void)
{
    if (!s_passed && s_saw_detail && s_saw_back) {
        s_passed = true;
        demo_pass("h19");
    }
}

static void fill_detail(int idx)
{
    if (idx < 0 || idx >= MSG_N) {
        return;
    }
    const audio_msg_t *m = &s_msgs[idx];
    lv_label_set_text(s_d_sender, m->sender);
    lv_label_set_text(s_d_when, m->sent_at);
    lv_label_set_text(s_d_meta, m->read ? "read" : "unread");
    lv_obj_set_style_text_color(s_d_meta, lv_color_hex(m->read ? 0xA8B0B8 : 0xE8C040), 0);
    lv_bar_set_range(s_d_bar, 0, m->duration_ms > 0 ? m->duration_ms : 1);
    lv_bar_set_value(s_d_bar, m->position_ms, LV_ANIM_OFF);
    char line[32];
    fmt_clock(line, sizeof(line), m->position_ms, m->duration_ms);
    lv_label_set_text(s_d_clock, line);
    lv_label_set_text(s_d_play, "Play");
}

static void nav_open_detail(int idx)
{
    s_open = idx;
    s_saw_detail = true;
    ESP_LOGI(TAG, "open %s (%s)", s_msgs[idx].id, s_msgs[idx].sender);
    s_detail_pos = s_msgs[idx].position_ms;
    fill_detail(idx);
    nav_load(s_detail_scr, true);
}

static void nav_back_list(void)
{
    s_saw_back = true;
    ESP_LOGI(TAG, "back to list");
    nav_load(s_list_scr, false);
    maybe_pass();
}

static void on_row(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    nav_open_detail(idx);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    nav_back_list();
}

static void on_fake_play(lv_event_t *e)
{
    (void)e;
    if (s_open < 0) {
        return;
    }
    s_detail_pos += 800;
    if (s_detail_pos > s_msgs[s_open].duration_ms) {
        s_detail_pos = 0;
    }
    /* Local bar only — this demo is navigation, not the codec. */
    lv_bar_set_value(s_d_bar, s_detail_pos, LV_ANIM_ON);
    char line[32];
    fmt_clock(line, sizeof(line), s_detail_pos, s_msgs[s_open].duration_ms);
    lv_label_set_text(s_d_clock, line);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *title, int w, int h)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_size(btn, w, h);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, title);
    lv_obj_center(lab);
    return btn;
}

static void paint_row(lv_obj_t *list, int idx, int y)
{
    const audio_msg_t *m = &s_msgs[idx];
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, 296, 52);
    lv_obj_set_pos(row, 12, y);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2A3038), 0);
    lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, 10, 22);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(m->read ? 0x3A4048 : 0xE8C040), 0);

    lv_obj_t *who = lv_label_create(row);
    lv_label_set_text(who, m->sender);
    lv_obj_set_style_text_color(who, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_pos(who, 28, 6);

    lv_obj_t *when = lv_label_create(row);
    lv_label_set_text(when, m->sent_at);
    lv_obj_set_style_text_color(when, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(when, 28, 28);

    char dur[16];
    snprintf(dur, sizeof(dur), "%d:%02d", (m->duration_ms / 1000) / 60, (m->duration_ms / 1000) % 60);
    lv_obj_t *len = lv_label_create(row);
    lv_label_set_text(len, dur);
    lv_obj_set_style_text_color(len, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(len, LV_ALIGN_RIGHT_MID, -12, 0);
}

static void build_list(void)
{
    s_list_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_list_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_list_scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_list_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_list_scr, 0, 0);

    lv_obj_t *title = lv_label_create(s_list_scr);
    lv_label_set_text(title, "messages");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A9298), 0);
    lv_obj_set_pos(title, 16, 8);

    lv_obj_t *list = lv_obj_create(s_list_scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 320, 200);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);

    const int row_h = 56;
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < MSG_N; i++) {
        paint_row(list, i, 4 + i * row_h);
    }
    /* Spacer so the scroll parent is taller than 200 px. */
    lv_obj_t *end = lv_obj_create(list);
    lv_obj_remove_style_all(end);
    lv_obj_remove_flag(end, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, 4 + MSG_N * row_h);
}

static void build_detail(void)
{
    s_detail_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_detail_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_detail_scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_detail_scr, LV_OPA_COVER, 0);

    lv_obj_t *back = make_btn(s_detail_scr, "Back", 72, 32);
    lv_obj_set_pos(back, 8, 6);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    s_d_meta = lv_label_create(s_detail_scr);
    lv_label_set_text(s_d_meta, "unread");
    lv_obj_align(s_d_meta, LV_ALIGN_TOP_RIGHT, -12, 12);

    s_d_sender = lv_label_create(s_detail_scr);
    lv_label_set_text(s_d_sender, "");
    lv_obj_set_style_text_color(s_d_sender, lv_color_hex(0xE8F0E8), 0);
#if defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(s_d_sender, &lv_font_montserrat_22, 0);
#endif
    lv_obj_align(s_d_sender, LV_ALIGN_TOP_LEFT, 16, 48);

    s_d_when = lv_label_create(s_detail_scr);
    lv_label_set_text(s_d_when, "");
    lv_obj_set_style_text_color(s_d_when, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_d_when, LV_ALIGN_TOP_LEFT, 16, 80);

    s_d_bar = lv_bar_create(s_detail_scr);
    lv_obj_set_size(s_d_bar, 288, 14);
    lv_obj_align(s_d_bar, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_bg_color(s_d_bar, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_bg_opa(s_d_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_d_bar, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_d_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_d_clock = lv_label_create(s_detail_scr);
    lv_label_set_text(s_d_clock, "");
    lv_obj_set_style_text_color(s_d_clock, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_d_clock, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t *play = make_btn(s_detail_scr, "Play", 140, 48);
    lv_obj_align(play, LV_ALIGN_BOTTOM_MID, 0, -16);
    s_d_play = lv_obj_get_child(play, 0);
    lv_obj_add_event_cb(play, on_fake_play, LV_EVENT_CLICKED, NULL);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h19", "display");
        return;
    }
    if (!board_lvgl_lock(1000)) {
        demo_fail("h19", "lvgl lock");
        return;
    }
    build_list();
    build_detail();
#if LVGL_VERSION_MAJOR >= 9
    lv_screen_load(s_list_scr);
#else
    lv_scr_load(s_list_scr);
#endif
    board_lvgl_unlock();
    ESP_LOGI(TAG, "%d messages. tap a row, then Back.", MSG_N);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
