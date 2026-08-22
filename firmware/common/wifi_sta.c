#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_sta";
#define BIT_IP   BIT0
#define BIT_FAIL BIT1

static EventGroupHandle_t s_bits;
static int s_tries;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_tries++;
        if (s_tries > 8) {
            xEventGroupSetBits(s_bits, BIT_FAIL);
            return;
        }
        esp_wifi_connect();
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        wifi_ap_record_t ap = { 0 };
        (void)esp_wifi_sta_get_ap_info(&ap);
        ESP_LOGI(TAG, "ip=" IPSTR " rssi=%d ch=%u",
                 IP2STR(&ev->ip_info.ip), (int)ap.rssi, (unsigned)ap.primary);
        xEventGroupSetBits(s_bits, BIT_IP);
    }
}

esp_err_t wifi_sta_join(const char *ssid, const char *password, uint32_t timeout_ms)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_bits = xEventGroupCreate();
    s_tries = 0;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid ? ssid : "", sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password ? password : "", sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    EventBits_t bits = xEventGroupWaitBits(
        s_bits, BIT_IP | BIT_FAIL, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (bits & BIT_IP) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "join failed ssid='%s'", ssid ? ssid : "");
    return ESP_FAIL;
}
