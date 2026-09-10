#include "v1_connect.h"

#include "v1_api.h"
#include "v1_auth.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "http_bearer.h"
#include "nvs.h"

#include <string.h>

static const char *TAG = "v1_connect";

static bool s_server_online;
static int64_t s_conn_retry_us;
static int64_t s_conn_dot_anim_us;
static int64_t s_wifi_retry_us;
static user_t s_users[V1_USER_MAX];
static int s_user_n;
static char s_last_user[16];
static lv_obj_t *s_offline_lab;
static lv_obj_t *s_conn_dots;
static lv_obj_t *s_dot_circles[3];

static uint32_t default_accent(int idx)
{
    static const uint32_t hues[V1_ACCENT_COUNT] = {
        0x5AA0E8, 0xE8C040, 0x7AC47A, 0xC070E8,
        0xE87A9A, 0x7AD4E8, 0xD4A0E8, 0xE8A87A,
        0x4ECDC4, 0xFF6B6B,
    };
    if (idx < 0) {
        idx = 0;
    }
    return hues[idx % V1_ACCENT_COUNT];
}

static uint32_t parse_hex_color(const char *s)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    unsigned v = 0;
    if (sscanf(s + 1, "%6x", &v) != 1) {
        return 0;
    }
    return (uint32_t)v;
}

static int parse_hangout_users(cJSON *users, user_t *out, int max)
{
    if (!cJSON_IsArray(users)) {
        return 0;
    }
    int loaded = 0;
    int n = cJSON_GetArraySize(users);
    for (int i = 0; i < n && loaded < max; i++) {
        cJSON *it = cJSON_GetArrayItem(users, i);
        cJSON *id = cJSON_GetObjectItem(it, "id");
        cJSON *name = cJSON_GetObjectItem(it, "name");
        if (!cJSON_IsString(id)) {
            continue;
        }
        strncpy(out[loaded].id, id->valuestring, sizeof(out[0].id) - 1);
        out[loaded].id[sizeof(out[0].id) - 1] = 0;
        if (cJSON_IsString(name)) {
            strncpy(out[loaded].name, name->valuestring, sizeof(out[0].name) - 1);
        } else {
            strncpy(out[loaded].name, id->valuestring, sizeof(out[0].name) - 1);
        }
        out[loaded].name[sizeof(out[0].name) - 1] = 0;
        out[loaded].avatar_slot = 0;
        out[loaded].accent = 0;
        cJSON *prof = cJSON_GetObjectItem(it, "profile");
        if (prof) {
            cJSON *slot = cJSON_GetObjectItem(prof, "avatar_slot");
            cJSON *accent = cJSON_GetObjectItem(prof, "accent_hex");
            if (cJSON_IsNumber(slot)) {
                int v = slot->valueint;
                if (v < 0) {
                    v = 0;
                }
                if (v > V1_AVATAR_SLOTS) {
                    v = V1_AVATAR_SLOTS;
                }
                out[loaded].avatar_slot = (uint8_t)v;
            }
            if (cJSON_IsString(accent)) {
                uint32_t c = parse_hex_color(accent->valuestring);
                if (c) {
                    out[loaded].accent = c;
                }
            }
        }
        loaded++;
    }
    return loaded;
}

static bool apply_hangout_users(int loaded, const user_t *fresh)
{
    if (loaded > 0) {
        memcpy(s_users, fresh, sizeof(user_t) * loaded);
        s_user_n = loaded;
        return true;
    }
    return s_user_n > 0;
}

void v1_connect_init(void)
{
    s_server_online = false;
    s_user_n = 0;
    s_last_user[0] = 0;
    v1_connect_clear_conn_dots();
}

bool v1_connect_online(void)
{
    return s_server_online;
}

void v1_connect_mark_online(void)
{
    if (!s_server_online) {
        s_server_online = true;
        v1_ui_request_repaint();
    }
}

void v1_connect_mark_offline(void)
{
    if (s_server_online) {
        s_server_online = false;
        v1_ui_request_repaint();
    }
}

