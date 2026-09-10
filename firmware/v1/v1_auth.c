#include "v1_auth.h"

#include "v1_api.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
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

void v1_auth_paint_pin(lv_obj_t *scr)
{
    if (v1_connect_awaiting_server("", v1_state_get())) {
        v1_connect_paint_connecting(scr);
        return;
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    v1_ui_paint_face(scr, v1_connect_user_index(s_pick_id), 12, 6);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, v1_connect_user_name(s_pick_id));
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 14);
    lv_obj_t *dots = lv_label_create(scr);
    lv_obj_set_style_text_color(dots, lv_color_hex(0xE8C040), 0);
    lv_obj_align(dots, LV_ALIGN_TOP_MID, 0, 38);
    v1_ui_bind_dots(dots);
    v1_ui_refresh_dots(s_elen);
    lv_obj_t *status = lv_label_create(scr);
    lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 54);
    v1_ui_bind_status(status);
    if (pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        v1_ui_set_status(status, line, V1_UI_ERROR);
    } else {
        v1_ui_set_status(status, "", 0xA8B0B8);
    }
    const char *keys[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    const int btn_w = 88;
    const int btn_h = 40;
    const int gap_x = 10;
    const int gap_y = 8;
    const int start_x = (320 - 3 * btn_w - 2 * gap_x) / 2;
    const int start_y = 72;
    for (int i = 0; i < 9; i++) {
        int row = i / 3;
        int col = i % 3;
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, start_x + col * (btn_w + gap_x), start_y + row * (btn_h + gap_y));
        lv_obj_set_size(b, btn_w, btn_h);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, keys[i]);
        lv_obj_center(t);
        lv_obj_add_event_cb(b, on_pin_key_event, LV_EVENT_CLICKED, (void *)keys[i]);
    }
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
