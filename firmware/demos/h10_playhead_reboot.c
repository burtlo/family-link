/*
 * h10 — Playhead lives on the server (demo 04). Device RAM dies on reset.
 * First boot: seed 3 texts if needed, "play" 1–2, PUT playhead 2, wait for Reset.
 * Second boot: unread is seq 3 only → -- PASS h10. Playhead is not in NVS.
 *
 * Run server 04 with a long TTL:  FAMILY_TTL_S=3600  (default 2s expires).
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "board.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEMO_PEER_TOKEN
#define DEMO_PEER_TOKEN "change-me-b"
#endif

static const char *TAG = "h10";

typedef struct {
    char body[2048];
    int len;
} buf_t;

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    buf_t *b = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int n = evt->data_len;
        if (b->len + n > (int)sizeof(b->body) - 1) {
            n = (int)sizeof(b->body) - 1 - b->len;
        }
        if (n > 0) {
            memcpy(b->body + b->len, evt->data, n);
            b->len += n;
            b->body[b->len] = 0;
        }
    }
    return ESP_OK;
}

static int http_do(const char *method, const char *path, const char *token, const char *json, buf_t *out)
{
    memset(out, 0, sizeof(*out));
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = out,
        .timeout_ms = 8000,
    };
    if (strcmp(method, "PUT") == 0) {
        cfg.method = HTTP_METHOD_PUT;
    } else if (strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    }
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", token);
    esp_http_client_set_header(client, "Authorization", auth);
    if (json) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json, (int)strlen(json));
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return -1;
    }
    return status;
}

static int get_playhead(void)
{
    buf_t b;
    if (http_do("GET", "/v1/me", DEMO_DEVICE_TOKEN, NULL, &b) != 200) {
        return -1;
    }
    cJSON *j = cJSON_Parse(b.body);
    cJSON *ph = j ? cJSON_GetObjectItem(j, "playhead") : NULL;
    int v = cJSON_IsNumber(ph) ? ph->valueint : -1;
    cJSON_Delete(j);
    return v;
}

static int count_messages(void)
{
    buf_t b;
    if (http_do("GET", "/v1/messages", DEMO_DEVICE_TOKEN, NULL, &b) != 200) {
        return -1;
    }
    cJSON *j = cJSON_Parse(b.body);
    cJSON *arr = j ? cJSON_GetObjectItem(j, "messages") : NULL;
    int n = cJSON_GetArraySize(arr);
    ESP_LOGI(TAG, "inbox %s", b.body);
    cJSON_Delete(j);
    return n;
}

static bool seed_three(void)
{
    const char *texts[] = {
        "{\"kind\":\"text\",\"text\":\"one\"}",
        "{\"kind\":\"text\",\"text\":\"two\"}",
        "{\"kind\":\"text\",\"text\":\"three\"}",
    };
    for (int i = 0; i < 3; i++) {
        buf_t b;
        int st = http_do("POST", "/v1/messages", DEMO_PEER_TOKEN, texts[i], &b);
        ESP_LOGI(TAG, "seed %d status=%d %s", i + 1, st, b.body);
        if (st != 200) {
            return false;
        }
    }
    return true;
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h10 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h10", "wifi");
        return;
    }

    /* Prove playhead is not sitting in our NVS: we never write it. */
    nvs_handle_t nv;
    if (nvs_open("h10", NVS_READONLY, &nv) == ESP_OK) {
        nvs_close(nv);
    }

    board_status_set("talking to server 04\nuse a long TTL");
    int playhead = get_playhead();
    int unread = count_messages();
    ESP_LOGI(TAG, "boot playhead=%d unread=%d (not from NVS)", playhead, unread);

    if (playhead >= 2 && unread == 1) {
        board_status_set("unread=1 after reboot\nPASS");
        demo_pass("h10");
        return;
    }

    if (unread < 3) {
        board_status_set("seeding 3 texts as peer");
        if (!seed_three()) {
            demo_fail("h10", "seed");
            return;
        }
        unread = count_messages();
    }

    ESP_LOGI(TAG, "playing seq 1 (server still has it)");
    ESP_LOGI(TAG, "playing seq 2 (server still has it)");
    buf_t b;
    int st = http_do("PUT", "/v1/playhead", DEMO_DEVICE_TOKEN, "{\"seq\":2}", &b);
    ESP_LOGI(TAG, "PUT playhead 2 status=%d %s", st, b.body);
    if (st != 200) {
        demo_fail("h10", "PUT playhead");
        return;
    }

    board_status_set("playhead=2 on server\ntap Reset on the box");
    ESP_LOGI(TAG, "tap Reset. Playhead is NOT in device NVS. After reboot unread should be 1.");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
