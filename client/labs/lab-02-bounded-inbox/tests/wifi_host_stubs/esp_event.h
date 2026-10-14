#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *data);
typedef void *esp_event_handler_instance_t;

extern const char wifi_event_base[];
extern const char ip_event_base[];
#define WIFI_EVENT wifi_event_base
#define IP_EVENT ip_event_base
#define ESP_EVENT_ANY_ID (-1)
#define WIFI_EVENT_STA_START 0
#define WIFI_EVENT_STA_DISCONNECTED 1
#define IP_EVENT_STA_GOT_IP 2

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base, int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base, int32_t event_id,
                                                esp_event_handler_instance_t instance);
