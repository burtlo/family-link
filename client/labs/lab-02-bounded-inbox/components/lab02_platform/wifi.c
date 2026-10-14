#include "lab02_platform/wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "lab02_wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_wifi_sta_netif;
static lab02_wifi_transport_fn s_transport_notifier;
static bool s_wifi_initialized;
static bool s_wifi_started;
static bool s_handlers_registered;
static esp_event_handler_instance_t s_wifi_event_handler;
static esp_event_handler_instance_t s_ip_event_handler;

void lab02_board_wifi_set_transport_notifier(lab02_wifi_transport_fn fn)
{
    s_transport_notifier = fn;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_transport_notifier) {
            s_transport_notifier(false);
        }
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_transport_notifier) {
            s_transport_notifier(true);
        }
    }
}

bool lab02_board_wifi_connect(void)
{
    if (strlen(CONFIG_LAB02_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "LAB02_WIFI_SSID is empty");
        return false;
    }
    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            ESP_LOGE(TAG, "xEventGroupCreate failed");
            return false;
        }
    }
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
        return false;
    }
    if (s_wifi_sta_netif == NULL) {
        s_wifi_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_wifi_sta_netif == NULL) {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
            return false;
        }
    }
    if (!s_wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
            return false;
        }
        s_wifi_initialized = true;
    }
    if (!s_handlers_registered) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  on_wifi_event, NULL, &s_wifi_event_handler);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi event handler registration: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  on_wifi_event, NULL, &s_ip_event_handler);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "IP event handler registration: %s", esp_err_to_name(err));
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  s_wifi_event_handler);
            s_wifi_event_handler = NULL;
            return false;
        }
        s_handlers_registered = true;
    }
    bool started_now = false;
    if (!s_wifi_started) {
        wifi_config_t wcfg = {0};
        strncpy((char *)wcfg.sta.ssid, CONFIG_LAB02_WIFI_SSID, sizeof(wcfg.sta.ssid) - 1);
        strncpy((char *)wcfg.sta.password, CONFIG_LAB02_WIFI_PASSWORD, sizeof(wcfg.sta.password) - 1);
        wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_set_mode: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_set_config: %s", esp_err_to_name(err));
            return false;
        }
        err = esp_wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
            return false;
        }
        s_wifi_started = true;
        started_now = true;
    }
    if (!started_now) {
        EventBits_t current = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, 0);
        if ((current & WIFI_CONNECTED_BIT) == 0) {
            err = esp_wifi_connect();
            if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
                ESP_LOGE(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
                return false;
            }
        }
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "Wi-Fi connect timeout");
        return false;
    }
    ESP_LOGI(TAG, "Wi-Fi connected");
    return true;
}
