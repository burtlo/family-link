/*
 * x02 — v1 product shell: roster, PIN, carousel, playback, record.
 *
 * Host: python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
 * HTTPS desk: DEMO_SERVER_TLS 1 + make v1-server-tls (h16 skip-verify).
 * -- PASS x02 after login and carousel paints inbox.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"
#include "nvs.h"

#include <time.h>

#include "board.h"
#include "http_bearer.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

#if __has_include("assets/avatars/avatars.h")
#include "assets/avatars/avatars.h"
#endif

#ifndef DEMO_SERVER_TLS
#define DEMO_SERVER_TLS 0
#endif
#ifndef DEMO_TLS_SKIP_VERIFY
#define DEMO_TLS_SKIP_VERIFY 1
#endif

#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#endif

static const char *TAG = "x02";

#define USER_MAX       8
#define MSG_MAX        16
#define ENTRY_MAX      8
#define IDLE_MS        60000
#define DIM_MS         120000
#define SLEEP_MS       300000
#define BRIGHT_NORM    80
#define BRIGHT_DIM     32
#define BRIGHT_SLEEP       4
#define BRIGHT_SLEEP_PEAK  6
#define SLEEP_FADE_MS      1500
#define SLEEP_BREATHE_MS   5000
#define SLEEP_HINT_MS      25000
#define SLEEP_HINT_PULSE_MS 900
#define UI_SLEEP_BG        0x080A0C
#define JSON_CAP       4096
#define SAMPLE_RATE    16000
#define CHUNK          640
#define MAX_RECORD_SEC 180
#define SILENCE_SEC    5
#define TRIM_MS        150
#define TALK_PEAK      256
#define BUF_CAP        (320 * 1024)
#define ROOMVOL_FIRST  78
#define ROOMVOL_STEP   2
#define ROOMVOL_MAX    100
#define ROOMVOL_ON     (((ROOMVOL_MAX - ROOMVOL_FIRST) / ROOMVOL_STEP) + 1)
#define CIRCLE_DEBOUNCE_MS 400
#define PIN_TRIES      5
#define PIN_COOLDOWN_MS 60000
#define PICK_TIMEOUT_MS 10000
#define SNAP_SCROLL_MS     380
#define SNAP_SCROLL_MS_MAX 720
#define RIBBON_H       20
#define CONTENT_H      (240 - 2 * RIBBON_H)
#define LCD_W          320
#define SCROLL_CARD_W  132
#define SCROLL_CARD_H  100
#define SCROLL_CARD_GAP 12
#define SCROLL_CARD_Y  (RIBBON_H + 8)
#define CAROUSEL_TRANSPORT_Y (SCROLL_CARD_Y + SCROLL_CARD_H + 4)
#define CARD_RADIUS    8
#define CARD_PORTRAIT  22
#define CAROUSEL_PLAY  32
#define CAROUSEL_DISK  24
#define CARD_PLAY_ICON 24
#define FACE_SZ        96
#define PLAY_HIT       64
#define PLAY_DISK      40
#define AVATAR_SLOTS   12
#define ACCENT_COUNT   10
#define CARD_GRAD_N    3
#define CHIRP_MS       160
#define CHIRP_DRAIN_MS 60
#define CHIRP_VOL      70
#define CHIRP_WRITE    1024
#define CHIRP_SAMPLES  (SAMPLE_RATE * CHIRP_MS / 1000)
#define CHIRP_DRAIN    (SAMPLE_RATE * CHIRP_DRAIN_MS / 1000)
#define UI_BG          0x101418
#define UI_CARD        0x1C2228
#define UI_CARD_PRESS  0x242C34
#define UI_TEXT        0xF0F4F0
#define UI_TEXT_MUT    0xB0B8C0
#define UI_TEXT_DIM    0x788088
#define UI_ACCENT      0xE8C040
#define UI_ERROR       0xE85A5A
#define ROSTER_FACE_SZ 36
#define CONNECT_RETRY_MS  5000
#define CONNECT_PROBE_MS  2500
#define WIFI_RETRY_MS     30000
#define CONNECT_ICON_SCALE 150
#define HANGOUT_NVS_MAX   1536

typedef enum {
    ST_CONNECTING, ST_WIFI_ERR, ST_ROSTER, ST_PIN, ST_CAROUSEL, ST_SETTINGS, ST_PICK, ST_RECORD
} state_t;

typedef struct {
    char id[16];
    char name[24];
    uint8_t avatar_slot;
    uint32_t accent;
} user_t;

typedef struct {
    int seq;
    char from_label[24];
    bool read;
    int position_ms;
    int duration_ms;
} msg_t;

typedef struct {
    uint8_t id;
    uint32_t top;
    uint32_t bot;
    bool light_ui;
} card_grad_t;

typedef struct {
    lv_obj_t *card;
} carousel_card_ui_t;

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static state_t s_st = ST_CONNECTING;
static bool s_server_online;
static int64_t s_connect_start_us;
static int64_t s_conn_retry_us;
static int64_t s_conn_dot_anim_us;
static int64_t s_wifi_retry_us;
static user_t s_users[USER_MAX];
static int s_user_n;
static char s_pick_id[16];
static char s_session_user[16];
static char s_send_to[16];
static bool s_send_all;
static char s_entry[ENTRY_MAX + 1];
static size_t s_elen;
static msg_t s_msgs[MSG_MAX];
static int s_msg_n;
static int s_focus;
static int s_card_face_seq;
static bool s_card_face_read;
static int64_t s_idle_us;
static int64_t s_activity_us;
static bool s_asleep;
static bool s_dimmed;
static bool s_passed;
static volatile bool s_repaint;
static volatile bool s_stop_play;
static volatile bool s_stop_record;
static volatile bool s_cancel_pick;
static volatile bool s_want_play;
static volatile bool s_toast_pending;
static volatile int s_chirp_hz;
static volatile int s_chirp_hz2;
static char s_toast_msg[24];
static bool s_playing;
static int s_play_pos_ms;
static int s_vol_notch;
static int s_volume;
static int s_pin_fails;
static char s_pin_fail_user[16];
static char s_last_user[16];
static int64_t s_pin_lock_until_us;
static int64_t s_pick_open_us;
static bool s_scrubbing;
static bool s_bar_sync;
static volatile bool s_inbox_dirty;
static volatile bool s_transport_dirty;
static char s_inbox_for[16];
static int64_t s_carousel_ready_us;
static uint8_t s_card_grad = 1;
static bool s_scroll_lock;
static bool s_carousel_locked;
static int32_t s_snap_target;
static int64_t s_snap_t0;
static bool s_send_hint_shown;
static esp_websocket_client_handle_t s_ws;

static lv_obj_t *s_ribbon_top;
static lv_obj_t *s_ribbon_bot;
static lv_obj_t *s_count_lab;
static lv_obj_t *s_status;
static lv_obj_t *s_dots;
static lv_obj_t *s_settings_scroll;
static lv_obj_t *s_vol_val_lab;
static lv_obj_t *s_carousel_scroll;
static carousel_card_ui_t s_card_ui[MSG_MAX];
static lv_obj_t *s_bar;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_play_icon;
static lv_obj_t *s_pause_icon;
static uint16_t s_play_icon_fb[CARD_PLAY_ICON * CARD_PLAY_ICON];
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_overlay;
static lv_obj_t *s_toast;
static lv_obj_t *s_offline_lab;
static lv_obj_t *s_conn_dots;
static lv_obj_t *s_dot_circles[3];
static lv_obj_t *s_sleep_glow;
static lv_obj_t *s_sleep_hint;
static lv_obj_t *s_sleep_badge;
static lv_obj_t *s_sleep_badge_lab;
static int64_t s_sleep_enter_us;

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_buf;
static uint8_t *s_pcm;
static SemaphoreHandle_t s_work;
static SemaphoreHandle_t s_login_work;
static volatile bool s_pin_login_busy;
static bool s_spk_open;
static uint8_t s_json[JSON_CAP];
static int16_t s_chirp_pcm[CHIRP_SAMPLES + CHIRP_DRAIN];

static int64_t now_us(void) { return esp_timer_get_time(); }
static void bump_idle(void) { s_idle_us = now_us(); }
static void request_repaint(void) { s_repaint = true; }
static void request_transport_refresh(void) { s_transport_dirty = true; }

static void note_activity(void)
{
    s_activity_us = now_us();
    bump_idle();
    if (s_asleep || s_dimmed) {
        bool was_asleep = s_asleep;
        s_asleep = false;
        s_dimmed = false;
        board_backlight_set(BRIGHT_NORM);
        if (was_asleep) {
            request_repaint();
        }
    }
}

static void set_toast(const char *msg)
{
    if (s_toast) {
        lv_label_set_text(s_toast, msg ? msg : "");
    }
}

static void on_scr_activity(lv_event_t *e)
{
    (void)e;
    note_activity();
}

static void hook_scr(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, on_scr_activity, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_scr_activity, LV_EVENT_CLICKED, NULL);
}

static int roomvol_codec(int notch)
{
    if (notch <= 0) {
        return 0;
    }
    int v = ROOMVOL_FIRST + (notch - 1) * ROOMVOL_STEP;
    return v > ROOMVOL_MAX ? ROOMVOL_MAX : v;
}

static void apply_volume(int vol)
{
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    s_volume = vol;
    if (s_spk) {
        (void)esp_codec_dev_set_out_vol(s_spk, vol);
        (void)esp_codec_dev_set_out_mute(s_spk, vol == 0);
    }
}

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void format_url(char *url, size_t cap, const char *path)
{
    snprintf(url, cap, "%s://%s:%d%s",
#if DEMO_SERVER_TLS
             "https",
#else
             "http",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT, path ? path : "/");
}

static void apply_tls(esp_http_client_config_t *cfg)
{
#if DEMO_SERVER_TLS
    cfg->transport_type = HTTP_TRANSPORT_OVER_SSL;
#if DEMO_TLS_SKIP_VERIFY
    cfg->crt_bundle_attach = NULL;
    cfg->skip_cert_common_name_check = true;
#else
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
#else
    (void)cfg;
#endif
}

static bool pin_locked(void)
{
    return s_pin_lock_until_us > now_us();
}

static int pin_lock_sec(void)
{
    int64_t left = s_pin_lock_until_us - now_us();
    if (left <= 0) {
        return 0;
    }
    return (int)((left + 999999) / 1000000);
}

static const char *user_name(const char *id)
{
    for (int i = 0; i < s_user_n; i++) {
        if (id && strcmp(s_users[i].id, id) == 0 && s_users[i].name[0]) {
            return s_users[i].name;
        }
    }
    return id && id[0] ? id : "";
}

static int user_index(const char *id)
{
    for (int i = 0; i < s_user_n; i++) {
        if (id && strcmp(s_users[i].id, id) == 0) {
            return i;
        }
    }
    return 0;
}

static uint32_t default_accent(int idx)
{
    static const uint32_t hues[ACCENT_COUNT] = {
        0x5AA0E8, 0xE8C040, 0x7AC47A, 0xC070E8,
        0xE87A9A, 0x7AD4E8, 0xD4A0E8, 0xE8A87A,
        0x4ECDC4, 0xFF6B6B,
    };
    if (idx < 0) {
        idx = 0;
    }
    return hues[idx % ACCENT_COUNT];
}

static uint32_t parse_hex_color(const char *s)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    unsigned v = 0;
    if (sscanf(s + 1, "%6x", &v) != 1) {
        return 0;
    }
    return (uint32_t)v;
}

static uint32_t user_accent(int idx)
{
    if (idx < 0 || idx >= s_user_n) {
        return default_accent(0);
    }
    if (s_users[idx].accent) {
        return s_users[idx].accent;
    }
    return default_accent(idx);
}

static uint8_t user_avatar_slot(int idx)
{
    if (idx < 0 || idx >= s_user_n) {
        return 0;
    }
    return s_users[idx].avatar_slot;
}

static void apply_profile_json(cJSON *prof, user_t *u)
{
    if (!prof || !u) {
        return;
    }
    cJSON *slot = cJSON_GetObjectItem(prof, "avatar_slot");
    cJSON *accent = cJSON_GetObjectItem(prof, "accent_hex");
    if (cJSON_IsNumber(slot)) {
        int v = slot->valueint;
        if (v < 0) {
            v = 0;
        }
        if (v > AVATAR_SLOTS) {
            v = AVATAR_SLOTS;
        }
        u->avatar_slot = (uint8_t)v;
    }
    if (cJSON_IsString(accent)) {
        uint32_t c = parse_hex_color(accent->valuestring);
        if (c) {
            u->accent = c;
        }
    }
}

static const lv_image_dsc_t *avatar_image(uint8_t slot)
{
#if defined(AVATAR_SLOT_COUNT) && AVATAR_SLOT_COUNT > 0
    if (slot >= 1 && slot <= AVATAR_SLOT_COUNT) {
        return avatar_slots[slot - 1];
    }
#endif
    return NULL;
}

static int user_index_by_label(const char *label)
{
    if (!label || !label[0]) {
        return 0;
    }
    for (int i = 0; i < s_user_n; i++) {
        if (strcmp(s_users[i].name, label) == 0 || strcmp(s_users[i].id, label) == 0) {
            return i;
        }
    }
    return 0;
}

static void nvs_load_last(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof(s_last_user);
    (void)nvs_get_str(h, "last", s_last_user, &n);
    nvs_close(h);
}

static const card_grad_t s_card_grads[CARD_GRAD_N] = {
    {1, 0x5AA0E8, 0x101418, false},
    {2, 0xE85A5A, 0xE8C040, false},
    {5, 0xF0F2F5, 0x8898A8, true},
};

static bool card_grad_id_valid(uint8_t id)
{
    for (int i = 0; i < CARD_GRAD_N; i++) {
        if (s_card_grads[i].id == id) {
            return true;
        }
    }
    return false;
}

static const card_grad_t *active_card_grad(void)
{
    for (int i = 0; i < CARD_GRAD_N; i++) {
        if (s_card_grads[i].id == s_card_grad) {
            return &s_card_grads[i];
        }
    }
    return &s_card_grads[0];
}

static void nvs_load_card_grad(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t g = 1;
    if (nvs_get_u8(h, "card_grad", &g) == ESP_OK && card_grad_id_valid(g)) {
        s_card_grad = g;
    }
    nvs_close(h);
}

static void nvs_save_card_grad(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(h, "card_grad", s_card_grad);
    (void)nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_last(const char *id)
{
    if (!id || !id[0]) {
        return;
    }
    strncpy(s_last_user, id, sizeof(s_last_user) - 1);
    s_last_user[sizeof(s_last_user) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_str(h, "last", id);
    (void)nvs_commit(h);
    nvs_close(h);
}

static void server_mark_online(void)
{
    if (!s_server_online) {
        s_server_online = true;
        request_repaint();
    }
}

static void server_mark_offline(void)
{
    if (s_server_online) {
        s_server_online = false;
        request_repaint();
    }
}

static bool signed_out_pre_auth(void)
{
    return s_session_user[0] == 0 && s_st != ST_WIFI_ERR;
}

static void enter_connecting_from_signin(void)
{
    if (s_pin_login_busy) {
        return;
    }
    s_elen = 0;
    s_entry[0] = 0;
    if (s_st != ST_CONNECTING) {
        s_st = ST_CONNECTING;
        request_repaint();
    }
}

static int parse_hangout_users(cJSON *users, user_t *out, int max)
{
    if (!cJSON_IsArray(users)) {
        return 0;
    }
    int loaded = 0;
    int n = cJSON_GetArraySize(users);
    for (int i = 0; i < n && loaded < max; i++) {
        cJSON *it = cJSON_GetArrayItem(users, i);
        cJSON *id = cJSON_GetObjectItem(it, "id");
        cJSON *name = cJSON_GetObjectItem(it, "name");
        if (!cJSON_IsString(id)) {
            continue;
        }
        strncpy(out[loaded].id, id->valuestring, sizeof(out[0].id) - 1);
        out[loaded].id[sizeof(out[0].id) - 1] = 0;
        if (cJSON_IsString(name)) {
            strncpy(out[loaded].name, name->valuestring, sizeof(out[0].name) - 1);
        } else {
            strncpy(out[loaded].name, id->valuestring, sizeof(out[0].name) - 1);
        }
        out[loaded].name[sizeof(out[0].name) - 1] = 0;
        out[loaded].avatar_slot = 0;
        out[loaded].accent = 0;
        cJSON *prof = cJSON_GetObjectItem(it, "profile");
        apply_profile_json(prof, &out[loaded]);
        loaded++;
    }
    return loaded;
}

static bool apply_hangout_users(int loaded, const user_t *fresh)
{
    if (loaded > 0) {
        memcpy(s_users, fresh, sizeof(user_t) * loaded);
        s_user_n = loaded;
        return true;
    }
    return s_user_n > 0;
}

static void nvs_save_hangout(void)
{
    if (s_user_n <= 0) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        return;
    }
    for (int i = 0; i < s_user_n; i++) {
        cJSON *u = cJSON_CreateObject();
        if (!u) {
            continue;
        }
        cJSON_AddStringToObject(u, "id", s_users[i].id);
        cJSON_AddStringToObject(u, "name", s_users[i].name);
        cJSON *prof = cJSON_CreateObject();
        if (prof) {
            char hex[8];
            uint32_t c = s_users[i].accent ? s_users[i].accent : default_accent(i);
            snprintf(hex, sizeof(hex), "#%06X", (unsigned)(c & 0xFFFFFF));
            cJSON_AddNumberToObject(prof, "avatar_slot", s_users[i].avatar_slot);
            cJSON_AddStringToObject(prof, "accent_hex", hex);
            cJSON_AddItemToObject(u, "profile", prof);
        }
        cJSON_AddItemToArray(arr, u);
    }
    cJSON_AddItemToObject(root, "users", arr);
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        cJSON_free(printed);
        return;
    }
    (void)nvs_set_blob(h, "hangout", printed, strlen(printed) + 1);
    (void)nvs_commit(h);
    nvs_close(h);
    cJSON_free(printed);
}

static bool nvs_load_hangout(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = 0;
    if (nvs_get_blob(h, "hangout", NULL, &n) != ESP_OK || n <= 1 || n > HANGOUT_NVS_MAX) {
        nvs_close(h);
        return false;
    }
    char *buf = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        nvs_close(h);
        return false;
    }
    if (nvs_get_blob(h, "hangout", buf, &n) != ESP_OK) {
        free(buf);
        nvs_close(h);
        return false;
    }
    nvs_close(h);
    buf[n - 1] = 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    cJSON *users = root ? cJSON_GetObjectItem(root, "users") : NULL;
    user_t fresh[USER_MAX];
    int loaded = parse_hangout_users(users, fresh, USER_MAX);
    cJSON_Delete(root);
    if (loaded > 0) {
        apply_hangout_users(loaded, fresh);
        ESP_LOGI(TAG, "hangout cache %d users", loaded);
        return true;
    }
    return false;
}

static void paint_geometry_face(lv_obj_t *parent, uint32_t accent, int sz)
{
    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_set_size(head, sz, sz);
    lv_obj_center(head);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    int esz = sz / 7;
    if (esz < 3) {
        esz = 3;
    }
    lv_obj_t *e1 = lv_obj_create(head);
    lv_obj_set_size(e1, esz, esz);
    lv_obj_set_pos(e1, sz * 7 / 28, sz * 9 / 28);
    lv_obj_set_style_radius(e1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(e1, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(e1, 0, 0);
    lv_obj_set_style_pad_all(e1, 0, 0);
    lv_obj_clear_flag(e1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *e2 = lv_obj_create(head);
    lv_obj_set_size(e2, esz, esz);
    lv_obj_set_pos(e2, sz * 17 / 28, sz * 9 / 28);
    lv_obj_set_style_radius(e2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(e2, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(e2, 0, 0);
    lv_obj_set_style_pad_all(e2, 0, 0);
    lv_obj_clear_flag(e2, LV_OBJ_FLAG_SCROLLABLE);
}

static void paint_user_portrait_aligned(lv_obj_t *parent, int idx, int sz,
                                        lv_align_t align, int x_ofs, int y_ofs)
{
    uint8_t slot = user_avatar_slot(idx);
    const lv_image_dsc_t *img = avatar_image(slot);
    if (img) {
        lv_obj_t *av = lv_image_create(parent);
        lv_image_set_src(av, img);
        lv_obj_set_size(av, sz, sz);
        lv_obj_align(av, align, x_ofs, y_ofs);
        return;
    }
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, sz, sz);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(box, align, x_ofs, y_ofs);
    paint_geometry_face(box, user_accent(idx), sz);
}

static void paint_user_portrait(lv_obj_t *parent, int idx, int sz)
{
    paint_user_portrait_aligned(parent, idx, sz, LV_ALIGN_CENTER, 0, 0);
}

static void style_list_row(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_CARD_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

static void style_list_row_label(lv_obj_t *lab)
{
    lv_obj_set_style_text_color(lab, lv_color_hex(UI_TEXT), 0);
}

static void roster_row_layout(int n, int *y0, int *row_h, int *row_step, int *face_sz)
{
    *face_sz = ROSTER_FACE_SZ;
    if (n <= 1) {
        *row_h = 78;
        *row_step = 82;
        *y0 = 76;
    } else if (n == 2) {
        *row_h = 78;
        *row_step = 82;
        *y0 = 40;
    } else if (n == 3) {
        *row_h = 68;
        *row_step = 72;
        *y0 = 28;
    } else {
        *row_h = 54;
        *row_step = 56;
        *y0 = 14;
    }
}

static void paint_face_sized(lv_obj_t *parent, int idx, int x, int y, int sz)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, sz + 4, sz + 4);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    paint_user_portrait(box, idx, sz);
}

static void paint_face(lv_obj_t *parent, int idx, int x, int y)
{
    paint_face_sized(parent, idx, x, y, 24);
}

static void paint_asterisk_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 28, 28);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar1 = lv_obj_create(box);
    lv_obj_set_size(bar1, 18, 4);
    lv_obj_set_style_bg_color(bar1, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar1, 0, 0);
    lv_obj_set_style_pad_all(bar1, 0, 0);
    lv_obj_clear_flag(bar1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar1);
    lv_obj_t *bar2 = lv_obj_create(box);
    lv_obj_set_size(bar2, 4, 18);
    lv_obj_set_style_bg_color(bar2, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar2, 0, 0);
    lv_obj_set_style_pad_all(bar2, 0, 0);
    lv_obj_clear_flag(bar2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar2);
    lv_obj_t *bar3 = lv_obj_create(box);
    lv_obj_set_size(bar3, 14, 4);
    lv_obj_set_style_bg_color(bar3, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar3, 0, 0);
    lv_obj_set_style_pad_all(bar3, 0, 0);
    lv_obj_set_style_transform_angle(bar3, 450, 0);
    lv_obj_clear_flag(bar3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar3);
    lv_obj_t *bar4 = lv_obj_create(box);
    lv_obj_set_size(bar4, 14, 4);
    lv_obj_set_style_bg_color(bar4, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar4, 0, 0);
    lv_obj_set_style_pad_all(bar4, 0, 0);
    lv_obj_set_style_transform_angle(bar4, 1350, 0);
    lv_obj_clear_flag(bar4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar4);
}

static lv_obj_t *carousel_card_at(int idx)
{
    if (idx < 0 || idx >= s_msg_n) {
        return NULL;
    }
    return s_card_ui[idx].card;
}

static const char *sender_display_name(const msg_t *m)
{
    if (!m) {
        return "Family";
    }
    int uid = user_index_by_label(m->from_label);
    if (uid >= 0 && uid < s_user_n && s_users[uid].name[0]) {
        return s_users[uid].name;
    }
    if (m->from_label[0]) {
        return m->from_label;
    }
    return "Family";
}

static void apply_card_grad_style(lv_obj_t *card)
{
    const card_grad_t *g = active_card_grad();
    lv_obj_set_style_bg_color(card, lv_color_hex(g->top), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(g->bot), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
}

static void style_msg_card(lv_obj_t *card)
{
    lv_obj_set_style_border_width(card, 0, 0);
}

static int http_json_timeout(const char *method, const char *path, const char *json,
                             const char *user_id, http_buf_t *body, int timeout_ms)
{
    char url[160];
    format_url(url, sizeof(url), path);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .buffer_size = 2048,
    };
    apply_tls(&cfg);
    if (strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    } else if (strcmp(method, "PUT") == 0) {
        cfg.method = HTTP_METHOD_PUT;
    }

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(c, "Authorization", auth);
    if (user_id && user_id[0]) {
        esp_http_client_set_header(c, "X-User-Id", user_id);
    }
    if (json) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, json, (int)strlen(json));
    }

    if (body && body->buf) {
        body->len = 0;
        int total = 0;
        if (esp_http_client_open(c, json ? (int)strlen(json) : 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            return -1;
        }
        if (json) {
            esp_http_client_write(c, json, (int)strlen(json));
        }
        (void)esp_http_client_fetch_headers(c);
        int64_t read_t0 = esp_timer_get_time();
        while (total < body->cap - 1) {
            if ((esp_timer_get_time() - read_t0) / 1000 > timeout_ms) {
                esp_http_client_close(c);
                esp_http_client_cleanup(c);
                return -1;
            }
            int n = esp_http_client_read(c, (char *)body->buf + total, body->cap - 1 - total);
            if (n <= 0) {
                break;
            }
            total += n;
        }
        body->len = total;
        body->buf[total] = 0;
        int st = esp_http_client_get_status_code(c);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return st;
    }

    esp_err_t err = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return err == ESP_OK ? st : -1;
}

static int http_json(const char *method, const char *path, const char *json,
                     const char *user_id, http_buf_t *body)
{
    return http_json_timeout(method, path, json, user_id, body, 15000);
}

static int http_blob_get(int seq, http_buf_t *body)
{
    char path[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/blob", seq);
    return http_json("GET", path, NULL, s_session_user, body);
}


static void parse_inbox(const char *js)
{
    s_msg_n = 0;
    cJSON *root = cJSON_Parse(js);
    if (!root) {
        return;
    }
    cJSON *arr = cJSON_GetObjectItem(root, "messages");
    cJSON *view = cJSON_GetObjectItem(root, "last_viewed_seq");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n && s_msg_n < MSG_MAX; i++) {
            cJSON *it = cJSON_GetArrayItem(arr, i);
            cJSON *seq = cJSON_GetObjectItem(it, "seq");
            cJSON *label = cJSON_GetObjectItem(it, "from_label");
            cJSON *read = cJSON_GetObjectItem(it, "read");
            cJSON *pos = cJSON_GetObjectItem(it, "position_ms");
            cJSON *dur = cJSON_GetObjectItem(it, "duration_ms");
            if (!cJSON_IsNumber(seq)) {
                continue;
            }
            msg_t *m = &s_msgs[s_msg_n++];
            m->seq = seq->valueint;
            m->from_label[0] = 0;
            if (cJSON_IsString(label)) {
                strncpy(m->from_label, label->valuestring, sizeof(m->from_label) - 1);
            }
            m->read = cJSON_IsTrue(read);
            m->position_ms = cJSON_IsNumber(pos) ? pos->valueint : 0;
            m->duration_ms = cJSON_IsNumber(dur) ? dur->valueint : 0;
        }
    }
    s_focus = s_msg_n > 0 ? 0 : 0;
    if (cJSON_IsNumber(view)) {
        for (int i = 0; i < s_msg_n; i++) {
            if (s_msgs[i].seq == view->valueint) {
                s_focus = i;
                break;
            }
        }
    }
    cJSON_Delete(root);
}

static bool reload_inbox(void)
{
    http_buf_t b = { .buf = s_json, .cap = sizeof(s_json) };
    if (http_json("GET", "/v1/inbox", NULL, s_session_user, &b) != 200) {
        return false;
    }
    parse_inbox((char *)s_json);
    return true;
}

static bool s_login_pin_reset;

static bool login_user(const char *user_id, const char *pin)
{
    char js[80];
    snprintf(js, sizeof(js), "{\"user_id\":\"%s\",\"pin\":\"%s\"}", user_id, pin);
    http_buf_t b = { .buf = s_json, .cap = sizeof(s_json) };
    s_login_pin_reset = false;
    int st = http_json_timeout("POST", "/v1/session/login", js, NULL, &b, CONNECT_PROBE_MS);
    if (st != 200) {
        if (st < 0) {
            server_mark_offline();
        }
        return false;
    }
    server_mark_online();
    cJSON *root = cJSON_Parse((char *)s_json);
    cJSON *ok = root ? cJSON_GetObjectItem(root, "ok") : NULL;
    bool good = cJSON_IsTrue(ok);
    if (good) {
        strncpy(s_session_user, user_id, sizeof(s_session_user) - 1);
        nvs_save_last(user_id);
        parse_inbox((char *)s_json);
        cJSON *prof = cJSON_GetObjectItem(root, "profile");
        int idx = user_index(user_id);
        if (idx >= 0) {
            apply_profile_json(prof, &s_users[idx]);
        }
        cJSON *pr = cJSON_GetObjectItem(root, "pin_reset");
        s_login_pin_reset = cJSON_IsTrue(pr);
    }
    cJSON_Delete(root);
    return good;
}

static void save_position(int seq, int pos_ms)
{
    char path[48];
    char js[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/position", seq);
    snprintf(js, sizeof(js), "{\"position_ms\":%d}", pos_ms);
    uint8_t tmp[32];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", path, js, s_session_user, &b);
}

static void mark_read(int seq, int pos_ms)
{
    char path[48];
    char js[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/read", seq);
    snprintf(js, sizeof(js), "{\"position_ms\":%d}", pos_ms);
    uint8_t tmp[32];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", path, js, s_session_user, &b);
    for (int i = 0; i < s_msg_n; i++) {
        if (s_msgs[i].seq == seq) {
            s_msgs[i].read = true;
            s_msgs[i].position_ms = pos_ms;
            break;
        }
    }
}

static void wav_header(uint8_t *p, uint32_t pcm_bytes)
{
    uint32_t rate = SAMPLE_RATE;
    uint16_t ch = 1, bps = 16, audio = 1, block = 2;
    uint32_t riff = 36 + pcm_bytes;
    uint32_t fmt = 16;
    uint32_t byte_rate = rate * 2;
    memcpy(p, "RIFF", 4);
    memcpy(p + 4, &riff, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    memcpy(p + 16, &fmt, 4);
    memcpy(p + 20, &audio, 2);
    memcpy(p + 22, &ch, 2);
    memcpy(p + 24, &rate, 4);
    memcpy(p + 28, &byte_rate, 4);
    memcpy(p + 32, &block, 2);
    memcpy(p + 34, &bps, 2);
    memcpy(p + 36, "data", 4);
    memcpy(p + 40, &pcm_bytes, 4);
}

static int16_t pcm_peak(const uint8_t *p, size_t n)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)p;
    for (size_t i = 0; i < n / 2; i++) {
        int16_t a = s[i];
        if (a < 0) {
            a = -a;
        }
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static void save_profile(void);
static void ui_refresh_transport(void);
static void refresh_vol_label(void);
static lv_obj_t *make_play_icon_buf(lv_obj_t *parent, uint32_t disk_hex, uint16_t *fb);

static void stop_playback(void);

static void request_chirp(int hz)
{
    if (hz > 0) {
        s_chirp_hz = hz;
        s_chirp_hz2 = 0;
    }
}

static void request_chirp_pair(int a, int b)
{
    if (a > 0) {
        s_chirp_hz = a;
        s_chirp_hz2 = b > 0 ? b : 0;
    }
}

static void chirp_fade_edges(int16_t *pcm, int n)
{
    int fade = n / 8;
    if (fade < 8) {
        fade = 8;
    }
    if (fade * 2 > n) {
        fade = n / 4;
    }
    for (int i = 0; i < fade; i++) {
        pcm[i] = (int16_t)((int)pcm[i] * i / fade);
        pcm[n - 1 - i] = (int16_t)((int)pcm[n - 1 - i] * i / fade);
    }
}

static bool chirp_write_pcm(const int16_t *pcm, int samples)
{
    const uint8_t *p = (const uint8_t *)pcm;
    int bytes = samples * 2;
    while (bytes > 0) {
        int n = bytes > CHIRP_WRITE ? CHIRP_WRITE : bytes;
        if (esp_codec_dev_write(s_spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            return false;
        }
        p += n;
        bytes -= n;
    }
    return true;
}

static void play_chirp(int hz)
{
    if (!s_spk || hz <= 0) {
        return;
    }
    stop_playback();
    int n = CHIRP_SAMPLES;
    int drain = CHIRP_DRAIN;
    int half = SAMPLE_RATE / (hz * 2);
    if (half < 1) {
        half = 1;
    }
    int sign = 1;
    int left = half;
    for (int i = 0; i < n; i++) {
        s_chirp_pcm[i] = (int16_t)(sign * 8000);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    chirp_fade_edges(s_chirp_pcm, n);
    memset(s_chirp_pcm + n, 0, (size_t)drain * sizeof(int16_t));

    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        ESP_LOGW(TAG, "chirp speaker open failed");
        return;
    }
    s_spk_open = true;
    (void)esp_codec_dev_set_out_vol(s_spk, CHIRP_VOL);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    vTaskDelay(pdMS_TO_TICKS(30));

    if (!chirp_write_pcm(s_chirp_pcm, n + drain)) {
        ESP_LOGW(TAG, "chirp write failed");
    }
    (void)esp_codec_dev_close(s_spk);
    s_spk_open = false;
    apply_volume(s_volume);
}

static void play_chirp_pair(int a, int b)
{
    play_chirp(a);
    if (b > 0) {
        play_chirp(b);
    }
}

static int post_wav(const uint8_t *wav, int wav_len, bool broadcast, const char *to_user)
{
    char url[128];
    format_url(url, sizeof(url), "/v1/messages");
    static const char *bnd = "----FamilyLinkX02";
    char pre[512];
    int pre_len;
    if (broadcast) {
        pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"broadcast\"\r\n\r\n"
                           "true\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; "
                           "filename=\"clip.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
                           bnd, bnd, bnd);
    } else {
        pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"to_user_id\"\r\n\r\n"
                           "%s\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; "
                           "filename=\"clip.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
                           bnd, bnd, to_user, bnd);
    }
    char post[64];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000 };
    apply_tls(&cfg);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "X-User-Id", s_session_user);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(c, "Content-Type", ctype);
    if (esp_http_client_open(c, pre_len + wav_len + post_len) != ESP_OK) {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_http_client_write(c, pre, pre_len);
    esp_http_client_write(c, (const char *)wav, wav_len);
    esp_http_client_write(c, post, post_len);
    (void)esp_http_client_fetch_headers(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return st;
}

static msg_t *focus_msg(void)
{
    if (s_msg_n <= 0) {
        return NULL;
    }
    if (s_focus < 0) {
        s_focus = 0;
    }
    if (s_focus >= s_msg_n) {
        s_focus = s_msg_n - 1;
    }
    return &s_msgs[s_focus];
}

static void stop_playback(void)
{
    s_stop_play = true;
    while (s_playing) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_stop_play = false;
}

static bool message_at_end(const msg_t *m)
{
    if (!m || m->duration_ms <= 0) {
        return false;
    }
    return m->position_ms >= (m->duration_ms - 150);
}

static void restart_message_if_at_end(msg_t *m)
{
    if (!message_at_end(m)) {
        return;
    }
    m->position_ms = 0;
    save_position(m->seq, 0);
}

static int32_t snap_target_x(lv_obj_t *card, lv_obj_t *scroller)
{
    int32_t card_x = lv_obj_get_x(card);
    int32_t card_w = lv_obj_get_width(card);
    int32_t view_w = lv_obj_get_width(scroller);
    int32_t target = card_x + card_w / 2 - view_w / 2;
    int32_t content_right = 0;
    uint32_t n = lv_obj_get_child_cnt(scroller);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(scroller, i);
        int32_t right = lv_obj_get_x(ch) + lv_obj_get_width(ch);
        if (right > content_right) {
            content_right = right;
        }
    }
    int32_t max_x = content_right - view_w;
    if (max_x < 0) {
        max_x = 0;
    }
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    return target;
}

static int carousel_center_index(void)
{
    if (!s_carousel_scroll || s_msg_n <= 0) {
        return s_focus;
    }
    int32_t mid = lv_obj_get_scroll_x(s_carousel_scroll) + lv_obj_get_width(s_carousel_scroll) / 2;
    int best = 0;
    int32_t best_dist = INT32_MAX;
    for (int i = 0; i < s_msg_n; i++) {
        lv_obj_t *ch = carousel_card_at(i);
        if (!ch) {
            continue;
        }
        int32_t center = lv_obj_get_x(ch) + lv_obj_get_width(ch) / 2;
        int32_t dist = center > mid ? center - mid : mid - center;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static void refresh_card_visuals_from_scroll(void)
{
    if (!s_carousel_scroll || s_msg_n <= 0) {
        return;
    }
    int visual = carousel_center_index();
    for (int i = 0; i < s_msg_n; i++) {
        lv_obj_t *card = s_card_ui[i].card;
        if (!card) {
            continue;
        }
        if (i == visual) {
            lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_scale(card, 256, 0);
        } else {
            lv_obj_set_style_opa(card, LV_OPA_50, 0);
            lv_obj_set_style_transform_scale(card, 230, 0);
        }
    }
}

static void scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
    if (s_st == ST_CAROUSEL) {
        refresh_card_visuals_from_scroll();
    }
}

static void snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    if (scroller != NULL) {
        lv_obj_scroll_to_x(scroller, s_snap_target, LV_ANIM_OFF);
    }
    s_scroll_lock = false;
    s_carousel_locked = true;
    ui_refresh_transport();
    (void)s_snap_t0;
}

static void snap_scroll_to(lv_obj_t *scroller, int32_t target)
{
    int32_t start = lv_obj_get_scroll_x(scroller);

    lv_anim_delete(scroller, scroll_x_exec);
    s_snap_target = target;
    s_scroll_lock = true;
    s_carousel_locked = false;
    ui_refresh_transport();
    s_snap_t0 = esp_timer_get_time();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroller);
    lv_anim_set_user_data(&a, scroller);
    int32_t delta = target > start ? target - start : start - target;
    int duration = SNAP_SCROLL_MS + (int)(delta * 2 / 5);
    if (duration > SNAP_SCROLL_MS_MAX) {
        duration = SNAP_SCROLL_MS_MAX;
    }
    lv_anim_set_values(&a, start, target);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, scroll_x_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, snap_anim_done);
    lv_anim_start(&a);
}

static void carousel_snap_to_focus(void)
{
    lv_obj_t *card = carousel_card_at(s_focus);
    if (!card || !s_carousel_scroll) {
        return;
    }
    int32_t target = snap_target_x(card, s_carousel_scroll);
    if (lv_obj_get_scroll_x(s_carousel_scroll) == target) {
        s_scroll_lock = false;
        s_carousel_locked = true;
        ui_refresh_transport();
        return;
    }
    snap_scroll_to(s_carousel_scroll, target);
}

static void sync_focus_from_scroll(void)
{
    if (s_scroll_lock || !s_carousel_scroll || s_msg_n <= 0) {
        return;
    }
    int32_t mid = lv_obj_get_scroll_x(s_carousel_scroll) + lv_obj_get_width(s_carousel_scroll) / 2;
    int best = s_focus;
    int32_t best_dist = -1;
    for (int i = 0; i < s_msg_n; i++) {
        lv_obj_t *ch = carousel_card_at(i);
        if (!ch) {
            continue;
        }
        int32_t center = lv_obj_get_x(ch) + lv_obj_get_width(ch) / 2;
        int32_t dist = center > mid ? center - mid : mid - center;
        if (best_dist < 0 || dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    if (best == s_focus) {
        return;
    }
    stop_playback();
    s_focus = best;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_msgs[s_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", "/v1/session/view", js, s_session_user, &b);
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        request_chirp(784);
    }
}

static void set_transport_icon(bool playing)
{
    if (s_play_icon) {
        if (playing) {
            lv_obj_add_flag(s_play_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_play_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_pause_icon) {
        if (playing) {
            lv_obj_clear_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_card_focus_states(void)
{
    if (s_scroll_lock) {
        refresh_card_visuals_from_scroll();
    } else {
        for (int i = 0; i < s_msg_n; i++) {
            lv_obj_t *card = s_card_ui[i].card;
            if (!card) {
                continue;
            }
            if (i == s_focus) {
                lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
                lv_obj_set_style_transform_scale(card, 256, 0);
            } else {
                lv_obj_set_style_opa(card, LV_OPA_50, 0);
                lv_obj_set_style_transform_scale(card, 230, 0);
            }
        }
    }
    for (int i = 0; i < s_msg_n; i++) {
        lv_obj_t *card = s_card_ui[i].card;
        if (!card) {
            continue;
        }
        if (i == s_focus && !s_scroll_lock) {
            lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    bool transport = s_carousel_locked && !s_scroll_lock && s_msg_n > 0;
    if (s_play_btn) {
        if (transport) {
            lv_obj_add_flag(s_play_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_play_btn, LV_OPA_COVER, 0);
        } else {
            lv_obj_remove_flag(s_play_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_play_btn, LV_OPA_50, 0);
        }
    }
    if (s_bar) {
        if (transport) {
            lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_bar, LV_OPA_COVER, 0);
        } else {
            lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_bar, LV_OPA_50, 0);
        }
    }
}

static lv_obj_t *make_pause_icon(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 20, 20);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_obj_create(box);
    lv_obj_set_size(l, 4, 16);
    lv_obj_set_style_bg_color(l, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(l, LV_ALIGN_CENTER, -5, 0);
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_set_size(r, 4, 16);
    lv_obj_set_style_bg_color(r, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(r, LV_ALIGN_CENTER, 5, 0);
    lv_obj_center(box);
    return box;
}

static void style_transport_slider(lv_obj_t *sl)
{
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x2A3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x4ECDC4), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xE8F0E8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
}

static void ui_refresh_transport(void)
{
    msg_t *m = focus_msg();
    refresh_card_focus_states();
    if (!m) {
        return;
    }
    if (s_bar && !s_scrubbing) {
        int dur = m->duration_ms > 0 ? m->duration_ms : 1;
        s_bar_sync = true;
        lv_slider_set_range(s_bar, 0, dur);
        lv_slider_set_value(s_bar, s_playing ? s_play_pos_ms : m->position_ms, LV_ANIM_OFF);
        s_bar_sync = false;
    }
    set_transport_icon(s_playing);
    if (m->seq != s_card_face_seq || m->read != s_card_face_read) {
        s_card_face_seq = m->seq;
        s_card_face_read = m->read;
    }
    if (s_count_lab) {
        char who[16];
        snprintf(who, sizeof(who), "%d/%d",
                 s_msg_n > 0 ? s_focus + 1 : 0, s_msg_n);
        lv_label_set_text(s_count_lab, who);
    }
}

static void playback_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_want_play) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        s_want_play = false;
        msg_t *m = focus_msg();
        if (!m || s_msg_n <= 0) {
            continue;
        }
        if (s_playing) {
            stop_playback();
            continue;
        }
        http_buf_t b = { .buf = s_buf, .cap = BUF_CAP, .len = 0 };
        if (http_blob_get(m->seq, &b) != 200 || b.len < 64) {
            if (!s_server_online) {
                s_toast_pending = true;
                strncpy(s_toast_msg, "can't play right now", sizeof(s_toast_msg) - 1);
            }
            continue;
        }
        const uint8_t *pcm = b.buf;
        int pcm_len = b.len;
        if (pcm_len > 44 && memcmp(pcm, "RIFF", 4) == 0) {
            pcm += 44;
            pcm_len -= 44;
        }
        int skip = (TRIM_MS * SAMPLE_RATE * 2) / 1000;
        if (skip > pcm_len) {
            skip = 0;
        }
        const uint8_t *pcm_base = pcm + skip;
        int pcm_base_len = pcm_len - skip;
        restart_message_if_at_end(m);
        int offset_bytes = (m->position_ms * SAMPLE_RATE * 2) / 1000;
        if (offset_bytes > pcm_base_len) {
            offset_bytes = 0;
            m->position_ms = 0;
        }
        pcm = pcm_base + offset_bytes;
        pcm_len = pcm_base_len - offset_bytes;
        if (pcm_len <= 0) {
            m->position_ms = 0;
            save_position(m->seq, 0);
            pcm = pcm_base;
            pcm_len = pcm_base_len;
        }
        if (!s_spk_open) {
            bool opened = false;
            for (int try = 0; try < 8; try++) {
                if (esp_codec_dev_open(s_spk, &s_fs) == ESP_OK) {
                    opened = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            if (!opened) {
                s_want_play = true;
                continue;
            }
            s_spk_open = true;
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        apply_volume(s_volume);
        s_playing = true;
        s_play_pos_ms = m->position_ms;
        int pos_bytes = 0;
        while (pos_bytes < pcm_len && !s_stop_play) {
            int chunk = pcm_len - pos_bytes;
            if (chunk > 2048) {
                chunk = 2048;
            }
            if (esp_codec_dev_write(s_spk, (void *)(pcm + pos_bytes), chunk) != ESP_CODEC_DEV_OK) {
                break;
            }
            pos_bytes += chunk;
            s_play_pos_ms = m->position_ms + (pos_bytes * 1000) / (SAMPLE_RATE * 2);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_playing = false;
        m->position_ms = s_play_pos_ms;
        save_position(m->seq, s_play_pos_ms);
        if (!s_stop_play) {
            mark_read(m->seq, s_play_pos_ms);
        }
        request_transport_refresh();
    }
}

static size_t record_take(size_t cap)
{
    size_t n = 0;
    int64_t last_talk = now_us();
    const size_t trim_bytes = (TRIM_MS * SAMPLE_RATE * 2) / 1000;
    const size_t max_pcm = cap - 44;

    if (mute_latched()) {
        return 0;
    }
    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
    vTaskDelay(pdMS_TO_TICKS(TRIM_MS));

    while (n + CHUNK <= max_pcm) {
        if (s_stop_record || s_cancel_pick) {
            break;
        }
        if (esp_codec_dev_read(s_mic, s_pcm + 44 + n, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        int16_t pk = pcm_peak(s_pcm + 44 + n, CHUNK);
        if (pk >= TALK_PEAK) {
            last_talk = now_us();
        } else if (pk < 64 && (now_us() - last_talk) > 500000) {
            break;
        }
        n += CHUNK;
        if ((now_us() - last_talk) > (int64_t)SILENCE_SEC * 1000000) {
            break;
        }
        if (n >= (size_t)MAX_RECORD_SEC * SAMPLE_RATE * 2) {
            break;
        }
    }
    (void)esp_codec_dev_close(s_mic);
    if (n <= trim_bytes) {
        return 0;
    }
    memmove(s_pcm + 44, s_pcm + 44 + trim_bytes, n - trim_bytes);
    return n - trim_bytes;
}

static bool pcm_buffer_ready(void)
{
    if (s_pcm) {
        return true;
    }
    size_t pcm_cap = 44 + (size_t)MAX_RECORD_SEC * SAMPLE_RATE * 2;
    s_pcm = heap_caps_malloc(pcm_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm) {
        s_pcm = heap_caps_malloc(pcm_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_pcm) {
        ESP_LOGE(TAG, "record buffer alloc failed (%u bytes)", (unsigned)pcm_cap);
    }
    return s_pcm != NULL;
}

static void record_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_work, portMAX_DELAY);
        if (s_st != ST_RECORD) {
            continue;
        }
        if (!s_server_online) {
            s_toast_pending = true;
            strncpy(s_toast_msg, "can't send right now", sizeof(s_toast_msg) - 1);
            s_st = ST_CAROUSEL;
            request_repaint();
            continue;
        }
        if (!s_mic || !pcm_buffer_ready()) {
            s_toast_pending = true;
            strncpy(s_toast_msg, "no mic", sizeof(s_toast_msg) - 1);
            s_st = ST_CAROUSEL;
            request_repaint();
            continue;
        }
        if (mute_latched()) {
            s_toast_pending = true;
            strncpy(s_toast_msg, "unmute first", sizeof(s_toast_msg) - 1);
            s_st = ST_CAROUSEL;
            request_repaint();
            continue;
        }
        play_chirp_pair(523, 784);
        s_stop_record = false;
        size_t pcm = record_take(44 + (size_t)MAX_RECORD_SEC * SAMPLE_RATE * 2);
        play_chirp_pair(784, 392);
        if (pcm > 0 && !s_cancel_pick) {
            wav_header(s_pcm, (uint32_t)pcm);
            int wav_len = 44 + (int)pcm;
            int st = post_wav(s_pcm, wav_len, s_send_all, s_send_to);
            ESP_LOGI(TAG, "upload status %d", st);
            if (st == 200) {
                int keep = -1;
                msg_t *fm = focus_msg();
                if (fm) {
                    keep = fm->seq;
                }
                if (reload_inbox() && keep > 0) {
                    for (int i = 0; i < s_msg_n; i++) {
                        if (s_msgs[i].seq == keep) {
                            s_focus = i;
                            break;
                        }
                    }
                }
                s_toast_pending = true;
                strncpy(s_toast_msg, "sent", sizeof(s_toast_msg) - 1);
            } else {
                server_mark_offline();
                s_toast_pending = true;
                strncpy(s_toast_msg, "couldn't send", sizeof(s_toast_msg) - 1);
            }
        } else if (s_cancel_pick) {
            s_toast_pending = true;
            strncpy(s_toast_msg, "cancelled", sizeof(s_toast_msg) - 1);
        }
        s_cancel_pick = false;
        s_st = ST_CAROUSEL;
        request_repaint();
    }
}

static bool load_hangout_ms(int timeout_ms)
{
    http_buf_t b = { .buf = s_json, .cap = sizeof(s_json) };
    int st = http_json_timeout("GET", "/v1/hangout", NULL, NULL, &b, timeout_ms);
    if (st != 200) {
        ESP_LOGW(TAG, "GET /v1/hangout failed st=%d (have %d users)", st, s_user_n);
        server_mark_offline();
        return s_user_n > 0;
    }
    cJSON *root = cJSON_Parse((char *)s_json);
    cJSON *users = root ? cJSON_GetObjectItem(root, "users") : NULL;
    user_t fresh[USER_MAX];
    int loaded = parse_hangout_users(users, fresh, USER_MAX);
    cJSON_Delete(root);
    if (apply_hangout_users(loaded, fresh)) {
        server_mark_online();
        nvs_save_hangout();
        return true;
    }
    ESP_LOGW(TAG, "hangout response had no users");
    server_mark_offline();
    return s_user_n > 0;
}

static bool load_hangout(void)
{
    return load_hangout_ms(15000);
}

static void ws_hello(void)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    char hello[128];
    snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"token\":\"%s\"}", DEMO_DEVICE_TOKEN);
    esp_websocket_client_send_text(s_ws, hello, (int)strlen(hello), pdMS_TO_TICKS(1000));
}

static void on_ws_text(const char *s, int n)
{
    char tmp[256];
    if (n >= (int)sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    cJSON *j = cJSON_Parse(tmp);
    if (!j) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(j, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (strcmp(t, "inbox") == 0) {
        cJSON *uid = cJSON_GetObjectItem(j, "user_id");
        s_inbox_for[0] = 0;
        if (cJSON_IsString(uid) && uid->valuestring) {
            strncpy(s_inbox_for, uid->valuestring, sizeof(s_inbox_for) - 1);
        }
        s_inbox_dirty = true;
        ESP_LOGI(TAG, "ws inbox user=%s", s_inbox_for);
    } else if (strcmp(t, "hello_ok") == 0) {
        ESP_LOGI(TAG, "ws hello_ok");
    }
    cJSON_Delete(j);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        server_mark_online();
        ws_hello();
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR) {
        server_mark_offline();
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev == NULL) {
        return;
    }
    if (ev->op_code == 0x01) {
        on_ws_text(ev->data_ptr, ev->data_len);
    }
}

static void ws_start(void)
{
    if (s_ws) {
        return;
    }
    char uri[128];
    snprintf(uri, sizeof(uri), "%s://%s:%d/v1/ws",
#if DEMO_SERVER_TLS
             "wss",
#else
             "ws",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
#if DEMO_SERVER_TLS
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
#if DEMO_TLS_SKIP_VERIFY
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = NULL,
#else
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
#endif
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) {
        ESP_LOGW(TAG, "ws init failed");
        return;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGW(TAG, "ws start failed");
        return;
    }
    ESP_LOGI(TAG, "ws %s", uri);
}

#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
static void sntp_wait(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    for (int i = 0; i < 50; i++) {
        time_t now = 0;
        time(&now);
        if (now > 1700000000) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

static void set_status(const char *t, uint32_t color)
{
    if (s_status) {
        lv_label_set_text(s_status, t);
        lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    }
}

static void refresh_dots(void)
{
    if (!s_dots) {
        return;
    }
    char d[ENTRY_MAX + 1];
    memset(d, '*', s_elen);
    d[s_elen] = 0;
    lv_label_set_text(s_dots, s_elen ? d : " ");
}

static void on_user_btn(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (!id) {
        return;
    }
    strncpy(s_pick_id, id, sizeof(s_pick_id) - 1);
    s_elen = 0;
    s_entry[0] = 0;
    s_st = ST_PIN;
    request_repaint();
    note_activity();
}

static void on_pick_btn(lv_event_t *e)
{
    if (!s_server_online) {
        s_toast_pending = true;
        strncpy(s_toast_msg, "can't send right now", sizeof(s_toast_msg) - 1);
        s_st = ST_CAROUSEL;
        request_repaint();
        return;
    }
    const char *id = (const char *)lv_event_get_user_data(e);
    s_send_all = (id == NULL);
    if (!s_send_all && id) {
        strncpy(s_send_to, id, sizeof(s_send_to) - 1);
    }
    s_st = ST_RECORD;
    request_repaint();
    xSemaphoreGive(s_work);
    note_activity();
}

static void on_sign_out(lv_event_t *e)
{
    (void)e;
    stop_playback();
    s_session_user[0] = 0;
    s_st = ST_ROSTER;
    request_repaint();
    note_activity();
}

static void shift_focus_to(int idx)
{
    if (s_msg_n <= 0 || idx < 0 || idx >= s_msg_n) {
        return;
    }
    if (idx == s_focus) {
        return;
    }
    stop_playback();
    msg_t *m = focus_msg();
    if (m) {
        save_position(m->seq, m->position_ms);
    }
    s_focus = idx;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_msgs[s_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", "/v1/session/view", js, s_session_user, &b);
    carousel_snap_to_focus();
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        request_chirp(784);
    }
    note_activity();
}

static void on_carousel_card_click(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_t *scroller = s_carousel_scroll;
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    if ((int)idx == s_focus || !card || !scroller) {
        return;
    }
    stop_playback();
    msg_t *m = focus_msg();
    if (m) {
        save_position(m->seq, m->position_ms);
    }
    s_focus = (int)idx;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_msgs[s_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", "/v1/session/view", js, s_session_user, &b);
    snap_scroll_to(scroller, snap_target_x(card, scroller));
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        request_chirp(784);
    }
    note_activity();
}

static void on_carousel_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL) {
        refresh_card_visuals_from_scroll();
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        sync_focus_from_scroll();
        carousel_snap_to_focus();
    }
}

static void on_play(lv_event_t *e)
{
    (void)e;
    if (s_playing) {
        stop_playback();
        request_chirp(660);
    } else {
        msg_t *m = focus_msg();
        restart_message_if_at_end(m);
        s_chirp_hz = 0;
        s_chirp_hz2 = 0;
        s_want_play = true;
    }
    note_activity();
}

static void on_volume(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int notch = (int)lv_slider_get_value(sl);
    s_vol_notch = notch;
    apply_volume(roomvol_codec(notch));
    refresh_vol_label();
    note_activity();
}

static void on_scrub(lv_event_t *e)
{
    if (s_bar_sync || !s_bar || !s_carousel_locked || s_scroll_lock) {
        return;
    }
    if (lv_event_get_target(e) != s_bar) {
        return;
    }
    msg_t *m = focus_msg();
    if (!m) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_scrubbing = true;
        if (s_playing) {
            stop_playback();
        }
        note_activity();
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED) {
        int pos = (int)lv_slider_get_value(s_bar);
        m->position_ms = pos;
        s_play_pos_ms = pos;
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        save_position(m->seq, m->position_ms);
        s_scrubbing = false;
        note_activity();
    }
}

static void on_pin_key(lv_event_t *e)
{
    const char *key = (const char *)lv_event_get_user_data(e);
    if (!key || s_st != ST_PIN) {
        return;
    }
    if (s_pin_login_busy) {
        return;
    }
    if (pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        set_status(line, 0xE85A5A);
        return;
    }
    if (s_elen >= ENTRY_MAX) {
        return;
    }
    s_entry[s_elen++] = key[0];
    s_entry[s_elen] = 0;
    refresh_dots();
    if (s_elen < 4) {
        return;
    }
    if (s_pin_login_busy) {
        return;
    }
    if (!s_server_online) {
        enter_connecting_from_signin();
        note_activity();
        return;
    }
    s_pin_login_busy = true;
    board_lvgl_lock(0);
    set_status("checking...", UI_TEXT_MUT);
    board_lvgl_unlock();
    xSemaphoreGive(s_login_work);
    note_activity();
}

static void login_task_fn(void *arg)
{
    (void)arg;
    char user_id[16];
    char pin[5];
    for (;;) {
        xSemaphoreTake(s_login_work, portMAX_DELAY);
        if (s_st != ST_PIN || s_elen != 4) {
            s_pin_login_busy = false;
            continue;
        }
        strncpy(user_id, s_pick_id, sizeof(user_id) - 1);
        user_id[sizeof(user_id) - 1] = 0;
        strncpy(pin, s_entry, sizeof(pin) - 1);
        pin[sizeof(pin) - 1] = 0;

        if (!s_server_online) {
            s_pin_login_busy = false;
            enter_connecting_from_signin();
            request_repaint();
            continue;
        }

        bool ok = login_user(user_id, pin);
        s_pin_login_busy = false;
        if (ok) {
            s_pin_fails = 0;
            s_pin_lock_until_us = 0;
            s_elen = 0;
            s_entry[0] = 0;
            s_st = ST_CAROUSEL;
            if (s_login_pin_reset) {
                s_toast_pending = true;
                strncpy(s_toast_msg, "PIN reset - ask Lynn", sizeof(s_toast_msg) - 1);
            }
            if (!s_send_hint_shown) {
                s_send_hint_shown = true;
                s_toast_pending = true;
                strncpy(s_toast_msg, "tap circle to send", sizeof(s_toast_msg) - 1);
            }
            s_carousel_ready_us = now_us();
            if (!s_passed) {
                s_passed = true;
                demo_pass("x02");
            }
            request_chirp(988);
            request_repaint();
            note_activity();
            continue;
        }
        if (s_st != ST_PIN) {
            request_repaint();
            continue;
        }
        if (!s_server_online) {
            enter_connecting_from_signin();
            request_repaint();
            continue;
        }
        if (strcmp(s_pin_fail_user, user_id) != 0) {
            strncpy(s_pin_fail_user, user_id, sizeof(s_pin_fail_user) - 1);
            s_pin_fails = 0;
        }
        s_pin_fails++;
        s_elen = 0;
        s_entry[0] = 0;
        board_lvgl_lock(0);
        refresh_dots();
        if (s_pin_fails >= PIN_TRIES) {
            s_pin_lock_until_us = now_us() + (int64_t)PIN_COOLDOWN_MS * 1000;
            set_status("ask Lynn", UI_ERROR);
        } else {
            set_status("wrong pin", UI_ERROR);
        }
        board_lvgl_unlock();
        note_activity();
    }
}

#define PLAY_ICON_W 24
#define PLAY_ICON_H 24

static uint16_t rgb565_from_hex(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static void play_tri_rot_pt(int pivot_x, int pivot_y, int x, int y, int rot_tenths, int *ox, int *oy)
{
    int dx = x - pivot_x;
    int dy = y - pivot_y;

    switch (rot_tenths) {
    case 900:
        *ox = pivot_x + dy;
        *oy = pivot_y - dx;
        break;
    case 1800:
        *ox = pivot_x - dx;
        *oy = pivot_y - dy;
        break;
    case 2700:
        *ox = pivot_x - dy;
        *oy = pivot_y + dx;
        break;
    default:
        *ox = x;
        *oy = y;
        break;
    }
}

static int play_tri_edge(int ax, int ay, int bx, int by, int cx, int cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}

static void fill_play_icon_fb(uint16_t *fb, uint32_t disk_hex)
{
    const uint16_t bg = rgb565_from_hex(disk_hex);
    const uint16_t fg = rgb565_from_hex(0xFFFFFF);
    int x0 = 12;
    int y0 = 4;
    int x1 = 5;
    int y1 = 19;
    int x2 = 19;
    int y2 = 19;
    int pivot_x = (x0 + x1 + x2) / 3;
    int pivot_y = (y0 + y1 + y2) / 3;
    int ax;
    int ay;
    int bx;
    int by;
    int cx;
    int cy;

    play_tri_rot_pt(pivot_x, pivot_y, x0, y0, 2700, &ax, &ay);
    play_tri_rot_pt(pivot_x, pivot_y, x1, y1, 2700, &bx, &by);
    play_tri_rot_pt(pivot_x, pivot_y, x2, y2, 2700, &cx, &cy);

    for (int y = 0; y < PLAY_ICON_H; y++) {
        for (int x = 0; x < PLAY_ICON_W; x++) {
            int w0 = play_tri_edge(ax, ay, bx, by, x, y);
            int w1 = play_tri_edge(bx, by, cx, cy, x, y);
            int w2 = play_tri_edge(cx, cy, ax, ay, x, y);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            fb[y * PLAY_ICON_W + x] = inside ? fg : bg;
        }
    }
}

static lv_obj_t *make_play_icon_buf(lv_obj_t *parent, uint32_t disk_hex, uint16_t *fb)
{
    fill_play_icon_fb(fb, disk_hex);
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, fb, PLAY_ICON_W, PLAY_ICON_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    return canvas;
}

static void refresh_vol_label(void)
{
    if (!s_vol_val_lab) {
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", roomvol_codec(s_vol_notch));
    lv_label_set_text(s_vol_val_lab, buf);
}

static void on_accent_pick(lv_event_t *e)
{
    uint32_t color = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    int idx = user_index(s_session_user);
    if (idx < 0) {
        return;
    }
    s_users[idx].accent = color;
    save_profile();
    request_repaint();
    note_activity();
}

static void on_grad_pick(lv_event_t *e)
{
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (!card_grad_id_valid(id)) {
        return;
    }
    s_card_grad = id;
    nvs_save_card_grad();
    request_repaint();
    note_activity();
}

static void on_avatar_pick(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    int idx = user_index(s_session_user);
    if (idx < 0) {
        return;
    }
    s_users[idx].avatar_slot = (uint8_t)slot;
    save_profile();
    request_repaint();
    note_activity();
}

static void on_circle_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (s_asleep) {
        note_activity();
        return;
    }
    if (s_st == ST_CAROUSEL) {
        if (s_playing || s_scrubbing) {
            return;
        }
        if (!s_server_online) {
            s_toast_pending = true;
            strncpy(s_toast_msg, "can't send right now", sizeof(s_toast_msg) - 1);
            return;
        }
        if ((now_us() - s_carousel_ready_us) < (int64_t)CIRCLE_DEBOUNCE_MS * 1000) {
            return;
        }
        s_cancel_pick = false;
        s_pick_open_us = now_us();
        s_st = ST_PICK;
        request_repaint();
    } else if (s_st == ST_RECORD) {
        s_stop_record = true;
    }
    note_activity();
}

static void on_boot_press(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (s_st == ST_PIN) {
        if (s_elen > 0) {
            s_elen = 0;
            s_entry[0] = 0;
            refresh_dots();
            if (s_status && !pin_locked()) {
                set_status("", 0xA8B0B8);
            }
        } else {
            s_st = ST_ROSTER;
            request_repaint();
        }
    } else if (s_st == ST_CAROUSEL) {
        stop_playback();
        s_st = ST_SETTINGS;
        request_repaint();
    } else if (s_st == ST_SETTINGS) {
        s_st = ST_CAROUSEL;
        request_repaint();
    } else if (s_st == ST_PICK || s_st == ST_RECORD) {
        s_cancel_pick = true;
        s_stop_record = true;
        request_chirp_pair(523, 392);
        if (s_st == ST_RECORD) {
            /* record_task will return to carousel */
        } else {
            s_st = ST_CAROUSEL;
            request_repaint();
        }
    }
    note_activity();
}

