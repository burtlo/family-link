#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Join 2.4 GHz STA using baked SSID/password. Blocks until IP or timeout. */
esp_err_t wifi_sta_join(const char *ssid, const char *password, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
