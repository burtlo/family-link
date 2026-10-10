/*
 * h07 — GET /v1/me with a good bearer token, then a bad one. HTTP, no TLS.
 * Needs server demo 01 on the LAN (not localhost). secrets.h: DEMO_SERVER_HOST.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "board.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h07";

typedef struct {
    char body[512];
    int len;
} http_buf_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    http_buf_t *buf = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int n = evt->data_len;
        if (buf->len + n > (int)sizeof(buf->body) - 1) {
            n = (int)sizeof(buf->body) - 1 - buf->len;
        }
        if (n > 0) {
            memcpy(buf->body + buf->len, evt->data, n);
            buf->len += n;
            buf->body[buf->len] = 0;
        }
    }
    return ESP_OK;
}

static int get_me(const char *token, http_buf_t *buf)
{
    memset(buf, 0, sizeof(*buf));
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/v1/me", DEMO_SERVER_HOST, DEMO_SERVER_PORT);

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = buf,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s err=%s", url, esp_err_to_name(err));
        return -1;
    }
    return status;
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h07 wifi…");
    }

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h07", "wifi");
        return;
    }

    board_status_set("GET /v1/me");
    http_buf_t buf;
    int good = get_me(DEMO_DEVICE_TOKEN, &buf);
    ESP_LOGI(TAG, "good token status=%d body=%s", good, buf.body);
    if (good != 200) {
        board_status_set("good token not 200\n(is the Mac server on LAN?)");
        demo_fail("h07", "good token");
        return;
    }
    cJSON *json = cJSON_Parse(buf.body);
    const cJSON *id = json ? cJSON_GetObjectItem(json, "device_id") : NULL;
    bool id_ok = cJSON_IsString(id) && id->valuestring && strcmp(id->valuestring, DEMO_DEVICE_ID) == 0;
    cJSON_Delete(json);
    if (!id_ok) {
        demo_fail("h07", "device_id");
        return;
    }

    int bad = get_me("nope", &buf);
    ESP_LOGI(TAG, "bad token status=%d", bad);
    if (bad != 401) {
        demo_fail("h07", "bad token not 401");
        return;
    }

    board_status_set("200 + 401  PASS");
    demo_pass("h07");
}