static void refresh_offline_ribbon(void)
{
    if (s_offline_lab) {
        if (s_server_online || !s_session_user[0]) {
            lv_obj_add_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void paint_ribbons(lv_obj_t *scr)
{
    s_ribbon_top = lv_obj_create(scr);
    lv_obj_set_pos(s_ribbon_top, 0, 0);
    lv_obj_set_size(s_ribbon_top, 320, RIBBON_H);
    lv_obj_set_style_bg_color(s_ribbon_top, lv_color_hex(0x0C0E10), 0);
    lv_obj_set_style_border_width(s_ribbon_top, 0, 0);
    lv_obj_set_style_pad_all(s_ribbon_top, 0, 0);
    lv_obj_clear_flag(s_ribbon_top, LV_OBJ_FLAG_SCROLLABLE);

    s_offline_lab = lv_label_create(s_ribbon_top);
    lv_label_set_text(s_offline_lab, "offline");
    lv_obj_set_style_text_color(s_offline_lab, lv_color_hex(0xE85A5A), 0);
    lv_obj_align(s_offline_lab, LV_ALIGN_LEFT_MID, 8, 0);
    if (s_server_online || !s_session_user[0]) {
        lv_obj_add_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
    }

    s_count_lab = lv_label_create(s_ribbon_top);
    lv_label_set_text(s_count_lab, "");
    lv_obj_set_style_text_color(s_count_lab, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_count_lab, LV_ALIGN_RIGHT_MID, -8, 0);

    s_ribbon_bot = lv_obj_create(scr);
    lv_obj_set_pos(s_ribbon_bot, 0, 240 - RIBBON_H);
    lv_obj_set_size(s_ribbon_bot, 320, RIBBON_H);
    lv_obj_set_style_bg_color(s_ribbon_bot, lv_color_hex(0x141A1E), 0);
    lv_obj_set_style_border_width(s_ribbon_bot, 0, 0);
    lv_obj_set_style_pad_all(s_ribbon_bot, 0, 0);
    lv_obj_clear_flag(s_ribbon_bot, LV_OBJ_FLAG_SCROLLABLE);

    s_toast = lv_label_create(s_ribbon_bot);
    lv_label_set_text(s_toast, "");
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0xE8C040), 0);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, 0);
}