int v1_connect_user_count(void)
{
    return s_user_n;
}

const user_t *v1_connect_users(void)
{
    return s_users;
}

user_t *v1_connect_users_mut(void)
{
    return s_users;
}

const char *v1_connect_last_user(void)
{
    return s_last_user;
}

const char *v1_connect_user_name(const char *id)
{
    for (int i = 0; i < s_user_n; i++) {
        if (id && strcmp(s_users[i].id, id) == 0 && s_users[i].name[0]) {
            return s_users[i].name;
        }
    }
    return id && id[0] ? id : "";
}

int v1_connect_user_index(const char *id)
{
    for (int i = 0; i < s_user_n; i++) {
        if (id && strcmp(s_users[i].id, id) == 0) {
            return i;
        }
    }
    return 0;
}

uint32_t v1_connect_user_accent(int idx)
{
    if (idx < 0 || idx >= s_user_n) {
        return default_accent(0);
    }
    if (s_users[idx].accent) {
        return s_users[idx].accent;
    }
    return default_accent(idx);
}

uint8_t v1_connect_user_avatar_slot(int idx)
{
    if (idx < 0 || idx >= s_user_n) {
        return 0;
    }
    return s_users[idx].avatar_slot;
}

int v1_connect_user_index_by_label(const char *label)
{
    if (!label || !label[0]) {
        return 0;
    }
    for (int i = 0; i < s_user_n; i++) {
        if (strcmp(s_users[i].name, label) == 0 || strcmp(s_users[i].id, label) == 0) {
            return i;
        }
    }
    return 0;
}

void v1_connect_apply_profile(const char *user_id, cJSON *prof)
{
    if (!prof || !user_id) {
        return;
    }
    int idx = v1_connect_user_index(user_id);
    if (idx < 0 || idx >= s_user_n) {
        return;
    }
    user_t *u = &s_users[idx];
    cJSON *slot = cJSON_GetObjectItem(prof, "avatar_slot");
    cJSON *accent = cJSON_GetObjectItem(prof, "accent_hex");
    if (cJSON_IsNumber(slot)) {
        int v = slot->valueint;
        if (v < 0) {
            v = 0;
        }
        if (v > V1_AVATAR_SLOTS) {
            v = V1_AVATAR_SLOTS;
        }
        u->avatar_slot = (uint8_t)v;
    }
    if (cJSON_IsString(accent)) {
        uint32_t c = parse_hex_color(accent->valuestring);
        if (c) {
            u->accent = c;
        }
    }
}

void v1_connect_nvs_load_last(char *last_user, size_t cap)
{
    if (!last_user || cap == 0) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = cap;
    (void)nvs_get_str(h, "last", last_user, &n);
    nvs_close(h);
    strncpy(s_last_user, last_user, sizeof(s_last_user) - 1);
    s_last_user[sizeof(s_last_user) - 1] = 0;
}

void v1_connect_nvs_save_last(const char *id)
{
    if (!id || !id[0]) {
        return;
    }
    strncpy(s_last_user, id, sizeof(s_last_user) - 1);
    s_last_user[sizeof(s_last_user) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_str(h, "last", id);
    (void)nvs_commit(h);
    nvs_close(h);
}

static void nvs_save_hangout(void)
{
    if (s_user_n <= 0) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        return;
    }
    for (int i = 0; i < s_user_n; i++) {
        cJSON *u = cJSON_CreateObject();
        if (!u) {
            continue;
        }
        cJSON_AddStringToObject(u, "id", s_users[i].id);
        cJSON_AddStringToObject(u, "name", s_users[i].name);
        cJSON *prof = cJSON_CreateObject();
        if (prof) {
            char hex[8];
            uint32_t c = s_users[i].accent ? s_users[i].accent : default_accent(i);
            snprintf(hex, sizeof(hex), "#%06X", (unsigned)(c & 0xFFFFFF));
            cJSON_AddNumberToObject(prof, "avatar_slot", s_users[i].avatar_slot);
            cJSON_AddStringToObject(prof, "accent_hex", hex);
            cJSON_AddItemToObject(u, "profile", prof);
        }
        cJSON_AddItemToArray(arr, u);
    }
    cJSON_AddItemToObject(root, "users", arr);
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        cJSON_free(printed);
        return;
    }
    (void)nvs_set_blob(h, "hangout", printed, strlen(printed) + 1);
    (void)nvs_commit(h);
    nvs_close(h);
    cJSON_free(printed);
}

