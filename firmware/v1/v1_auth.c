#include "v1_auth.h"

#include "v1_api.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "board.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "v1_auth";

static SemaphoreHandle_t s_login_work;
static volatile bool s_pin_login_busy;
static uint32_t s_login_generation;
static char s_pick_id[16];
static char s_entry[V1_ENTRY_MAX + 1];
static size_t s_elen;
static char s_login_pending_user[16];
static char s_login_pending_pin[5];
static uint32_t s_login_pending_gen;
static int s_pin_fails;
static char s_pin_fail_user[16];
static int64_t s_pin_lock_until_us;
static bool s_login_pin_reset;

static void (*s_on_login_ok_cb)(const char *user_id, bool pin_reset);

void v1_auth_set_login_ok_cb(void (*cb)(const char *user_id, bool pin_reset))
{
    s_on_login_ok_cb = cb;
}

static void on_pin_key_event(lv_event_t *e);

void v1_auth_init(SemaphoreHandle_t work_sem)
{
    s_login_work = work_sem;
    s_pin_login_busy = false;
    s_login_generation = 0;
    s_pick_id[0] = 0;
    s_entry[0] = 0;
    s_elen = 0;
    s_pin_fails = 0;
    s_pin_fail_user[0] = 0;
    s_pin_lock_until_us = 0;
}

void v1_auth_start_task(void)
{
    xTaskCreate(v1_auth_login_task, "login", 8192, NULL, 5, NULL);
}

bool v1_auth_login_active(void)
{
    return s_pin_login_busy;
}

uint32_t v1_auth_generation(void)
{
    return s_login_generation;
}

void v1_auth_set_pick_id(const char *id)
{
    if (!id) {
        s_pick_id[0] = 0;
        return;
    }
    strncpy(s_pick_id, id, sizeof(s_pick_id) - 1);
    s_pick_id[sizeof(s_pick_id) - 1] = 0;
}

const char *v1_auth_pick_id(void)
{
    return s_pick_id;
}

size_t v1_auth_entry_len(void)
{
    return s_elen;
}

void v1_auth_clear_entry(void)
{
    s_elen = 0;
    s_entry[0] = 0;
}

void v1_auth_set_entry_len(size_t elen)
{
    s_elen = elen;
}

static bool pin_locked(void)
{
    return s_pin_lock_until_us > esp_timer_get_time();
}

static int pin_lock_sec(void)
{
    int64_t left = s_pin_lock_until_us - esp_timer_get_time();
    if (left <= 0) {
        return 0;
    }
    return (int)((left + 999999) / 1000000);
}

void v1_auth_on_roster_pick(const char *user_id)
{
    if (!user_id) {
        return;
    }
    v1_state_post_pick(V1_EV_GOTO_PIN, user_id);
    v1_auth_clear_entry();
    v1_ui_request_repaint();
    v1_ui_bump_activity();
}

void v1_auth_on_pin_key(const char *key)
{
    if (!key || v1_state_get() != ST_PIN) {
        return;
    }
    if (s_pin_login_busy) {
        return;
    }
    if (pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        v1_ui_set_status(NULL, line, V1_UI_ERROR);
        return;
    }
    if (s_elen >= V1_ENTRY_MAX) {
        return;
    }
    s_entry[s_elen++] = key[0];
    s_entry[s_elen] = 0;
    v1_ui_refresh_dots(s_elen);
    if (s_elen < 4) {
        return;
    }
    if (s_pin_login_busy) {
        return;
    }
    if (!v1_connect_online()) {
        v1_connect_enter_from_signin();
        return;
    }
    s_login_generation++;
    s_login_pending_gen = s_login_generation;
    strncpy(s_login_pending_user, s_pick_id, sizeof(s_login_pending_user) - 1);
    s_login_pending_user[sizeof(s_login_pending_user) - 1] = 0;
    strncpy(s_login_pending_pin, s_entry, sizeof(s_login_pending_pin) - 1);
    s_login_pending_pin[sizeof(s_login_pending_pin) - 1] = 0;
    s_pin_login_busy = true;
    board_lvgl_lock(0);
    v1_ui_set_status(NULL, "checking...", V1_UI_TEXT_MUT);
    board_lvgl_unlock();
    ESP_LOGI(TAG, "pin phase event=submit st=%d busy=%d gen=%lu http=%d",
             (int)v1_state_get(), (int)s_pin_login_busy, (unsigned long)s_login_pending_gen,
             v1_connect_online() ? 1 : 0);
    xSemaphoreGive(s_login_work);
    v1_ui_bump_activity();
}

