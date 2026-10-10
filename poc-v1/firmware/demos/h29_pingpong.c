/*
 * h29 — Async voice ping-pong (Mazi ↔ Arlo). Hold the red circle to record,
 * release to send. Incoming clips play immediately. Recording is blocked while
 * a clip is playing. Messages cap at 10 s. Chirp up on record start, chirp
 * down on release.
 *
 * Host (leave running):
 *   python -m demos.server.h29_pingpong.server --host 0.0.0.0 --port 8080
 *
 *   make flash DEMO=h29 WHO=mazi
 *   make flash DEMO=h29 WHO=arlo
 * -- PASS h29 after a successful send, or the first inbound clip played.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h29";

#define SAMPLE_RATE    16000
#define CHUNK          640
#define MAX_SEC        10
#define PCM_CAP        (SAMPLE_RATE * 2 * MAX_SEC)
#define MAX_BLOB       (44 + PCM_CAP)
#define SPK_VOL        100
#define CHIRP_VOL      70
#define CHIRP_MS       160
#define CHIRP_DRAIN_MS 60
#define CHIRP_SAMPLES  (SAMPLE_RATE * CHIRP_MS / 1000)
#define CHIRP_DRAIN    (SAMPLE_RATE * CHIRP_DRAIN_MS / 1000)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    uint8_t *buf;
    int cap;
    int len;
} body_t;

static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;
static uint8_t *s_pcm;
static int16_t s_chirp_pcm[CHIRP_SAMPLES + CHIRP_DRAIN];

static esp_websocket_client_handle_t s_ws;
static SemaphoreHandle_t s_down;
static SemaphoreHandle_t s_play_wake;
static volatile bool s_held;
static volatile bool s_playing;
static volatile int s_pending_seq;
static volatile int s_last_played_seq;
static bool s_inbox_ready;

static lv_obj_t *s_title;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;

static char s_self_name[24];
static char s_peer_name[24];
static uint8_t s_http_mem[512];
static int s_poll_ticks;
static bool s_passed;
static bool s_sent_ok;
static bool s_played_ok;

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void set_status(const char *line1, const char *line2)
{
    char buf[64];
    if (line2 && line2[0]) {
        snprintf(buf, sizeof(buf), "%s\n%s", line1, line2);
    } else {
        snprintf(buf, sizeof(buf), "%s", line1);
    }
    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_status) {
        lv_label_set_text(s_status, buf);
    }
    board_lvgl_unlock();
}

static void paint_home(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_title = lv_label_create(scr);
    lv_label_set_text_fmt(s_title, "ping-pong  %s", s_peer_name);
    lv_obj_set_style_text_color(s_title, lv_color_hex(0x8A9298), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);

    s_status = lv_label_create(scr);
    lv_label_set_text(s_status, "hold red circle\nto record");
    lv_obj_set_style_text_color(s_status, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_status, 280);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, -8);

    s_hint = lv_label_create(scr);
    lv_label_set_text(s_hint, "unmute first (LED off)");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x666666), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    board_lvgl_unlock();
}

static void maybe_pass(void)
{
    if (s_passed || (!s_sent_ok && !s_played_ok)) {
        return;
    }
    s_passed = true;
    demo_pass("h29");
}

static void fade_edges(int16_t *pcm, int n)
{
    int fade = SAMPLE_RATE / 200;
    if (fade > n / 4) {
        fade = n / 4;
    }
    if (fade < 1) {
        return;
    }
    for (int i = 0; i < fade; i++) {
        pcm[i] = (int16_t)((pcm[i] * i) / fade);
        pcm[n - 1 - i] = (int16_t)((pcm[n - 1 - i] * i) / fade);
    }
}

static bool write_pcm(const int16_t *pcm, int samples)
{
    const uint8_t *p = (const uint8_t *)pcm;
    int bytes = samples * 2;
    while (bytes > 0) {
        int n = bytes > 1024 ? 1024 : bytes;
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
    if (!s_spk || hz <= 0 || s_playing) {
        return;
    }
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
    fade_edges(s_chirp_pcm, n);
    memset(s_chirp_pcm + n, 0, (size_t)drain * sizeof(int16_t));

    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, CHIRP_VOL);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    vTaskDelay(pdMS_TO_TICKS(30));
    (void)write_pcm(s_chirp_pcm, n + drain);
    (void)esp_codec_dev_close(s_spk);
}

static void play_chirp_pair(int a, int b)
{
    play_chirp(a);
    if (b > 0) {
        play_chirp(b);
    }
}

static void wav_header(uint8_t *p, uint32_t pcm_bytes)
{
    uint32_t riff = 36 + pcm_bytes;
    uint32_t fmt = 16;
    uint16_t audio = 1, ch = 1, bps = 16;
    uint32_t rate = SAMPLE_RATE;
    uint32_t byte_rate = rate * ch * bps / 8;
    uint16_t block = ch * bps / 8;
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

static int16_t pcm_peak(const uint8_t *p, size_t nbytes)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)p;
    size_t n = nbytes / 2;
    for (size_t i = 0; i < n; i++) {
        int16_t a = s[i];
        if (a < 0) {
            a = (int16_t)(-a);
        }
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static size_t record_while_held(void)
{
    size_t filled = 0;
    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
    while (s_held && filled + CHUNK <= PCM_CAP && !s_playing) {
        if (esp_codec_dev_read(s_mic, s_pcm + 44 + filled, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        filled += CHUNK;
    }
    (void)esp_codec_dev_close(s_mic);
    return filled;
}

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    body_t *b = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int n = evt->data_len;
        if (b->len + n > b->cap) {
            n = b->cap - b->len;
        }
        if (n > 0) {
            memcpy(b->buf + b->len, evt->data, n);
            b->len += n;
        }
    }
    return ESP_OK;
}

static int http_get_blob(const char *path, body_t *body)
{
    body->len = 0;
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = body,
        .timeout_ms = 15000,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return -1;
    }
    return status;
}

static int post_wav(const uint8_t *wav, int wav_len)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/v1/messages", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    static const char *bnd = "----FamilyLinkH29";
    char pre[256];
    int pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; filename=\"clip.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n",
                           bnd, bnd);
    char post[80];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    int total = pre_len + wav_len + post_len;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(client, "Content-Type", ctype);

    if (esp_http_client_open(client, total) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    esp_http_client_write(client, pre, pre_len);
    esp_http_client_write(client, (const char *)wav, wav_len);
    esp_http_client_write(client, post, post_len);
    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

static bool play_pcm(const uint8_t *pcm, int pcm_len)
{
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        return false;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOL);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    vTaskDelay(pdMS_TO_TICKS(30));
    while (pcm_len > 0) {
        int chunk = pcm_len > 2048 ? 2048 : pcm_len;
        if (esp_codec_dev_write(s_spk, (void *)pcm, chunk) != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(s_spk);
            return false;
        }
        pcm += chunk;
        pcm_len -= chunk;
    }
    (void)esp_codec_dev_close(s_spk);
    return true;
}

static void queue_play(int seq)
{
    if (seq <= 0 || seq <= s_last_played_seq) {
        return;
    }
    if (seq > s_pending_seq) {
        s_pending_seq = seq;
    }
    if (s_play_wake) {
        xSemaphoreGive(s_play_wake);
    }
}

static int inbox_latest_audio_seq(void)
{
    body_t json = { .buf = s_http_mem, .cap = sizeof(s_http_mem) };
    int st = http_get_blob("/v1/messages", &json);
    if (st != 200) {
        ESP_LOGW(TAG, "inbox list status=%d", st);
        return -1;
    }
    cJSON *arr = cJSON_Parse((char *)json.buf);
    if (!arr) {
        return -1;
    }
    int n = cJSON_GetArraySize(arr);
    int latest = 0;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *kind = cJSON_GetObjectItem(it, "kind");
        cJSON *seq = cJSON_GetObjectItem(it, "seq");
        if (cJSON_IsString(kind) && strcmp(kind->valuestring, "audio") == 0 && cJSON_IsNumber(seq)) {
            if (seq->valueint > latest) {
                latest = seq->valueint;
            }
        }
    }
    cJSON_Delete(arr);
    return latest;
}

/* Mark everything already in the inbox as heard; do not play history. */
static void catch_up_inbox(void)
{
    if (s_inbox_ready) {
        return;
    }
    int latest = inbox_latest_audio_seq();
    if (latest < 0) {
        return;
    }
    if (s_pending_seq > s_last_played_seq) {
        /* WS already queued a live clip; skip older history only. */
        s_last_played_seq = s_pending_seq - 1;
    } else {
        s_last_played_seq = latest;
        s_pending_seq = latest;
    }
    s_inbox_ready = true;
    ESP_LOGI(TAG, "inbox caught up at seq=%d (history skipped)", s_last_played_seq);
}

