/*
 * h17 — Cross-network heartbeat. Box joins 2.4 GHz Wi-Fi, POSTs
 * /v1/heartbeat to a server that may live on another SSID.
 *
 * Host: make demo-cross-heartbeat  (binds 0.0.0.0, flashes this demo,
 * injects FAMILY_SERVER_HOST). Same-router 2.4 + 5 GHz is enough; isolated
 * networks need TUNNEL=1 (cloudflared) or SERVER_HOST=...
 *
 * LCD: beat count + peer line. UART: -- PASS h17 after the first 200.
 * Mic stays closed.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
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
#include "server_override.h"

static const char *TAG = "h17";

static volatile int s_beats;
static volatile int s_last_status = -1;
static volatile int s_peer_online = -1;
static bool s_passed;
static lv_obj_t *s_count;
static lv_obj_t *s_peer;
static lv_obj_t *s_host;
static uint8_t s_http_mem[512];

static int use_tls(void)
{
#if defined(DEMO_SERVER_TLS) && DEMO_SERVER_TLS
    return 1;
#else
    return 0;
#endif
}

static void maybe_pass(void)
{
    if (s_passed || s_beats < 1) {
        return;
    }
    s_passed = true;
    demo_pass("h17");
}

static void paint_idle(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x102018), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "cross Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_hex(0x88cc88), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_host = lv_label_create(scr);
    lv_label_set_text(s_host, DEMO_SERVER_HOST);
    lv_obj_set_style_text_color(s_host, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_width(s_host, 300);
    lv_obj_align(s_host, LV_ALIGN_TOP_MID, 0, 28);

    s_count = lv_label_create(scr);
    lv_label_set_text(s_count, "beats 0");
    lv_obj_set_style_text_color(s_count, lv_color_white(), 0);
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(s_count, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(s_count, LV_ALIGN_CENTER, 0, 4);

    s_peer = lv_label_create(scr);
    lv_label_set_text(s_peer, "waiting for server");
    lv_obj_set_style_text_color(s_peer, lv_color_hex(0x888888), 0);
    lv_obj_align(s_peer, LV_ALIGN_CENTER, 0, 48);

    board_lvgl_unlock();
}

static void refresh_ui(void)
{
    char count[32];
    char peer[48];
    snprintf(count, sizeof(count), "beats %d", s_beats);
    if (s_last_status == 200 && s_peer_online == 1) {
        snprintf(peer, sizeof(peer), "peer online");
    } else if (s_last_status == 200 && s_peer_online == 0) {
        snprintf(peer, sizeof(peer), "server ok  peer off");
    } else if (s_last_status > 0) {
        snprintf(peer, sizeof(peer), "http %d", s_last_status);
    } else if (s_last_status < 0) {
        snprintf(peer, sizeof(peer), "no route yet");
    } else {
        snprintf(peer, sizeof(peer), "waiting for server");
    }

    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_count) {
        lv_label_set_text(s_count, count);
    }
    if (s_peer) {
        lv_label_set_text(s_peer, peer);
        lv_obj_set_style_text_color(
            s_peer, lv_color_hex(s_peer_online == 1 ? 0x88cc88 : 0xcccc88), 0);
    }
    board_lvgl_unlock();
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

static void log_radio(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return;
    }
    ESP_LOGI(TAG, "ssid=%s ch=%u rssi=%d (2.4 GHz if ch 1-14)", ap.ssid,
             (unsigned)ap.primary, (int)ap.rssi);
    if (ap.primary > 14) {
        ESP_LOGW(TAG, "channel %u is not 2.4 GHz — this radio cannot stay up",
                 (unsigned)ap.primary);
    }
}

void app_main(void)
{
    const int tls = use_tls();
    ESP_LOGI(TAG, "server %s://%s:%d  device=%s", tls ? "https" : "http",
             DEMO_SERVER_HOST, DEMO_SERVER_PORT, DEMO_DEVICE_ID);

    if (board_display_start() == ESP_OK) {
        board_status_set("h17 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h17", "wifi");
        return;
    }
    log_radio();

    paint_idle();
    board_backlight_set(60);

    http_buf_t body = {.buf = s_http_mem, .cap = sizeof(s_http_mem)};
    int timeout_ms = tls ? 15000 : 8000;
    while (1) {
        int hb = http_bearer_do_ex(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "POST",
                                   "/v1/heartbeat", DEMO_DEVICE_TOKEN, "{}",
                                   &body, timeout_ms, tls);
        ESP_LOGI(TAG, "heartbeat status=%d %s", hb, (char *)body.buf);
        s_last_status = hb;
        if (hb == 200) {
            s_beats++;
            int from_hb = peer_online_from_json((char *)body.buf);
            if (from_hb >= 0) {
                s_peer_online = from_hb;
            }
            maybe_pass();
        }

        int me = http_bearer_do_ex(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET",
                                   "/v1/me", DEMO_DEVICE_TOKEN, NULL, &body,
                                   timeout_ms, tls);
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