bool v1_connect_nvs_load_hangout(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t n = 0;
    if (nvs_get_blob(h, "hangout", NULL, &n) != ESP_OK || n <= 1 || n > V1_HANGOUT_NVS_MAX) {
        nvs_close(h);
        return false;
    }
    char *buf = heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf) {
        nvs_close(h);
        return false;
    }
    if (nvs_get_blob(h, "hangout", buf, &n) != ESP_OK) {
        free(buf);
        nvs_close(h);
        return false;
    }
    nvs_close(h);
    buf[n - 1] = 0;
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    cJSON *users = root ? cJSON_GetObjectItem(root, "users") : NULL;
    user_t fresh[V1_USER_MAX];
    int loaded = parse_hangout_users(users, fresh, V1_USER_MAX);
    cJSON_Delete(root);
    if (loaded > 0) {
        apply_hangout_users(loaded, fresh);
        ESP_LOGI(TAG, "hangout cache %d users", loaded);
        return true;
    }
    return false;
}

bool v1_connect_load_hangout_ms(int timeout_ms)
{
    uint8_t *json = v1_api_json_buf();
    http_buf_t b = { .buf = json, .cap = (int)v1_api_json_cap() };
    int st = v1_api_http_json_timeout("GET", "/v1/hangout", NULL, NULL, &b, timeout_ms);
    if (st != 200) {
        ESP_LOGW(TAG, "GET /v1/hangout failed st=%d (have %d users)", st, s_user_n);
        v1_connect_mark_offline();
        return s_user_n > 0;
    }
    cJSON *root = cJSON_Parse((char *)json);
    cJSON *users = root ? cJSON_GetObjectItem(root, "users") : NULL;
    user_t fresh[V1_USER_MAX];
    int loaded = parse_hangout_users(users, fresh, V1_USER_MAX);
    cJSON_Delete(root);
    if (apply_hangout_users(loaded, fresh)) {
        v1_connect_mark_online();
        nvs_save_hangout();
        return true;
    }
    ESP_LOGW(TAG, "hangout response had no users");
    v1_connect_mark_offline();
    return s_user_n > 0;
}

bool v1_connect_load_hangout(void)
{
    return v1_connect_load_hangout_ms(15000);
}

bool v1_connect_signed_out_pre_auth(const char *session_user, state_t st)
{
    return (!session_user || session_user[0] == 0) && st != ST_WIFI_ERR;
}

bool v1_connect_awaiting_server(const char *session_user, state_t st)
{
    (void)st;
    return !s_server_online && (!session_user || session_user[0] == 0);
}

void v1_connect_enter_from_signin(void)
{
    if (v1_auth_login_active()) {
        return;
    }
    v1_auth_clear_entry();
    v1_state_post(V1_EV_ENTER_CONNECTING, 0);
    v1_ui_request_repaint();
}

void v1_connect_signed_out_probe(void)
{
    if (!v1_state_may_probe()) {
        return;
    }
    (void)v1_connect_load_hangout_ms(V1_CONNECT_PROBE_MS);
    if (s_server_online) {
        if (v1_state_get() == ST_CONNECTING && !v1_auth_login_active()) {
            v1_state_post(V1_EV_ROSTER_READY, 0);
            v1_ui_request_repaint();
        }
    } else {
        v1_connect_enter_from_signin();
    }
    s_conn_retry_us = esp_timer_get_time();
    s_conn_dot_anim_us = esp_timer_get_time();
    if (v1_state_get() == ST_CONNECTING) {
        v1_connect_refresh_conn_dots();
    }
}