/* WS fallback: play only clips that arrived after catch-up. */
static void poll_for_new_inbox(void)
{
    if (!s_inbox_ready) {
        catch_up_inbox();
        return;
    }
    int latest = inbox_latest_audio_seq();
    if (latest > s_last_played_seq) {
        ESP_LOGI(TAG, "inbox poll new audio seq=%d", latest);
        queue_play(latest);
    }
}

static void play_task_fn(void *arg)
{
    (void)arg;
    body_t wav = { .buf = s_pcm, .cap = MAX_BLOB };
    for (;;) {
        if (xSemaphoreTake(s_play_wake, pdMS_TO_TICKS(3000)) != pdTRUE) {
            if (++s_poll_ticks >= 1) {
                s_poll_ticks = 0;
                if (!s_playing) {
                    poll_for_new_inbox();
                }
            }
            continue;
        }
        while (s_pending_seq > s_last_played_seq) {
            int seq = s_last_played_seq + 1;
            s_playing = true;
            set_status("playing…", s_peer_name);
            char path[64];
            snprintf(path, sizeof(path), "/v1/messages/%d/blob", seq);
            int st = http_get_blob(path, &wav);
            ESP_LOGI(TAG, "play seq=%d status=%d bytes=%d", seq, st, wav.len);
            if (st == 200 && wav.len > 44) {
                const uint8_t *pcm = wav.buf;
                int pcm_len = wav.len;
                if (memcmp(wav.buf, "RIFF", 4) == 0) {
                    pcm = wav.buf + 44;
                    pcm_len = wav.len - 44;
                }
                if (play_pcm(pcm, pcm_len)) {
                    s_last_played_seq = seq;
                    s_played_ok = true;
                    maybe_pass();
                } else {
                    ESP_LOGW(TAG, "play_pcm failed seq=%d", seq);
                    break;
                }
            } else {
                ESP_LOGW(TAG, "blob fetch failed seq=%d status=%d", seq, st);
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
            }
            s_playing = false;
            set_status("hold red circle", "to record");
        }
        if (s_playing) {
            s_playing = false;
            set_status("hold red circle", "to record");
        }
    }
}

