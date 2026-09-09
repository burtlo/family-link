/*
 * h20 — Mute latch as open/away. Heartbeat shares our state; the response
 * carries the friend's last known state (Mazi ↔ Arlo).
 *
 * Mute down (red LED on)  → available=false (away). Analog mics are dead.
 * Mute up  (red LED off)  → available=true  (open).
 * Our line follows the mute key immediately. The friend line follows
 * heartbeats (name is also baked in at flash via WHO=).
 *
 * Host (leave running — this demo, not combined):
 *   python -m demos.server.h20_presence.server --host 0.0.0.0 --port 8080
 *
 *   make flash DEMO=h20 WHO=mazi
 *   make flash DEMO=h20 WHO=arlo
 * (PORT= only the first time if both kits are plugged in.)
 * -- PASS h20 after a 200 that includes a peer object.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "http_bearer.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h20";

#define HB_PERIOD_MS 1000
#define POLL_MS      40

static lv_obj_t *s_you_name;
static lv_obj_t *s_you_state;
static lv_obj_t *s_peer_name;
static lv_obj_t *s_peer_state;

static char s_self_name[24];
static char s_peer_name_txt[24];
static volatile int s_self_open = -1; /* -1 unknown, 0 away, 1 open */
static volatile int s_peer_open = -1;
static volatile int s_peer_online = -1;
static volatile bool s_need_hb;

static uint8_t s_http_mem[768];
static bool s_passed;

static bool mute_latched(void)
{
    /* Active-low. 0 = mute engaged = away. */
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

/* ASCII only. Unicode ellipsis is missing from default Montserrat 14 and
 * paints as an empty rectangle. */
static const char *open_label(int open, int online)
{
    if (online == 0) {
        return "offline";
    }
    if (open < 0) {
        return "waiting";
    }
    return open ? "open" : "away";
}

static uint32_t open_color(int open, int online)
{
    if (online == 0) {
        return 0x888888;
    }
    if (open == 1) {
        return 0x7DCC7A;
    }
    if (open == 0) {
        return 0xCC8844;
    }
    return 0xA8B0B8;
}

static void make_card(lv_obj_t *scr, int y, lv_obj_t **name_out, lv_obj_t **state_out,
                      const char *name, const char *state)
{
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 296, 72);
    lv_obj_set_pos(card, 12, y);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    lv_obj_t *nm = lv_label_create(card);
    lv_obj_set_width(nm, 272);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_WRAP);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_opa(nm, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 12, 10);

    lv_obj_t *st = lv_label_create(card);
    lv_label_set_text(st, state);
    lv_obj_set_style_text_opa(st, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(st, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    *name_out = nm;
    *state_out = st;
}

static void paint_idle(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "open line");
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A9298), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    make_card(scr, 32, &s_you_name, &s_you_state, "you", "waiting");
    make_card(scr, 112, &s_peer_name, &s_peer_state, DEMO_PEER_NAME, "waiting");

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "mute = away");
    lv_obj_set_style_text_opa(hint, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    board_lvgl_unlock();
}

static void refresh_ui(void)
{
    char you[48];
    char peer[48];
    int self_open = s_self_open;
    int peer_open = s_peer_open;
    int peer_online = s_peer_online;

    snprintf(you, sizeof(you), "%s (you)", s_self_name);
    snprintf(peer, sizeof(peer), "%s", s_peer_name_txt);

    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_you_name) {
        lv_label_set_text(s_you_name, you);
    }
    if (s_you_state) {
        lv_label_set_text(s_you_state, open_label(self_open, 1));
        lv_obj_set_style_text_color(s_you_state, lv_color_hex(open_color(self_open, 1)), 0);
    }
    if (s_peer_name) {
        lv_label_set_text(s_peer_name, peer);
    }
    if (s_peer_state) {
        lv_label_set_text(s_peer_state, open_label(peer_open, peer_online));
        lv_obj_set_style_text_color(
            s_peer_state, lv_color_hex(open_color(peer_open, peer_online)), 0);
    }
    board_lvgl_unlock();
}

