/*
 * h18 — Playback screen for inbound audio messages.
 *
 * GET /demo/h18/message?i=N. The JSON is the message contract: sender,
 * sent_at, url, duration_ms, position_ms, read, plus index/count for the
 * catalog. Paint those, then stream the WAV at `url` with play/pause, a
 * progress bar, and a ROOMVOL slider.
 *
 * ROOMVOL is a stepped speaker control for a live room, not a 0–100
 * percentage. Desk note (2026-08-23): codec 75–100 is audible enough in
 * an active room with fans blowing and food cooking. Mute plus 78, 80,
 * … 100 (step 2) is the usable subset; the slider snaps to those
 * notches. Starts muted. ROOMVOL_SHOW_LEVEL paints the codec number so
 * you can check levels while exploring; leave it off for the product look.
 *
 * Volume 100 is a valid codec setting (esp_codec_dev maps 100 → 0 dB, not
 * extra digital boost). Espressif's BOX-3 BSP playback example uses 50;
 * this tree's working playback demos use 50–70. Nothing in Espressif docs
 * says 100 is past the speaker. The kit is an 8 Ω / 1 W cone + PA on
 * GPIO46 (HARDWARE.md: desk-volume, not a room). Smooth playback that
 * still "breaks up" at 100 is more likely analog/mechanical clipping than
 * an illegal register. Further testing before capping ROOMVOL_MAX: 90 vs
 * 100, melody vs voice, whether a 1 kHz tone at 0.5 FS still breaks up.
 *
 * Keep the speaker stream open across clips. Close/reopen was leaving
 * GPIO46 PA off, so clip 1 played and clips 2+ painted with no sound.
 *
 * Boot (GPIO0 / config) loads the next catalog entry and wraps.
 * Host (leave running):
 *   python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080
 *
 * -- PASS h18 after the JSON is on screen. Play/pause is the listen check.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_codec_dev.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "http_bearer.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h18";

#define SAMPLE_RATE 16000
#define BYTES_PER_MS 32
#define CHUNK 640
#define JSON_CAP 1024

/*
 * ROOMVOL_* — room-usable stepped volume. Common prefix for this control.
 * Codec 75–100 is the audible band on the desk with fans + cooking.
 */
#ifndef ROOMVOL_SHOW_LEVEL
#define ROOMVOL_SHOW_LEVEL 1 /* 1 = paint codec number (dev / level checks) */
#endif
#ifndef ROOMVOL_FIRST_ON
#define ROOMVOL_FIRST_ON 78
#endif
#ifndef ROOMVOL_STEP
#define ROOMVOL_STEP 2
#endif
#ifndef ROOMVOL_MAX
#define ROOMVOL_MAX 100
#endif
#define ROOMVOL_ON_COUNT (((ROOMVOL_MAX - ROOMVOL_FIRST_ON) / ROOMVOL_STEP) + 1)
#define ROOMVOL_SLIDER_MAX ROOMVOL_ON_COUNT /* notches 0=mute … ON_COUNT=100 */

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    char id[24];
    char sender[32];
    char sent_at[40];
    char url[256];
    int duration_ms;
    int position_ms;
    bool read;
} audio_msg_t;

static audio_msg_t s_msg;
static esp_codec_dev_handle_t s_spk;
static lv_obj_t *s_sender;
static lv_obj_t *s_when;
static lv_obj_t *s_meta;
static lv_obj_t *s_bar;
static lv_obj_t *s_clock;
static lv_obj_t *s_play_lab;
static lv_obj_t *s_vol_lab;
static int s_index;
static int s_count = 1;
static volatile int s_notch;
static volatile int s_volume;
static volatile bool s_playing;
static volatile bool s_want_play;
static volatile bool s_abort;
static volatile bool s_hold_play;
static volatile bool s_next_msg;
static bool s_spk_open;
static bool s_passed;

static int roomvol_codec(int notch)
{
    if (notch <= 0) {
        return 0;
    }
    int v = ROOMVOL_FIRST_ON + (notch - 1) * ROOMVOL_STEP;
    if (v > ROOMVOL_MAX) {
        v = ROOMVOL_MAX;
    }
    return v;
}

static void fmt_clock(char *out, size_t cap, int pos_ms, int dur_ms)
{
    int p = pos_ms / 1000;
    int d = dur_ms / 1000;
    if (p < 0) {
        p = 0;
    }
    if (d < 0) {
        d = 0;
    }
    snprintf(out, cap, "%d:%02d / %d:%02d", p / 60, p % 60, d / 60, d % 60);
}

#if ROOMVOL_SHOW_LEVEL
static void fmt_level(char *out, size_t cap, int codec)
{
    if (codec <= 0) {
        snprintf(out, cap, "mute");
        return;
    }
    snprintf(out, cap, "%d", codec);
}
#endif