static void send_json(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(1000));
}

static void on_ws_text(const char *s, int n)
{
    char tmp[256];
    if (n >= (int)sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
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
        ESP_LOGI(TAG, "ws hello_ok");
        catch_up_inbox();
    } else if (strcmp(t, "inbox") == 0) {
        cJSON *kind = cJSON_GetObjectItem(j, "kind");
        cJSON *seq = cJSON_GetObjectItem(j, "seq");
        if (cJSON_IsString(kind) && strcmp(kind->valuestring, "audio") == 0 && cJSON_IsNumber(seq)) {
            queue_play(seq->valueint);
        }
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
        on_ws_text(ev->data_ptr, ev->data_len);
    }
}

static void ptt_down(void *b, void *u)
{
    (void)b;
    (void)u;
    if (s_playing) {
        return;
    }
    s_held = true;
    if (s_down) {
        xSemaphoreGive(s_down);
    }
}

static void ptt_up(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = false;
}

void app_main(void)
{
    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name, sizeof(s_peer_name), DEMO_PEER_NAME);

    if (board_display_start() == ESP_OK) {
        board_status_set("h29 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h29", "wifi");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        demo_fail("h29", "codec");
        return;
    }

    s_pcm = heap_caps_malloc(MAX_BLOB, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pcm == NULL) {
        s_pcm = heap_caps_malloc(MAX_BLOB, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_pcm == NULL) {
        demo_fail("h29", "OOM");
        return;
    }

    paint_home();
    board_backlight_set(55);

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t wcfg = {
        .uri = uri,
        .buffer_size = 2048,
    };
    s_ws = esp_websocket_client_init(&wcfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("h29", "ws start");
        return;
    }

    s_down = xSemaphoreCreateBinary();
    s_play_wake = xSemaphoreCreateBinary();
    xTaskCreate(play_task_fn, "play", 8192, NULL, 5, NULL);

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        demo_fail("h29", "red circle");
        return;
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, ptt_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, ptt_up, NULL);

    ESP_LOGI(TAG, "%s -> %s  hold red circle, release to send", s_self_name, s_peer_name);
    set_status("hold red circle", "to record");

    while (1) {
        xSemaphoreTake(s_down, portMAX_DELAY);
        if (!s_held) {
            continue;
        }
        if (s_playing) {
            while (s_held) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }
        if (mute_latched()) {
            set_status("unmute first", "red LED must be off");
            while (s_held) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }

        play_chirp_pair(523, 784);
        set_status("recording…", "release to send");
        size_t n = record_while_held();
        while (s_held) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        play_chirp_pair(784, 392);

        if (n < SAMPLE_RATE * 2 / 5) {
            set_status("too short", "hold longer");
            continue;
        }
        wav_header(s_pcm, (uint32_t)n);
        int wav_len = 44 + (int)n;
        if (pcm_peak(s_pcm + 44, n) < 64) {
            set_status("too quiet", "talk louder");
            continue;
        }

        set_status("sending…", "");
        int status = post_wav(s_pcm, wav_len);
        ESP_LOGI(TAG, "POST status=%d bytes=%d", status, wav_len);
        if (status == 200) {
            s_sent_ok = true;
            maybe_pass();
            set_status("sent", "hold red circle");
            continue;
        }
        set_status("send failed", "");
        demo_fail("h29", "POST");
        return;
    }
}
