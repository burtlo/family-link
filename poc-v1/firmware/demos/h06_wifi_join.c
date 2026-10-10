/*
 * h06 — Wi-Fi join (ESP32-S3-BOX-3 STA).
 *
 * This demo proves the desk AP is 2.4 GHz and the box can stay associated.
 * No HTTP, no TLS. Pass only after 60 s still up with an IP.
 */

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "pass.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h06";

#define HOLD_MS 60000
#define JOIN_MS 30000
#define MAX_JOIN_TRIES 8

#define BIT_GOT_IP BIT0
#define BIT_FAIL   BIT1

static EventGroupHandle_t s_events;
static bool s_finished;
static bool s_got_ip;
static int s_join_tries;
static char s_fail_reason[80];

static void finish_fail(const char *reason)
{
    if (s_finished) {
        return;
    }
    s_finished = true;
    demo_fail("h06", reason);
    if (s_events) {
        xEventGroupSetBits(s_events, BIT_FAIL);
    }
}

static void finish_pass(void)
{
    if (s_finished) {
        return;
    }
    s_finished = true;
    demo_pass("h06"); /* prints -- PASS h06 once */
}

static const char *disconnect_reason(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "auth fail";
    case WIFI_REASON_NO_AP_FOUND:
        return "no AP found (5 GHz-only SSID?)";
    default:
        snprintf(s_fail_reason, sizeof(s_fail_reason), "disconnect reason %u", reason);
        return s_fail_reason;
    }
}

static bool is_auth_fail(uint8_t reason)
{
    return reason == WIFI_REASON_AUTH_FAIL
        || reason == WIFI_REASON_AUTH_EXPIRE
        || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
        || reason == WIFI_REASON_HANDSHAKE_TIMEOUT
        || reason == WIFI_REASON_802_1X_AUTH_FAILED;
}

static void log_link(const esp_netif_ip_info_t *ip)
{
    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(TAG, "ip=" IPSTR " rssi=%d channel=%u",
                 IP2STR(&ip->ip), (int)ap.rssi, (unsigned)ap.primary);
    } else {
        ESP_LOGI(TAG, "ip=" IPSTR " rssi=? channel=?", IP2STR(&ip->ip));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA start, connecting SSID='%s'", DEMO_WIFI_SSID);
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        uint8_t reason = ev ? ev->reason : 0;
        ESP_LOGW(TAG, "disconnected reason=%u", reason);

        if (s_finished) {
            return;
        }
        if (s_got_ip) {
            finish_fail("lost association");
            return;
        }
        if (is_auth_fail(reason) || reason == WIFI_REASON_NO_AP_FOUND) {
            finish_fail(disconnect_reason(reason));
            return;
        }

        s_join_tries++;
        if (s_join_tries >= MAX_JOIN_TRIES) {
            finish_fail(disconnect_reason(reason));
            return;
        }
        esp_wifi_connect();
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        s_got_ip = true;
        log_link(&ev->ip_info);
        xEventGroupSetBits(s_events, BIT_GOT_IP);
    }
}

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    nvs_init();

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, DEMO_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, DEMO_WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(
        WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    EventBits_t bits = xEventGroupWaitBits(
        s_events, BIT_GOT_IP | BIT_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(JOIN_MS));
    if (s_finished || (bits & BIT_FAIL)) {
        return;
    }
    if (!(bits & BIT_GOT_IP)) {
        finish_fail("no IP");
        return;
    }

    bits = xEventGroupWaitBits(s_events, BIT_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(HOLD_MS));
    if (s_finished || (bits & BIT_FAIL)) {
        return;
    }

    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        finish_fail("lost association");
        return;
    }

    finish_pass();
}
