/*
 * x01 — Product-shaped BOX-3 shell: locked / PIN / inbox / record / hangout.
 * Wires h02–h12. One FAMILY_DEMO, not a framework.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "http_bearer.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEMO_PIN
#define DEMO_PIN "1234"
#endif

static const char *TAG = "x01";
#define SAMPLE_RATE 16000
#define CHUNK       640
#define PCM_CAP     (SAMPLE_RATE * 2 * 10)
#define BUF_CAP     (320 * 1024)
#define PREVIEW_N   (320 * 240 * 2)
#define ENTRY_MAX   8
#define IDLE_MS     30000
#define TAP_MS      400
#define EV_ACT      BIT0
#define EV_WS       BIT1

typedef enum { ST_LOCKED, ST_PIN, ST_INBOX, ST_RECORD, ST_HANGOUT } state_t;
typedef enum { HG_NONE, HG_CALLING, HG_RINGING, HG_LIVE } hang_t;
typedef enum {
    ACT_NONE, ACT_UNLOCK, ACT_LOCK, ACT_PLAY, ACT_INVITE, ACT_ACCEPT, ACT_HANGUP, ACT_PIN_OK
} act_t;

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static esp_websocket_client_handle_t s_ws;
static esp_codec_dev_handle_t s_mic, s_spk;
static uint8_t *s_buf;
static EventGroupHandle_t s_ev;
static state_t s_st = ST_LOCKED, s_after_rec = ST_LOCKED;
static hang_t s_hg;
static volatile bool s_held, s_spk_open, s_hello, s_passed, s_peer_on;
static volatile bool s_mute_arm, s_need_fetch, s_pending_ring;
static volatile int s_unread, s_tx, s_rx;
static volatile act_t s_act;
static int64_t s_idle_us, s_mute_us;
static char s_floor[20], s_entry[ENTRY_MAX + 1], s_kind[12], s_text[160];
static size_t s_elen;
static int s_seq = -1;
static bool s_have_preview;
static lv_image_dsc_t s_img;
static lv_obj_t *s_count, *s_peer, *s_live, *s_dots, *s_note;

static int64_t now_us(void) { return esp_timer_get_time(); }
static void bump_idle(void) { s_idle_us = now_us(); }
static bool is_image(void)
{
    return strcmp(s_kind, "image") == 0 || strcmp(s_kind, "photo") == 0;
}

static void mark_pass(void)
{
    if (!s_passed && s_hello) {
        s_passed = true;
        demo_pass("x01");
    }
}

static void send_json(const char *json)
{
    if (s_ws && esp_websocket_client_is_connected(s_ws)) {
        esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(1000));
    }
}

static void post_act(act_t a)
{
    s_act = a;
    if (s_ev) {
        xEventGroupSetBits(s_ev, EV_ACT);
    }
}

static void wake_ws(void)
{
    if (s_ev) {
        xEventGroupSetBits(s_ev, EV_WS);
    }
}

static int http(const char *method, const char *path, const char *json, http_buf_t *body, int timeout_ms)
{
    return http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, method, path,
                          DEMO_DEVICE_TOKEN, json, body, timeout_ms);
}

static void put_playhead(int seq)
{
    char js[40];
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    snprintf(js, sizeof(js), "{\"seq\":%d}", seq);
    (void)http("PUT", "/v1/playhead", js, &b, 8000);
}

static void pull_me(void)
{
    uint8_t tmp[256];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    if (http("GET", "/v1/me", NULL, &b, 8000) != 200) {
        return;
    }
    cJSON *j = cJSON_Parse((char *)tmp);
    cJSON *u = j ? cJSON_GetObjectItem(j, "unread") : NULL;
    cJSON *on = j ? cJSON_GetObjectItem(j, "peer_online") : NULL;
    if (cJSON_IsNumber(u)) {
        s_unread = u->valueint;
    }
    if (cJSON_IsTrue(on) || (cJSON_IsNumber(on) && on->valueint)) {
        s_peer_on = true;
    } else if (on) {
        s_peer_on = false;
    }
    cJSON_Delete(j);
}

static void wav_header(uint8_t *p, uint32_t n)
{
    uint32_t riff = 36 + n, fmt = 16, rate = SAMPLE_RATE, br = rate * 2;
    uint16_t audio = 1, ch = 1, bps = 16, block = 2;
    memcpy(p, "RIFF", 4);
    memcpy(p + 4, &riff, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    memcpy(p + 16, &fmt, 4);
    memcpy(p + 20, &audio, 2);
    memcpy(p + 22, &ch, 2);
    memcpy(p + 24, &rate, 4);
    memcpy(p + 28, &br, 4);
    memcpy(p + 32, &block, 2);
    memcpy(p + 34, &bps, 2);
    memcpy(p + 36, "data", 4);
    memcpy(p + 40, &n, 4);
}

static int post_wav(int wav_len)
{
    char url[128], pre[256], post[64], auth[96], ctype[80];
    static const char *bnd = "----FamilyLinkBound";
    snprintf(url, sizeof(url), "http://%s:%d/v1/messages", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    int pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; filename=\"clip.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n",
                           bnd, bnd);
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 15000 };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "Content-Type", ctype);
    if (esp_http_client_open(c, pre_len + wav_len + post_len) != ESP_OK) {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_http_client_write(c, pre, pre_len);
    esp_http_client_write(c, (const char *)s_buf, wav_len);
    esp_http_client_write(c, post, post_len);
    (void)esp_http_client_fetch_headers(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return st;
}

static bool play_pcm(const uint8_t *p, int n)
{
    if (s_spk_open) {
        (void)esp_codec_dev_close(s_spk);
        s_spk_open = false;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 70);
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        return false;
    }
    while (n > 0) {
        int chunk = n > 2048 ? 2048 : n;
        if (esp_codec_dev_write(s_spk, (void *)p, chunk) != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(s_spk);
            return false;
        }
        p += chunk;
        n -= chunk;
    }
    (void)esp_codec_dev_close(s_spk);
    return true;
}

static size_t record_while_held(void)
{
    size_t n = 0;
    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
    while (s_held && n + CHUNK <= PCM_CAP && n + 44 + CHUNK <= BUF_CAP) {
        if (esp_codec_dev_read(s_mic, s_buf + 44 + n, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        n += CHUNK;
    }
    (void)esp_codec_dev_close(s_mic);
    return n;
}

static void parse_list(char *js)
{
    s_seq = -1;
    s_kind[0] = 0;
    s_text[0] = 0;
    s_have_preview = false;
    cJSON *root = cJSON_Parse(js);
    if (!root) {
        return;
    }
    cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "messages");
    int len = cJSON_GetArraySize(arr);
    for (int i = 0; i < len; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *seq = cJSON_GetObjectItem(it, "seq");
        cJSON *kind = cJSON_GetObjectItem(it, "kind");
        cJSON *text = cJSON_GetObjectItem(it, "text");
        if (!cJSON_IsNumber(seq) || !cJSON_IsString(kind) || seq->valueint < s_seq) {
            continue;
        }
        s_seq = seq->valueint;
        strncpy(s_kind, kind->valuestring, sizeof(s_kind) - 1);
        s_kind[sizeof(s_kind) - 1] = 0;
        s_text[0] = 0;
        if (cJSON_IsString(text) && text->valuestring) {
            strncpy(s_text, text->valuestring, sizeof(s_text) - 1);
            s_text[sizeof(s_text) - 1] = 0;
        }
    }
    if (s_unread < len) {
        s_unread = len;
    }
    cJSON_Delete(root);
}

static void fetch_inbox(void)
{
    uint8_t json[3072];
    http_buf_t b = { .buf = json, .cap = sizeof(json) };
    if (http("GET", "/v1/messages", NULL, &b, 8000) == 200) {
        parse_list((char *)json);
    }
}

static void fetch_preview(void)
{
    char path[64];
    http_buf_t b = { .buf = s_buf, .cap = BUF_CAP };
    s_have_preview = false;
    if (s_seq < 0) {
        return;
    }
    snprintf(path, sizeof(path), "/v1/messages/%d/preview", s_seq);
    if (http("GET", path, NULL, &b, 20000) != 200 || b.len != PREVIEW_N) {
        return;
    }
    memset(&s_img, 0, sizeof(s_img));
    s_img.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_img.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img.header.w = 320;
    s_img.header.h = 240;
    s_img.header.stride = 640;
    s_img.data_size = PREVIEW_N;
    s_img.data = s_buf;
    s_have_preview = true;
}

static void ack(int seq)
{
    put_playhead(seq);
    if (s_unread > 0) {
        s_unread--;
    }
}

static void play_latest(void)
{
    char path[64];
    http_buf_t b = { .buf = s_buf, .cap = BUF_CAP };
    if (s_seq < 0 || strcmp(s_kind, "audio") != 0) {
        return;
    }
    snprintf(path, sizeof(path), "/v1/messages/%d/blob", s_seq);
    if (http("GET", path, NULL, &b, 15000) != 200 || b.len < 64) {
        return;
    }
    const uint8_t *pcm = s_buf;
    int n = b.len;
    if (n > 44 && memcmp(s_buf, "RIFF", 4) == 0) {
        pcm = s_buf + 44;
        n -= 44;
    }
    (void)play_pcm(pcm, n);
    ack(s_seq);
}

static lv_obj_t *scr_bg(uint32_t hex)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    s_count = s_peer = s_live = s_dots = s_note = NULL;
    return scr;
}

static lv_obj_t *shape(lv_obj_t *p, int w, int h, int r, uint32_t hex)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static lv_obj_t *lab(lv_obj_t *p, const char *t, uint32_t hex)
{
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, t);
    lv_obj_set_style_text_color(o, lv_color_hex(hex), 0);
    return o;
}

static void on_act_btn(lv_event_t *e)
{
    post_act((act_t)(intptr_t)lv_event_get_user_data(e));
}

static void btn(lv_obj_t *p, const char *t, int x, int y, int w, int h, act_t a)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_t *txt = lv_label_create(b);
    lv_label_set_text(txt, t);
    lv_obj_center(txt);
    lv_obj_add_event_cb(b, on_act_btn, LV_EVENT_CLICKED, (void *)(intptr_t)a);
}

static void paint_face(lv_obj_t *scr, int x, int y, int s)
{
    lv_obj_t *head = shape(scr, s, s, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(head, x, y);
    int e = s / 5;
    lv_obj_set_pos(shape(head, e, e, LV_RADIUS_CIRCLE, 0xFFF6E8), s / 5, s / 3);
    lv_obj_set_pos(shape(head, e, e, LV_RADIUS_CIRCLE, 0xFFF6E8), s / 2, s / 3);
    lv_obj_set_pos(shape(head, s / 3, s / 10, 6, 0xC0453C), s / 3, (s * 2) / 3);
}

static void refresh_dots(void)
{
    char d[ENTRY_MAX + 1];
    memset(d, '*', s_elen);
    d[s_elen] = 0;
    if (s_dots) {
        lv_label_set_text(s_dots, s_elen ? d : " ");
    }
}

static void on_key(lv_event_t *e)
{
    const char *key = lv_event_get_user_data(e);
    if (s_st != ST_PIN || !key) {
        return;
    }
    bump_idle();
    if (strcmp(key, "Clear") == 0) {
        s_elen = 0;
        s_entry[0] = 0;
        if (s_note) {
            lv_label_set_text(s_note, "locked");
        }
        refresh_dots();
        return;
    }
    if (s_elen >= ENTRY_MAX) {
        return;
    }
    s_entry[s_elen++] = key[0];
    s_entry[s_elen] = 0;
    refresh_dots();
    if (s_elen < strlen(DEMO_PIN)) {
        return;
    }
    if (strcmp(s_entry, DEMO_PIN) == 0) {
        post_act(ACT_PIN_OK);
        return;
    }
    s_elen = 0;
    s_entry[0] = 0;
    if (s_note) {
        lv_label_set_text(s_note, "wrong");
        lv_obj_set_style_text_color(s_note, lv_color_hex(0xff5555), 0);
    }
    refresh_dots();
}

static void make_key(lv_obj_t *p, const char *label, int x, int y, int w, int h)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_t *txt = lv_label_create(b);
    lv_label_set_text(txt, label);
    lv_obj_center(txt);
    lv_obj_add_event_cb(b, on_key, LV_EVENT_CLICKED, (void *)label);
}

static void paint(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_st == ST_LOCKED) {
        lv_obj_t *scr = scr_bg(0x143044);
        char n[24];
        board_backlight_set(40);
        paint_face(scr, 110, 8, 88);
        snprintf(n, sizeof(n), "%d new", (int)s_unread);
        s_count = lab(scr, n, 0xFFFFFF);
        lv_obj_align(s_count, LV_ALIGN_CENTER, 0, 28);
        s_peer = lab(scr, s_peer_on ? "peer on" : "peer off", 0xA8C0C8);
        lv_obj_align(s_peer, LV_ALIGN_CENTER, 0, 52);
        btn(scr, "Unlock", 16, 188, 140, 40, ACT_UNLOCK);
        btn(scr, "Invite", 164, 188, 140, 40, ACT_INVITE);
    } else if (s_st == ST_PIN) {
        lv_obj_t *scr = scr_bg(0x1a1a1a);
        static const char *const digits[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
        const int ox = 8, oy = 52, gap = 6, bw = 98, bh = 42;
        board_backlight_set(80);
        s_elen = 0;
        s_entry[0] = 0;
        s_note = lab(scr, "locked", 0xcccccc);
        lv_obj_align(s_note, LV_ALIGN_TOP_MID, 0, 6);
        s_dots = lab(scr, " ", 0xFFFFFF);
        lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 28);
        for (int i = 0; i < 9; i++) {
            make_key(scr, digits[i], ox + (i % 3) * (bw + gap), oy + (i / 3) * (bh + gap), bw, bh);
        }
        make_key(scr, "Clear", ox, oy + 3 * (bh + gap), bw * 2 + gap, bh);
        make_key(scr, "0", ox + 2 * (bw + gap), oy + 3 * (bh + gap), bw, bh);
    } else if (s_st == ST_INBOX) {
        lv_obj_t *scr = scr_bg(0x101418);
        board_backlight_set(80);
        if (s_have_preview) {
            lv_obj_t *img = lv_image_create(scr);
            lv_image_set_src(img, &s_img);
            lv_obj_center(img);
        }
        btn(scr, "Lock", 8, 6, 72, 32, ACT_LOCK);
        btn(scr, "Invite", 240, 6, 72, 32, ACT_INVITE);
        if (!s_have_preview) {
            s_note = lab(scr, s_seq < 0 ? "inbox empty" :
                         strcmp(s_kind, "text") == 0 ? (s_text[0] ? s_text : "(text)") :
                         strcmp(s_kind, "audio") == 0 ? "audio  Play or mute tap" :
                         is_image() ? "photo" : s_kind, 0xE8F0E8);
            lv_obj_set_width(s_note, 300);
            lv_label_set_long_mode(s_note, LV_LABEL_LONG_WRAP);
            lv_obj_align(s_note, LV_ALIGN_TOP_MID, 0, 48);
            if (strcmp(s_kind, "audio") == 0) {
                btn(scr, "Play", 110, 120, 100, 40, ACT_PLAY);
            }
        }
    } else if (s_st == ST_RECORD) {
        lv_obj_t *scr = scr_bg(0x143044);
        board_backlight_set(80);
        paint_face(scr, 100, 28, 120);
        s_note = lab(scr, "listen", 0xE8F0E8);
        lv_obj_align(s_note, LV_ALIGN_BOTTOM_MID, 0, -16);
    } else {
        lv_obj_t *scr = scr_bg(0x102018);
        const char *t = s_hg == HG_RINGING ? "incoming" : s_hg == HG_CALLING ? "calling…" : "LIVE";
        board_backlight_set(80);
        s_live = lab(scr, t, 0xE8F0E8);
        lv_obj_align(s_live, LV_ALIGN_CENTER, 0, -20);
        if (s_hg == HG_RINGING) {
            btn(scr, "Accept", 16, 180, 140, 44, ACT_ACCEPT);
            btn(scr, "Hangup", 164, 180, 140, 44, ACT_HANGUP);
        } else {
            btn(scr, "Hangup", 90, 180, 140, 44, ACT_HANGUP);
        }
    }
    board_lvgl_unlock();
}

static void goto_st(state_t st)
{
    s_st = st;
    bump_idle();
    paint();
}

static void hangup_local(void)
{
    if (s_spk_open) {
        (void)esp_codec_dev_close(s_spk);
        s_spk_open = false;
    }
    s_hg = HG_NONE;
    s_tx = s_rx = 0;
    s_floor[0] = 0;
    goto_st(ST_LOCKED);
}

static void on_text(const char *s, int n)
{
    char tmp[256];
    if (n >= (int)sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    ESP_LOGI(TAG, "ws %s", tmp);
    cJSON *j = cJSON_Parse(tmp);
    if (!j) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(j, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (strcmp(t, "hello_ok") == 0) {
        s_hello = true;
        printf("x01 shell live\n");
        fflush(stdout);
        wake_ws();
    } else if (strcmp(t, "inbox") == 0) {
        s_unread++;
        s_need_fetch = (s_st == ST_INBOX);
        wake_ws();
    } else if (strcmp(t, "presence") == 0) {
        cJSON *on = cJSON_GetObjectItem(j, "online");
        if (cJSON_IsBool(on) || cJSON_IsNumber(on)) {
            s_peer_on = cJSON_IsTrue(on) || (cJSON_IsNumber(on) && on->valueint);
        }
        wake_ws();
    } else if (strcmp(t, "ring") == 0) {
        s_pending_ring = true;
        wake_ws();
    } else if (strcmp(t, "session_start") == 0) {
        s_hg = HG_LIVE;
        mark_pass();
        if (s_spk && !s_spk_open && esp_codec_dev_open(s_spk, &s_fs) == ESP_OK) {
            (void)esp_codec_dev_set_out_vol(s_spk, 70);
            s_spk_open = true;
        }
        if (s_st != ST_HANGOUT) {
            s_st = ST_HANGOUT;
        }
        wake_ws();
    } else if (strcmp(t, "floor") == 0) {
        cJSON *h = cJSON_GetObjectItem(j, "holder");
        if (cJSON_IsString(h) && h->valuestring) {
            strncpy(s_floor, h->valuestring, sizeof(s_floor) - 1);
            s_floor[sizeof(s_floor) - 1] = 0;
        } else {
            s_floor[0] = 0;
        }
    } else if (strcmp(t, "hangup") == 0 || strcmp(t, "timeout") == 0) {
        s_hg = HG_NONE;
        wake_ws();
    }
    cJSON_Delete(j);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        char hello[192];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello\",\"device_id\":\"%s\",\"token\":\"%s\"}",
                 DEMO_DEVICE_ID, DEMO_DEVICE_TOKEN);
        send_json(hello);
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev == NULL) {
        return;
    }
    if (ev->op_code == 0x01) {
        on_text(ev->data_ptr, ev->data_len);
    } else if (ev->op_code == 0x02 && ev->data_ptr && ev->data_len > 0) {
        s_rx++;
        if (!s_held && s_spk_open) {
            (void)esp_codec_dev_write(s_spk, ev->data_ptr, ev->data_len);
        }
    }
}

static void mute_down(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = true;
    s_mute_us = now_us();
    bump_idle();
    if (s_st == ST_HANGOUT && s_hg == HG_RINGING) {
        post_act(ACT_ACCEPT);
    } else if (s_st == ST_HANGOUT && s_hg == HG_LIVE) {
        send_json("{\"type\":\"floor_request\"}");
    } else if (s_st == ST_LOCKED || s_st == ST_INBOX) {
        s_mute_arm = true;
    }
}

static void mute_up(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = false;
    if (s_st == ST_HANGOUT && s_hg == HG_LIVE) {
        send_json("{\"type\":\"floor_release\"}");
        return;
    }
    if (s_mute_arm && s_st == ST_INBOX && strcmp(s_kind, "audio") == 0 &&
        (now_us() - s_mute_us) < TAP_MS * 1000) {
        s_mute_arm = false;
        post_act(ACT_PLAY);
        return;
    }
    s_mute_arm = false;
}

static void ptt_task(void *arg)
{
    (void)arg;
    uint8_t chunk[CHUNK];
    while (1) {
        if (!s_held || s_st != ST_HANGOUT || s_hg != HG_LIVE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
        while (s_held && s_st == ST_HANGOUT && s_hg == HG_LIVE) {
            if (esp_codec_dev_read(s_mic, chunk, CHUNK) == ESP_CODEC_DEV_OK && s_ws) {
                esp_websocket_client_send_bin(s_ws, (const char *)chunk, CHUNK, pdMS_TO_TICKS(50));
                s_tx++;
            }
        }
        (void)esp_codec_dev_close(s_mic);
    }
}

static void do_record(void)
{
    s_after_rec = (s_st == ST_INBOX) ? ST_INBOX : ST_LOCKED;
    s_mute_arm = false;
    goto_st(ST_RECORD);
    size_t n = record_while_held();
    while (s_held) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (n < SAMPLE_RATE * 2 / 5) {
        goto_st(s_after_rec);
        return;
    }
    wav_header(s_buf, (uint32_t)n);
    if (board_lvgl_lock(80)) {
        if (s_note) {
            lv_label_set_text(s_note, "sending…");
        }
        board_lvgl_unlock();
    }
    int st = post_wav(44 + (int)n);
    ESP_LOGI(TAG, "POST audio status=%d bytes=%d", st, 44 + (int)n);
    if (st == 200) {
        mark_pass();
    }
    goto_st(s_after_rec);
    if (s_after_rec == ST_INBOX) {
        s_need_fetch = true;
    }
}

static void handle_act(act_t a)
{
    bump_idle();
    if (a == ACT_UNLOCK && s_st == ST_LOCKED) {
        goto_st(ST_PIN);
    } else if (a == ACT_PIN_OK) {
        mark_pass();
        s_need_fetch = true;
        goto_st(ST_INBOX);
    } else if (a == ACT_LOCK) {
        goto_st(ST_LOCKED);
    } else if (a == ACT_PLAY && s_st == ST_INBOX) {
        if (board_lvgl_lock(80)) {
            if (s_note) {
                lv_label_set_text(s_note, "playing…");
            }
            board_lvgl_unlock();
        }
        play_latest();
        paint();
    } else if (a == ACT_INVITE) {
        send_json("{\"type\":\"invite\"}");
        s_pending_ring = false;
        s_hg = HG_CALLING;
        goto_st(ST_HANGOUT);
    } else if (a == ACT_ACCEPT) {
        send_json("{\"type\":\"accept\"}");
        s_hg = HG_LIVE;
        goto_st(ST_HANGOUT);
    } else if (a == ACT_HANGUP) {
        send_json("{\"type\":\"hangup\"}");
        hangup_local();
    }
}

static void shell_task(void *arg)
{
    (void)arg;
    int64_t last_hb = 0, last_live = 0;
    bool live_ui = false;
    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_ev, EV_ACT | EV_WS, pdTRUE, pdFALSE, pdMS_TO_TICKS(50));
        if (s_hello && !live_ui) {
            live_ui = true;
            pull_me();
            goto_st(ST_LOCKED);
        }
        if (s_st == ST_LOCKED && s_count && (bits & EV_WS) && !s_pending_ring && board_lvgl_lock(50)) {
            char n[24];
            snprintf(n, sizeof(n), "%d new", (int)s_unread);
            lv_label_set_text(s_count, n);
            if (s_peer) {
                lv_label_set_text(s_peer, s_peer_on ? "peer on" : "peer off");
            }
            board_lvgl_unlock();
        }
        if (bits & EV_WS) {
            if (s_pending_ring && s_st != ST_RECORD) {
                s_pending_ring = false;
                s_hg = HG_RINGING;
                goto_st(ST_HANGOUT);
            }
            if (s_st == ST_HANGOUT && s_hg == HG_NONE) {
                hangup_local();
            } else if (s_st == ST_HANGOUT && s_hg == HG_LIVE) {
                paint();
            }
        }
        if (s_mute_arm && s_held && s_st == ST_LOCKED) {
            do_record();
            if (s_pending_ring) {
                s_pending_ring = false;
                s_hg = HG_RINGING;
                goto_st(ST_HANGOUT);
            }
        } else if (s_mute_arm && s_held && s_st == ST_INBOX &&
                   (now_us() - s_mute_us) >= TAP_MS * 1000) {
            do_record();
        }
        if (s_act != ACT_NONE) {
            act_t a = s_act;
            s_act = ACT_NONE;
            handle_act(a);
        }
        if (s_need_fetch && s_st == ST_INBOX) {
            s_need_fetch = false;
            fetch_inbox();
            if (is_image()) {
                fetch_preview();
            }
            paint();
            if (s_seq >= 0 && strcmp(s_kind, "audio") != 0) {
                ack(s_seq);
            }
        }
        int64_t t = now_us();
        if (t - last_hb > 5000000) {
            last_hb = t;
            char js[80];
            uint8_t tmp[192];
            http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
            wifi_ap_record_t ap = { 0 };
            (void)esp_wifi_sta_get_ap_info(&ap);
            snprintf(js, sizeof(js), "{\"uptime_s\":%.1f,\"rssi\":%d}", t / 1e6, (int)ap.rssi);
            if (http("POST", "/v1/heartbeat", js, &b, 8000) == 200) {
                cJSON *j = cJSON_Parse((char *)tmp);
                cJSON *on = j ? cJSON_GetObjectItem(j, "peer_online") : NULL;
                if (cJSON_IsTrue(on) || (cJSON_IsNumber(on) && on->valueint)) {
                    s_peer_on = true;
                } else if (on) {
                    s_peer_on = false;
                }
                cJSON_Delete(j);
            }
            if (s_st == ST_LOCKED) {
                pull_me();
            }
        }
        if (s_st == ST_HANGOUT && s_hg == HG_LIVE && s_live && t - last_live > 500000) {
            char line[48];
            last_live = t;
            snprintf(line, sizeof(line), "LIVE %s tx:%d rx:%d",
                     s_held ? "talk" : (s_floor[0] ? s_floor : "idle"), (int)s_tx, (int)s_rx);
            if (board_lvgl_lock(50)) {
                lv_label_set_text(s_live, line);
                board_lvgl_unlock();
            }
        }
        if ((s_st == ST_INBOX || s_st == ST_PIN) && t - s_idle_us > IDLE_MS * 1000LL) {
            goto_st(ST_LOCKED);
        }
    }
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("x01", "display");
        return;
    }
    if (board_lvgl_lock(0)) {
        lv_obj_t *t = lab(scr_bg(0x101418), "wifi…", 0xE8F0E8);
        lv_obj_center(t);
        board_lvgl_unlock();
    }
    board_backlight_set(80);

    s_buf = heap_caps_malloc(BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buf == NULL) {
        demo_fail("x01", "PSRAM");
        if (board_lvgl_lock(80)) {
            lv_obj_center(lab(scr_bg(0x400000), "PSRAM fail", 0xFFFFFF));
            board_lvgl_unlock();
        }
        return;
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("x01", "wifi");
        return;
    }
    ESP_LOGI(TAG, "wifi ok, waiting ws hello");

    s_mic = bsp_audio_codec_microphone_init();
    s_spk = bsp_audio_codec_speaker_init();
    if (s_mic == NULL || s_spk == NULL) {
        demo_fail("x01", "codec");
        return;
    }

    s_ev = xEventGroupCreate();
    button_handle_t btns[BSP_BUTTON_NUM] = { 0 };
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, mute_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, mute_up, NULL);

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = { .uri = uri, .buffer_size = 2048 };
    s_ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("x01", "ws start");
        return;
    }
    xTaskCreate(ptt_task, "ptt", 4096, NULL, 5, NULL);
    xTaskCreate(shell_task, "shell", 8192, NULL, 4, NULL);
    ESP_LOGI(TAG, "uri=%s  hold mute=record (no PIN)  unlock for inbox", uri);
}
