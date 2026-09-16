#include "v1_connect.h"

#include "v1_api.h"
#include "v1_auth.h"
#include "v1_carousel.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "http_bearer.h"
#include "nvs.h"

#include <limits.h>
#include <string.h>

static const char *TAG = "v1_connect";

static bool s_server_online;
static int64_t s_conn_retry_us;
static int64_t s_conn_dot_anim_us;
static int64_t s_wifi_retry_us;
static user_t s_users[V1_USER_MAX];
static int s_user_n;
static char s_last_user[16];
/** Most-recent login first; empty until someone has signed in on this box. */
static char s_login_order[V1_USER_MAX][16];
static int s_login_order_n;
static lv_obj_t *s_offline_lab;
static lv_obj_t *s_conn_dots;
static lv_obj_t *s_dot_circles[3];

/* Sign-in roster: same card geometry / snap UX as the message carousel. */
static lv_obj_t *s_roster_scroll;
static lv_obj_t *s_roster_cards[V1_USER_MAX];
static int s_roster_user_at[V1_USER_MAX];
static int s_roster_n;
static int s_roster_focus;
static bool s_roster_scroll_lock;
static int32_t s_roster_snap_target;

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
    s_login_order_n = 0;
    memset(s_login_order, 0, sizeof(s_login_order));
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

static void login_order_persist(nvs_handle_t h)
{
    char blob[V1_USER_MAX * 16];
    size_t pos = 0;
    blob[0] = 0;
    for (int i = 0; i < s_login_order_n; i++) {
        size_t len = strlen(s_login_order[i]);
        if (len == 0 || pos + len + 2 > sizeof(blob)) {
            break;
        }
        if (pos > 0) {
            blob[pos++] = ',';
        }
        memcpy(blob + pos, s_login_order[i], len);
        pos += len;
        blob[pos] = 0;
    }
    (void)nvs_set_str(h, "login_ord", blob);
}

static void login_order_load_from_str(const char *blob)
{
    s_login_order_n = 0;
    memset(s_login_order, 0, sizeof(s_login_order));
    if (!blob || !blob[0]) {
        return;
    }
    const char *p = blob;
    while (*p && s_login_order_n < V1_USER_MAX) {
        while (*p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && *p != ',') {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len >= sizeof(s_login_order[0])) {
            len = sizeof(s_login_order[0]) - 1;
        }
        if (len > 0) {
            memcpy(s_login_order[s_login_order_n], start, len);
            s_login_order[s_login_order_n][len] = 0;
            s_login_order_n++;
        }
    }
}

static void login_order_note(const char *id)
{
    if (!id || !id[0]) {
        return;
    }
    for (int i = 0; i < s_login_order_n; i++) {
        if (strcmp(s_login_order[i], id) == 0) {
            for (int j = i; j < s_login_order_n - 1; j++) {
                memcpy(s_login_order[j], s_login_order[j + 1], sizeof(s_login_order[0]));
            }
            s_login_order_n--;
            break;
        }
    }
    if (s_login_order_n < V1_USER_MAX) {
        for (int i = s_login_order_n; i > 0; i--) {
            memcpy(s_login_order[i], s_login_order[i - 1], sizeof(s_login_order[0]));
        }
        s_login_order_n++;
    } else {
        for (int i = V1_USER_MAX - 1; i > 0; i--) {
            memcpy(s_login_order[i], s_login_order[i - 1], sizeof(s_login_order[0]));
        }
    }
    strncpy(s_login_order[0], id, sizeof(s_login_order[0]) - 1);
    s_login_order[0][sizeof(s_login_order[0]) - 1] = 0;
}

/** Fill out[] with s_users indices: recent logins first, then hangout order. */
static int roster_build_order(int *out)
{
    bool used[V1_USER_MAX] = {0};
    int n = 0;
    for (int i = 0; i < s_login_order_n && n < s_user_n; i++) {
        int idx = v1_connect_user_index(s_login_order[i]);
        if (idx < 0 || used[idx]) {
            continue;
        }
        used[idx] = true;
        out[n++] = idx;
    }
    for (int i = 0; i < s_user_n && n < s_user_n; i++) {
        if (used[i]) {
            continue;
        }
        out[n++] = i;
    }
    return n;
}

