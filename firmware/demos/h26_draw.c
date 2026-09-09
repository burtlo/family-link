/*
 * h26 — Shared drawing between two boxes (Mazi ↔ Arlo).
 *
 * Both screens start black. Finger on the glass sends 320×240 touch
 * coordinates through the server; the friend paints the same stroke.
 * Draw a heart here, it appears over there. Either kit can ink.
 *
 * Boot (GPIO0) clears both canvases. No mute, no audio.
 *
 * Host (leave running):
 *   python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080
 *
 *   make flash DEMO=h26 WHO=mazi
 *   make flash DEMO=h26 WHO=arlo
 * -- PASS h26 after the first stroke from the friend is painted.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h26";

#define W          320
#define H          240
#define FB_BYTES   (W * H * 2)
#define BRUSH      2
#define QLEN       64
#define MIN_MOVE   2

#define COL_BG     0x000000
#define COL_YOU    0xE8F0E8
#define COL_FRIEND 0x5AA0E8
#define COL_DIM    0x666666
#define COL_HERE   0x7DCC7A
#define COL_WAIT   0x888888

enum {
    PH_DOWN = 0,
    PH_MOVE = 1,
    PH_UP = 2,
    PH_CLEAR = 3,
};

typedef struct {
    uint8_t phase;
    int16_t x;
    int16_t y;
} ev_t;

static const char *s_phase_name[] = {"down", "move", "up"};

static uint16_t *s_fb;
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_img;
static lv_obj_t *s_banner;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;

static esp_websocket_client_handle_t s_ws;
static QueueHandle_t s_txq;
static QueueHandle_t s_rxq;

static volatile int s_peer_online = -1;
static volatile int s_ws_ok; /* -1 connecting, 0 down, 1 up */
static volatile bool s_ui_dirty;
static bool s_passed;
static int s_rx_strokes;

static int16_t s_pen_x = -1;
static int16_t s_pen_y = -1;
static int16_t s_peer_x = -1;
static int16_t s_peer_y = -1;
static int16_t s_last_tx_x = -1;
static int16_t s_last_tx_y = -1;

static char s_self_name[24];
static char s_peer_name[24];
static char s_txt[512];
static int s_txt_n;

static uint16_t rgb565(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static void fill_bg(uint16_t c)
{
    uint32_t pair = ((uint32_t)c << 16) | c;
    uint32_t *p = (uint32_t *)s_fb;
    for (int i = 0; i < (W * H) / 2; i++) {
        p[i] = pair;
    }
}

static void stamp(int x, int y, uint16_t c)
{
    for (int dy = -BRUSH; dy <= BRUSH; dy++) {
        for (int dx = -BRUSH; dx <= BRUSH; dx++) {
            if (dx * dx + dy * dy > BRUSH * BRUSH) {
                continue;
            }
            int px = x + dx;
            int py = y + dy;
            if ((unsigned)px >= W || (unsigned)py >= H) {
                continue;
            }
            s_fb[py * W + px] = c;
        }
    }
}

static void line_to(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        stamp(x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int16_t clamp_x(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > W - 1) {
        return W - 1;
    }
    return (int16_t)v;
}

static int16_t clamp_y(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > H - 1) {
        return H - 1;
    }
    return (int16_t)v;
}

static void ink_clear(void)
{
    fill_bg(rgb565(COL_BG));
    s_pen_x = s_pen_y = -1;
    s_peer_x = s_peer_y = -1;
}

static void ink_point(int16_t *px, int16_t *py, uint8_t phase, int16_t x, int16_t y, uint16_t c)
{
    x = clamp_x(x);
    y = clamp_y(y);
    if (phase == PH_DOWN || *px < 0) {
        stamp(x, y, c);
    } else {
        line_to(*px, *py, x, y, c);
    }
    *px = x;
    *py = y;
    if (phase == PH_UP) {
        *px = -1;
        *py = -1;
    }
}

static void apply_ev(const ev_t *ev, bool local)
{
    if (ev->phase == PH_CLEAR) {
        ink_clear();
        return;
    }
    if (local) {
        ink_point(&s_pen_x, &s_pen_y, ev->phase, ev->x, ev->y, rgb565(COL_YOU));
    } else {
        ink_point(&s_peer_x, &s_peer_y, ev->phase, ev->x, ev->y, rgb565(COL_FRIEND));
        if (!s_passed && (ev->phase == PH_DOWN || ev->phase == PH_MOVE || ev->phase == PH_UP)) {
            s_rx_strokes++;
            if (s_rx_strokes >= 1) {
                s_passed = true;
                demo_pass("h26");
            }
        }
    }
}

static void enqueue(QueueHandle_t q, uint8_t phase, int16_t x, int16_t y)
{
    ev_t ev = {.phase = phase, .x = x, .y = y};
    (void)xQueueSend(q, &ev, 0);
}

static void invalidate_canvas(void)
{
    if (s_img) {
        lv_obj_invalidate(s_img);
    }
}

static void on_pointer(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t phase;
    if (code == LV_EVENT_PRESSED) {
        phase = PH_DOWN;
    } else if (code == LV_EVENT_PRESSING) {
        phase = PH_MOVE;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        phase = PH_UP;
    } else {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int16_t x = clamp_x(p.x);
    int16_t y = clamp_y(p.y);

    if (phase == PH_MOVE) {
        if (s_last_tx_x >= 0) {
            int dx = x - s_last_tx_x;
            int dy = y - s_last_tx_y;
            if (dx * dx + dy * dy < MIN_MOVE * MIN_MOVE) {
                return;
            }
        }
    }

    ev_t local = {.phase = phase, .x = x, .y = y};
    apply_ev(&local, true);
    invalidate_canvas();
    enqueue(s_txq, phase, x, y);
    s_last_tx_x = x;
    s_last_tx_y = y;
    if (phase == PH_UP) {
        s_last_tx_x = -1;
        s_last_tx_y = -1;
    }
}

static void on_boot(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    enqueue(s_txq, PH_CLEAR, 0, 0);
    enqueue(s_rxq, PH_CLEAR, 0, 0);
}

static void send_json(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(200));
}

static void send_ev(const ev_t *ev)
{
    char json[80];
    if (ev->phase == PH_CLEAR) {
        send_json("{\"type\":\"clear\"}");
        return;
    }
    if (ev->phase > PH_UP) {
        return;
    }
    snprintf(json, sizeof(json), "{\"type\":\"stroke\",\"phase\":\"%s\",\"x\":%d,\"y\":%d}",
             s_phase_name[ev->phase], (int)ev->x, (int)ev->y);
    send_json(json);
}

static void copy_str(char *dst, size_t cap, cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        strncpy(dst, v->valuestring, cap - 1);
        dst[cap - 1] = 0;
    }
}