void v1_connect_tick(int64_t now_us, bool signed_out_pre_auth)
{
    if (!signed_out_pre_auth) {
        return;
    }
    state_t st = v1_state_get();
    if (!s_server_online && st != ST_CONNECTING) {
        v1_connect_enter_from_signin();
    } else if (s_server_online && st == ST_CONNECTING && !v1_auth_login_active()) {
        v1_state_post(V1_EV_ROSTER_READY, 0);
        v1_ui_request_repaint();
    } else if ((now_us - s_conn_retry_us) > (int64_t)V1_CONNECT_RETRY_MS * 1000) {
        if (v1_state_may_probe()) {
            v1_connect_signed_out_probe();
        }
    }
}

int64_t v1_connect_conn_retry_us(void)
{
    return s_conn_retry_us;
}

void v1_connect_set_conn_retry_us(int64_t us)
{
    s_conn_retry_us = us;
    s_conn_dot_anim_us = us;
}

int64_t v1_connect_wifi_retry_us(void)
{
    return s_wifi_retry_us;
}

void v1_connect_set_wifi_retry_us(int64_t us)
{
    s_wifi_retry_us = us;
}

void v1_connect_clear_conn_dots(void)
{
    s_conn_dots = NULL;
    s_dot_circles[0] = NULL;
    s_dot_circles[1] = NULL;
    s_dot_circles[2] = NULL;
}

