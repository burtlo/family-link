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
#define PEEK_ANIM_MS   130
#define RIBBON_H       20
#define CARD_MARGIN    8
#define CONTENT_H      (240 - 2 * RIBBON_H)
#define CARD_Y         (RIBBON_H + CARD_MARGIN)
#define CARD_TRACK_H   22
#define CARD_PANE_H    (CONTENT_H - 2 * CARD_MARGIN - CARD_TRACK_H)
#define CARD_H         (CARD_PANE_H + CARD_TRACK_H)
#define CARD_W         200
#define CARD_PANE_W    (CARD_W / 2)
#define PEEK_W         56
#define FACE_SZ        96
#define PLAY_HIT       64
#define PLAY_DISK      40
#define AVATAR_SLOTS   12
#define ACCENT_COUNT   10
#define CHIRP_MS       160
#define CHIRP_DRAIN_MS 60
#define CHIRP_VOL      70
#define CHIRP_WRITE    1024
#define CHIRP_SAMPLES  (SAMPLE_RATE * CHIRP_MS / 1000)
#define CHIRP_DRAIN    (SAMPLE_RATE * CHIRP_DRAIN_MS / 1000)

typedef enum { ST_ROSTER, ST_PIN, ST_CAROUSEL, ST_SETTINGS, ST_PICK, ST_RECORD } state_t;

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

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static state_t s_st = ST_ROSTER;
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
static char s_inbox_for[16];
static int s_peek_dir;
static int64_t s_carousel_ready_us;
static bool s_send_hint_shown;
static int64_t s_roster_retry_us;
static esp_websocket_client_handle_t s_ws;

static lv_obj_t *s_ribbon_top;
static lv_obj_t *s_ribbon_bot;
static lv_obj_t *s_count_lab;
static lv_obj_t *s_back_hint;
static lv_obj_t *s_status;
static lv_obj_t *s_dots;
static lv_obj_t *s_settings_scroll;
static lv_obj_t *s_vol_val_lab;
static lv_obj_t *s_card;
static lv_obj_t *s_card_face_pane;
static lv_obj_t *s_card_play_pane;
static lv_obj_t *s_peek_left;
static lv_obj_t *s_peek_right;
static lv_obj_t *s_peek_left_lab;
static lv_obj_t *s_peek_right_lab;
static lv_obj_t *s_bar;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_play_icon;
static lv_obj_t *s_pause_icon;
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_overlay;
static lv_obj_t *s_toast;

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_buf;
static uint8_t *s_pcm;
static SemaphoreHandle_t s_work;
static bool s_spk_open;
static uint8_t s_json[JSON_CAP];
static int16_t s_chirp_pcm[CHIRP_SAMPLES + CHIRP_DRAIN];

static int64_t now_us(void) { return esp_timer_get_time(); }
static void bump_idle(void) { s_idle_us = now_us(); }

static void note_activity(void)
{
    s_activity_us = now_us();
    bump_idle();
    if (s_asleep || s_dimmed) {
        s_asleep = false;
        s_dimmed = false;
        board_backlight_set(BRIGHT_NORM);
    }
}
static void request_repaint(void) { s_repaint = true; }

