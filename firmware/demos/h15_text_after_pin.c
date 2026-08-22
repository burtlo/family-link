/*
 * h15 — PIN pad (h03) then show the latest inbox text. While locked, only
 * “N new” — never the body. Wrong PIN flashes “wrong” and does not leak.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
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

#ifndef DEMO_PIN
#define DEMO_PIN "1234"
#endif

static const char *TAG = "h15";
#define ENTRY_MAX 8
#define JSON_CAP  8192

static char s_entry[ENTRY_MAX + 1];
static size_t s_len;
static volatile bool s_unlocked;
static bool s_passed;
static lv_obj_t *s_status;
static lv_obj_t *s_dots;
static lv_obj_t *s_badge;
static uint8_t s_json_mem[JSON_CAP];

static void set_status(const char *text, uint32_t color)
{
    if (s_status == NULL) {
        return;
    }
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
}

static void refresh_dots(void)
{
    char dots[ENTRY_MAX + 1];
    memset(dots, '*', s_len);
    dots[s_len] = '\0';
    lv_label_set_text(s_dots, s_len ? dots : " ");
}

static void on_key(lv_event_t *e)
{
    if (s_unlocked) {
        return;
    }

    const char *key = lv_event_get_user_data(e);
    if (key == NULL) {
        return;
    }

    if (strcmp(key, "Clear") == 0) {
        s_len = 0;
        s_entry[0] = '\0';
        set_status("locked", 0xcccccc);
        refresh_dots();
        return;
    }

    if (s_len >= ENTRY_MAX) {
        return;
    }
    s_entry[s_len++] = key[0];
    s_entry[s_len] = '\0';
    refresh_dots();

    size_t pin_len = strlen(DEMO_PIN);
    if (s_len < pin_len) {
        set_status("locked", 0xcccccc);
        return;
    }

    if (strcmp(s_entry, DEMO_PIN) == 0) {
        s_unlocked = true;
        set_status("unlocked", 0x55dd88);
        return;
    }

    s_len = 0;
    s_entry[0] = '\0';
    set_status("wrong", 0xff5555);
    refresh_dots();
}

static void make_key(lv_obj_t *parent, const char *label, int x, int y, int w, int h)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, label);
    lv_obj_center(txt);
    lv_obj_add_event_cb(btn, on_key, LV_EVENT_CLICKED, (void *)label);
}

static void paint_pad(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    s_status = lv_label_create(scr);
    set_status("locked", 0xcccccc);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 6);

    s_dots = lv_label_create(scr);
    lv_label_set_text(s_dots, " ");
    lv_obj_set_style_text_color(s_dots, lv_color_white(), 0);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 28);

    s_badge = lv_label_create(scr);
    lv_label_set_text(s_badge, "");
    lv_obj_set_style_text_color(s_badge, lv_color_hex(0x88aacc), 0);
    lv_obj_align(s_badge, LV_ALIGN_TOP_RIGHT, -8, 6);

    const int ox = 8;
    const int oy = 52;
    const int gap = 6;
    const int bw = 98;
    const int bh = 42;
    static const char *const digits[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9",
    };
    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        make_key(scr, digits[i], ox + col * (bw + gap), oy + row * (bh + gap), bw, bh);
    }
    make_key(scr, "Clear", ox, oy + 3 * (bh + gap), bw * 2 + gap, bh);
    make_key(scr, "0", ox + 2 * (bw + gap), oy + 3 * (bh + gap), bw, bh);
}

static void show_body(const char *line)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    s_status = NULL;
    s_dots = NULL;
    s_badge = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_t *lab = lv_label_create(scr);
    lv_obj_set_width(lab, 300);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xE8F0E8), 0);
    lv_label_set_text(lab, line ? line : "");
    lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);
    board_lvgl_unlock();
}

static void set_badge_count(int n)
{
    if (!board_lvgl_lock(200) || s_badge == NULL) {
        return;
    }
    if (n > 0) {
        char line[24];
        snprintf(line, sizeof(line), "%d new", n);
        lv_label_set_text(s_badge, line);
    } else {
        lv_label_set_text(s_badge, "");
    }
    board_lvgl_unlock();
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

static int http_get_messages(http_buf_t *body)
{
    return http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET", "/v1/messages",
                          DEMO_DEVICE_TOKEN, NULL, body, 8000);
}

/* Latest text body into out. Returns inbox length. kind_out is latest non-text. */
static int parse_inbox(const char *json, char *text_out, int text_cap, char *kind_out, int kind_cap)
{
    if (text_out && text_cap > 0) {
        text_out[0] = 0;
    }
    if (kind_out && kind_cap > 0) {
        kind_out[0] = 0;
    }
    cJSON *root = cJSON_Parse(json);
    cJSON *arr = messages_array(root);
    int n = cJSON_GetArraySize(arr);
    int best_text_seq = -1;
    int best_other_seq = -1;
    const char *best_text = NULL;
    const char *best_other = NULL;
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(arr, i);
        cJSON *kind = cJSON_GetObjectItem(it, "kind");
        cJSON *s = cJSON_GetObjectItem(it, "seq");
        cJSON *text = cJSON_GetObjectItem(it, "text");
        int seq = cJSON_IsNumber(s) ? s->valueint : i;
        const char *k = cJSON_IsString(kind) ? kind->valuestring : "";
        if (strcmp(k, "text") == 0 && cJSON_IsString(text) && text->valuestring) {
            if (seq >= best_text_seq) {
                best_text_seq = seq;
                best_text = text->valuestring;
            }
        } else if (k[0]) {
            if (seq >= best_other_seq) {
                best_other_seq = seq;
                best_other = k;
            }
        }
    }
    if (best_text && text_out) {
        strncpy(text_out, best_text, text_cap - 1);
        text_out[text_cap - 1] = 0;
    }
    if (best_other && kind_out) {
        strncpy(kind_out, best_other, kind_cap - 1);
        kind_out[kind_cap - 1] = 0;
    }
    cJSON_Delete(root);
    return n;
}

static void inbox_task(void *arg)
{
    (void)arg;
    http_buf_t body = { .buf = s_json_mem, .cap = sizeof(s_json_mem) };
    char text[512];
    char kind[16];

    while (!s_unlocked) {
        int st = http_get_messages(&body);
        if (st == 200) {
            int n = parse_inbox((char *)body.buf, NULL, 0, NULL, 0);
            set_badge_count(n);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(120000);
    while (xTaskGetTickCount() < deadline) {
        int st = http_get_messages(&body);
        ESP_LOGI(TAG, "unlocked list status=%d %.*s", st, body.len, (char *)body.buf);
        if (st == 200) {
            int n = parse_inbox((char *)body.buf, text, sizeof(text), kind, sizeof(kind));
            if (text[0]) {
                show_body(text);
                if (!s_passed) {
                    s_passed = true;
                    demo_pass("h15");
                }
                vTaskDelete(NULL);
                return;
            }
            if (strcmp(kind, "image") == 0 || strcmp(kind, "photo") == 0) {
                show_body("1 photo");
            } else if (strcmp(kind, "audio") == 0) {
                show_body("1 audio");
            } else if (n == 0) {
                show_body("waiting for text…");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    show_body("empty inbox");
    demo_fail("h15", "empty inbox");
    vTaskDelete(NULL);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h15", "display");
        return;
    }
    board_status_set("h15 wifi…");

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h15", "wifi");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("h15", "lvgl lock");
        return;
    }
    paint_pad();
    board_lvgl_unlock();

    xTaskCreate(inbox_task, "inbox", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "locked: badge only. unlock with DEMO_PIN then latest text.");
}
