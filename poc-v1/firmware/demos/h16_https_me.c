/*
 * h16 — GET /v1/me with a good bearer token, then a bad one. HTTPS, skip-verify.
 * Same contract as h07, URL is https://DEMO_SERVER_HOST:DEMO_SERVER_PORT.
 * LAN demo only: no CA bundle, no SNTP. UART logs skip-verify.
 * Needs scripts/dev_https.py (or 01_auth --ssl-certfile) on the LAN.
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

static const char *TAG = "h16";

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

/* Returns HTTP status, or -1 on TLS/transport error (reason in err_out). */
static int get_me(const char *token, http_buf_t *buf, char *err_out, size_t err_len)
{
    memset(buf, 0, sizeof(*buf));
    if (err_out && err_len) {
        err_out[0] = 0;
    }
    char url[128];
    snprintf(url, sizeof(url), "https://%s:%d/v1/me", DEMO_SERVER_HOST, DEMO_SERVER_PORT);

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = buf,
        .timeout_ms = 8000,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = NULL, /* skip-verify: no CA (LAN demo only) */
        .skip_cert_common_name_check = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        if (err_out && err_len) {
            snprintf(err_out, err_len, "http init");
        }
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GET %s err=%s", url, esp_err_to_name(err));
        if (err_out && err_len) {
            snprintf(err_out, err_len, "%s", esp_err_to_name(err));
        }
        return -1;
    }
    return status;
}

void app_main(void)
{
    printf("h16 skip-verify (LAN demo; no CA / SNTP)\n");
    fflush(stdout);
    ESP_LOGW(TAG, "HTTPS skip-verify: crt_bundle_attach=NULL, CONFIG_ESP_TLS_INSECURE");

    if (board_display_start() == ESP_OK) {
        board_status_set("h16 wifi…");
    }

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h16", "wifi");
        return;
    }

    board_status_set("HTTPS GET /v1/me");
    http_buf_t buf;
    char https_err[48];
    int good = get_me(DEMO_DEVICE_TOKEN, &buf, https_err, sizeof(https_err));
    ESP_LOGI(TAG, "good token status=%d body=%s", good, buf.body);
    if (good < 0) {
        board_status_set("https fail");
        demo_fail("h16", https_err[0] ? https_err : "https");
        return;
    }
    if (good != 200) {
        board_status_set("good token not 200\n(HTTPS server on LAN?)");
        demo_fail("h16", "good token");
        return;
    }
    cJSON *json = cJSON_Parse(buf.body);
    const cJSON *id = json ? cJSON_GetObjectItem(json, "device_id") : NULL;
    bool id_ok = cJSON_IsString(id) && id->valuestring && strcmp(id->valuestring, DEMO_DEVICE_ID) == 0;
    cJSON_Delete(json);
    if (!id_ok) {
        demo_fail("h16", "device_id");
        return;
    }

    int bad = get_me("nope", &buf, https_err, sizeof(https_err));
    ESP_LOGI(TAG, "bad token status=%d", bad);
    if (bad < 0) {
        board_status_set("https fail");
        demo_fail("h16", https_err[0] ? https_err : "https");
        return;
    }
    if (bad != 401) {
        demo_fail("h16", "bad token not 401");
        return;
    }

    board_status_set("200 + 401  PASS");
    demo_pass("h16");
}