static void set_toast(const char *msg)
{
    if (s_toast) {
        lv_label_set_text(s_toast, msg ? msg : "");
    }
    if (s_back_hint) {
        if (msg && msg[0]) {
            lv_obj_add_flag(s_back_hint, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_back_hint, LV_OBJ_FLAG_HIDDEN);
        }
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

static uint32_t hue_lighten(uint32_t hex, int pct)
{
    int r = (int)((hex >> 16) & 0xFF);
    int g = (int)((hex >> 8) & 0xFF);
    int b = (int)(hex & 0xFF);
    r = r + (255 - r) * pct / 100;
    g = g + (255 - g) * pct / 100;
    b = b + (255 - b) * pct / 100;
    return (uint32_t)((r << 16) | (g << 8) | b);
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

static void paint_user_portrait(lv_obj_t *parent, int idx, int sz)
{
    uint8_t slot = user_avatar_slot(idx);
    const lv_image_dsc_t *img = avatar_image(slot);
    if (img) {
        lv_obj_t *av = lv_image_create(parent);
        lv_image_set_src(av, img);
        lv_obj_set_size(av, sz, sz);
        lv_obj_center(av);
        return;
    }
    paint_geometry_face(parent, user_accent(idx), sz);
}

static void paint_face(lv_obj_t *parent, int idx, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 28, 28);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    paint_user_portrait(box, idx, 24);
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

static void refresh_card_face(msg_t *m)
{
    if (!s_card_face_pane || !m) {
        return;
    }
    int idx = user_index_by_label(m->from_label);
    uint32_t hue = user_accent(idx);
    lv_obj_clean(s_card_face_pane);
    lv_obj_set_style_bg_color(s_card_face_pane, lv_color_hex(hue), 0);
    if (s_card_play_pane) {
        lv_obj_set_style_bg_color(s_card_play_pane, lv_color_hex(hue_lighten(hue, 28)), 0);
    }
    paint_user_portrait(s_card_face_pane, idx, FACE_SZ);
    if (s_card) {
        if (!m->read) {
            lv_obj_set_style_border_width(s_card, 2, 0);
            lv_obj_set_style_border_color(s_card, lv_color_hex(0xE8C040), 0);
        } else {
            lv_obj_set_style_border_width(s_card, 0, 0);
        }
    }
}

static void peek_text(char *out, size_t cap, const msg_t *m, const char *fallback)
{
    if (!m || !m->from_label[0]) {
        snprintf(out, cap, "%s", fallback);
        return;
    }
    snprintf(out, cap, "%.4s", m->from_label);
}

static int http_json(const char *method, const char *path, const char *json,
                     const char *user_id, http_buf_t *body)
{
    char url[160];
    format_url(url, sizeof(url), path);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
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
        while (total < body->cap - 1) {
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
    if (http_json("POST", "/v1/session/login", js, NULL, &b) != 200) {
        return false;
    }
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

static void paint_peek_content(lv_obj_t *peek, const msg_t *m, bool older);
static void save_profile(void);
static void refresh_vol_label(void);
static lv_obj_t *make_play_icon(lv_obj_t *parent);

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

static void ui_refresh_peeks(void)
{
    if (s_peek_left) {
        if (s_focus > 0) {
            lv_obj_clear_flag(s_peek_left, LV_OBJ_FLAG_HIDDEN);
            paint_peek_content(s_peek_left, &s_msgs[s_focus - 1], true);
        } else {
            lv_obj_add_flag(s_peek_left, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_peek_right) {
        if (s_focus + 1 < s_msg_n) {
            lv_obj_clear_flag(s_peek_right, LV_OBJ_FLAG_HIDDEN);
            paint_peek_content(s_peek_right, &s_msgs[s_focus + 1], false);
        } else {
            lv_obj_add_flag(s_peek_right, LV_OBJ_FLAG_HIDDEN);
        }
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

static lv_obj_t *make_pause_icon(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 20, 20);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = lv_obj_create(box);
    lv_obj_set_size(l, 4, 16);
    lv_obj_set_style_bg_color(l, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(l, LV_ALIGN_CENTER, -5, 0);
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_set_size(r, 4, 16);
    lv_obj_set_style_bg_color(r, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
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
    if (s_play_btn) {
        set_transport_icon(s_playing);
    }
    if (m->seq != s_card_face_seq || m->read != s_card_face_read) {
        s_card_face_seq = m->seq;
        s_card_face_read = m->read;
        refresh_card_face(m);
    } else if (s_card) {
        if (!m->read) {
            lv_obj_set_style_border_width(s_card, 2, 0);
            lv_obj_set_style_border_color(s_card, lv_color_hex(0xE8C040), 0);
        } else {
            lv_obj_set_style_border_width(s_card, 0, 0);
        }
    }
    if (s_count_lab) {
        char who[16];
        snprintf(who, sizeof(who), "%d/%d",
                 s_msg_n > 0 ? s_focus + 1 : 0, s_msg_n);
        lv_label_set_text(s_count_lab, who);
    }
    ui_refresh_peeks();
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
            if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
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
        request_repaint();
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
                (void)reload_inbox();
                s_toast_pending = true;
                strncpy(s_toast_msg, "sent", sizeof(s_toast_msg) - 1);
            } else {
                s_toast_pending = true;
                strncpy(s_toast_msg, "send failed", sizeof(s_toast_msg) - 1);
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

static bool load_hangout(void)
{
    http_buf_t b = { .buf = s_json, .cap = sizeof(s_json) };
    if (http_json("GET", "/v1/hangout", NULL, NULL, &b) != 200) {
        ESP_LOGW(TAG, "GET /v1/hangout failed (have %d cached users)", s_user_n);
        return s_user_n > 0;
    }
    cJSON *root = cJSON_Parse((char *)s_json);
    cJSON *users = root ? cJSON_GetObjectItem(root, "users") : NULL;
    int loaded = 0;
    user_t fresh[USER_MAX];
    if (cJSON_IsArray(users)) {
        int n = cJSON_GetArraySize(users);
        for (int i = 0; i < n && loaded < USER_MAX; i++) {
            cJSON *it = cJSON_GetArrayItem(users, i);
            cJSON *id = cJSON_GetObjectItem(it, "id");
            cJSON *name = cJSON_GetObjectItem(it, "name");
            if (!cJSON_IsString(id)) {
                continue;
            }
            strncpy(fresh[loaded].id, id->valuestring, sizeof(fresh[0].id) - 1);
            fresh[loaded].id[sizeof(fresh[0].id) - 1] = 0;
            if (cJSON_IsString(name)) {
                strncpy(fresh[loaded].name, name->valuestring, sizeof(fresh[0].name) - 1);
            } else {
                strncpy(fresh[loaded].name, id->valuestring, sizeof(fresh[0].name) - 1);
            }
            fresh[loaded].name[sizeof(fresh[0].name) - 1] = 0;
            fresh[loaded].avatar_slot = 0;
            fresh[loaded].accent = 0;
            cJSON *prof = cJSON_GetObjectItem(it, "profile");
            apply_profile_json(prof, &fresh[loaded]);
            loaded++;
        }
    }
    cJSON_Delete(root);
    if (loaded > 0) {
        memcpy(s_users, fresh, sizeof(fresh[0]) * loaded);
        s_user_n = loaded;
        return true;
    }
    ESP_LOGW(TAG, "hangout response had no users");
    return s_user_n > 0;
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
        ws_hello();
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

static void card_translate_x(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0);
}

static void peek_anim_in(void)
{
    if (!s_card) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_card);
    lv_anim_set_values(&a, s_peek_dir > 0 ? 48 : -48, 0);
    lv_anim_set_duration(&a, PEEK_ANIM_MS);
    lv_anim_set_exec_cb(&a, card_translate_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void peek_anim_out_done(lv_anim_t *a)
{
    (void)a;
    ui_refresh_transport();
    peek_anim_in();
}

static void start_peek_anim(int dir)
{
    s_peek_dir = dir;
    if (!s_card) {
        ui_refresh_transport();
        return;
    }
    lv_anim_delete(s_card, card_translate_x);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_card);
    lv_anim_set_values(&a, 0, dir > 0 ? -48 : 48);
    lv_anim_set_duration(&a, PEEK_ANIM_MS);
    lv_anim_set_exec_cb(&a, card_translate_x);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, peek_anim_out_done);
    lv_anim_start(&a);
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

static void on_shift(lv_event_t *e)
{
    intptr_t dir = (intptr_t)lv_event_get_user_data(e);
    if (s_msg_n <= 0) {
        return;
    }
    stop_playback();
    int next = s_focus + (int)dir;
    if (next < 0 || next >= s_msg_n) {
        return;
    }
    msg_t *m = focus_msg();
    if (m) {
        save_position(m->seq, m->position_ms);
    }
    s_focus = next;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_msgs[s_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)http_json("PUT", "/v1/session/view", js, s_session_user, &b);
    request_chirp(784);
    start_peek_anim((int)dir);
    note_activity();
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
        request_repaint();
        request_chirp(880);
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
    if (s_bar_sync || !s_bar) {
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
    if (login_user(s_pick_id, s_entry)) {
        s_pin_fails = 0;
        s_pin_lock_until_us = 0;
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
    } else {
        if (strcmp(s_pin_fail_user, s_pick_id) != 0) {
            strncpy(s_pin_fail_user, s_pick_id, sizeof(s_pin_fail_user) - 1);
            s_pin_fails = 0;
        }
        s_pin_fails++;
        s_elen = 0;
        s_entry[0] = 0;
        refresh_dots();
        if (s_pin_fails >= PIN_TRIES) {
            s_pin_lock_until_us = now_us() + (int64_t)PIN_COOLDOWN_MS * 1000;
            set_status("ask Lynn", 0xE85A5A);
        } else {
            set_status("wrong pin", 0xE85A5A);
        }
    }
    note_activity();
}

static lv_obj_t *make_play_icon(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 24, 24);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *a = lv_obj_create(box);
    lv_obj_set_size(a, 4, 18);
    lv_obj_set_style_bg_color(a, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(a, 0, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_transform_angle(a, 900, 0);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(a, LV_ALIGN_CENTER, -4, 0);
    lv_obj_t *b = lv_obj_create(box);
    lv_obj_set_size(b, 4, 18);
    lv_obj_set_style_bg_color(b, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_transform_angle(b, 2700, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(b, LV_ALIGN_CENTER, 4, 0);
    lv_obj_center(box);
    return box;
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

static void paint_ribbons(lv_obj_t *scr)
{
    s_ribbon_top = lv_obj_create(scr);
    lv_obj_set_pos(s_ribbon_top, 0, 0);
    lv_obj_set_size(s_ribbon_top, 320, RIBBON_H);
    lv_obj_set_style_bg_color(s_ribbon_top, lv_color_hex(0x0C0E10), 0);
    lv_obj_set_style_border_width(s_ribbon_top, 0, 0);
    lv_obj_set_style_pad_all(s_ribbon_top, 0, 0);
    lv_obj_clear_flag(s_ribbon_top, LV_OBJ_FLAG_SCROLLABLE);

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

    s_back_hint = lv_label_create(s_ribbon_bot);
    lv_label_set_text(s_back_hint, "shoulder = back");
    lv_obj_set_style_text_color(s_back_hint, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_back_hint, LV_ALIGN_CENTER, 0, 0);

    s_toast = lv_label_create(s_ribbon_bot);
    lv_label_set_text(s_toast, "");
    lv_obj_set_style_text_color(s_toast, lv_color_hex(0xE8C040), 0);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, 0);
}

static void paint_peek_content(lv_obj_t *peek, const msg_t *m, bool older)
{
    if (!peek || !m) {
        return;
    }
    lv_obj_clean(peek);
    lv_obj_set_style_bg_color(peek, lv_color_hex(0x242C34), 0);
    lv_obj_set_style_radius(peek, 4, 0);
    lv_obj_set_style_pad_all(peek, 0, 0);
    if (!m->read) {
        lv_obj_set_style_border_width(peek, 2, 0);
        lv_obj_set_style_border_color(peek, lv_color_hex(0xE8C040), 0);
        if (older) {
            lv_obj_set_style_border_side(peek, LV_BORDER_SIDE_LEFT, 0);
        } else {
            lv_obj_set_style_border_side(peek, LV_BORDER_SIDE_RIGHT, 0);
        }
    } else {
        lv_obj_set_style_border_width(peek, 0, 0);
    }
    int idx = user_index_by_label(m->from_label);
    uint32_t hue = user_accent(idx);
    lv_obj_t *band = lv_obj_create(peek);
    lv_obj_set_size(band, PEEK_W - 4, CARD_PANE_H - 28);
    lv_obj_set_pos(band, 2, 2);
    lv_obj_set_style_bg_color(band, lv_color_hex(hue), 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_set_style_pad_all(band, 0, 0);
    lv_obj_clear_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    paint_user_portrait(band, idx, 32);
    lv_obj_t *lab = lv_label_create(peek);
    char name[12];
    peek_text(name, sizeof(name), m, "?");
    lv_label_set_text(lab, name);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_width(lab, PEEK_W - 6);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -4);
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

static void paint_roster(lv_obj_t *scr)
{
    (void)load_hangout();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "sign in");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    int y0 = s_user_n > 3 ? 26 : 36;
    int row_h = s_user_n > 3 ? 44 : 52;
    int row_step = s_user_n > 3 ? 46 : 58;
    int y = y0;
    for (int i = 0; i < s_user_n; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 4, 0);
        if (s_last_user[0] && strcmp(s_users[i].id, s_last_user) == 0) {
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(0xE8C040), 0);
        }
        paint_face(b, i, 4, 8);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, s_users[i].name);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 40, 0);
        lv_obj_add_event_cb(b, on_user_btn, LV_EVENT_CLICKED, s_users[i].id);
        y += row_step;
    }
    if (s_user_n <= 0) {
        lv_obj_t *hint = lv_label_create(scr);
        lv_label_set_text(hint, "no server\nrun make v1-server");
        lv_obj_set_style_text_color(hint, lv_color_hex(0xA8B0B8), 0);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, 24);
    }
    hook_scr(scr);
}

static void paint_pin(lv_obj_t *scr)
{
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
    int row_h = pick_rows > 3 ? 44 : 52;
    int row_step = pick_rows > 3 ? 46 : 58;
    int y = pick_rows > 3 ? 30 : 36;
    for (int i = 0; i < s_user_n; i++) {
        if (strcmp(s_users[i].id, s_session_user) == 0) {
            continue;
        }
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 4, 0);
        paint_face(b, i, 4, 8);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, s_users[i].name);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 40, 0);
        lv_obj_add_event_cb(b, on_pick_btn, LV_EVENT_CLICKED, s_users[i].id);
        y += row_step;
    }
    lv_obj_t *all = lv_button_create(scr);
    lv_obj_set_pos(all, 16, y);
    lv_obj_set_size(all, 288, row_h);
    lv_obj_set_style_pad_all(all, 4, 0);
    paint_asterisk_icon(all, 4, 8);
    lv_obj_t *at = lv_label_create(all);
    lv_label_set_text(at, "Everyone");
    lv_obj_align(at, LV_ALIGN_LEFT_MID, 40, 0);
    lv_obj_add_event_cb(all, on_pick_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "shoulder or 10s = cancel");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    hook_scr(scr);
}

static void paint_carousel(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    s_overlay = NULL;
    s_card = NULL;
    s_card_face_pane = NULL;
    s_card_play_pane = NULL;
    s_peek_left = NULL;
    s_peek_right = NULL;
    s_peek_left_lab = NULL;
    s_peek_right_lab = NULL;
    s_bar = NULL;
    s_play_btn = NULL;
    s_play_icon = NULL;
    s_pause_icon = NULL;
    s_vol_slider = NULL;
    s_ribbon_top = NULL;
    s_ribbon_bot = NULL;
    s_count_lab = NULL;
    s_back_hint = NULL;
    s_card_face_seq = -1;
    s_card_face_read = false;

    paint_ribbons(scr);
    s_carousel_ready_us = now_us();

    s_peek_left = lv_button_create(scr);
    lv_obj_set_size(s_peek_left, PEEK_W, CARD_H);
    lv_obj_set_pos(s_peek_left, 4, CARD_Y);
    lv_obj_add_event_cb(s_peek_left, on_shift, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

    s_peek_right = lv_button_create(scr);
    lv_obj_set_size(s_peek_right, PEEK_W, CARD_H);
    lv_obj_set_pos(s_peek_right, 320 - 4 - PEEK_W, CARD_Y);
    lv_obj_add_event_cb(s_peek_right, on_shift, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    s_card = lv_obj_create(scr);
    lv_obj_set_size(s_card, CARD_W, CARD_H);
    lv_obj_align(s_card, LV_ALIGN_TOP_MID, 0, CARD_Y);
    lv_obj_set_style_bg_color(s_card, lv_color_hex(0x1C2228), 0);
    lv_obj_set_style_border_width(s_card, 0, 0);
    lv_obj_set_style_pad_all(s_card, 0, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    s_card_face_pane = lv_obj_create(s_card);
    lv_obj_set_size(s_card_face_pane, CARD_PANE_W, CARD_PANE_H);
    lv_obj_set_pos(s_card_face_pane, 0, 0);
    lv_obj_set_style_border_width(s_card_face_pane, 0, 0);
    lv_obj_set_style_radius(s_card_face_pane, 0, 0);
    lv_obj_set_style_pad_all(s_card_face_pane, 0, 0);
    lv_obj_clear_flag(s_card_face_pane, LV_OBJ_FLAG_SCROLLABLE);

    s_card_play_pane = lv_obj_create(s_card);
    lv_obj_set_size(s_card_play_pane, CARD_PANE_W, CARD_PANE_H);
    lv_obj_set_pos(s_card_play_pane, CARD_PANE_W, 0);
    lv_obj_set_style_border_width(s_card_play_pane, 0, 0);
    lv_obj_set_style_radius(s_card_play_pane, 0, 0);
    lv_obj_set_style_pad_all(s_card_play_pane, 0, 0);
    lv_obj_clear_flag(s_card_play_pane, LV_OBJ_FLAG_SCROLLABLE);

    s_play_btn = lv_button_create(s_card_play_pane);
    lv_obj_set_size(s_play_btn, PLAY_HIT, PLAY_HIT);
    lv_obj_set_style_bg_opa(s_play_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_play_btn, 0, 0);
    lv_obj_set_style_border_width(s_play_btn, 0, 0);
    lv_obj_set_style_pad_all(s_play_btn, 0, 0);
    lv_obj_center(s_play_btn);
    lv_obj_add_event_cb(s_play_btn, on_play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disk = lv_obj_create(s_play_btn);
    lv_obj_set_size(disk, PLAY_DISK, PLAY_DISK);
    lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disk, lv_color_hex(0x3A4450), 0);
    lv_obj_set_style_border_width(disk, 0, 0);
    lv_obj_set_style_pad_all(disk, 0, 0);
    lv_obj_clear_flag(disk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(disk);

    s_play_icon = make_play_icon(s_play_btn);
    s_pause_icon = make_pause_icon(s_play_btn);
    lv_obj_add_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *track = lv_obj_create(s_card);
    lv_obj_set_size(track, CARD_W, CARD_TRACK_H);
    lv_obj_set_pos(track, 0, CARD_PANE_H);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x1C2228), 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_set_style_pad_hor(track, 8, 0);
    lv_obj_set_style_pad_ver(track, 5, 0);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    s_bar = lv_slider_create(track);
    lv_obj_set_width(s_bar, CARD_W - 16);
    lv_obj_set_height(s_bar, 12);
    lv_obj_center(s_bar);
    style_transport_slider(s_bar);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_RELEASED, NULL);

    ui_refresh_transport();
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
    if (s_back_hint) {
        lv_obj_clear_flag(s_back_hint, LV_OBJ_FLAG_HIDDEN);
    }

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

static void paint(void)
{
    s_status = NULL;
    s_dots = NULL;
    s_card = NULL;
    s_card_face_pane = NULL;
    s_card_play_pane = NULL;
    s_peek_left = NULL;
    s_peek_right = NULL;
    s_peek_left_lab = NULL;
    s_peek_right_lab = NULL;
    s_bar = NULL;
    s_play_btn = NULL;
    s_play_icon = NULL;
    s_pause_icon = NULL;
    s_vol_slider = NULL;
    s_overlay = NULL;
    s_toast = NULL;
    s_ribbon_top = NULL;
    s_ribbon_bot = NULL;
    s_count_lab = NULL;
    s_back_hint = NULL;
    s_settings_scroll = NULL;
    s_vol_val_lab = NULL;
    lv_obj_t *scr = lv_screen_active();
    switch (s_st) {
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
        if (s_chirp_hz) {
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
        if (!s_asleep && idle > (int64_t)SLEEP_MS * 1000) {
            s_asleep = true;
            s_dimmed = false;
            board_backlight_set(0);
        } else if (!s_dimmed && !s_asleep && s_activity_us > 0 &&
                   idle > (int64_t)DIM_MS * 1000) {
            s_dimmed = true;
            board_backlight_set(BRIGHT_DIM);
        }
        if (s_st == ST_ROSTER && s_user_n <= 0 &&
            (now_us() - s_roster_retry_us) > 5000000) {
            s_roster_retry_us = now_us();
            if (load_hangout()) {
                board_lvgl_lock(0);
                paint();
                board_lvgl_unlock();
            }
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
        if (s_repaint || s_playing) {
            board_lvgl_lock(0);
            if (s_repaint) {
                paint();
            } else if (s_st == ST_CAROUSEL) {
                ui_refresh_transport();
            }
            board_lvgl_unlock();
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

    board_status_set("wifi…");
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi failed\n/check secrets.h");
        ESP_LOGE(TAG, "wifi join failed");
        return;
    }
#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
    sntp_wait();
#endif

    s_buf = heap_caps_malloc(BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        s_buf = heap_caps_malloc(BUF_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_buf) {
        board_status_set("OOM playback buf");
        ESP_LOGE(TAG, "playback buffer alloc failed");
        return;
    }

    if (!load_hangout()) {
        board_status_set("no hangout\nrun make v1-server");
        ESP_LOGW(TAG, "GET /v1/hangout failed");
    } else {
        ESP_LOGI(TAG, "hangout %d users", s_user_n);
    }
    nvs_load_last();
    ws_start();

    s_work = xSemaphoreCreateBinary();
    xTaskCreate(ui_task, "ui", 12288, NULL, 5, NULL);

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
