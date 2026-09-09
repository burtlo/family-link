/*
 * h24 — Heartbeat like h20, plus a device event log shipped on each beat.
 *
 * The box keeps a small in-RAM ring of timestamped lines (boot, wifi, mute,
 * heartbeat results, button taps). Unsent lines ride on POST /v1/heartbeat;
 * the host acks seq and appends to data/h24_device_log/ on the Mac.
 *
 * RAM-only today — buying SD (see docs/STORAGE.md) would let the ring and
 * ack cursor survive reboot before upload.
 *
 * Host:
 *   python -m demos.server.h24_device_log.server --host 0.0.0.0 --port 8080
 *
 * -- PASS h24 after a 200 heartbeat that acks at least one log line.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "http_bearer.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h24";

#define HB_PERIOD_MS 2000
#define POLL_MS      40
#define LOG_CAP      24
#define LOG_MSG      44
#define LOG_BATCH    8

typedef struct {
    uint32_t seq;
    uint32_t ms;
    char lvl;
    char msg[LOG_MSG];
} log_line_t;

static log_line_t s_lines[LOG_CAP];
static uint8_t s_line_count;
static uint8_t s_line_start;
static uint32_t s_next_seq = 1;
static uint32_t s_ack_seq;
static uint32_t s_boot_id;

static volatile int s_self_open = -1;
static volatile int s_peer_open = -1;
static volatile int s_peer_online = -1;
static volatile bool s_need_hb;
static char s_self_name[24];
static char s_peer_name_txt[24];
static int s_last_peer_open = -2;
static int s_last_peer_online = -2;

static uint8_t s_http_mem[1024];
static bool s_passed;

static int64_t uptime_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static int wifi_rssi(void)
{
    wifi_ap_record_t ap = {0};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return (int)ap.rssi;
    }
    return 0;
}

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_SW:
        return "sw";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "other";
    }
}

static void dlog_push(char lvl, const char *msg)
{
    if (msg == NULL) {
        return;
    }
    if (s_line_count >= LOG_CAP) {
        s_line_start = (uint8_t)((s_line_start + 1) % LOG_CAP);
        s_line_count--;
        ESP_LOGW(TAG, "log ring full; dropped oldest");
    }
    uint8_t idx = (uint8_t)((s_line_start + s_line_count) % LOG_CAP);
    log_line_t *row = &s_lines[idx];
    row->seq = s_next_seq++;
    row->ms = (uint32_t)uptime_ms();
    row->lvl = lvl;
    strncpy(row->msg, msg, LOG_MSG - 1);
    row->msg[LOG_MSG - 1] = 0;
    s_line_count++;
    ESP_LOGI(TAG, "log seq=%u %c %s", (unsigned)row->seq, lvl, row->msg);
}

static void dlog_trim_acked(uint32_t ack)
{
    if (ack <= s_ack_seq) {
        return;
    }
    s_ack_seq = ack;
    while (s_line_count > 0 && s_lines[s_line_start].seq <= s_ack_seq) {
        s_line_start = (uint8_t)((s_line_start + 1) % LOG_CAP);
        s_line_count--;
    }
}

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

static void paint_status(void)
{
    char line[96];
    snprintf(line, sizeof(line), "h24 log\n%s %s\n%s %s\npending %u ack %u",
             s_self_name, open_label(s_self_open, 1), s_peer_name_txt,
             open_label(s_peer_open, s_peer_online), (unsigned)s_line_count,
             (unsigned)s_ack_seq);
    board_status_set(line);
}

static void apply_mute(bool muted)
{
    int open = muted ? 0 : 1;
    if (s_self_open == open) {
        return;
    }
    s_self_open = open;
    s_need_hb = true;
    char note[LOG_MSG];
    snprintf(note, sizeof(note), "mute %s", muted ? "away" : "open");
    dlog_push('I', note);
    paint_status();
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

static void on_circle_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    dlog_push('I', "red circle tap");
    s_need_hb = true;
    paint_status();
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

static bool parse_heartbeat(const char *body, uint32_t *logs_ack_out)
{
    cJSON *j = cJSON_Parse(body);
    if (!j) {
        return false;
    }
    bool good = false;
    cJSON *self = cJSON_GetObjectItem(j, "self");
    cJSON *peer = cJSON_GetObjectItem(j, "peer");
    cJSON *ack = cJSON_GetObjectItem(j, "logs_ack");
    if (cJSON_IsNumber(ack)) {
        *logs_ack_out = (uint32_t)ack->valuedouble;
    }
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
    }
    cJSON_Delete(j);
    return good;
}

static char *build_heartbeat_json(bool muted)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddBoolToObject(root, "available", !muted);
    cJSON_AddNumberToObject(root, "boot_id", (double)s_boot_id);
    cJSON *arr = cJSON_AddArrayToObject(root, "logs");
    if (arr == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    int sent = 0;
    for (uint8_t i = 0; i < s_line_count && sent < LOG_BATCH; i++) {
        uint8_t idx = (uint8_t)((s_line_start + i) % LOG_CAP);
        log_line_t *row = &s_lines[idx];
        if (row->seq <= s_ack_seq) {
            continue;
        }
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            break;
        }
        cJSON_AddNumberToObject(item, "seq", (double)row->seq);
        cJSON_AddNumberToObject(item, "ms", (double)row->ms);
        char lvl[2] = {row->lvl, 0};
        cJSON_AddStringToObject(item, "lvl", lvl);
        cJSON_AddStringToObject(item, "msg", row->msg);
        cJSON_AddItemToArray(arr, item);
        sent++;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
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
            char *json = build_heartbeat_json(muted);
            if (json == NULL) {
                dlog_push('E', "json oom");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            int hb = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "POST", "/v1/heartbeat",
                                    DEMO_DEVICE_TOKEN, json, &body, 5000);
            cJSON_free(json);
            uint32_t ack = s_ack_seq;
            if (hb == 200 && parse_heartbeat((char *)body.buf, &ack)) {
                dlog_trim_acked(ack);
                if (s_peer_open != s_last_peer_open || s_peer_online != s_last_peer_online) {
                    char note[LOG_MSG];
                    snprintf(note, sizeof(note), "peer %s",
                             open_label(s_peer_open, s_peer_online));
                    dlog_push('I', note);
                    s_last_peer_open = s_peer_open;
                    s_last_peer_online = s_peer_online;
                }
                if (!s_passed && ack > 0) {
                    s_passed = true;
                    demo_pass("h24");
                }
            } else {
                char note[LOG_MSG];
                snprintf(note, sizeof(note), "hb fail status=%d", hb);
                dlog_push('W', note);
                ESP_LOGW(TAG,
                         "heartbeat failed; run: python -m demos.server.h24_device_log.server "
                         "--host 0.0.0.0 --port 8080");
            }
            paint_status();
            last_hb = xTaskGetTickCount();
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void app_main(void)
{
    s_boot_id = esp_random() ^ (uint32_t)esp_timer_get_time();

    if (board_display_start() == ESP_OK) {
        board_status_set("h24 wifi…");
    }

    char boot_note[LOG_MSG];
    snprintf(boot_note, sizeof(boot_note), "boot %s id=%08x",
             reset_reason_str(esp_reset_reason()), (unsigned)s_boot_id);
    dlog_push('I', boot_note);

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        dlog_push('E', "wifi fail");
        board_status_set("wifi fail");
        demo_fail("h24", "wifi");
        return;
    }

    char wifi_note[LOG_MSG];
    snprintf(wifi_note, sizeof(wifi_note), "wifi ok rssi=%d heap=%u", wifi_rssi(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    dlog_push('I', wifi_note);

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }
    if (btns[BSP_BUTTON_MUTE]) {
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    }
    if (btns[BSP_BUTTON_MAIN]) {
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, on_circle_up, NULL);
    }

    apply_mute(mute_latched());
    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name_txt, sizeof(s_peer_name_txt), DEMO_PEER_NAME);
    paint_status();
    ESP_LOGI(TAG, "who id=%s name=%s friend=%s boot=%08x", DEMO_DEVICE_ID, DEMO_DEVICE_NAME,
             DEMO_PEER_NAME, (unsigned)s_boot_id);

    xTaskCreate(heartbeat_task, "hb", 8192, NULL, 5, NULL);

    while (1) {
        apply_mute(mute_latched());
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
