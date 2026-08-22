/*
 * h13 — Pull an inbox image preview (320×240 RGB565) and paint it on the LCD.
 * Combined server should already have kind=image in box-a inbox, or this polls.
 * Preview is the product path (server downscales). No JPEG decoder in this demo.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "board.h"
#include "http_bearer.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h13";
#define PREVIEW_BYTES (320 * 240 * 2)

static uint8_t *s_rgb;
static lv_image_dsc_t s_dsc;
static uint8_t s_json_mem[8192];

static int http_get(const char *path, http_buf_t *body, int timeout_ms)
{
    return http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET", path,
                          DEMO_DEVICE_TOKEN, NULL, body, timeout_ms);
}

static cJSON *messages_array(cJSON *root)
{
    if (cJSON_IsArray(root)) {
        return root;
    }
    if (root) {
        cJSON *arr = cJSON_GetObjectItem(root, "messages");
        if (cJSON_IsArray(arr)) {
            return arr;
        }
    }
    return NULL;
}

static bool kind_is_image(const char *kind)
{
    return kind && (strcmp(kind, "image") == 0 || strcmp(kind, "photo") == 0);
}

static int find_image_seq(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    cJSON *arr = messages_array(root);
    int seq = -1;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *kind = cJSON_GetObjectItem(it, "kind");
        cJSON *s = cJSON_GetObjectItem(it, "seq");
        if (cJSON_IsString(kind) && kind_is_image(kind->valuestring) && cJSON_IsNumber(s)) {
            seq = s->valueint;
        }
    }
    cJSON_Delete(root);
    return seq;
}

static bool looks_jpeg(const uint8_t *p, int n)
{
    return n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF;
}

static void paint_preview(void)
{
    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = 320;
    s_dsc.header.h = 240;
    s_dsc.header.stride = 640;
    s_dsc.data_size = PREVIEW_BYTES;
    s_dsc.data = s_rgb;

    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &s_dsc);
    lv_obj_center(img);
    board_lvgl_unlock();
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("waiting for photo");
    }

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h13", "wifi");
        return;
    }

    s_rgb = heap_caps_malloc(PREVIEW_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rgb == NULL) {
        s_rgb = heap_caps_malloc(PREVIEW_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_rgb == NULL) {
        board_status_set("OOM (need PSRAM)");
        demo_fail("h13", "OOM");
        return;
    }

    http_buf_t json = { .buf = s_json_mem, .cap = sizeof(s_json_mem) };
    http_buf_t preview = { .buf = s_rgb, .cap = PREVIEW_BYTES };

    int seq = -1;
    for (int i = 0; i < 30; i++) {
        int st = http_get("/v1/messages", &json, 8000);
        ESP_LOGI(TAG, "list status=%d body=%.*s", st, json.len, (char *)json.buf);
        if (st == 200) {
            seq = find_image_seq((char *)json.buf);
        }
        if (seq >= 0) {
            break;
        }
        board_status_set("waiting for photo");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    if (seq < 0) {
        board_status_set("no photo in inbox");
        demo_fail("h13", "no photo");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), "/v1/messages/%d/preview", seq);
    board_status_set("downloading preview…");
    int st = http_get(path, &preview, 20000);
    ESP_LOGI(TAG, "preview status=%d bytes=%d", st, preview.len);

    if (st == 404) {
        snprintf(path, sizeof(path), "/v1/messages/%d/blob", seq);
        board_status_set("preview 404, trying blob");
        st = http_get(path, &preview, 20000);
        ESP_LOGI(TAG, "blob status=%d bytes=%d", st, preview.len);
        if (st == 200 && looks_jpeg(s_rgb, preview.len)) {
            board_status_set("JPEG blob — no decoder\nuse /preview RGB565");
            demo_fail("h13", "no jpeg decoder");
            return;
        }
        if (st != 200 || preview.len != PREVIEW_BYTES) {
            board_status_set("no RGB565 preview\n(server should downscale)");
            demo_fail("h13", "preview");
            return;
        }
    } else if (st != 200 || preview.len != PREVIEW_BYTES) {
        board_status_set("preview not 320x240 RGB565");
        demo_fail("h13", "preview");
        return;
    }

    /* Keep s_rgb alive for the image descriptor. Do not free. */
    paint_preview();
    ESP_LOGI(TAG, "painted seq=%d %d bytes", seq, PREVIEW_BYTES);
    demo_pass("h13");
}