static int pcm_peak(const uint8_t *p, int n)
{
    int peak = 0;
    const int16_t *s = (const int16_t *)p;
    int count = n / 2;
    for (int i = 0; i < count; i++) {
        int v = s[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
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

static void apply_notch(int notch)
{
    if (notch < 0) {
        notch = 0;
    }
    if (notch > ROOMVOL_SLIDER_MAX) {
        notch = ROOMVOL_SLIDER_MAX;
    }
    s_notch = notch;
    apply_volume(roomvol_codec(notch));
}

static bool speaker_ensure_open(void)
{
    if (!s_spk_open) {
        if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
            ESP_LOGE(TAG, "speaker open failed");
            return false;
        }
        s_spk_open = true;
        /* PA GPIO46 needs a beat after enable; first I2S write after open can be a no-op. */
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    apply_volume((int)s_volume);
    uint8_t z[CHUNK];
    memset(z, 0, sizeof(z));
    (void)esp_codec_dev_write(s_spk, z, sizeof(z));
    return true;
}

static void ui_refresh(void)
{
    if (!board_lvgl_lock(80)) {
        return;
    }
    if (s_bar && s_msg.duration_ms > 0) {
        lv_bar_set_range(s_bar, 0, s_msg.duration_ms);
        lv_bar_set_value(s_bar, s_msg.position_ms, LV_ANIM_OFF);
    }
    if (s_clock) {
        char line[32];
        fmt_clock(line, sizeof(line), s_msg.position_ms, s_msg.duration_ms);
        lv_label_set_text(s_clock, line);
    }
    if (s_play_lab) {
        lv_label_set_text(s_play_lab, s_playing ? "Pause" : "Play");
    }
    if (s_meta) {
        lv_label_set_text(s_meta, s_msg.read ? "read" : "unread");
        lv_obj_set_style_text_color(s_meta, lv_color_hex(s_msg.read ? 0xA8B0B8 : 0xE8C040), 0);
    }
    board_lvgl_unlock();
}

static void on_play(lv_event_t *e)
{
    (void)e;
    if (s_playing) {
        s_abort = true;
        s_want_play = false;
        return;
    }
    s_want_play = true;
}

static void on_volume(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    apply_notch((int)lv_slider_get_value(slider));
#if ROOMVOL_SHOW_LEVEL
    if (s_vol_lab) {
        char line[12];
        fmt_level(line, sizeof(line), (int)s_volume);
        lv_label_set_text(s_vol_lab, line);
    }
#else
    (void)s_vol_lab;
#endif
}

static void on_boot(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_next_msg = true;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *title, int x, int y, int w, int h)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, title);
    lv_obj_center(lab);
    return btn;
}

static void paint_player(void)
{
    if (!board_lvgl_lock(400)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *badge = lv_label_create(scr);
    s_meta = badge;
    lv_label_set_text(badge, s_msg.read ? "read" : "unread");
    lv_obj_set_style_text_color(badge, lv_color_hex(s_msg.read ? 0xA8B0B8 : 0xE8C040), 0);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 10);

    char idxline[24];
    snprintf(idxline, sizeof(idxline), "%d/%d", s_index + 1, s_count > 0 ? s_count : 1);
    lv_obj_t *idx = lv_label_create(scr);
    lv_label_set_text(idx, idxline);
    lv_obj_set_style_text_color(idx, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(idx, LV_ALIGN_TOP_RIGHT, -12, 28);

    s_sender = lv_label_create(scr);
    lv_label_set_text(s_sender, s_msg.sender[0] ? s_msg.sender : "(sender)");
    lv_obj_set_style_text_color(s_sender, lv_color_hex(0xE8F0E8), 0);
#if defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(s_sender, &lv_font_montserrat_22, 0);
#endif
    lv_obj_align(s_sender, LV_ALIGN_TOP_LEFT, 16, 16);

    s_when = lv_label_create(scr);
    lv_label_set_text(s_when, s_msg.sent_at[0] ? s_msg.sent_at : "");
    lv_obj_set_style_text_color(s_when, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_when, LV_ALIGN_TOP_LEFT, 16, 48);

    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, 288, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 72);
    lv_bar_set_range(s_bar, 0, s_msg.duration_ms > 0 ? s_msg.duration_ms : 1);
    lv_bar_set_value(s_bar, s_msg.position_ms, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_clock = lv_label_create(scr);
    char line[32];
    fmt_clock(line, sizeof(line), s_msg.position_ms, s_msg.duration_ms);
    lv_label_set_text(s_clock, line);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 90);

    lv_obj_t *btn = make_btn(scr, "Play", 100, 118, 120, 40);
    s_play_lab = lv_obj_get_child(btn, 0);
    lv_obj_add_event_cb(btn, on_play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vol_tag = lv_label_create(scr);
    lv_label_set_text(vol_tag, "vol");
    lv_obj_set_style_text_color(vol_tag, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(vol_tag, 16, 176);

    s_vol_lab = NULL;
#if ROOMVOL_SHOW_LEVEL
    s_vol_lab = lv_label_create(scr);
    fmt_level(line, sizeof(line), (int)s_volume);
    lv_label_set_text(s_vol_lab, line);
    lv_obj_set_style_text_color(s_vol_lab, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(s_vol_lab, LV_ALIGN_TOP_RIGHT, -16, 176);
#endif

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 232, 18);
    lv_obj_set_pos(slider, 48, 198);
    lv_slider_set_range(slider, 0, ROOMVOL_SLIDER_MAX);
    lv_slider_set_value(slider, s_notch, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xE8F0E8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, NULL);

    board_lvgl_unlock();
}

static bool parse_message(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    memset(&s_msg, 0, sizeof(s_msg));
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    const cJSON *sender = cJSON_GetObjectItem(root, "sender");
    if (!cJSON_IsString(sender)) {
        sender = cJSON_GetObjectItem(root, "from");
    }
    const cJSON *sent = cJSON_GetObjectItem(root, "sent_at");
    const cJSON *url = cJSON_GetObjectItem(root, "url");
    const cJSON *dur = cJSON_GetObjectItem(root, "duration_ms");
    if (!cJSON_IsNumber(dur)) {
        dur = cJSON_GetObjectItem(root, "length_ms");
    }
    const cJSON *pos = cJSON_GetObjectItem(root, "position_ms");
    const cJSON *read = cJSON_GetObjectItem(root, "read");
    const cJSON *index = cJSON_GetObjectItem(root, "index");
    const cJSON *count = cJSON_GetObjectItem(root, "count");
    if (cJSON_IsString(id)) {
        strlcpy(s_msg.id, id->valuestring, sizeof(s_msg.id));
    }
    if (cJSON_IsString(sender)) {
        strlcpy(s_msg.sender, sender->valuestring, sizeof(s_msg.sender));
    }
    if (cJSON_IsString(sent)) {
        strlcpy(s_msg.sent_at, sent->valuestring, sizeof(s_msg.sent_at));
    }
    if (cJSON_IsString(url)) {
        strlcpy(s_msg.url, url->valuestring, sizeof(s_msg.url));
    }
    if (cJSON_IsNumber(dur)) {
        s_msg.duration_ms = dur->valueint;
    }
    if (cJSON_IsNumber(pos)) {
        s_msg.position_ms = pos->valueint;
    }
    s_msg.read = cJSON_IsTrue(read);
    if (cJSON_IsNumber(index)) {
        s_index = index->valueint;
    }
    if (cJSON_IsNumber(count) && count->valueint > 0) {
        s_count = count->valueint;
    }
    cJSON_Delete(root);
    return s_msg.url[0] != 0 && s_msg.duration_ms > 0;
}

static int wav_data_off(const uint8_t *p, int n)
{
    if (n < 12 || memcmp(p, "RIFF", 4) != 0) {
        return 0;
    }
    int i = 12;
    while (i + 8 <= n) {
        uint32_t sz = 0;
        memcpy(&sz, p + i + 4, 4);
        if (memcmp(p + i, "data", 4) == 0) {
            return i + 8;
        }
        i += 8 + (int)sz;
        if (sz & 1u) {
            i++;
        }
    }
    return 44;
}

static bool stream_from_url(void)
{
    if (s_msg.url[0] == 0) {
        return false;
    }
    char url[320];
    if (strncmp(s_msg.url, "http://", 7) == 0 || strncmp(s_msg.url, "https://", 8) == 0) {
        strlcpy(url, s_msg.url, sizeof(url));
    } else {
        snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT,
                 s_msg.url[0] == '/' ? s_msg.url : "/");
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 60000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return false;
    }
#ifdef DEMO_DEVICE_TOKEN
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
#endif
    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }
    int clen = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "GET %s status=%d clen=%d", url, status, clen);
    if (status != 200) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    uint8_t head[512];
    int got = 0;
    while (got < (int)sizeof(head)) {
        int n = esp_http_client_read(client, (char *)head + got, (int)sizeof(head) - got);
        if (n <= 0) {
            break;
        }
        got += n;
    }
    int data_off = wav_data_off(head, got);
    int pcm_have = got - data_off;
    if (pcm_have < 0) {
        pcm_have = 0;
        data_off = got;
    }
    ESP_LOGI(TAG, "wav got=%d data_off=%d", got, data_off);

    int skip = s_msg.position_ms * BYTES_PER_MS;
    if (skip < 0) {
        skip = 0;
    }
    const uint8_t *pcm = head + data_off;
    while (skip > 0 && pcm_have > 0) {
        int n = skip < pcm_have ? skip : pcm_have;
        pcm += n;
        pcm_have -= n;
        skip -= n;
    }
    uint8_t dump[512];
    while (skip > 0) {
        int n = skip > (int)sizeof(dump) ? (int)sizeof(dump) : skip;
        int r = esp_http_client_read(client, (char *)dump, n);
        if (r <= 0) {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        skip -= r;
    }

    if (!speaker_ensure_open()) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    s_playing = true;
    s_msg.read = true;
    ui_refresh();

    bool ok = true;
    bool logged_peak = false;
    while (!s_abort) {
        uint8_t chunk[CHUNK];
        int n = 0;
        if (pcm_have > 0) {
            n = pcm_have > CHUNK ? CHUNK : pcm_have;
            memcpy(chunk, pcm, n);
            pcm += n;
            pcm_have -= n;
        } else {
            n = esp_http_client_read(client, (char *)chunk, CHUNK);
            if (n <= 0) {
                break;
            }
        }
        if (n & 1) {
            n--;
        }
        if (n <= 0) {
            continue;
        }
        if (!logged_peak) {
            ESP_LOGI(TAG, "pcm peak=%d", pcm_peak(chunk, n));
            logged_peak = true;
        }
        if (esp_codec_dev_write(s_spk, chunk, n) != ESP_CODEC_DEV_OK) {
            ok = false;
            s_spk_open = false;
            (void)esp_codec_dev_close(s_spk);
            break;
        }
        s_msg.position_ms += (n * 1000) / (SAMPLE_RATE * 2);
        if (s_msg.duration_ms > 0 && s_msg.position_ms > s_msg.duration_ms) {
            s_msg.position_ms = s_msg.duration_ms;
        }
        ui_refresh();
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    s_playing = false;
    if (!s_abort && s_msg.duration_ms > 0) {
        s_msg.position_ms = s_msg.duration_ms;
    }
    s_abort = false;
    ui_refresh();
    return ok;
}

static void play_task(void *arg)
{
    (void)arg;
    while (1) {
        if (s_hold_play || !s_want_play) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        s_want_play = false;
        if (s_msg.duration_ms > 0 && s_msg.position_ms >= s_msg.duration_ms) {
            s_msg.position_ms = 0;
        }
        ESP_LOGI(TAG, "play %s at %d ms", s_msg.url, s_msg.position_ms);
        if (!stream_from_url()) {
            ESP_LOGW(TAG, "stream failed");
        }
    }
}

static bool load_message(int index)
{
    s_hold_play = true;
    s_want_play = false;
    s_abort = true;
    int waits = 0;
    while (s_playing && waits < 100) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waits++;
    }
    s_abort = false;

    char path[48];
    snprintf(path, sizeof(path), "/demo/h18/message?i=%d", index);
    uint8_t json[JSON_CAP];
    http_buf_t body = { .buf = json, .cap = sizeof(json) };
    int st = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET", path, DEMO_DEVICE_TOKEN,
                            NULL, &body, 8000);
    ESP_LOGI(TAG, "message i=%d status=%d body=%.*s", index, st, body.len, (char *)json);
    bool ok = st == 200 && body.len >= 8 && parse_message((char *)json);
    s_hold_play = false;
    return ok;
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h18", "display");
        return;
    }
    board_status_set("h18 wifi…");
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h18", "wifi");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("h18", "ES8311");
        return;
    }
    apply_notch(0);

    board_status_set("GET /demo/h18/message");
    if (!load_message(0)) {
        board_status_set("message fetch failed");
        demo_fail("h18", "message");
        return;
    }

    paint_player();
    if (!s_passed) {
        s_passed = true;
        demo_pass("h18");
    }
    ESP_LOGI(TAG, "sender=%s url=%s dur=%d pos=%d %d/%d roomvol mute..%d step %d", s_msg.sender,
             s_msg.url, s_msg.duration_ms, s_msg.position_ms, s_index + 1, s_count,
             ROOMVOL_MAX, ROOMVOL_STEP);

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK && btns[BSP_BUTTON_CONFIG]) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot, NULL);
    } else {
        ESP_LOGW(TAG, "boot button not registered");
    }

    xTaskCreate(play_task, "h18_play", 12288, NULL, 5, NULL);
    while (1) {
        if (s_next_msg) {
            s_next_msg = false;
            int next = s_count > 0 ? (s_index + 1) % s_count : 0;
            ESP_LOGI(TAG, "boot → message %d", next);
            if (load_message(next)) {
                paint_player();
            } else {
                ESP_LOGW(TAG, "next message failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
