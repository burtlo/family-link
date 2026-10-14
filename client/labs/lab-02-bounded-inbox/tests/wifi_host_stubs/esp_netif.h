#pragma once

#include "esp_err.h"

typedef struct esp_netif_stub esp_netif_t;

esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
