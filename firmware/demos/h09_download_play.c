/*
 * h09 — Download an inbox audio blob from server demo 3 and play it.
 * Seed the box inbox from the Mac (parent token), then flash this demo.
 * No PIN in this demo.
 */

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h09";
#define SAMPLE_RATE 16000
#define MAX_BLOB    (320 * 1024)

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

static int http_get(const char *path, const char *token, body_t *body)
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
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return -1;
    }
    if (body->len < body->cap) {
        body->buf[body->len] = 0;
    }
    return status;
}

static bool play_pcm(esp_codec_dev_handle_t spk, const uint8_t *p, int n)
{
    (void)esp_codec_dev_set_out_vol(spk, 70);
    if (esp_codec_dev_open(spk, &s_fs) != ESP_OK) {
        return false;
    }
    while (n > 0) {
        int chunk = n > 2048 ? 2048 : n;
        if (esp_codec_dev_write(spk, (void *)p, chunk) != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(spk);
            return false;
        }
        p += chunk;
        n -= chunk;
    }
    (void)esp_codec_dev_close(spk);
    return true;
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h09 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h09", "wifi");
        return;
    }

    uint8_t json_mem[2048];
    body_t json = { .buf = json_mem, .cap = sizeof(json_mem) };
    uint8_t *blob = heap_caps_malloc(MAX_BLOB, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (blob == NULL) {
        blob = heap_caps_malloc(MAX_BLOB, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (blob == NULL) {
        demo_fail("h09", "OOM");
        return;
    }
    body_t wav = { .buf = blob, .cap = MAX_BLOB };

    board_status_set("waiting for audio\nin inbox (server 03)");
    int seq = -1;
    for (int i = 0; i < 60 && seq < 0; i++) {
        int st = http_get("/v1/messages", DEMO_DEVICE_TOKEN, &json);
        ESP_LOGI(TAG, "list status=%d body=%.*s", st, json.len, (char *)json.buf);
        if (st == 200) {
            cJSON *arr = cJSON_Parse((char *)json.buf);
            int n = cJSON_GetArraySize(arr);
            for (int k = 0; k < n; k++) {
                cJSON *it = cJSON_GetArrayItem(arr, k);
                cJSON *kind = cJSON_GetObjectItem(it, "kind");
                cJSON *s = cJSON_GetObjectItem(it, "seq");
                if (cJSON_IsString(kind) && strcmp(kind->valuestring, "audio") == 0 && cJSON_IsNumber(s)) {
                    seq = s->valueint;
                    break;
                }
            }
            cJSON_Delete(arr);
        }
        if (seq < 0) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    if (seq < 0) {
        board_status_set("no audio in inbox");
        demo_fail("h09", "empty inbox");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/v1/messages/%d/blob", seq);
    board_status_set("downloading…");
    int st = http_get(path, DEMO_DEVICE_TOKEN, &wav);
    ESP_LOGI(TAG, "blob status=%d bytes=%d", st, wav.len);
    if (st != 200 || wav.len < 64) {
        demo_fail("h09", "blob");
        return;
    }

    const uint8_t *pcm = wav.buf;
    int pcm_len = wav.len;
    if (wav.len > 44 && memcmp(wav.buf, "RIFF", 4) == 0) {
        pcm = wav.buf + 44;
        pcm_len = wav.len - 44;
    }

    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    if (spk == NULL) {
        demo_fail("h09", "ES8311");
        return;
    }
    board_status_set("playing…");
    if (!play_pcm(spk, pcm, pcm_len)) {
        demo_fail("h09", "play");
        return;
    }
    board_status_set("played  PASS");
    demo_pass("h09");
}
