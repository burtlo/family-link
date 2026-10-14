#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_event.h"

typedef enum {
    WIFI_IF_STA = 0,
} wifi_interface_t;

typedef enum {
    WIFI_MODE_STA = 1,
} wifi_mode_t;

typedef struct {
    int authmode;
} wifi_auth_threshold_t;

typedef struct {
    uint8_t ssid[33];
    uint8_t password[65];
    wifi_auth_threshold_t threshold;
} wifi_sta_config_t;

typedef struct {
    wifi_sta_config_t sta;
} wifi_config_t;

typedef struct {
    int unused;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_AUTH_WPA2_PSK 1

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
