#include "http_bearer.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "http_bearer";

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    http_buf_t *b = evt->user_data;
    if (b == NULL || b->buf == NULL) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int n = evt->data_len;
        if (b->len + n > b->cap) {
            n = b->cap - b->len;
        }
        if (n > 0) {
            memcpy(b->buf + b->len, evt->data, n);
            b->len += n;
        }
    }
    return ESP_OK;
}

int http_bearer_do(const char *host, int port, const char *method, const char *path,
                   const char *token, const char *json, http_buf_t *out, int timeout_ms)
{
    if (out) {
        out->len = 0;
        if (out->buf && out->cap > 0) {
            out->buf[0] = 0;
        }
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", host ? host : "", port, path ? path : "/");

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = out,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 8000,
        .buffer_size = 2048,
    };
    if (method && strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    } else if (method && strcmp(method, "PUT") == 0) {
        cfg.method = HTTP_METHOD_PUT;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }

    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", token ? token : "");
    esp_http_client_set_header(client, "Authorization", auth);
    if (json) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json, (int)strlen(json));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s %s err=%s", method ? method : "?", url, esp_err_to_name(err));
        return -1;
    }
    if (out && out->buf && out->len < out->cap) {
        out->buf[out->len] = 0;
    }
    return status;
}