void v1_connect_nvs_load_last(char *last_user, size_t cap)
{
    if (!last_user || cap == 0) {
        return;
    }
    last_user[0] = 0;
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = cap;
    (void)nvs_get_str(h, "last", last_user, &n);
    char ord[V1_USER_MAX * 16];
    size_t ord_n = sizeof(ord);
    if (nvs_get_str(h, "login_ord", ord, &ord_n) == ESP_OK) {
        login_order_load_from_str(ord);
    } else if (last_user[0]) {
        /* Migrate single last-user into order list. */
        login_order_note(last_user);
    }
    nvs_close(h);
    strncpy(s_last_user, last_user, sizeof(s_last_user) - 1);
    s_last_user[sizeof(s_last_user) - 1] = 0;
    if (!s_last_user[0] && s_login_order_n > 0) {
        strncpy(s_last_user, s_login_order[0], sizeof(s_last_user) - 1);
        s_last_user[sizeof(s_last_user) - 1] = 0;
        if (cap > 0) {
            strncpy(last_user, s_last_user, cap - 1);
            last_user[cap - 1] = 0;
        }
    }
}

void v1_connect_nvs_save_last(const char *id)
{
    if (!id || !id[0]) {
        return;
    }
    login_order_note(id);
    strncpy(s_last_user, id, sizeof(s_last_user) - 1);
    s_last_user[sizeof(s_last_user) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_str(h, "last", id);
    login_order_persist(h);
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
        if (v1_state_get() == ST_CONNECTING && !v1_auth_login_active() && s_user_n > 0) {
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
    } else if (s_server_online && st == ST_CONNECTING && !v1_auth_login_active() &&
               s_user_n > 0) {
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

static void roster_clear_ui(void)
{
    s_roster_scroll = NULL;
    memset(s_roster_cards, 0, sizeof(s_roster_cards));
    memset(s_roster_user_at, 0, sizeof(s_roster_user_at));
    s_roster_n = 0;
    s_roster_scroll_lock = false;
}

static void roster_scroll_x_exec(void *obj, int32_t v);

/** Stop snap anims and drop handles before another screen lv_obj_clean's the tree. */
void v1_connect_invalidate_roster(void)
{
    if (s_roster_scroll) {
        lv_anim_delete(s_roster_scroll, roster_scroll_x_exec);
    }
    roster_clear_ui();
    /* Roster ribbons are destroyed with the screen; never leave a dangling lab. */
    s_offline_lab = NULL;
}

static void apply_roster_card_grad(lv_obj_t *card)
{
    static const struct {
        uint8_t id;
        uint32_t top;
        uint32_t bot;
    } grads[V1_CARD_GRAD_N] = {
        {1, 0x5AA0E8, 0x101418},
        {2, 0xE85A5A, 0xE8C040},
        {5, 0xF0F2F5, 0x8898A8},
    };
    uint8_t id = v1_carousel_card_grad();
    const uint32_t *top = &grads[0].top;
    const uint32_t *bot = &grads[0].bot;
    for (int i = 0; i < V1_CARD_GRAD_N; i++) {
        if (grads[i].id == id) {
            top = &grads[i].top;
            bot = &grads[i].bot;
            break;
        }
    }
    lv_obj_set_style_bg_color(card, lv_color_hex(*top), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(*bot), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
}

static int32_t roster_snap_target_x(lv_obj_t *card, lv_obj_t *scroller)
{
    int32_t card_x = lv_obj_get_x(card);
    int32_t card_w = lv_obj_get_width(card);
    int32_t view_w = lv_obj_get_width(scroller);
    int32_t target = card_x + card_w / 2 - view_w / 2;
    int32_t content_right = 0;
    uint32_t n = lv_obj_get_child_cnt(scroller);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(scroller, i);
        int32_t right = lv_obj_get_x(ch) + lv_obj_get_width(ch);
        if (right > content_right) {
            content_right = right;
        }
    }
    int32_t max_x = content_right - view_w;
    if (max_x < 0) {
        max_x = 0;
    }
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    return target;
}

static int roster_center_index(void)
{
    if (!s_roster_scroll || s_roster_n <= 0) {
        return s_roster_focus;
    }
    int32_t mid = lv_obj_get_scroll_x(s_roster_scroll) + lv_obj_get_width(s_roster_scroll) / 2;
    int best = 0;
    int32_t best_dist = INT32_MAX;
    for (int i = 0; i < s_roster_n; i++) {
        lv_obj_t *ch = s_roster_cards[i];
        if (!ch) {
            continue;
        }
        int32_t center = lv_obj_get_x(ch) + lv_obj_get_width(ch) / 2;
        int32_t dist = center > mid ? center - mid : mid - center;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static void roster_refresh_visuals(void)
{
    if (!s_roster_scroll || s_roster_n <= 0) {
        return;
    }
    int visual = roster_center_index();
    for (int i = 0; i < s_roster_n; i++) {
        lv_obj_t *card = s_roster_cards[i];
        if (!card) {
            continue;
        }
        /* Opacity only — scale layers blow the 64 KB LVGL heap (same as carousel). */
        if (i == visual) {
            lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_opa(card, LV_OPA_50, 0);
        }
    }
}

static void roster_scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
    if (v1_state_get() == ST_ROSTER) {
        roster_refresh_visuals();
    }
}

static void roster_snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    if (scroller != NULL) {
        lv_obj_scroll_to_x(scroller, s_roster_snap_target, LV_ANIM_OFF);
    }
    s_roster_scroll_lock = false;
    roster_refresh_visuals();
}

static void roster_snap_scroll_to(lv_obj_t *scroller, int32_t target)
{
    int32_t start = lv_obj_get_scroll_x(scroller);

    lv_anim_delete(scroller, roster_scroll_x_exec);
    s_roster_snap_target = target;
    s_roster_scroll_lock = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroller);
    lv_anim_set_user_data(&a, scroller);
    int32_t delta = target > start ? target - start : start - target;
    int duration = V1_CAROUSEL_SNAP_MS_MIN + (int)(delta * 2 / 5);
    if (duration > V1_CAROUSEL_SNAP_MS_MAX) {
        duration = V1_CAROUSEL_SNAP_MS_MAX;
    }
    lv_anim_set_values(&a, start, target);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, roster_scroll_x_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, roster_snap_anim_done);
    lv_anim_start(&a);
}

static void roster_snap_to_focus(void)
{
    if (s_roster_focus < 0 || s_roster_focus >= s_user_n) {
        return;
    }
    lv_obj_t *card = s_roster_cards[s_roster_focus];
    if (!card || !s_roster_scroll) {
        return;
    }
    int32_t target = roster_snap_target_x(card, s_roster_scroll);
    if (lv_obj_get_scroll_x(s_roster_scroll) == target) {
        s_roster_scroll_lock = false;
        roster_refresh_visuals();
        return;
    }
    roster_snap_scroll_to(s_roster_scroll, target);
}

static void on_roster_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL) {
        roster_refresh_visuals();
        return;
    }
    if (code == LV_EVENT_SCROLL_END && !s_roster_scroll_lock) {
        s_roster_focus = roster_center_index();
        roster_snap_to_focus();
    }
}

