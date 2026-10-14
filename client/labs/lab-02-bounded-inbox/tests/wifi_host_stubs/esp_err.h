#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL 0x100
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_WIFI_CONN 0x3007

static inline const char *esp_err_to_name(esp_err_t err)
{
    (void)err;
    return "stub-error";
}