static void carousel_add_card(int msg_idx, int x)
{
    msg_t *m = &s_msgs[msg_idx];
    const card_grad_t *g = active_card_grad();
    int uid = user_index_by_label(m->from_label);
    carousel_card_ui_t *ui = &s_card_ui[msg_idx];
    uint32_t text_hex = g->light_ui ? 0x101418U : 0xF0F4F0U;

    memset(ui, 0, sizeof(*ui));

    lv_obj_t *card = lv_obj_create(s_carousel_scroll);
    ui->card = card;
    lv_obj_set_size(card, SCROLL_CARD_W, SCROLL_CARD_H);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    apply_card_grad_style(card);
    style_msg_card(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_carousel_card_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)msg_idx);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(card, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(card, lv_pct(50), 0);

    paint_user_portrait_aligned(card, uid, CARD_PORTRAIT, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, sender_display_name(m));
    lv_obj_set_style_text_color(name, lv_color_hex(text_hex), 0);
#if defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
#elif defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
#endif
    lv_obj_set_width(name, SCROLL_CARD_W - CARD_PORTRAIT - 24);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10 + CARD_PORTRAIT + 6, 0);
}

static void save_profile(void)
{
    int idx = user_index(s_session_user);
    if (idx < 0 || !s_session_user[0]) {
        return;
    }
    char js[96];
    char hex[8];
    uint32_t c = user_accent(idx);
    snprintf(hex, sizeof(hex), "#%06X", (unsigned)(c & 0xFFFFFF));
    snprintf(js, sizeof(js), "{\"avatar_slot\":%u,\"accent_hex\":\"%s\"}",
             (unsigned)user_avatar_slot(idx), hex);
    uint8_t tmp[128];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", "/v1/profile", js, s_session_user, &b);
}

