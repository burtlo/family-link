#include "product_network.hpp"

#include "product_buttons.hpp"
#include "product_login_service.hpp"
#include "product_mem.hpp"

#include "bsp/esp-box-3.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <cstring>

namespace product_network
{

namespace
{

const char *kTag = "product_network";
#define WIFI_CONNECTED_BIT BIT0

EventGroupHandle_t s_wifi_events;
esp_netif_t *s_sta_netif;
bool s_wifi_started;
bool s_wifi_init_attempted;
bool s_wifi_init_ok;
int s_last_disconnect_reason;

bool join_configured_wifi();

} /* namespace */

extern "C" {

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *disc = static_cast<const wifi_event_sta_disconnected_t *>(data);
        s_last_disconnect_reason = disc != NULL ? (int)disc->reason : -1;
        ESP_LOGW(kTag, "disconnect reason=%d", s_last_disconnect_reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        product_ui::loginService().scheduleWifi(false, s_last_disconnect_reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(kTag, "station got IP");
        product_mem::probe_heap("wifi_got_ip", 0);
        product_ui::loginService().scheduleWifi(true, 0);
    }
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

} /* extern "C" */

namespace
{

bool join_configured_wifi()
{
    esp_err_t err;
    wifi_config_t wcfg{};
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
            product_ui::loginService().scheduleWifiDriverFailed();
            return false;
        }
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            s_wifi_init_ok = false;
            product_ui::loginService().scheduleWifiDriverFailed();
            return false;
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            s_wifi_init_ok = false;
            product_ui::loginService().scheduleWifiDriverFailed();
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
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK ||
            esp_wifi_start() != ESP_OK) {
            memset(&wcfg, 0, sizeof(wcfg));
            return false;
        }
        memset(&wcfg, 0, sizeof(wcfg));
        (void)esp_wifi_set_ps(WIFI_PS_NONE);
        s_wifi_started = true;
        if (product_buttons::mute_latched() == 0) {
            (void)bsp_display_backlight_on();
        }
    }
    bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(45000));
    if ((bits & WIFI_CONNECTED_BIT) == 0 && s_last_disconnect_reason != 0) {
        product_ui::loginService().scheduleWifi(false, s_last_disconnect_reason);
    }
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

} /* namespace */

bool sta_up()
{
    if (s_wifi_events == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) != 0;
}

void start()
{
    if (xTaskCreate(network_task, "product_net", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(kTag, "network task create failed");
    }
}

} /* namespace product_network */
