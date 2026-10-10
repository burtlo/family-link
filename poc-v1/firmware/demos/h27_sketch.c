/*
 * h27 — Drawing note (store-and-forward, Mazi ↔ Arlo).
 *
 * Home: Draw button + a New message button when something is waiting.
 * Draw starts a timed recording on the glass. Boot sends the clip to the
 * friend (they do not need to be watching). Opening a message replays it
 * at the same speed. Boot during playback stops and returns home; the note
 * stays until it plays to the end.
 *
 * Host (leave running):
 *   python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080
 *
 *   make flash DEMO=h27 WHO=mazi
 *   make flash DEMO=h27 WHO=arlo
 * -- PASS h27 after a successful send, or the first inbound point painted.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "http_bearer.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h27";

#define W          320
#define H          240
#define FB_BYTES   (W * H * 2)
#define BRUSH      2
#define MIN_MOVE   2
#define MAX_POINTS 2048
#define MAX_MS     30000
#define HDR_LEN    8
#define PT_LEN     8
#define BLOB_CAP   (HDR_LEN + MAX_POINTS * PT_LEN)

#define COL_BG     0x101418
#define COL_CANVAS 0x000000
#define COL_YOU    0xE8F0E8
#define COL_FRIEND 0x5AA0E8
#define COL_DIM    0x888888
#define COL_GOLD   0xE8C040

enum {
    PH_DOWN = 0,
    PH_MOVE = 1,
    PH_UP = 2,
};

enum {
    ST_HOME = 0,
    ST_RECORD,
    ST_PLAY,
};

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t ver;
    uint8_t pad;
    uint16_t n;
} hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t t;
    uint8_t phase;
    uint8_t pad;
    uint16_t x;
    uint16_t y;
} pt_t;

_Static_assert(sizeof(hdr_t) == HDR_LEN, "hdr");
_Static_assert(sizeof(pt_t) == PT_LEN, "pt");

typedef struct {
    uint8_t *buf;
    int cap;
    int len;
} body_t;

static uint16_t *s_fb;
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_img;
static lv_obj_t *s_banner;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_btn_draw;
static lv_obj_t *s_btn_msg;
static lv_obj_t *s_btn_msg_lab;

static pt_t *s_pts;
static int s_n;
static uint8_t *s_blob;
static uint8_t s_http_mem[1024];

static volatile int s_st = ST_HOME;
static volatile bool s_go_record;
static volatile bool s_go_play;
static volatile bool s_boot;
static volatile bool s_busy;
static int s_unread;
static char s_open_id[16];
static char s_from_name[24];
static int64_t s_t0;
static int s_play_i;
static int16_t s_pen_x = -1;
static int16_t s_pen_y = -1;
static int16_t s_last_x = -1;
static int16_t s_last_y = -1;
static bool s_passed;
static bool s_home_dirty = true;
static int s_poll_left;
static int s_shown_unread = -1;

static char s_self_name[24];
static char s_peer_name[24];

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
    fill_bg(rgb565(COL_CANVAS));
    s_pen_x = s_pen_y = -1;
}

static void ink_point(uint8_t phase, int16_t x, int16_t y, uint16_t c)
{
    x = clamp_x(x);
    y = clamp_y(y);
    if (phase == PH_DOWN || s_pen_x < 0) {
        stamp(x, y, c);
    } else {
        line_to(s_pen_x, s_pen_y, x, y, c);
    }
    s_pen_x = x;
    s_pen_y = y;
    if (phase == PH_UP) {
        s_pen_x = -1;
        s_pen_y = -1;
    }
}

static void invalidate_canvas(void)
{
    if (s_img) {
        lv_obj_invalidate(s_img);
    }
}

static void maybe_pass(const char *why)
{
    if (s_passed) {
        return;
    }
    s_passed = true;
    ESP_LOGI(TAG, "pass %s", why);
    demo_pass("h27");
}

static void set_banner(const char *line, uint32_t fg, uint32_t bg)
{
    if (!board_lvgl_lock(80)) {
        return;
    }
    if (s_banner) {
        lv_obj_set_style_bg_color(s_banner, lv_color_hex(bg), 0);
    }
    if (s_status) {
        lv_label_set_text(s_status, line);
        lv_obj_set_style_text_color(s_status, lv_color_hex(fg), 0);
    }
    board_lvgl_unlock();
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *title, int x, int y, int w, int h,
                         uint32_t bg, uint32_t fg)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, title);
    lv_obj_set_style_text_color(lab, lv_color_hex(fg), 0);
    lv_obj_center(lab);
    return btn;
}

static void on_draw_btn(lv_event_t *e)
{
    (void)e;
    if (s_st == ST_HOME && !s_busy) {
        s_go_record = true;
    }
}

static void on_msg_btn(lv_event_t *e)
{
    (void)e;
    if (s_st == ST_HOME && !s_busy && s_unread > 0) {
        s_go_play = true;
    }
}

static void on_boot(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_boot = true;
}

static void capture_point(uint8_t phase, int16_t x, int16_t y)
{
    if (s_n >= MAX_POINTS) {
        return;
    }
    int64_t now = (esp_timer_get_time() - s_t0) / 1000;
    if (now < 0) {
        now = 0;
    }
    if (now > MAX_MS) {
        now = MAX_MS;
    }
    if (now > 65535) {
        now = 65535;
    }
    pt_t *p = &s_pts[s_n++];
    p->t = (uint16_t)now;
    p->phase = phase;
    p->pad = 0;
    p->x = (uint16_t)x;
    p->y = (uint16_t)y;
}

static void on_pointer(lv_event_t *e)
{
    if (s_st != ST_RECORD || s_busy) {
        return;
    }
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

    if (phase == PH_MOVE && s_last_x >= 0) {
        int dx = x - s_last_x;
        int dy = y - s_last_y;
        if (dx * dx + dy * dy < MIN_MOVE * MIN_MOVE) {
            return;
        }
    }

    ink_point(phase, x, y, rgb565(COL_YOU));
    invalidate_canvas();
    capture_point(phase, x, y);
    s_last_x = x;
    s_last_y = y;
    if (phase == PH_UP) {
        s_last_x = -1;
        s_last_y = -1;
    }
}

static esp_err_t on_http(esp_http_client_event_t *evt)
{
    body_t *b = evt->user_data;
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

static int http_bin(const char *method, const char *path, const uint8_t *body, int body_len,
                     body_t *out)
{
    if (out) {
        out->len = 0;
        if (out->buf && out->cap > 0) {
            out->buf[0] = 0;
        }
    }
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http,
        .user_data = out,
        .timeout_ms = 12000,
        .buffer_size = 2048,
    };
    if (method && strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    }
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    if (body && body_len > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(client, (const char *)body, body_len);
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

static int pack_blob(void)
{
    if (s_n <= 0 || s_n > MAX_POINTS) {
        return 0;
    }
    hdr_t *h = (hdr_t *)s_blob;
    memcpy(h->magic, "FLSK", 4);
    h->ver = 1;
    h->pad = 0;
    h->n = (uint16_t)s_n;
    memcpy(s_blob + HDR_LEN, s_pts, (size_t)s_n * PT_LEN);
    return HDR_LEN + s_n * PT_LEN;
}

static bool unpack_blob(const uint8_t *blob, int len)
{
    if (len < HDR_LEN) {
        return false;
    }
    const hdr_t *h = (const hdr_t *)blob;
    if (memcmp(h->magic, "FLSK", 4) != 0 || h->ver != 1) {
        return false;
    }
    int n = h->n;
    if (n <= 0 || n > MAX_POINTS) {
        return false;
    }
    if (len < HDR_LEN + n * PT_LEN) {
        return false;
    }
    memcpy(s_pts, blob + HDR_LEN, (size_t)n * PT_LEN);
    s_n = n;
    return true;
}

static void refresh_inbox(void)
{
    http_buf_t out = {.buf = s_http_mem, .cap = (int)sizeof(s_http_mem), .len = 0};
    int st = http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "GET", "/v1/sketches",
                            DEMO_DEVICE_TOKEN, NULL, &out, 8000);
    if (st != 200) {
        ESP_LOGW(TAG, "list %d", st);
        return;
    }
    cJSON *j = cJSON_Parse((char *)s_http_mem);
    if (!j) {
        return;
    }
    cJSON *unread = cJSON_GetObjectItem(j, "unread");
    int n = cJSON_IsArray(unread) ? cJSON_GetArraySize(unread) : 0;
    s_unread = n;
    s_open_id[0] = 0;
    s_from_name[0] = 0;
    if (n > 0) {
        cJSON *first = cJSON_GetArrayItem(unread, 0);
        cJSON *id = cJSON_GetObjectItem(first, "id");
        cJSON *from = cJSON_GetObjectItem(first, "from_name");
        if (cJSON_IsString(id) && id->valuestring) {
            strncpy(s_open_id, id->valuestring, sizeof(s_open_id) - 1);
        }
        if (cJSON_IsString(from) && from->valuestring) {
            strncpy(s_from_name, from->valuestring, sizeof(s_from_name) - 1);
        }
    }
    cJSON_Delete(j);
}

static void paint_home(void)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    s_img = NULL;
    s_banner = NULL;
    s_status = NULL;
    s_hint = NULL;
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    char who[48];
    snprintf(who, sizeof(who), "%s", s_self_name);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, who);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    char sub[48];
    snprintf(sub, sizeof(sub), "note for %s", s_peer_name);
    lv_obj_t *sub_l = lv_label_create(scr);
    lv_label_set_text(sub_l, sub);
    lv_obj_set_style_text_color(sub_l, lv_color_hex(COL_DIM), 0);
    lv_obj_align(sub_l, LV_ALIGN_TOP_MID, 0, 40);

    int draw_y = s_unread > 0 ? 78 : 96;
    s_btn_draw = make_btn(scr, "draw", 36, draw_y, 248, 64, 0x3A6EA5, 0xFFFFFF);
    lv_obj_add_event_cb(s_btn_draw, on_draw_btn, LV_EVENT_CLICKED, NULL);

    s_btn_msg = make_btn(scr, "new message", 36, 158, 248, 52, COL_GOLD, 0x1A1A1A);
    s_btn_msg_lab = lv_obj_get_child(s_btn_msg, 0);
    lv_obj_add_event_cb(s_btn_msg, on_msg_btn, LV_EVENT_CLICKED, NULL);
    if (s_unread <= 0) {
        lv_obj_add_flag(s_btn_msg, LV_OBJ_FLAG_HIDDEN);
    } else if (s_unread > 1 && s_btn_msg_lab) {
        char line[24];
        snprintf(line, sizeof(line), "%d new", s_unread);
        lv_label_set_text(s_btn_msg_lab, line);
    }

    board_lvgl_unlock();
    s_shown_unread = s_unread;
    s_home_dirty = false;
}

static void paint_canvas(const char *line, uint32_t fg, uint32_t bg, const char *hint)
{
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_CANVAS), 0);
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
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(bg), 0);

    s_status = lv_label_create(s_banner);
    lv_label_set_text(s_status, line);
    lv_obj_set_style_text_color(s_status, lv_color_hex(fg), 0);
    lv_obj_align(s_status, LV_ALIGN_LEFT_MID, 8, 0);

    s_hint = lv_label_create(scr);
    lv_label_set_text(s_hint, hint);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(COL_DIM), 0);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_CLICKABLE);

    s_btn_draw = NULL;
    s_btn_msg = NULL;
    s_btn_msg_lab = NULL;
    board_lvgl_unlock();
}

static void enter_home(void)
{
    s_st = ST_HOME;
    s_boot = false;
    s_go_record = false;
    s_go_play = false;
    s_busy = false;
    s_n = 0;
    s_play_i = 0;
    s_last_x = s_last_y = -1;
    s_poll_left = 100;
    refresh_inbox();
    paint_home();
}

static void enter_record(void)
{
    s_st = ST_RECORD;
    s_n = 0;
    s_boot = false;
    s_last_x = s_last_y = -1;
    s_t0 = esp_timer_get_time();
    char line[48];
    snprintf(line, sizeof(line), "recording for %s", s_peer_name);
    paint_canvas(line, 0xE8F0E8, 0x3A2018, "boot sends");
}

static bool fetch_open(void)
{
    if (s_open_id[0] == 0) {
        refresh_inbox();
    }
    if (s_open_id[0] == 0) {
        return false;
    }
    char path[48];
    snprintf(path, sizeof(path), "/v1/sketches/%s/blob", s_open_id);
    body_t out = {.buf = s_blob, .cap = BLOB_CAP, .len = 0};
    int st = http_bin("GET", path, NULL, 0, &out);
    if (st != 200) {
        ESP_LOGW(TAG, "blob %d", st);
        return false;
    }
    if (!unpack_blob(s_blob, out.len)) {
        ESP_LOGW(TAG, "bad blob");
        return false;
    }
    return true;
}

static void enter_play(void)
{
    s_busy = true;
    if (!fetch_open()) {
        s_busy = false;
        paint_home();
        return;
    }
    s_st = ST_PLAY;
    s_boot = false;
    s_play_i = 0;
    s_t0 = esp_timer_get_time();
    char line[48];
    snprintf(line, sizeof(line), "from %s", s_from_name[0] ? s_from_name : s_peer_name);
    paint_canvas(line, COL_FRIEND, 0x1A3020, "boot stops");
    s_busy = false;
}

static void send_record(void)
{
    if (s_n <= 0) {
        enter_home();
        return;
    }
    s_busy = true;
    set_banner("sending…", 0xE8F0E8, 0x2A3038);
    int n = pack_blob();
    body_t out = {.buf = s_http_mem, .cap = (int)sizeof(s_http_mem), .len = 0};
    int st = http_bin("POST", "/v1/sketches", s_blob, n, &out);
    s_busy = false;
    if (st == 200) {
        maybe_pass("sent");
        enter_home();
        return;
    }
    ESP_LOGW(TAG, "send %d", st);
    set_banner("send failed · boot retries", 0xE07050, 0x3A2018);
    s_st = ST_RECORD;
}

static void finish_play(bool keep)
{
    if (!keep && s_open_id[0]) {
        char path[48];
        snprintf(path, sizeof(path), "/v1/sketches/%s/read", s_open_id);
        http_buf_t out = {.buf = s_http_mem, .cap = (int)sizeof(s_http_mem), .len = 0};
        (void)http_bearer_do(DEMO_SERVER_HOST, DEMO_SERVER_PORT, "POST", path, DEMO_DEVICE_TOKEN,
                             "{}", &out, 8000);
    }
    enter_home();
}

static void play_tick(void)
{
    if (s_boot) {
        s_boot = false;
        finish_play(true);
        return;
    }
    int64_t now = (esp_timer_get_time() - s_t0) / 1000;
    while (s_play_i < s_n && s_pts[s_play_i].t <= now) {
        const pt_t *p = &s_pts[s_play_i];
        if (!board_lvgl_lock(40)) {
            break;
        }
        ink_point(p->phase, (int16_t)p->x, (int16_t)p->y, rgb565(COL_FRIEND));
        invalidate_canvas();
        board_lvgl_unlock();
        maybe_pass("playback");
        s_play_i++;
    }
    if (s_play_i >= s_n) {
        finish_play(false);
    }
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h27 wifi...");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h27", "wifi");
        return;
    }

    s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_pts = heap_caps_malloc(sizeof(pt_t) * MAX_POINTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pts == NULL) {
        s_pts = heap_caps_malloc(sizeof(pt_t) * MAX_POINTS, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_blob = heap_caps_malloc(BLOB_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_blob == NULL) {
        s_blob = heap_caps_malloc(BLOB_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_fb == NULL || s_pts == NULL || s_blob == NULL) {
        demo_fail("h27", "OOM");
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

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK && btns[BSP_BUTTON_CONFIG]) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot, NULL);
    }

    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name, sizeof(s_peer_name), DEMO_PEER_NAME);
    board_backlight_set(80);
    enter_home();

    ESP_LOGI(TAG, "who id=%s name=%s friend=%s", DEMO_DEVICE_ID, DEMO_DEVICE_NAME, DEMO_PEER_NAME);

    while (1) {
        if (s_st == ST_HOME) {
            if (s_go_record) {
                s_go_record = false;
                enter_record();
            } else if (s_go_play) {
                s_go_play = false;
                enter_play();
            } else {
                s_boot = false;
                if (--s_poll_left <= 0) {
                    s_poll_left = 100;
                    int before = s_unread;
                    refresh_inbox();
                    if (s_unread != before || s_home_dirty) {
                        paint_home();
                    }
                }
            }
        } else if (s_st == ST_RECORD) {
            if (s_boot) {
                s_boot = false;
                send_record();
            }
        } else if (s_st == ST_PLAY) {
            play_tick();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