static void apply_mute(bool muted)
{
    int open = muted ? 0 : 1;
    if (s_self_open == open) {
        return;
    }
    s_self_open = open;
    s_need_hb = true;
    ESP_LOGI(TAG, "mute GPIO%d -> %s", (int)BSP_MUTE_STATUS, muted ? "away" : "open");
    refresh_ui();
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    apply_mute(true);
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    apply_mute(false);
}

static void copy_name(char *dst, size_t cap, cJSON *obj)
{
    cJSON *name = cJSON_GetObjectItem(obj, "name");
    if (cJSON_IsString(name) && name->valuestring && name->valuestring[0]) {
        strncpy(dst, name->valuestring, cap - 1);
        dst[cap - 1] = 0;
    }
}

static int json_tristate(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v || cJSON_IsNull(v)) {
        return -1;
    }
    if (cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint)) {
        return 1;
    }
    if (cJSON_IsFalse(v) || cJSON_IsNumber(v)) {
        return 0;
    }
    return -1;
}

static bool parse_heartbeat(const char *body)
{
    cJSON *j = cJSON_Parse(body);
    if (!j) {
        ESP_LOGW(TAG, "heartbeat JSON parse failed");
        return false;
    }
    bool good = false;
    cJSON *self = cJSON_GetObjectItem(j, "self");
    cJSON *peer = cJSON_GetObjectItem(j, "peer");
    if (cJSON_IsObject(self)) {
        copy_name(s_self_name, sizeof(s_self_name), self);
    }
    if (cJSON_IsObject(peer)) {
        copy_name(s_peer_name_txt, sizeof(s_peer_name_txt), peer);
        s_peer_open = json_tristate(peer, "available");
        s_peer_online = json_tristate(peer, "online");
        if (s_peer_online < 0) {
            s_peer_online = (s_peer_open >= 0) ? 1 : 0;
        }
        good = true;
    } else {
        int online = json_tristate(j, "peer_online");
        if (online >= 0) {
            s_peer_online = online;
            if (online == 0) {
                s_peer_open = -1;
            }
            good = true;
        }
    }
    cJSON_Delete(j);
    return good;
}

static void heartbeat_task(void *arg)
{
    (void)arg;
    http_buf_t body = {.buf = s_http_mem, .cap = sizeof(s_http_mem)};
    TickType_t last_hb = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        bool due = (last_hb == 0) || (now - last_hb) >= pdMS_TO_TICKS(HB_PERIOD_MS);
        if (s_need_hb || due) {
            s_need_hb = false;
            bool muted = mute_latched();
            char js[32];
            snprintf(js, sizeof(js), "{\"available\":%s}", muted ? "false" : "true");
            int hb = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "POST", "/v1/heartbeat",
                                    DEMO_DEVICE_TOKEN, js, &body, 4000);
            ESP_LOGI(TAG, "hb muted=%d status=%d %s", (int)muted, hb, (char *)body.buf);
            if (hb == 200 && parse_heartbeat((char *)body.buf)) {
                if (!s_passed) {
                    s_passed = true;
                    demo_pass("h20");
                }
                refresh_ui();
            } else if (hb != 200) {
                ESP_LOGW(TAG,
                         "heartbeat failed; run: python -m demos.server.h20_presence.server "
                         "--host 0.0.0.0 --port 8080");
            }
            last_hb = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h20 wifi...");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        board_status_set("wifi fail");
        demo_fail("h20", "wifi");
        return;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }
    if (btns[BSP_BUTTON_MUTE]) {
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    }

    paint_idle();
    board_backlight_set(70);
    apply_mute(mute_latched());
    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name_txt, sizeof(s_peer_name_txt), DEMO_PEER_NAME);
    refresh_ui();
    ESP_LOGI(TAG, "who id=%s name=%s friend=%s", DEMO_DEVICE_ID, DEMO_DEVICE_NAME, DEMO_PEER_NAME);

    xTaskCreate(heartbeat_task, "hb", 8192, NULL, 5, NULL);

    while (1) {
        apply_mute(mute_latched());
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
