/*
 * h14 — Heartbeat presence + WebSocket inbox badge. Locked-idle: big count,
 * small peer line. Never show message body. Mic stays closed.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static const char *TAG = "h14";

static esp_websocket_client_handle_t s_ws;
static volatile bool s_hello_ok;
static volatile bool s_hb_ok;
static volatile int s_badge;
static volatile int s_peer_online = -1;
static bool s_passed;
static lv_obj_t *s_count;
static lv_obj_t *s_peer;
static uint8_t s_http_mem[512];

static void maybe_pass(void)
{
    if (s_passed || !s_hello_ok || !s_hb_ok) {
        return;
    }
    s_passed = true;
    demo_pass("h14");
}

static void paint_idle(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    s_count = lv_label_create(scr);
    lv_label_set_text(s_count, "0");
    lv_obj_set_style_text_color(s_count, lv_color_white(), 0);
#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_48, 0);
#elif defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(s_count, LV_ALIGN_CENTER, 0, -8);

    s_peer = lv_label_create(scr);
    lv_label_set_text(s_peer, "peer …");
    lv_obj_set_style_text_color(s_peer, lv_color_hex(0x888888), 0);
    lv_obj_align(s_peer, LV_ALIGN_CENTER, 0, 48);

    board_lvgl_unlock();
}

static void refresh_ui(void)
{
    char count[16];
    char peer[32];
    snprintf(count, sizeof(count), "%d", s_badge);
    if (s_peer_online == 1) {
        snprintf(peer, sizeof(peer), "peer online");
    } else if (s_peer_online == 0) {
        snprintf(peer, sizeof(peer), "peer offline");
    } else {
        snprintf(peer, sizeof(peer), "peer …");
    }

    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_count) {
        lv_label_set_text(s_count, count);
    }
    if (s_peer) {
        lv_label_set_text(s_peer, peer);
        lv_obj_set_style_text_color(s_peer,
                                    lv_color_hex(s_peer_online == 1 ? 0x88cc88 : 0x888888), 0);
    }
    board_lvgl_unlock();
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
        s_hello_ok = true;
        maybe_pass();
    } else if (strcmp(t, "inbox") == 0) {
        s_badge++;
        ESP_LOGI(TAG, "inbox event badge=%d (body not shown)", s_badge);
        refresh_ui();
    } else if (strcmp(t, "presence") == 0) {
        cJSON *online = cJSON_GetObjectItem(j, "online");
        if (cJSON_IsTrue(online) || (cJSON_IsNumber(online) && online->valueint)) {
            s_peer_online = 1;
        } else if (cJSON_IsFalse(online) || cJSON_IsNumber(online)) {
            s_peer_online = 0;
        }
        refresh_ui();
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
    }
}

static int peer_online_from_json(const char *body)
{
    cJSON *j = cJSON_Parse(body);
    if (!j) {
        return -1;
    }
    cJSON *online = cJSON_GetObjectItem(j, "peer_online");
    int v = -1;
    if (cJSON_IsTrue(online)) {
        v = 1;
    } else if (cJSON_IsFalse(online)) {
        v = 0;
    } else if (cJSON_IsNumber(online)) {
        v = online->valueint ? 1 : 0;
    }
    cJSON_Delete(j);
    return v;
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h14 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h14", "wifi");
        return;
    }

    paint_idle();
    board_backlight_set(55);

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
    };
    s_ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("h14", "ws start");
        return;
    }
    ESP_LOGI(TAG, "uri=%s  heartbeat every 4s  badge from WS inbox only", uri);

    http_buf_t body = { .buf = s_http_mem, .cap = sizeof(s_http_mem) };
    while (1) {
        int hb = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "POST", "/v1/heartbeat",
                                DEMO_DEVICE_TOKEN, "{}", &body, 8000);
        ESP_LOGI(TAG, "heartbeat status=%d %s", hb, (char *)body.buf);
        if (hb == 200) {
            s_hb_ok = true;
            int from_hb = peer_online_from_json((char *)body.buf);
            if (from_hb >= 0) {
                s_peer_online = from_hb;
            }
            maybe_pass();
        }

        int me = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET", "/v1/me",
                                DEMO_DEVICE_TOKEN, NULL, &body, 8000);
        if (me == 200) {
            int from_me = peer_online_from_json((char *)body.buf);
            if (from_me >= 0) {
                s_peer_online = from_me;
            }
        }
        refresh_ui();
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