static void apply_peer_online(cJSON *j, bool from_hello)
{
    cJSON *v = cJSON_GetObjectItem(j, from_hello ? "peer_online" : "online");
    if (!v) {
        v = cJSON_GetObjectItem(j, "peer_online");
    }
    if (cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint)) {
        s_peer_online = 1;
    } else if (cJSON_IsFalse(v) || cJSON_IsNumber(v)) {
        s_peer_online = 0;
    }
    copy_str(s_peer_name, sizeof(s_peer_name), j, "peer_name");
    s_ui_dirty = true;
}

static void on_text(const char *s, int n)
{
    char tmp[512];
    if (n >= (int)sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;

    cJSON *j = cJSON_Parse(tmp);
    if (!j) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(j, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (strcmp(t, "hello_ok") == 0) {
        copy_str(s_self_name, sizeof(s_self_name), j, "name");
        apply_peer_online(j, true);
    } else if (strcmp(t, "peer_status") == 0) {
        apply_peer_online(j, false);
    } else if (strcmp(t, "stroke") == 0) {
        cJSON *phase = cJSON_GetObjectItem(j, "phase");
        cJSON *x = cJSON_GetObjectItem(j, "x");
        cJSON *y = cJSON_GetObjectItem(j, "y");
        uint8_t p = PH_MOVE;
        if (cJSON_IsString(phase) && phase->valuestring) {
            if (strcmp(phase->valuestring, "down") == 0) {
                p = PH_DOWN;
            } else if (strcmp(phase->valuestring, "up") == 0) {
                p = PH_UP;
            }
        }
        int16_t px = cJSON_IsNumber(x) ? (int16_t)x->valueint : 0;
        int16_t py = cJSON_IsNumber(y) ? (int16_t)y->valueint : 0;
        enqueue(s_rxq, p, px, py);
    } else if (strcmp(t, "clear") == 0) {
        enqueue(s_rxq, PH_CLEAR, 0, 0);
    }
    cJSON_Delete(j);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        s_ws_ok = 1;
        s_ui_dirty = true;
        ESP_LOGI(TAG, "ws connected");
        char hello[192];
        snprintf(hello, sizeof(hello),
                 "{\"type\":\"hello\",\"device_id\":\"%s\",\"token\":\"%s\"}",
                 DEMO_DEVICE_ID, DEMO_DEVICE_TOKEN);
        send_json(hello);
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "ws disconnected");
        s_ws_ok = 0;
        s_peer_online = 0;
        s_ui_dirty = true;
        return;
    }
    if (id == WEBSOCKET_EVENT_ERROR) {
        ESP_LOGW(TAG, "ws error");
        s_ws_ok = 0;
        s_ui_dirty = true;
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev == NULL) {
        return;
    }
    bool text = (ev->op_code == 0x01) || (ev->op_code == 0x00 && s_txt_n > 0);
    if (!text || ev->data_ptr == NULL || ev->data_len <= 0) {
        return;
    }
    if (ev->payload_offset == 0) {
        s_txt_n = 0;
    }
    int room = (int)sizeof(s_txt) - 1 - s_txt_n;
    int n = ev->data_len < room ? ev->data_len : room;
    if (n > 0) {
        memcpy(s_txt + s_txt_n, ev->data_ptr, n);
        s_txt_n += n;
    }
    int got = ev->payload_offset + ev->data_len;
    bool done = (ev->payload_len <= 0) || (got >= (int)ev->payload_len);
    if (done && s_txt_n > 0) {
        on_text(s_txt, s_txt_n);
        s_txt_n = 0;
    }
}

