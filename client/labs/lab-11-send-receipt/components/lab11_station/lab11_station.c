#include "lab11_station.h"

#include "sdkconfig.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include <string.h>

#define LAB11_WIFI_CONNECTED_BIT BIT0

static bool s_up;
static bool s_transport_ready;
static bool s_wifi_started;
static int s_last_reason = -1;
static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_sta_netif;

void lab11_station_init(void)
{
    s_up = false;
}

bool lab11_station_prepare_transport(void)
{
    esp_err_t err;
    if (s_transport_ready) {
        return true;
    }
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }
    s_transport_ready = true;
    return true;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = data;
        s_last_reason = disc != NULL ? (int)disc->reason : -1;
        printf("LAB11_STATION status=disconnected reason=%d ms=%lu\n", s_last_reason,
               (unsigned long)esp_log_timestamp());
        xEventGroupClearBits(s_wifi_events, LAB11_WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, LAB11_WIFI_CONNECTED_BIT);
    }
}

bool lab11_station_join(void)
{
    esp_err_t err;
    wifi_config_t wcfg = {0};
    EventBits_t bits;
    if (strlen(CONFIG_LAB11_WIFI_SSID) == 0 || strlen(CONFIG_LAB11_WIFI_PASSWORD) == 0) {
        return false;
    }
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return false;
    }
    if (!lab11_station_prepare_transport()) {
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
    if (!s_wifi_started) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            return false;
        }
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            return false;
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL);
        if (err != ESP_OK) {
            return false;
        }
        strncpy((char *)wcfg.sta.ssid, CONFIG_LAB11_WIFI_SSID, sizeof(wcfg.sta.ssid) - 1);
        strncpy((char *)wcfg.sta.password, CONFIG_LAB11_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
        wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK
            || esp_wifi_start() != ESP_OK) {
            return false;
        }
        s_wifi_started = true;
    }
    printf("LAB11_STATION status=associating ms=%lu\n", (unsigned long)esp_log_timestamp());
    bits = xEventGroupWaitBits(s_wifi_events, LAB11_WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
    if ((bits & LAB11_WIFI_CONNECTED_BIT) == 0) {
        printf("LAB11_STATION status=join_failed reason=%d ms=%lu\n", s_last_reason,
               (unsigned long)esp_log_timestamp());
        return false;
    }
    if (esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
        return false;
    }
    return true;
}

void lab11_station_set_up(bool up)
{
    s_up = up;
}

bool lab11_station_is_up(void)
{
    return s_up;
}
