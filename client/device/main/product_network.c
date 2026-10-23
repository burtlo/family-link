#include "product_network.h"

#include "product_login_fw.h"

#include "bsp/esp-box-3.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <string.h>

static const char *kTag = "product_network";
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;
static bool s_wifi_started;
static bool s_wifi_init_attempted;
static bool s_wifi_init_ok;
static int s_last_disconnect_reason;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = data;
        s_last_disconnect_reason = disc != NULL ? (int)disc->reason : -1;
        ESP_LOGW(kTag, "disconnect reason=%d", s_last_disconnect_reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        product_login_schedule_wifi_link(false, s_last_disconnect_reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(kTag, "station got IP");
        product_login_schedule_wifi_link(true, 0);
    }
}

static bool join_configured_wifi(void)
{
    esp_err_t err;
    wifi_config_t wcfg = {0};
    EventBits_t bits;

    if (CONFIG_FAMILY_WIFI_SSID[0] == '\0') {
        ESP_LOGW(kTag, "FAMILY_WIFI_SSID empty; set via make app KIT=<nickname> after deployment Wi-Fi");
        return false;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "esp_netif_init: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "esp_event_loop_create_default: %s", esp_err_to_name(err));
        return false;
    }
    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return false;
        }
    }
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return false;
        }
    }
    if (!s_wifi_init_attempted) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        s_wifi_init_attempted = true;
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "esp_wifi_init: %s", esp_err_to_name(err));
            s_wifi_init_ok = false;
            product_login_schedule_wifi_driver_failed();
            return false;
        }
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            s_wifi_init_ok = false;
            product_login_schedule_wifi_driver_failed();
            return false;
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            s_wifi_init_ok = false;
            product_login_schedule_wifi_driver_failed();
            return false;
        }
        s_wifi_init_ok = true;
    } else if (!s_wifi_init_ok) {
        return false;
    }
    if (!s_wifi_started) {
        strncpy((char *)wcfg.sta.ssid, CONFIG_FAMILY_WIFI_SSID, sizeof(wcfg.sta.ssid) - 1);
        strncpy((char *)wcfg.sta.password, CONFIG_FAMILY_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
        wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wcfg.sta.pmf_cfg.capable = true;
        wcfg.sta.pmf_cfg.required = false;
        ESP_LOGI(kTag, "joining ssid=%s", CONFIG_FAMILY_WIFI_SSID);
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK
            || esp_wifi_start() != ESP_OK) {
            memset(&wcfg, 0, sizeof(wcfg));
            return false;
        }
        memset(&wcfg, 0, sizeof(wcfg));
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
        s_wifi_started = true;
        (void)bsp_display_backlight_on();
    }
    bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(45000));
    if ((bits & WIFI_CONNECTED_BIT) == 0 && s_last_disconnect_reason != 0) {
        product_login_schedule_wifi_link(false, s_last_disconnect_reason);
    }
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void network_task(void *arg)
{
    (void)arg;
    /* Let LVGL paint login chrome before Wi-Fi stack alloc/I2C contention. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    for (;;) {
        if (!join_configured_wifi()) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void product_network_start(void)
{
    if (xTaskCreate(network_task, "product_net", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(kTag, "network task create failed");
    }
}