static lv_obj_t *paint_icon_box_at(lv_obj_t *scr, int x, int y, int scale_pct)
{
    int w = 72 * scale_pct / 100;
    int h = 56 * scale_pct / 100;
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

static lv_obj_t *paint_icon_box(lv_obj_t *scr, int y, int scale_pct)
{
    int w = 72 * scale_pct / 100;
    lv_obj_t *box = paint_icon_box_at(scr, (320 - w) / 2, y, scale_pct);
    return box;
}

static void paint_mailbox_icon(lv_obj_t *parent, int scale_pct)
{
    int body_w = 44 * scale_pct / 100;
    int body_h = 32 * scale_pct / 100;
    int flap_w = 48 * scale_pct / 100;
    int flap_h = 14 * scale_pct / 100;
    int flap_y = 4 * scale_pct / 100;
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_size(body, body_w, body_h);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(body, 4 * scale_pct / 100, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *flap = lv_obj_create(parent);
    lv_obj_set_size(flap, flap_w, flap_h);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, flap_y);
    lv_obj_set_style_bg_color(flap, lv_color_hex(0xC8A030), 0);
    lv_obj_set_style_border_width(flap, 0, 0);
    lv_obj_set_style_pad_all(flap, 0, 0);
    lv_obj_set_style_transform_angle(flap, 450, 0);
    lv_obj_clear_flag(flap, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_conn_title_font(lv_obj_t *lab)
{
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_28, 0);
#else
    lv_obj_set_style_transform_pivot_x(lab, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(lab, lv_pct(50), 0);
    lv_obj_set_style_transform_scale(lab, 512, 0);
#endif
}

static lv_obj_t *paint_transparent_bar(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, y);
    lv_obj_set_size(bar, 320, h);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

static void clear_conn_dots(void)
{
    s_conn_dots = NULL;
    s_dot_circles[0] = NULL;
    s_dot_circles[1] = NULL;
    s_dot_circles[2] = NULL;
}

static void paint_conn_dots_row(lv_obj_t *scr, int y)
{
    s_conn_dots = paint_transparent_bar(scr, y, 28);
    const int dot_sz = 10;
    const int gap = 14;
    const int row_w = 3 * dot_sz + 2 * gap;
    const int x0 = (320 - row_w) / 2;
    for (int i = 0; i < 3; i++) {
        s_dot_circles[i] = lv_obj_create(s_conn_dots);
        lv_obj_set_size(s_dot_circles[i], dot_sz, dot_sz);
        lv_obj_set_pos(s_dot_circles[i], x0 + i * (dot_sz + gap), (28 - dot_sz) / 2);
        lv_obj_set_style_radius(s_dot_circles[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dot_circles[i], lv_color_hex(UI_ACCENT), 0);
        lv_obj_set_style_border_width(s_dot_circles[i], 0, 0);
        lv_obj_set_style_pad_all(s_dot_circles[i], 0, 0);
        lv_obj_clear_flag(s_dot_circles[i], LV_OBJ_FLAG_SCROLLABLE);
        if (i > 0) {
            lv_obj_add_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool awaiting_server(void)
{
    return !s_server_online && !s_session_user[0] && s_st != ST_WIFI_ERR;
}

static void paint_wifi_icon(lv_obj_t *parent)
{
    const int cx = 36;
    const int base_y = 44;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *arc = lv_obj_create(parent);
        int w = 20 + i * 14;
        int h = 10 + i * 8;
        lv_obj_set_size(arc, w, h);
        lv_obj_set_pos(arc, cx - w / 2, base_y - h - i * 6);
        lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc, 3, 0);
        lv_obj_set_style_border_color(arc, lv_color_hex(0xA8B0B8), 0);
        lv_obj_set_style_border_side(arc, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, cx - 3, base_y - 3);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *x1 = lv_obj_create(parent);
    lv_obj_set_size(x1, 4, 36);
    lv_obj_set_style_bg_color(x1, lv_color_hex(0xE85A5A), 0);
    lv_obj_set_style_border_width(x1, 0, 0);
    lv_obj_set_style_pad_all(x1, 0, 0);
    lv_obj_set_style_transform_angle(x1, 450, 0);
    lv_obj_clear_flag(x1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(x1);
    lv_obj_t *x2 = lv_obj_create(parent);
    lv_obj_set_size(x2, 4, 36);
    lv_obj_set_style_bg_color(x2, lv_color_hex(0xE85A5A), 0);
    lv_obj_set_style_border_width(x2, 0, 0);
    lv_obj_set_style_pad_all(x2, 0, 0);
    lv_obj_set_style_transform_angle(x2, 1350, 0);
    lv_obj_clear_flag(x2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(x2);
}

static lv_obj_t *paint_message_panel(lv_obj_t *scr, int y, const char *headline,
                                     const char *sub, const char *hint)
{
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 288, hint ? 148 : 88);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_CARD), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_CARD_PRESS), 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(panel);
    lv_label_set_text(h, headline);
    lv_obj_set_style_text_color(h, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_width(h, 264);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *s = lv_label_create(panel);
    lv_label_set_text(s, sub);
    lv_obj_set_style_text_color(s, lv_color_hex(UI_TEXT_MUT), 0);
    lv_obj_set_width(s, 264);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 36);

    if (hint && hint[0]) {
        lv_obj_t *t = lv_label_create(panel);
        lv_label_set_text(t, hint);
        lv_obj_set_style_text_color(t, lv_color_hex(UI_TEXT_DIM), 0);
        lv_obj_set_width(t, 264);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 88);
    }
    return panel;
}

static void refresh_conn_dots(void)
{
    if (!s_dot_circles[0]) {
        return;
    }
    int64_t elapsed = now_us() - s_conn_dot_anim_us;
    int64_t slice_us = (int64_t)CONNECT_RETRY_MS * 1000 / 3;
    int phase = slice_us > 0 ? (int)(elapsed / slice_us) : 0;
    if (phase > 2) {
        phase = 2;
    }
    for (int i = 0; i < 3; i++) {
        if (i <= phase) {
            lv_obj_clear_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void signed_out_hangout_probe(void)
{
    (void)load_hangout_ms(CONNECT_PROBE_MS);
    if (s_server_online) {
        if (s_st == ST_CONNECTING) {
            s_st = ST_ROSTER;
            request_repaint();
        }
    } else {
        enter_connecting_from_signin();
    }
    s_conn_retry_us = now_us();
    s_conn_dot_anim_us = now_us();
    if (s_st == ST_CONNECTING) {
        refresh_conn_dots();
    }
}

static void paint_connecting(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);

    const int icon_w = 72 * CONNECT_ICON_SCALE / 100;
    const int icon_h = 56 * CONNECT_ICON_SCALE / 100;
    const int title_h = 32;
    const int dots_h = 28;
    const int gap_icon_title = 22;
    const int gap_title_dots = 14;
    const int stack_h = icon_h + gap_icon_title + title_h + gap_title_dots + dots_h;
    int y = (240 - stack_h) / 2;

    lv_obj_t *icon = paint_icon_box_at(scr, (320 - icon_w) / 2, y, CONNECT_ICON_SCALE);
    paint_mailbox_icon(icon, CONNECT_ICON_SCALE);
    y += icon_h + gap_icon_title;

    lv_obj_t *title_bar = paint_transparent_bar(scr, y, title_h);
    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "connecting");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT), 0);
    style_conn_title_font(title);
    lv_obj_center(title);
    y += title_h + gap_title_dots;

    paint_conn_dots_row(scr, y);
    s_conn_dot_anim_us = now_us();
    refresh_conn_dots();
    hook_scr(scr);
}

static void paint_wifi_error(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);
    lv_obj_t *icon = paint_icon_box(scr, 36, 100);
    paint_wifi_icon(icon);
    paint_message_panel(scr, 108, "no Wi-Fi", "this box needs the home network",
                        "ask Lynn to check the network");
    hook_scr(scr);
}

static void paint_roster(lv_obj_t *scr)
{
    if (awaiting_server()) {
        paint_connecting(scr);
        return;
    }

    lv_obj_clean(scr);
    clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_BG), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "sign in");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    int y0, row_h, row_step, face_sz;
    roster_row_layout(s_user_n, &y0, &row_h, &row_step, &face_sz);
    int face_y = (row_h - face_sz - 4) / 2;
    int label_x = 12 + face_sz + 8;
    int y = y0;
    for (int i = 0; i < s_user_n; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 0, 0);
        style_list_row(b);
        if (s_last_user[0] && strcmp(s_users[i].id, s_last_user) == 0) {
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(UI_ACCENT), 0);
        }
        paint_face_sized(b, i, 8, face_y, face_sz);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, s_users[i].name);
        style_list_row_label(t);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, label_x, 0);
        lv_obj_add_event_cb(b, on_user_btn, LV_EVENT_CLICKED, s_users[i].id);
        y += row_step;
    }
    hook_scr(scr);
}