static void refresh_status(void)
{
    char line[64];
    uint32_t color = COL_WAIT;
    uint32_t bg = 0x2A3038;
    if (s_ws_ok != 1) {
        snprintf(line, sizeof(line), "%s · no server %s", s_self_name, DEMO_SERVER_HOST);
        color = 0xE07050;
        bg = 0x3A2018;
    } else if (s_peer_online == 1) {
        snprintf(line, sizeof(line), "%s · %s here", s_self_name, s_peer_name);
        color = COL_HERE;
        bg = 0x1A3020;
    } else {
        snprintf(line, sizeof(line), "%s · waiting for %s", s_self_name, s_peer_name);
        color = 0xE8C040;
        bg = 0x2A3038;
    }
    if (!board_lvgl_lock(80)) {
        return;
    }
    if (s_banner) {
        lv_obj_set_style_bg_color(s_banner, lv_color_hex(bg), 0);
    }
    if (s_status) {
        lv_label_set_text(s_status, line);
        lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
    }
    board_lvgl_unlock();
    s_ui_dirty = false;
}

static void paint_ui(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    ink_clear();

    s_img = lv_image_create(scr);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_set_size(s_img, W, H);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_img, on_pointer, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_img, on_pointer, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_img, on_pointer, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_img, on_pointer, LV_EVENT_PRESS_LOST, NULL);

    s_banner = lv_obj_create(scr);
    lv_obj_remove_style_all(s_banner);
    lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_banner, 320, 28);
    lv_obj_set_pos(s_banner, 0, 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(0x2A3038), 0);

    s_status = lv_label_create(s_banner);
    lv_label_set_text(s_status, "connecting");
    lv_obj_set_style_text_color(s_status, lv_color_hex(COL_WAIT), 0);
    lv_obj_align(s_status, LV_ALIGN_LEFT_MID, 8, 0);

    s_hint = lv_label_create(scr);
    lv_label_set_text(s_hint, "draw  ·  boot clears");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COL_DIM), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_CLICKABLE);

    board_lvgl_unlock();
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h26 wifi...");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h26", "wifi");
        return;
    }

    s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_fb == NULL) {
        demo_fail("h26", "OOM");
        return;
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = W;
    s_dsc.header.h = H;
    s_dsc.header.stride = W * 2;
    s_dsc.data_size = FB_BYTES;
    s_dsc.data = (const uint8_t *)s_fb;

    s_txq = xQueueCreate(QLEN, sizeof(ev_t));
    s_rxq = xQueueCreate(QLEN, sizeof(ev_t));
    if (s_txq == NULL || s_rxq == NULL) {
        demo_fail("h26", "queue");
        return;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK && btns[BSP_BUTTON_CONFIG]) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot, NULL);
    }

    paint_ui();
    board_backlight_set(80);
    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name, sizeof(s_peer_name), DEMO_PEER_NAME);
    s_ws_ok = -1;
    s_ui_dirty = true;
    refresh_status();

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 8000,
    };
    s_ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("h26", "ws start");
        return;
    }

    ESP_LOGI(TAG, "who id=%s name=%s friend=%s  uri=%s", DEMO_DEVICE_ID, DEMO_DEVICE_NAME,
             DEMO_PEER_NAME, uri);

    while (1) {
        ev_t ev;
        while (xQueueReceive(s_txq, &ev, 0) == pdTRUE) {
            send_ev(&ev);
        }
        while (xQueueReceive(s_rxq, &ev, 0) == pdTRUE) {
            if (!board_lvgl_lock(80)) {
                enqueue(s_rxq, ev.phase, ev.x, ev.y);
                break;
            }
            apply_ev(&ev, false);
            invalidate_canvas();
            board_lvgl_unlock();
        }
        if (s_ui_dirty) {
            refresh_status();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
