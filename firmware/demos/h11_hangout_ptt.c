/*
 * h11 — Hangout PTT through server demo 06. Half-duplex: mic only while mute
 * is held; speaker only while mute is up. No WebRTC.
 *
 * Other peer: Python twin as box-b (see demos/server/06_audio_relay/twin_peer.py).
 * -- PASS h11 after ~10 frames sent and ~10 received.
 */

#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"

#include "board.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h11";
#define CHUNK 640

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = 16000,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_websocket_client_handle_t s_ws;
static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static volatile bool s_held;
static volatile bool s_session;
static volatile bool s_spk_open;
static volatile int s_tx;
static volatile int s_rx;
static bool s_passed;
static char s_floor[24];

static void maybe_pass(void)
{
    if (s_passed || s_tx < 10 || s_rx < 10) {
        return;
    }
    s_passed = true;
    demo_pass("h11");
}

static void send_json(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(1000));
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
        send_json("{\"type\":\"invite\"}");
        board_status_set("invited peer\nrun twin_peer.py");
    } else if (strcmp(t, "ring") == 0) {
        send_json("{\"type\":\"accept\"}");
    } else if (strcmp(t, "session_start") == 0) {
        s_session = true;
        board_status_set("live  hold mute=PTT");
        if (s_spk && !s_spk_open && esp_codec_dev_open(s_spk, &s_fs) == ESP_OK) {
            (void)esp_codec_dev_set_out_vol(s_spk, 70);
            s_spk_open = true;
        }
    } else if (strcmp(t, "floor") == 0) {
        cJSON *h = cJSON_GetObjectItem(j, "holder");
        if (cJSON_IsString(h) && h->valuestring) {
            strncpy(s_floor, h->valuestring, sizeof(s_floor) - 1);
        } else {
            s_floor[0] = 0;
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
        on_text(ev->data_ptr, ev->data_len);
    } else if (ev->op_code == 0x02 && ev->data_ptr && ev->data_len > 0) {
        s_rx++;
        if (!s_held && s_spk_open) {
            (void)esp_codec_dev_write(s_spk, ev->data_ptr, ev->data_len);
        }
        maybe_pass();
    }
}

static void mute_down(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = true;
    send_json("{\"type\":\"floor_request\"}");
}

static void mute_up(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = false;
    send_json("{\"type\":\"floor_release\"}");
}

static void ptt_task(void *arg)
{
    (void)arg;
    uint8_t chunk[CHUNK];
    while (1) {
        if (!s_held || !s_session) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
        while (s_held && s_session) {
            if (esp_codec_dev_read(s_mic, chunk, CHUNK) == ESP_CODEC_DEV_OK && s_ws) {
                esp_websocket_client_send_bin(s_ws, (const char *)chunk, CHUNK, pdMS_TO_TICKS(50));
                s_tx++;
                maybe_pass();
            }
        }
        (void)esp_codec_dev_close(s_mic);
    }
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h11 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h11", "wifi");
        return;
    }

    s_mic = bsp_audio_codec_microphone_init();
    s_spk = bsp_audio_codec_speaker_init();
    if (s_mic == NULL || s_spk == NULL) {
        demo_fail("h11", "codec");
        return;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, mute_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, mute_up, NULL);

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
    };
    s_ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("h11", "ws start");
        return;
    }

    xTaskCreate(ptt_task, "ptt", 4096, NULL, 5, NULL);
    board_status_set("ws connecting…");
    ESP_LOGI(TAG, "uri=%s  mute=PTT  speaker muted while held", uri);
}