static void paint_pin(lv_obj_t *scr)
{
    if (awaiting_server()) {
        paint_connecting(scr);
        return;
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    paint_face(scr, user_index(s_pick_id), 12, 6);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, user_name(s_pick_id));
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 48, 14);
    s_dots = lv_label_create(scr);
    lv_obj_set_style_text_color(s_dots, lv_color_hex(0xE8C040), 0);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 38);
    refresh_dots();
    s_status = lv_label_create(scr);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 54);
    if (pin_locked()) {
        char line[24];
        snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
        set_status(line, 0xE85A5A);
    } else {
        set_status("", 0xA8B0B8);
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
        lv_obj_add_event_cb(b, on_pin_key, LV_EVENT_CLICKED, (void *)keys[i]);
    }
    hook_scr(scr);
}

static void paint_pick(lv_obj_t *scr)
{
    (void)load_hangout();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "send to");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    int pick_rows = 1;
    for (int i = 0; i < s_user_n; i++) {
        if (strcmp(s_users[i].id, s_session_user) != 0) {
            pick_rows++;
        }
    }
    int y0, row_h, row_step, face_sz;
    roster_row_layout(pick_rows, &y0, &row_h, &row_step, &face_sz);
    int face_y = (row_h - face_sz - 4) / 2;
    int label_x = 12 + face_sz + 8;
    int y = pick_rows > 3 ? 22 : 28;
    for (int i = 0; i < s_user_n; i++) {
        if (strcmp(s_users[i].id, s_session_user) == 0) {
            continue;
        }
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 0, 0);
        style_list_row(b);
        paint_face_sized(b, i, 8, face_y, face_sz);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, s_users[i].name);
        style_list_row_label(t);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, label_x, 0);
        lv_obj_add_event_cb(b, on_pick_btn, LV_EVENT_CLICKED, s_users[i].id);
        y += row_step;
    }
    lv_obj_t *all = lv_button_create(scr);
    lv_obj_set_pos(all, 16, y);
    lv_obj_set_size(all, 288, row_h);
    lv_obj_set_style_pad_all(all, 0, 0);
    style_list_row(all);
    paint_asterisk_icon(all, 8, face_y);
    lv_obj_t *at = lv_label_create(all);
    lv_label_set_text(at, "Everyone");
    style_list_row_label(at);
    lv_obj_align(at, LV_ALIGN_LEFT_MID, label_x, 0);
    lv_obj_add_event_cb(all, on_pick_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "shoulder or 10s = cancel");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    hook_scr(scr);
}