void v1_auth_login_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_login_work, portMAX_DELAY);
        char user_id[16];
        char pin[5];
        uint32_t gen = s_login_pending_gen;
        strncpy(user_id, s_login_pending_user, sizeof(user_id) - 1);
        user_id[sizeof(user_id) - 1] = 0;
        strncpy(pin, s_login_pending_pin, sizeof(pin) - 1);
        pin[sizeof(pin) - 1] = 0;

        ESP_LOGI(TAG, "pin phase event=worker_start st=%d busy=%d gen=%lu http=%d",
                 (int)v1_state_get(), (int)s_pin_login_busy, (unsigned long)gen,
                 v1_connect_online() ? 1 : 0);

        if (gen != s_login_generation || v1_state_get() != ST_PIN || s_elen != 4) {
            s_pin_login_busy = false;
            ESP_LOGI(TAG, "pin phase event=worker_stale st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, 0);
            v1_ui_request_repaint();
            continue;
        }

        if (!v1_connect_online()) {
            s_pin_login_busy = false;
            v1_connect_enter_from_signin();
            ESP_LOGI(TAG, "pin phase event=worker_connecting st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, 0);
            v1_ui_request_repaint();
            continue;
        }

        char session_user[16] = {0};
        int http = 0;
        bool pin_reset = false;
        bool ok = v1_api_login_user(user_id, pin, session_user, sizeof(session_user), &http,
                                    &pin_reset);
        s_login_pin_reset = pin_reset;

        if (gen != s_login_generation) {
            s_pin_login_busy = false;
            ESP_LOGI(TAG, "pin phase event=worker_stale st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, http);
            v1_ui_request_repaint();
            continue;
        }

        s_pin_login_busy = false;
        if (ok) {
            s_pin_fails = 0;
            s_pin_lock_until_us = 0;
            v1_auth_clear_entry();
            v1_connect_nvs_save_last(user_id);
            v1_state_post(V1_EV_AUTH_OK, gen);
            if (s_on_login_ok_cb) {
                s_on_login_ok_cb(user_id, s_login_pin_reset);
            }
            v1_ui_request_chirp(988);
            v1_ui_bump_activity();
            v1_ui_request_repaint();
            ESP_LOGI(TAG, "pin phase event=worker_ok st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, http);
            continue;
        }
        if (v1_state_get() != ST_PIN) {
            ESP_LOGI(TAG, "pin phase event=worker_fail st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, http);
            v1_ui_request_repaint();
            continue;
        }
        if (!v1_connect_online()) {
            v1_connect_enter_from_signin();
            ESP_LOGI(TAG, "pin phase event=worker_connecting st=%d busy=%d gen=%lu http=%d",
                     (int)v1_state_get(), 0, (unsigned long)gen, http);
            v1_ui_request_repaint();
            continue;
        }
        if (strcmp(s_pin_fail_user, user_id) != 0) {
            strncpy(s_pin_fail_user, user_id, sizeof(s_pin_fail_user) - 1);
            s_pin_fails = 0;
        }
        s_pin_fails++;
        v1_auth_clear_entry();
        board_lvgl_lock(0);
        v1_ui_refresh_dots(0);
        if (s_pin_fails >= V1_AUTH_PIN_TRIES) {
            s_pin_lock_until_us = esp_timer_get_time() + (int64_t)V1_AUTH_PIN_COOLDOWN_MS * 1000;
            v1_ui_set_status(NULL, "ask Lynn", V1_UI_ERROR);
        } else {
            v1_ui_set_status(NULL, "wrong pin", V1_UI_ERROR);
        }
        board_lvgl_unlock();
        ESP_LOGI(TAG, "pin phase event=worker_fail st=%d busy=%d gen=%lu http=%d",
                 (int)v1_state_get(), 0, (unsigned long)gen, http);
    }
}

void v1_auth_on_login_ok(void)
{
    v1_auth_clear_entry();
}

void v1_auth_on_boot_press(void)
{
    if (v1_state_get() != ST_PIN) {
        return;
    }
    if (s_pin_login_busy) {
        return;
    }
    if (s_elen > 0) {
        v1_auth_clear_entry();
        v1_ui_refresh_dots(0);
        if (!pin_locked()) {
            v1_ui_set_status(NULL, "", 0xA8B0B8);
        }
    } else {
        v1_state_post(V1_EV_GOTO_ROSTER, 0);
        v1_ui_request_repaint();
    }
}

static const lv_font_t *pin_title_font(void)
{
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    return &lv_font_montserrat_22;
#else
    return LV_FONT_DEFAULT;
#endif
}

static int pin_text_width(const char *text, const lv_font_t *font)
{
    lv_point_t sz = {0, 0};
    if (!text || !text[0] || !font) {
        return 0;
    }
    lv_text_get_size(&sz, text, font, 0, 0, INT32_MAX, LV_TEXT_FLAG_NONE);
    return (int)sz.x;
}

/** Fit "Sign in as" into budget_px by trimming from the right, then "...". */
static void pin_fit_sign_in_prefix(char *out, size_t cap, int budget_px, const lv_font_t *font)
{
    static const char full[] = "Sign in as";
    if (!out || cap == 0) {
        return;
    }
    out[0] = 0;
    if (budget_px <= 0) {
        return;
    }
    if (pin_text_width(full, font) <= budget_px) {
        snprintf(out, cap, "%s", full);
        return;
    }
    size_t n = sizeof(full) - 1;
    for (int keep = (int)n - 1; keep >= 1; keep--) {
        char stem[16];
        if ((size_t)keep >= sizeof(stem)) {
            keep = (int)sizeof(stem) - 1;
        }
        memcpy(stem, full, (size_t)keep);
        stem[keep] = 0;
        while (keep > 0 && stem[keep - 1] == ' ') {
            stem[--keep] = 0;
        }
        if (keep <= 0) {
            break;
        }
        char trial[24];
        snprintf(trial, sizeof(trial), "%s...", stem);
        if (pin_text_width(trial, font) <= budget_px) {
            snprintf(out, cap, "%s", trial);
            return;
        }
    }
    if (pin_text_width("...", font) <= budget_px) {
        snprintf(out, cap, "...");
    }
}

void v1_auth_paint_pin(lv_obj_t *scr)
{
    if (v1_connect_awaiting_server("", v1_state_get())) {
        v1_connect_paint_connecting(scr);
        return;
    }

    v1_connect_invalidate_roster();
    v1_connect_set_offline_lab(NULL);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    /* Same ribbon + header/keypad split as carousel / sign-in. */
    lv_obj_t *ribbon_top, *ribbon_bot, *count_lab, *offline_lab, *toast;
    v1_ui_paint_ribbons(scr, v1_connect_online(), "", &ribbon_top, &ribbon_bot,
                        &count_lab, &offline_lab, &toast);
    v1_connect_set_offline_lab(offline_lab);
    if (count_lab) {
        lv_label_set_text(count_lab, "");
    }
    v1_ui_bind_toast(toast);

    const lv_font_t *font = pin_title_font();
    const int face_sz = 28;
    const int gap = 8;
    const int max_cluster_w = (V1_LCD_W * 2) / 3;
    const int header_y_ofs = (V1_CAROUSEL_HEADER_Y + V1_CAROUSEL_HEADER_H / 2)
                             - (V1_CONTENT_H + 2 * V1_RIBBON_H) / 2;
    const char *uname = v1_connect_user_name(s_pick_id);
    int name_w = pin_text_width(uname, font);
    int prefix_budget = max_cluster_w - face_sz - 2 * gap - name_w;
    char prefix[24];
    pin_fit_sign_in_prefix(prefix, sizeof(prefix), prefix_budget, font);

    int x = V1_CAROUSEL_PLAY_PAD;
    lv_obj_t *as_lab = lv_label_create(scr);
    lv_label_set_text(as_lab, prefix);
    lv_obj_set_style_text_color(as_lab, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_set_style_text_font(as_lab, font, 0);
    lv_obj_align(as_lab, LV_ALIGN_LEFT_MID, x, header_y_ofs);
    int prefix_w = pin_text_width(prefix, font);
    if (prefix_w > 0) {
        x += prefix_w + gap;
    }

    int uid = v1_connect_user_index(s_pick_id);
    v1_ui_paint_user_portrait_aligned(scr, uid, face_sz, LV_ALIGN_LEFT_MID, x, header_y_ofs);
    x += face_sz + gap;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, uname);
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_set_style_text_font(title, font, 0);
    int name_max = max_cluster_w - (x - V1_CAROUSEL_PLAY_PAD);
    if (name_max < 24) {
        name_max = 24;
    }
    lv_obj_set_width(title, name_max);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, x, header_y_ofs);

    /* Right-justified masked PIN dots in the same upper third. */
    lv_obj_t *entry = lv_label_create(scr);
    lv_obj_set_style_text_color(entry, lv_color_hex(V1_UI_ACCENT), 0);
    lv_obj_set_style_text_font(entry, font, 0);
    lv_obj_set_style_text_align(entry, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(entry, V1_LCD_W - max_cluster_w - V1_CAROUSEL_PLAY_PAD);
    lv_obj_align(entry, LV_ALIGN_RIGHT_MID, -V1_CAROUSEL_PLAY_PAD, header_y_ofs);
    v1_ui_bind_dots(entry);
    v1_ui_refresh_dots(s_elen);

    lv_obj_t *status = lv_label_create(scr);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(status, V1_LCD_W - max_cluster_w - V1_CAROUSEL_PLAY_PAD);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -V1_CAROUSEL_PLAY_PAD, header_y_ofs + 18);
    v1_ui_bind_status(status);
    if (pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        v1_ui_set_status(status, line, V1_UI_ERROR);
    } else {
        v1_ui_set_status(status, "", 0xA8B0B8);
    }

    /* Keypad fills the lower 2/3 content band (same Y/H as carousel cards). */
    const char *keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    const int pad = 4;
    const int gap_x = 4;
    const int gap_y = 4;
    const int btn_w = (V1_LCD_W - 2 * pad - 2 * gap_x) / 3;
    const int btn_h = (V1_SCROLL_CARD_H - 2 * pad - 2 * gap_y) / 3;
    const int start_x = pad;
    const int start_y = V1_SCROLL_CARD_Y + pad;
    for (int i = 0; i < 9; i++) {
        int row = i / 3;
        int col = i % 3;
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, start_x + col * (btn_w + gap_x), start_y + row * (btn_h + gap_y));
        lv_obj_set_size(b, btn_w, btn_h);
        lv_obj_set_style_radius(b, V1_CARD_RADIUS, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(V1_UI_CARD), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(V1_UI_CARD_PRESS), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, keys[i]);
        lv_obj_set_style_text_color(t, lv_color_hex(V1_UI_TEXT), 0);
        lv_obj_set_style_text_font(t, font, 0);
        lv_obj_center(t);
        lv_obj_add_event_cb(b, on_pin_key_event, LV_EVENT_CLICKED, (void *)keys[i]);
    }
    ESP_LOGI(TAG, "paint pin heap=%u pick=%s", (unsigned)esp_get_free_heap_size(),
             s_pick_id[0] ? s_pick_id : "-");
    v1_ui_hook_scr(scr);
}

static void on_pin_key_event(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    v1_auth_on_pin_key(key);
}

void v1_auth_tick(state_t st)
{
    if (st == ST_PIN && pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        v1_ui_set_status(NULL, line, V1_UI_ERROR);
    } else if (st == ST_PIN && s_pin_fails >= V1_AUTH_PIN_TRIES && !pin_locked()) {
        s_pin_fails = 0;
        v1_ui_set_status(NULL, "", 0xA8B0B8);
    }
}