static void on_roster_card_click(lv_event_t *e)
{
    intptr_t display_idx = (intptr_t)lv_event_get_user_data(e);
    if (display_idx < 0 || (int)display_idx >= s_roster_n) {
        return;
    }
    lv_obj_t *card = s_roster_cards[display_idx];
    if (!card || !s_roster_scroll) {
        return;
    }
    if ((int)display_idx != s_roster_focus) {
        s_roster_focus = (int)display_idx;
        roster_snap_scroll_to(s_roster_scroll, roster_snap_target_x(card, s_roster_scroll));
        v1_ui_request_chirp(784);
        v1_ui_bump_activity();
        return;
    }
    int user_idx = s_roster_user_at[display_idx];
    if (user_idx < 0 || user_idx >= s_user_n) {
        return;
    }
    /* Centered card tap → PIN for that user. */
    v1_connect_invalidate_roster();
    v1_auth_on_roster_pick(s_users[user_idx].id);
    v1_ui_bump_activity();
}

static void roster_add_card(int display_idx, int user_idx, int x)
{
    lv_obj_t *card = lv_obj_create(s_roster_scroll);
    s_roster_cards[display_idx] = card;
    s_roster_user_at[display_idx] = user_idx;
    lv_obj_set_size(card, V1_SCROLL_CARD_W, V1_SCROLL_CARD_H);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_radius(card, V1_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    apply_roster_card_grad(card);
    if (s_last_user[0] && strcmp(s_users[user_idx].id, s_last_user) == 0) {
        lv_obj_set_style_border_width(card, 2, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(V1_UI_ACCENT), 0);
    }
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_roster_card_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)display_idx);

    /* Portrait 3x message-card size; name +2pt over default (14→16). */
    const int face_sz = V1_CARD_PORTRAIT * 3;
    v1_ui_paint_user_portrait_aligned(card, user_idx, face_sz,
                                      LV_ALIGN_CENTER, 0, -14);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, s_users[user_idx].name);
    lv_obj_set_style_text_color(name, lv_color_hex(V1_UI_TEXT), 0);
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
#endif
    lv_obj_set_width(name, V1_SCROLL_CARD_W - 16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void v1_connect_paint_roster(lv_obj_t *scr)
{
    if (v1_connect_awaiting_server("", v1_state_get()) || s_user_n <= 0) {
        v1_connect_paint_connecting(scr);
        return;
    }

    v1_connect_invalidate_roster();
    lv_obj_clean(scr);
    v1_connect_clear_conn_dots();
    s_offline_lab = NULL;
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    /* Same ribbon + header + card band as the message carousel. */
    lv_obj_t *ribbon_top, *ribbon_bot, *count_lab, *offline_lab, *toast;
    v1_ui_paint_ribbons(scr, true, "", &ribbon_top, &ribbon_bot, &count_lab,
                        &offline_lab, &toast);
    s_offline_lab = offline_lab;
    if (count_lab) {
        lv_label_set_text(count_lab, "");
    }
    v1_ui_bind_toast(toast);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Sign In");
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#elif defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
#endif
    lv_obj_set_width(title, V1_LCD_W - V1_CAROUSEL_PLAY_PAD * 2);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    /* Left-aligned, vertically centered in upper third (same slot as sender name). */
    lv_obj_align(title, LV_ALIGN_LEFT_MID, V1_CAROUSEL_PLAY_PAD,
                 (V1_CAROUSEL_HEADER_Y + V1_CAROUSEL_HEADER_H / 2)
                     - (V1_CONTENT_H + 2 * V1_RIBBON_H) / 2);

    s_roster_scroll = lv_obj_create(scr);
    lv_obj_set_pos(s_roster_scroll, 0, V1_SCROLL_CARD_Y);
    lv_obj_set_size(s_roster_scroll, V1_LCD_W, V1_SCROLL_CARD_H);
    lv_obj_set_style_bg_opa(s_roster_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_roster_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_roster_scroll, 0, 0);
    lv_obj_add_flag(s_roster_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_roster_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_roster_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_roster_scroll, on_roster_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_roster_scroll, on_roster_scroll, LV_EVENT_SCROLL_END, NULL);

    s_roster_focus = 0;
    int order[V1_USER_MAX];
    s_roster_n = roster_build_order(order);

    int x = V1_SCROLL_CARD_GAP;
    for (int i = 0; i < s_roster_n; i++) {
        roster_add_card(i, order[i], x);
        x += V1_SCROLL_CARD_W + V1_SCROLL_CARD_GAP;
    }
    lv_obj_t *end = lv_obj_create(s_roster_scroll);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, x, 0);

    if (s_roster_n > 0) {
        /* Instant center on first paint (most recent login), then opacity pass. */
        lv_obj_t *focus_card = s_roster_cards[s_roster_focus];
        if (focus_card) {
            int32_t target = roster_snap_target_x(focus_card, s_roster_scroll);
            lv_obj_scroll_to_x(s_roster_scroll, target, LV_ANIM_OFF);
        }
        roster_refresh_visuals();
    }

    ESP_LOGI(TAG, "paint roster users=%d heap=%u", s_roster_n,
             (unsigned)esp_get_free_heap_size());
    v1_ui_hook_scr(scr);
}

void v1_connect_refresh_offline_ribbon(const char *session_user)
{
    if (!s_offline_lab) {
        return;
    }
    /* Guard against use-after-free if a prior screen left a stale pointer. */
    if (!lv_obj_is_valid(s_offline_lab)) {
        s_offline_lab = NULL;
        return;
    }
    if (s_server_online || !session_user || !session_user[0]) {
        lv_obj_add_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_offline_lab, LV_OBJ_FLAG_HIDDEN);
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