static void paint_carousel(lv_obj_t *scr)
{
    const card_grad_t *g = active_card_grad();
    uint32_t disk_hex = g->light_ui ? 0x101418U : 0x3A4450U;
    int32_t keep_scroll = 0;
    int keep_focus = s_focus;
    bool restore_scroll = s_carousel_scroll != NULL;
    if (restore_scroll) {
        keep_scroll = lv_obj_get_scroll_x(s_carousel_scroll);
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    s_overlay = NULL;
    s_carousel_scroll = NULL;
    memset(s_card_ui, 0, sizeof(s_card_ui));
    s_bar = NULL;
    s_play_btn = NULL;
    s_play_icon = NULL;
    s_pause_icon = NULL;
    s_vol_slider = NULL;
    s_ribbon_top = NULL;
    s_ribbon_bot = NULL;
    s_count_lab = NULL;
    s_card_face_seq = -1;
    s_card_face_read = false;
    s_scroll_lock = false;
    s_carousel_locked = false;

    paint_ribbons(scr);
    s_carousel_ready_us = now_us();

    s_carousel_scroll = lv_obj_create(scr);
    lv_obj_set_pos(s_carousel_scroll, 0, SCROLL_CARD_Y);
    lv_obj_set_size(s_carousel_scroll, LCD_W, SCROLL_CARD_H);
    lv_obj_set_style_bg_opa(s_carousel_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_carousel_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_carousel_scroll, 0, 0);
    lv_obj_add_flag(s_carousel_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_carousel_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_carousel_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_carousel_scroll, on_carousel_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_carousel_scroll, on_carousel_scroll, LV_EVENT_SCROLL_END, NULL);

    int x = SCROLL_CARD_GAP;
    for (int i = 0; i < s_msg_n; i++) {
        carousel_add_card(i, x);
        x += SCROLL_CARD_W + SCROLL_CARD_GAP;
    }
    lv_obj_t *end = lv_obj_create(s_carousel_scroll);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, x, 0);

    s_play_btn = lv_button_create(scr);
    lv_obj_set_size(s_play_btn, CAROUSEL_PLAY, CAROUSEL_PLAY);
    lv_obj_align(s_play_btn, LV_ALIGN_TOP_MID, 0, CAROUSEL_TRANSPORT_Y);
    lv_obj_set_style_bg_opa(s_play_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_play_btn, 0, 0);
    lv_obj_set_style_border_width(s_play_btn, 0, 0);
    lv_obj_set_style_pad_all(s_play_btn, 0, 0);
    lv_obj_add_event_cb(s_play_btn, on_play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disk = lv_obj_create(s_play_btn);
    lv_obj_set_size(disk, CAROUSEL_DISK, CAROUSEL_DISK);
    lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disk, lv_color_hex(disk_hex), 0);
    lv_obj_set_style_bg_opa(disk, g->light_ui ? LV_OPA_20 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disk, 0, 0);
    lv_obj_set_style_pad_all(disk, 0, 0);
    lv_obj_clear_flag(disk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(disk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(disk);

    s_play_icon = make_play_icon_buf(s_play_btn, disk_hex, s_play_icon_fb);
    s_pause_icon = make_pause_icon(s_play_btn);
    lv_obj_add_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);

    s_bar = lv_slider_create(scr);
    lv_obj_set_size(s_bar, SCROLL_CARD_W - 16, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, CAROUSEL_TRANSPORT_Y + CAROUSEL_PLAY + 4);
    style_transport_slider(s_bar);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_RELEASED, NULL);

    ui_refresh_transport();
    if (s_msg_n > 0) {
        if (keep_focus >= 0 && keep_focus < s_msg_n) {
            s_focus = keep_focus;
        }
        if (restore_scroll) {
            s_scroll_lock = true;
            lv_obj_scroll_to_x(s_carousel_scroll, keep_scroll, LV_ANIM_OFF);
            s_scroll_lock = false;
            s_carousel_locked = true;
            ui_refresh_transport();
        } else {
            carousel_snap_to_focus();
        }
    } else {
        s_carousel_locked = true;
        ui_refresh_transport();
    }
    hook_scr(scr);
}