static void paint_conn_dots_row(lv_obj_t *scr, int y)
{
    s_conn_dots = v1_ui_paint_transparent_bar(scr, y, 28);
    const int dot_sz = 10;
    const int gap = 14;
    const int row_w = 3 * dot_sz + 2 * gap;
    const int x0 = (320 - row_w) / 2;
    for (int i = 0; i < 3; i++) {
        s_dot_circles[i] = lv_obj_create(s_conn_dots);
        lv_obj_set_size(s_dot_circles[i], dot_sz, dot_sz);
        lv_obj_set_pos(s_dot_circles[i], x0 + i * (dot_sz + gap), (28 - dot_sz) / 2);
        lv_obj_set_style_radius(s_dot_circles[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_dot_circles[i], lv_color_hex(V1_UI_ACCENT), 0);
        lv_obj_set_style_border_width(s_dot_circles[i], 0, 0);
        lv_obj_set_style_pad_all(s_dot_circles[i], 0, 0);
        lv_obj_clear_flag(s_dot_circles[i], LV_OBJ_FLAG_SCROLLABLE);
        if (i > 0) {
            lv_obj_add_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void v1_connect_refresh_conn_dots(void)
{
    if (!s_dot_circles[0]) {
        return;
    }
    int64_t elapsed = esp_timer_get_time() - s_conn_dot_anim_us;
    int64_t slice_us = (int64_t)V1_CONNECT_RETRY_MS * 1000 / 3;
    int phase = slice_us > 0 ? (int)(elapsed / slice_us) : 0;
    if (phase > 2) {
        phase = 2;
    }
    for (int i = 0; i < 3; i++) {
        if (i <= phase) {
            lv_obj_clear_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_dot_circles[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void paint_wifi_icon(lv_obj_t *parent)
{
    const int cx = 36;
    const int base_y = 44;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *arc = lv_obj_create(parent);
        int w = 20 + i * 14;
        int h = 10 + i * 8;
        lv_obj_set_size(arc, w, h);
        lv_obj_set_pos(arc, cx - w / 2, base_y - h - i * 6);
        lv_obj_set_style_radius(arc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(arc, 3, 0);
        lv_obj_set_style_border_color(arc, lv_color_hex(0xA8B0B8), 0);
        lv_obj_set_style_border_side(arc, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_pos(dot, cx - 3, base_y - 3);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_pad_all(dot, 0, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *x1 = lv_obj_create(parent);
    lv_obj_set_size(x1, 4, 36);
    lv_obj_set_style_bg_color(x1, lv_color_hex(0xE85A5A), 0);
    lv_obj_set_style_border_width(x1, 0, 0);
    lv_obj_set_style_pad_all(x1, 0, 0);
    lv_obj_set_style_transform_angle(x1, 450, 0);
    lv_obj_clear_flag(x1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(x1);
    lv_obj_t *x2 = lv_obj_create(parent);
    lv_obj_set_size(x2, 4, 36);
    lv_obj_set_style_bg_color(x2, lv_color_hex(0xE85A5A), 0);
    lv_obj_set_style_border_width(x2, 0, 0);
    lv_obj_set_style_pad_all(x2, 0, 0);
    lv_obj_set_style_transform_angle(x2, 1350, 0);
    lv_obj_clear_flag(x2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(x2);
}

void v1_connect_paint_connecting(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    v1_connect_clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    const int icon_w = 72 * V1_CONNECT_ICON_SCALE / 100;
    const int icon_h = 56 * V1_CONNECT_ICON_SCALE / 100;
    const int title_h = 32;
    const int dots_h = 28;
    const int gap_icon_title = 22;
    const int gap_title_dots = 14;
    const int stack_h = icon_h + gap_icon_title + title_h + gap_title_dots + dots_h;
    int y = (240 - stack_h) / 2;

    lv_obj_t *icon = v1_ui_paint_icon_box_at(scr, (320 - icon_w) / 2, y, V1_CONNECT_ICON_SCALE);
    v1_ui_paint_mailbox_icon(icon, V1_CONNECT_ICON_SCALE);
    y += icon_h + gap_icon_title;

    lv_obj_t *title_bar = v1_ui_paint_transparent_bar(scr, y, title_h);
    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "connecting");
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
    v1_ui_style_conn_title_font(title);
    lv_obj_center(title);
    y += title_h + gap_title_dots;

    paint_conn_dots_row(scr, y);
    s_conn_dot_anim_us = esp_timer_get_time();
    v1_connect_refresh_conn_dots();
    v1_ui_hook_scr(scr);
}

void v1_connect_paint_wifi_error(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    v1_connect_clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);
    lv_obj_t *icon = v1_ui_paint_icon_box_at(scr, (320 - 72) / 2, 36, 100);
    paint_wifi_icon(icon);
    v1_ui_paint_message_panel(scr, 108, "no Wi-Fi", "this box needs the home network",
                              "ask Lynn to check the network");
    v1_ui_hook_scr(scr);
}

static void on_user_btn(lv_event_t *e)
{
    const char *id = (const char *)lv_event_get_user_data(e);
    if (!id) {
        return;
    }
    v1_auth_on_roster_pick(id);
}

void v1_connect_paint_roster(lv_obj_t *scr)
{
    if (v1_connect_awaiting_server("", v1_state_get())) {
        v1_connect_paint_connecting(scr);
        return;
    }

    lv_obj_clean(scr);
    v1_connect_clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "sign in");
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    int y0, row_h, row_step, face_sz;
    v1_ui_roster_row_layout(s_user_n, &y0, &row_h, &row_step, &face_sz);
    int face_y = (row_h - face_sz - 4) / 2;
    int label_x = 12 + face_sz + 8;
    int y = y0;
    for (int i = 0; i < s_user_n; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 0, 0);
        v1_ui_style_list_row(b);
        if (s_last_user[0] && strcmp(s_users[i].id, s_last_user) == 0) {
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(V1_UI_ACCENT), 0);
        }
        v1_ui_paint_face_sized(b, i, 8, face_y, face_sz);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, s_users[i].name);
        v1_ui_style_list_row_label(t);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, label_x, 0);
        lv_obj_add_event_cb(b, on_user_btn, LV_EVENT_CLICKED, (void *)s_users[i].id);
        y += row_step;
    }
    v1_ui_hook_scr(scr);
}

void v1_connect_refresh_offline_ribbon(const char *session_user)
{
    if (s_offline_lab) {
        if (s_server_online || !session_user || !session_user[0]) {
            lv_obj_add_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

lv_obj_t *v1_connect_offline_lab(void)
{
    return s_offline_lab;
}

void v1_connect_set_offline_lab(lv_obj_t *lab)
{
    s_offline_lab = lab;
}