static void paint_settings(lv_obj_t *scr)
{
    static const uint32_t accents[ACCENT_COUNT] = {
        0x5AA0E8, 0xE8C040, 0x7AC47A, 0xC070E8,
        0xE87A9A, 0x7AD4E8, 0xD4A0E8, 0xE8A87A,
        0x4ECDC4, 0xFF6B6B,
    };

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    s_vol_slider = NULL;
    s_vol_val_lab = NULL;
    s_settings_scroll = NULL;

    paint_ribbons(scr);

    s_settings_scroll = lv_obj_create(scr);
    lv_obj_set_pos(s_settings_scroll, 0, RIBBON_H);
    lv_obj_set_size(s_settings_scroll, 320, CONTENT_H);
    lv_obj_set_style_bg_color(s_settings_scroll, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(s_settings_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_settings_scroll, 0, 0);
    lv_obj_add_flag(s_settings_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_settings_scroll, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *content = lv_obj_create(s_settings_scroll);
    lv_obj_set_width(content, 320);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    int y = 8;
    int me = user_index(s_session_user);
    uint32_t my_accent = user_accent(me);

    lv_obj_t *title = lv_label_create(content);
    lv_label_set_text(title, user_name(s_session_user));
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_pos(title, 16, y);
    y += 28;

    lv_obj_t *vt = lv_label_create(content);
    lv_label_set_text(vt, "vol");
    lv_obj_set_style_text_color(vt, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(vt, 16, y + 4);
    s_vol_slider = lv_slider_create(content);
    lv_obj_set_size(s_vol_slider, 200, 20);
    lv_obj_set_pos(s_vol_slider, 56, y);
    lv_slider_set_range(s_vol_slider, 0, ROOMVOL_ON);
    lv_slider_set_value(s_vol_slider, s_vol_notch, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(0x2A3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_vol_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_vol_slider, lv_color_hex(0xE8F0E8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_vol_slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(s_vol_slider, on_volume, LV_EVENT_VALUE_CHANGED, NULL);
    s_vol_val_lab = lv_label_create(content);
    lv_obj_set_style_text_color(s_vol_val_lab, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(s_vol_val_lab, 268, y + 2);
    refresh_vol_label();
    y += 40;

    lv_obj_t *ct = lv_label_create(content);
    lv_label_set_text(ct, "color");
    lv_obj_set_style_text_color(ct, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(ct, 16, y);
    y += 20;
    for (int i = 0; i < ACCENT_COUNT; i++) {
        int col = i % 5;
        int row = i / 5;
        lv_obj_t *sw = lv_button_create(content);
        lv_obj_set_size(sw, 40, 40);
        lv_obj_set_pos(sw, 16 + col * 48, y + row * 48);
        lv_obj_set_style_bg_color(sw, lv_color_hex(accents[i]), 0);
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
        if (accents[i] == my_accent) {
            lv_obj_set_style_border_width(sw, 2, 0);
            lv_obj_set_style_border_color(sw, lv_color_hex(0xE8F0E8), 0);
        }
        lv_obj_add_event_cb(sw, on_accent_pick, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)accents[i]);
    }
    y += 104;

    lv_obj_t *gt = lv_label_create(content);
    lv_label_set_text(gt, "card grad");
    lv_obj_set_style_text_color(gt, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(gt, 16, y);
    y += 20;
    for (int i = 0; i < CARD_GRAD_N; i++) {
        const card_grad_t *cg = &s_card_grads[i];
        lv_obj_t *gb = lv_button_create(content);
        lv_obj_set_size(gb, 88, 52);
        lv_obj_set_pos(gb, 16 + i * 96, y);
        lv_obj_set_style_bg_color(gb, lv_color_hex(cg->top), 0);
        lv_obj_set_style_bg_grad_color(gb, lv_color_hex(cg->bot), 0);
        lv_obj_set_style_bg_grad_dir(gb, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_radius(gb, CARD_RADIUS, 0);
        if (cg->id == s_card_grad) {
            lv_obj_set_style_border_width(gb, 2, 0);
            lv_obj_set_style_border_color(gb, lv_color_hex(0xE8F0E8), 0);
        }
        lv_obj_add_event_cb(gb, on_grad_pick, LV_EVENT_CLICKED, (void *)(uintptr_t)cg->id);
    }
    y += 60;

    lv_obj_t *ft = lv_label_create(content);
    lv_label_set_text(ft, "face");
    lv_obj_set_style_text_color(ft, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(ft, 16, y);
    y += 20;
    uint8_t my_slot = user_avatar_slot(me);
    for (int slot = 0; slot <= AVATAR_SLOTS; slot++) {
        int col = slot % 4;
        int row = slot / 4;
        lv_obj_t *fb = lv_button_create(content);
        lv_obj_set_size(fb, 48, 48);
        lv_obj_set_pos(fb, 16 + col * 56, y + row * 56);
        lv_obj_set_style_pad_all(fb, 2, 0);
        if (slot == (int)my_slot) {
            lv_obj_set_style_border_width(fb, 2, 0);
            lv_obj_set_style_border_color(fb, lv_color_hex(0xE8F0E8), 0);
        }
        if (slot == 0) {
            paint_geometry_face(fb, my_accent, 40);
        } else {
            const lv_image_dsc_t *img = avatar_image((uint8_t)slot);
            if (img) {
                lv_obj_t *av = lv_image_create(fb);
                lv_image_set_src(av, img);
                lv_obj_set_size(av, 40, 40);
                lv_obj_center(av);
            }
        }
        lv_obj_add_event_cb(fb, on_avatar_pick, LV_EVENT_CLICKED, (void *)(intptr_t)slot);
    }
    y += 4 * 56;

    lv_obj_t *out = lv_button_create(content);
    lv_obj_set_size(out, 200, 48);
    lv_obj_set_pos(out, 60, y);
    lv_obj_t *out_lab = lv_label_create(out);
    lv_label_set_text(out_lab, "sign out");
    lv_obj_set_style_text_color(out_lab, lv_color_hex(0xE8F0E8), 0);
    lv_obj_center(out_lab);
    lv_obj_add_event_cb(out, on_sign_out, LV_EVENT_CLICKED, NULL);
    y += 60;

    lv_obj_set_height(content, y);
    hook_scr(scr);
}

static void paint_record_overlay(lv_obj_t *scr)
{
    if (s_overlay) {
        return;
    }
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 300, 80);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x2A3038), 0);
    lv_obj_t *t = lv_label_create(s_overlay);
    lv_label_set_text(t, "listening...\ntap circle · shoulder cancel");
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8F0E8), 0);
    lv_obj_center(t);
}

static bool can_enter_sleep(void)
{
    return s_st == ST_ROSTER || s_st == ST_PIN || s_st == ST_CAROUSEL || s_st == ST_SETTINGS;
}

static bool session_signed_in(void)
{
    return s_session_user[0] != 0 &&
           (s_st == ST_CAROUSEL || s_st == ST_SETTINGS || s_st == ST_PIN);
}

static int unread_count(void)
{
    int n = 0;
    for (int i = 0; i < s_msg_n; i++) {
        if (!s_msgs[i].read) {
            n++;
        }
    }
    return n;
}

static uint32_t sleep_accent(void)
{
    if (session_signed_in()) {
        int idx = user_index(s_session_user);
        if (idx >= 0) {
            return user_accent(idx);
        }
    }
    return UI_ACCENT;
}

static int sleep_breathe_brightness(int64_t ms_since_enter)
{
    int64_t t = ms_since_enter % SLEEP_BREATHE_MS;
    int half = SLEEP_BREATHE_MS / 2;
    int span = BRIGHT_SLEEP_PEAK - BRIGHT_SLEEP;
    if (t < half) {
        return BRIGHT_SLEEP + (int)(span * t / half);
    }
    return BRIGHT_SLEEP_PEAK - (int)(span * (t - half) / half);
}

static lv_opa_t sleep_breathe_opa(int64_t ms_since_enter, lv_opa_t lo, lv_opa_t hi)
{
    int64_t t = ms_since_enter % SLEEP_BREATHE_MS;
    int half = SLEEP_BREATHE_MS / 2;
    int span = (int)hi - (int)lo;
    int v;
    if (t < half) {
        v = (int)lo + (int)(span * t / half);
    } else {
        v = (int)hi - (int)(span * (t - half) / half);
    }
    if (v < 0) {
        v = 0;
    }
    if (v > 255) {
        v = 255;
    }
    return (lv_opa_t)v;
}

static lv_opa_t sleep_hint_boost(int64_t ms_since_enter)
{
    int64_t phase = ms_since_enter % SLEEP_HINT_MS;
    if (phase >= SLEEP_HINT_PULSE_MS) {
        return 0;
    }
    int64_t t = phase;
    int half = SLEEP_HINT_PULSE_MS / 2;
    int v;
    if (t < half) {
        v = (int)(80 * t / half);
    } else {
        v = (int)(80 * (SLEEP_HINT_PULSE_MS - t) / half);
    }
    return (lv_opa_t)v;
}

static void paint_sleep(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    clear_conn_dots();
    s_offline_lab = NULL;
    s_sleep_glow = NULL;
    s_sleep_hint = NULL;
    s_sleep_badge = NULL;
    s_sleep_badge_lab = NULL;

    uint32_t accent = sleep_accent();
    int unread = session_signed_in() ? unread_count() : 0;

    lv_obj_set_style_bg_color(scr, lv_color_hex(UI_SLEEP_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_sleep_glow = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sleep_glow);
    lv_obj_remove_flag(s_sleep_glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sleep_glow, 320, 140);
    lv_obj_set_pos(s_sleep_glow, 0, 100);
    lv_obj_set_style_radius(s_sleep_glow, 0, 0);
    lv_obj_set_style_bg_color(s_sleep_glow, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_grad_color(s_sleep_glow, lv_color_hex(UI_SLEEP_BG), 0);
    lv_obj_set_style_bg_grad_dir(s_sleep_glow, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_sleep_glow, LV_OPA_20, 0);

    s_sleep_hint = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sleep_hint);
    lv_obj_remove_flag(s_sleep_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sleep_hint, 72, 36);
    lv_obj_set_pos(s_sleep_hint, 124, 188);
    lv_obj_set_style_radius(s_sleep_hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sleep_hint, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(s_sleep_hint, LV_OPA_0, 0);

    if (unread > 0) {
        s_sleep_badge = lv_obj_create(scr);
        lv_obj_remove_style_all(s_sleep_badge);
        lv_obj_remove_flag(s_sleep_badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_sleep_badge, 28, 28);
        lv_obj_align(s_sleep_badge, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_set_style_radius(s_sleep_badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_sleep_badge, lv_color_hex(accent), 0);
        lv_obj_set_style_bg_opa(s_sleep_badge, LV_OPA_60, 0);
        s_sleep_badge_lab = lv_label_create(s_sleep_badge);
        char buf[8];
        if (unread > 9) {
            snprintf(buf, sizeof(buf), "9+");
        } else {
            snprintf(buf, sizeof(buf), "%d", unread);
        }
        lv_label_set_text(s_sleep_badge_lab, buf);
        lv_obj_set_style_text_color(s_sleep_badge_lab, lv_color_hex(UI_SLEEP_BG), 0);
        lv_obj_center(s_sleep_badge_lab);
    }

    hook_scr(scr);
}

static void refresh_sleep_anim(void)
{
    if (!s_asleep || !s_sleep_glow) {
        return;
    }
    int64_t ms = (now_us() - s_sleep_enter_us) / 1000;
    int glow = (int)sleep_breathe_opa(ms, LV_OPA_10, LV_OPA_30) +
               (int)sleep_hint_boost(ms);
    if (glow > (int)LV_OPA_COVER) {
        glow = (int)LV_OPA_COVER;
    }
    lv_obj_set_style_bg_opa(s_sleep_glow, (lv_opa_t)glow, 0);

    if (s_sleep_hint) {
        lv_obj_set_style_bg_opa(s_sleep_hint, sleep_hint_boost(ms), 0);
    }

    if (s_sleep_badge && session_signed_in() && unread_count() > 0) {
        lv_obj_set_style_bg_opa(s_sleep_badge, sleep_breathe_opa(ms, LV_OPA_40, LV_OPA_80), 0);
    }

    int bl;
    if (ms < SLEEP_FADE_MS) {
        int target = sleep_breathe_brightness(ms);
        bl = BRIGHT_DIM + (target - BRIGHT_DIM) * (int)ms / SLEEP_FADE_MS;
    } else {
        bl = sleep_breathe_brightness(ms);
    }
    board_backlight_set(bl);

    lv_obj_invalidate(s_sleep_glow);
    if (s_sleep_hint) {
        lv_obj_invalidate(s_sleep_hint);
    }
    if (s_sleep_badge) {
        lv_obj_invalidate(s_sleep_badge);
    }
}

static void paint(void)
{
    s_status = NULL;
    s_dots = NULL;
    s_carousel_scroll = NULL;
    memset(s_card_ui, 0, sizeof(s_card_ui));
    s_vol_slider = NULL;
    s_overlay = NULL;
    s_toast = NULL;
    s_ribbon_top = NULL;
    s_ribbon_bot = NULL;
    s_count_lab = NULL;
    s_settings_scroll = NULL;
    s_vol_val_lab = NULL;
    s_offline_lab = NULL;
    clear_conn_dots();
    s_sleep_glow = NULL;
    s_sleep_hint = NULL;
    s_sleep_badge = NULL;
    s_sleep_badge_lab = NULL;
    lv_obj_t *scr = lv_screen_active();
    if (s_asleep) {
        paint_sleep(scr);
        s_repaint = false;
        refresh_sleep_anim();
        return;
    }
    switch (s_st) {
    case ST_CONNECTING:
        paint_connecting(scr);
        break;
    case ST_WIFI_ERR:
        paint_wifi_error(scr);
        break;
    case ST_ROSTER:
        paint_roster(scr);
        break;
    case ST_PIN:
        paint_pin(scr);
        break;
    case ST_PICK:
        paint_pick(scr);
        break;
    case ST_CAROUSEL:
        paint_carousel(scr);
        break;
    case ST_SETTINGS:
        paint_settings(scr);
        break;
    case ST_RECORD:
        if (scr != NULL) {
            paint_carousel(scr);
            paint_record_overlay(scr);
        }
        break;
    }
    s_repaint = false;
}

static void ui_task(void *arg)
{
    (void)arg;
    board_lvgl_lock(0);
    paint();
    board_lvgl_unlock();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_chirp_hz && !s_want_play && !s_playing) {
            int a = s_chirp_hz;
            int b = s_chirp_hz2;
            s_chirp_hz = 0;
            s_chirp_hz2 = 0;
            play_chirp(a);
            if (b) {
                play_chirp(b);
            }
        }
        int64_t idle = now_us() - s_activity_us;
        if (!s_asleep && can_enter_sleep() && idle > (int64_t)SLEEP_MS * 1000) {
            s_asleep = true;
            s_dimmed = false;
            s_sleep_enter_us = now_us();
            stop_playback();
            board_lvgl_lock(0);
            paint_sleep(lv_screen_active());
            board_lvgl_unlock();
            board_lvgl_lock(0);
            refresh_sleep_anim();
            board_lvgl_unlock();
        } else if (s_asleep) {
            board_lvgl_lock(0);
            refresh_sleep_anim();
            board_lvgl_unlock();
        } else if (!s_dimmed && !s_asleep && s_activity_us > 0 &&
                   idle > (int64_t)DIM_MS * 1000) {
            s_dimmed = true;
            board_backlight_set(BRIGHT_DIM);
        }
        if (signed_out_pre_auth()) {
            if (!s_server_online && s_st != ST_CONNECTING) {
                enter_connecting_from_signin();
            } else if (s_server_online && s_st == ST_CONNECTING) {
                s_st = ST_ROSTER;
                request_repaint();
            } else if ((now_us() - s_conn_retry_us) > (int64_t)CONNECT_RETRY_MS * 1000) {
                signed_out_hangout_probe();
            }
        }
        if (s_st == ST_WIFI_ERR &&
            (now_us() - s_wifi_retry_us) > (int64_t)WIFI_RETRY_MS * 1000) {
            s_wifi_retry_us = now_us();
            if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) == ESP_OK) {
                s_st = ST_CONNECTING;
                s_connect_start_us = now_us();
                s_conn_retry_us = now_us();
                s_conn_dot_anim_us = now_us();
                ws_start();
                (void)load_hangout();
                if (s_server_online) {
                    s_st = ST_ROSTER;
                }
                request_repaint();
            }
        }
        if (s_conn_dots && s_st == ST_CONNECTING) {
            board_lvgl_lock(0);
            refresh_conn_dots();
            board_lvgl_unlock();
        }
        if (s_st == ST_CAROUSEL || s_st == ST_SETTINGS) {
            board_lvgl_lock(0);
            refresh_offline_ribbon();
            board_lvgl_unlock();
        }
        if (s_repaint || s_playing || s_transport_dirty) {
            board_lvgl_lock(0);
            if (s_repaint) {
                paint();
            } else if (s_st == ST_CAROUSEL) {
                ui_refresh_transport();
            }
            s_transport_dirty = false;
            board_lvgl_unlock();
        }
        if (s_toast_pending) {
            s_toast_pending = false;
            board_lvgl_lock(0);
            set_toast(s_toast_msg);
            board_lvgl_unlock();
            vTaskDelay(pdMS_TO_TICKS(2500));
            board_lvgl_lock(0);
            set_toast("");
            board_lvgl_unlock();
        }
        if (s_st == ST_PIN && pin_locked()) {
            board_lvgl_lock(0);
            char line[24];
            snprintf(line, sizeof(line), "ask Lynn  %ds", pin_lock_sec());
            set_status(line, 0xE85A5A);
            board_lvgl_unlock();
        } else if (s_st == ST_PIN && s_pin_fails >= PIN_TRIES && !pin_locked()) {
            s_pin_fails = 0;
            board_lvgl_lock(0);
            set_status("", 0xA8B0B8);
            board_lvgl_unlock();
        }
        if (s_st == ST_PICK && s_pick_open_us > 0 &&
            (now_us() - s_pick_open_us) > (int64_t)PICK_TIMEOUT_MS * 1000) {
            s_pick_open_us = 0;
            request_chirp_pair(523, 392);
            s_st = ST_CAROUSEL;
            request_repaint();
        }
        if (s_inbox_dirty && s_session_user[0] &&
            (s_inbox_for[0] == 0 || strcmp(s_inbox_for, s_session_user) == 0)) {
            s_inbox_dirty = false;
            if (s_st == ST_RECORD) {
                s_inbox_dirty = true;
            } else {
                int keep = -1;
                msg_t *m = focus_msg();
                if (m) {
                    keep = m->seq;
                }
                if (s_st == ST_CAROUSEL || s_st == ST_SETTINGS) {
                    stop_playback();
                }
                if (reload_inbox()) {
                    if (keep > 0) {
                        for (int i = 0; i < s_msg_n; i++) {
                            if (s_msgs[i].seq == keep) {
                                s_focus = i;
                                break;
                            }
                        }
                    }
                    if (s_st == ST_CAROUSEL) {
                        request_chirp(784);
                        request_repaint();
                    }
                }
            }
        }
        if ((s_st == ST_CAROUSEL || s_st == ST_SETTINGS) &&
            (now_us() - s_idle_us) > (int64_t)IDLE_MS * 1000) {
            stop_playback();
            s_elen = 0;
            s_entry[0] = 0;
            s_st = ST_PIN;
            board_lvgl_lock(0);
            paint();
            board_lvgl_unlock();
            bump_idle();
            continue;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "x02 v1 shell (%s / %s, peer %s) %s://%s:%d tls=%d skip=%d",
             WHO_DEVICE_NAME, WHO_DEVICE_ID, WHO_PEER_NAME,
#if DEMO_SERVER_TLS
             "https",
#else
             "http",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT, DEMO_SERVER_TLS, DEMO_TLS_SKIP_VERIFY);
    if (board_display_start() != ESP_OK) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    board_backlight_set(BRIGHT_NORM);
    s_activity_us = now_us();
    bump_idle();
    s_connect_start_us = now_us();
    s_conn_retry_us = now_us();
    s_conn_dot_anim_us = now_us();
    s_wifi_retry_us = now_us();

    nvs_load_last();
    nvs_load_card_grad();
    (void)nvs_load_hangout();
    s_st = ST_CONNECTING;

    s_buf = heap_caps_malloc(BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        s_buf = heap_caps_malloc(BUF_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_buf) {
        board_status_set("OOM playback buf");
        ESP_LOGE(TAG, "playback buffer alloc failed");
        return;
    }

    s_work = xSemaphoreCreateBinary();
    s_login_work = xSemaphoreCreateBinary();
    xTaskCreate(login_task_fn, "login", 8192, NULL, 5, NULL);
    xTaskCreate(ui_task, "ui", 12288, NULL, 5, NULL);

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        ESP_LOGE(TAG, "wifi join failed");
        s_st = ST_WIFI_ERR;
        request_repaint();
    } else {
#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
        sntp_wait();
#endif
        ws_start();
        (void)load_hangout();
        if (s_server_online) {
            ESP_LOGI(TAG, "hangout %d users", s_user_n);
            s_st = ST_ROSTER;
        } else {
            ESP_LOGW(TAG, "GET /v1/hangout failed");
            s_st = ST_CONNECTING;
        }
        request_repaint();
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_spk || !s_mic) {
        board_status_set("codec failed");
        ESP_LOGE(TAG, "codec init failed spk=%p mic=%p", (void *)s_spk, (void *)s_mic);
    } else {
        (void)esp_codec_dev_set_out_vol(s_spk, 0);
    }

    xTaskCreate(record_task_fn, "rec", 8192, NULL, 5, NULL);
    xTaskCreate(playback_task, "play", 12288, NULL, 5, NULL);

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK) {
        if (btns[BSP_BUTTON_MAIN]) {
            iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL,
                                  on_circle_up, NULL);
        }
        if (btns[BSP_BUTTON_CONFIG]) {
            iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL,
                                  on_boot_press, NULL);
        }
    }

    s_vol_notch = 4;
    apply_volume(roomvol_codec(s_vol_notch));
}
