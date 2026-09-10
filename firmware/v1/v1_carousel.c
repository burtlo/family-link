#include "v1_carousel.h"

#include "pass.h"
#include "v1_api.h"
#include "v1_record.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "board.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "http_bearer.h"
#include "lvgl.h"
#include "nvs.h"
#include "who.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "v1_carousel";

#define CHUNK 640
#define PLAY_ICON_W V1_CARD_PLAY_ICON
#define PLAY_ICON_H V1_CARD_PLAY_ICON
#define V1_CHIRP_SAMPLES (V1_SAMPLE_RATE * V1_CHIRP_MS / 1000)
#define V1_CHIRP_DRAIN   (V1_SAMPLE_RATE * V1_CHIRP_DRAIN_MS / 1000)

typedef struct {
    uint8_t id;
    uint32_t top;
    uint32_t bot;
    bool light_ui;
} card_grad_t;

typedef struct {
    lv_obj_t *card;
} carousel_card_ui_t;

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = V1_SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static v1_carousel_cfg_t s_cfg;
static char *s_session;
static msg_t s_inbox_msgs[V1_MSG_MAX];
static int s_inbox_n;
static int s_inbox_focus;

static int64_t s_carousel_ready_us;
static uint8_t s_card_grad = 1;
static bool s_scroll_lock;
static bool s_carousel_locked;
static int32_t s_snap_target;
static int64_t s_snap_t0;
static bool s_send_hint_shown;
static bool s_passed;
static volatile bool s_stop_play;
static volatile bool s_want_play;
static bool s_playing;
static int s_play_pos_ms;
static bool s_scrubbing;
static bool s_bar_sync;
static volatile bool s_inbox_dirty;
static char s_inbox_for[16];
static int s_card_face_seq;
static bool s_card_face_read;
static bool s_spk_open;

static lv_obj_t *s_ribbon_top;
static lv_obj_t *s_ribbon_bot;
static lv_obj_t *s_count_lab;
static lv_obj_t *s_carousel_scroll;
static carousel_card_ui_t s_card_ui[V1_MSG_MAX];
static lv_obj_t *s_bar;
static lv_obj_t *s_play_btn;
static lv_obj_t *s_play_icon;
static lv_obj_t *s_pause_icon;
static uint16_t s_play_icon_fb[V1_CARD_PLAY_ICON * V1_CARD_PLAY_ICON];
static lv_obj_t *s_offline_lab;
static lv_obj_t *s_toast;

static int16_t s_chirp_pcm[V1_CHIRP_SAMPLES + V1_CHIRP_DRAIN];

static const card_grad_t s_card_grads[V1_CARD_GRAD_N] = {
    {1, 0x5AA0E8, 0x101418, false},
    {2, 0xE85A5A, 0xE8C040, false},
    {5, 0xF0F2F5, 0x8898A8, true},
};

static int64_t now_us(void) { return esp_timer_get_time(); }

static msg_t *focus_msg(void);
static lv_obj_t *carousel_card_at(int idx);
static const char *sender_display_name(const msg_t *m);
static void stop_playback(void);
static void playback_task(void *arg);
static void refresh_offline_ribbon(void);
static void paint_carousel(lv_obj_t *scr);
static void ui_refresh_transport(void);
static void apply_volume(int vol);
static int roomvol_codec(int notch);
static void play_chirp(int hz);
static void play_chirp_pair(int a, int b);
static void nvs_load_card_grad(void);
static void nvs_save_card_grad(void);

void v1_carousel_init(const v1_carousel_cfg_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
        s_session = cfg->session_user;
    }
}

void v1_carousel_bind_inbox(msg_t *msgs, int *msg_n, int *focus, int msg_max)
{
    (void)msgs;
    (void)msg_n;
    (void)focus;
    v1_api_inbox_bind(s_inbox_msgs, &s_inbox_n, &s_inbox_focus, msg_max);
}

void v1_carousel_on_auth_ok(void)
{
    if (!s_send_hint_shown) {
        s_send_hint_shown = true;
        v1_ui_set_toast(NULL, "tap circle to send");
    }
    s_carousel_ready_us = now_us();
    if (!s_passed) {
        s_passed = true;
        demo_pass("x02");
    }
}

void v1_carousel_on_ws_inbox(const char *user_id)
{
    s_inbox_for[0] = 0;
    if (user_id && user_id[0]) {
        strncpy(s_inbox_for, user_id, sizeof(s_inbox_for) - 1);
    }
    s_inbox_dirty = true;
}

void v1_carousel_stop_playback(void) { stop_playback(); }
bool v1_carousel_is_playing(void) { return s_playing; }

void v1_carousel_start_tasks(void)
{
    xTaskCreate(playback_task, "play", 12288, NULL, 5, NULL);
}

void v1_carousel_refresh_offline_ribbon(void) { refresh_offline_ribbon(); }
void v1_carousel_paint(lv_obj_t *scr) { paint_carousel(scr); }
void v1_carousel_refresh_transport(void) { ui_refresh_transport(); }
void v1_carousel_apply_volume(int vol) { apply_volume(vol); }
int v1_carousel_roomvol_codec(int notch) { return roomvol_codec(notch); }
void v1_carousel_play_chirp(int hz) { play_chirp(hz); }
void v1_carousel_play_chirp_pair(int a, int b) { play_chirp_pair(a, b); }
void v1_carousel_nvs_load_card_grad(void) { nvs_load_card_grad(); }
void v1_carousel_save_card_grad(void) { nvs_save_card_grad(); }
uint8_t v1_carousel_card_grad(void) { return s_card_grad; }
void v1_carousel_set_card_grad(uint8_t id) { s_card_grad = id; }

void v1_carousel_on_circle_press(int64_t t_us)
{
    (void)t_us;
    if (v1_state_get() != ST_CAROUSEL) {
        return;
    }
    if (s_playing || s_scrubbing) {
        return;
    }
    if (!v1_connect_online()) {
        v1_ui_set_toast(NULL, "can't send right now");
        return;
    }
    if ((now_us() - s_carousel_ready_us) < (int64_t)V1_CIRCLE_DEBOUNCE_MS * 1000) {
        return;
    }
    v1_record_open_pick(now_us());
}

void v1_carousel_on_shoulder_press(void)
{
    state_t st = v1_state_get();
    if (st == ST_CAROUSEL) {
        stop_playback();
        v1_state_post_goto(ST_SETTINGS);
        v1_ui_request_repaint();
    } else if (st == ST_SETTINGS) {
        v1_state_post_goto(ST_CAROUSEL);
        v1_ui_request_repaint();
    }
}

bool v1_carousel_tick_inbox(void)
{
    if (!s_inbox_dirty || !s_session || !s_session[0]) {
        return false;
    }
    if (s_inbox_for[0] != 0 && strcmp(s_inbox_for, s_session) != 0) {
        return false;
    }
    s_inbox_dirty = false;
    state_t st = v1_state_get();
    if (st == ST_RECORD) {
        s_inbox_dirty = true;
        return false;
    }
    int keep = -1;
    msg_t *m = focus_msg();
    if (m) {
        keep = m->seq;
    }
    if (st == ST_CAROUSEL || st == ST_SETTINGS) {
        stop_playback();
    }
    if (v1_api_reload_inbox(s_session)) {
        if (keep > 0) {
            for (int i = 0; i < s_inbox_n; i++) {
                if (s_inbox_msgs[i].seq == keep) {
                    s_inbox_focus = i;
                    break;
                }
            }
        }
        if (st == ST_CAROUSEL) {
            v1_ui_request_chirp(784);
            v1_ui_request_repaint();
        }
        return true;
    }
    return false;
}

static int roomvol_codec(int notch)
{
    if (notch <= 0) {
        return 0;
    }
    int v = V1_ROOMVOL_FIRST + (notch - 1) * V1_ROOMVOL_STEP;
    return v > V1_ROOMVOL_MAX ? V1_ROOMVOL_MAX : v;
}


static void apply_volume(int vol)
{
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    (*s_cfg.volume) = vol;
    if (s_cfg.spk) {
        (void)esp_codec_dev_set_out_vol(s_cfg.spk, vol);
        (void)esp_codec_dev_set_out_mute(s_cfg.spk, vol == 0);
    }
}


static bool card_grad_id_valid(uint8_t id)
{
    for (int i = 0; i < V1_CARD_GRAD_N; i++) {
        if (s_card_grads[i].id == id) {
            return true;
        }
    }
    return false;
}

static const card_grad_t *active_card_grad(void)
{
    for (int i = 0; i < V1_CARD_GRAD_N; i++) {
        if (s_card_grads[i].id == s_card_grad) {
            return &s_card_grads[i];
        }
    }
    return &s_card_grads[0];
}


static void nvs_load_card_grad(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t g = 1;
    if (nvs_get_u8(h, "card_grad", &g) == ESP_OK && card_grad_id_valid(g)) {
        s_card_grad = g;
    }
    nvs_close(h);
}


static void nvs_save_card_grad(void)
{
    nvs_handle_t h;
    if (nvs_open("x02", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(h, "card_grad", s_card_grad);
    (void)nvs_commit(h);
    nvs_close(h);
}


static void apply_card_grad_style(lv_obj_t *card)
{
    const card_grad_t *g = active_card_grad();
    lv_obj_set_style_bg_color(card, lv_color_hex(g->top), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(g->bot), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
}


static void style_msg_card(lv_obj_t *card)
{
    lv_obj_set_style_border_width(card, 0, 0);
}


static int http_blob_get(int seq, http_buf_t *body)
{
    char path[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/blob", seq);
    return v1_api_http_json("GET", path, NULL, s_session, body);
}



static void save_position(int seq, int pos_ms)
{
    char path[48];
    char js[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/position", seq);
    snprintf(js, sizeof(js), "{\"position_ms\":%d}", pos_ms);
    uint8_t tmp[32];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", path, js, s_session, &b);
}


static void mark_read(int seq, int pos_ms)
{
    char path[48];
    char js[48];
    snprintf(path, sizeof(path), "/v1/messages/%d/read", seq);
    snprintf(js, sizeof(js), "{\"position_ms\":%d}", pos_ms);
    uint8_t tmp[32];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", path, js, s_session, &b);
    for (int i = 0; i < s_inbox_n; i++) {
        if (s_inbox_msgs[i].seq == seq) {
            s_inbox_msgs[i].read = true;
            s_inbox_msgs[i].position_ms = pos_ms;
            break;
        }
    }
}


static void chirp_fade_edges(int16_t *pcm, int n)
{
    int fade = n / 8;
    if (fade < 8) {
        fade = 8;
    }
    if (fade * 2 > n) {
        fade = n / 4;
    }
    for (int i = 0; i < fade; i++) {
        pcm[i] = (int16_t)((int)pcm[i] * i / fade);
        pcm[n - 1 - i] = (int16_t)((int)pcm[n - 1 - i] * i / fade);
    }
}


static bool chirp_write_pcm(const int16_t *pcm, int samples)
{
    const uint8_t *p = (const uint8_t *)pcm;
    int bytes = samples * 2;
    while (bytes > 0) {
        int n = bytes > V1_CHIRP_WRITE ? V1_CHIRP_WRITE : bytes;
        if (esp_codec_dev_write(s_cfg.spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            return false;
        }
        p += n;
        bytes -= n;
    }
    return true;
}


static void play_chirp(int hz)
{
    if (!s_cfg.spk || hz <= 0) {
        return;
    }
    stop_playback();
    int n = V1_CHIRP_SAMPLES;
    int drain = V1_CHIRP_DRAIN;
    int half = V1_SAMPLE_RATE / (hz * 2);
    if (half < 1) {
        half = 1;
    }
    int sign = 1;
    int left = half;
    for (int i = 0; i < n; i++) {
        s_chirp_pcm[i] = (int16_t)(sign * 8000);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    chirp_fade_edges(s_chirp_pcm, n);
    memset(s_chirp_pcm + n, 0, (size_t)drain * sizeof(int16_t));

    if (esp_codec_dev_open(s_cfg.spk, &s_fs) != ESP_OK) {
        ESP_LOGW(TAG, "chirp speaker open failed");
        return;
    }
    s_spk_open = true;
    (void)esp_codec_dev_set_out_vol(s_cfg.spk, V1_CHIRP_VOL);
    (void)esp_codec_dev_set_out_mute(s_cfg.spk, false);
    vTaskDelay(pdMS_TO_TICKS(30));

    if (!chirp_write_pcm(s_chirp_pcm, n + drain)) {
        ESP_LOGW(TAG, "chirp write failed");
    }
    (void)esp_codec_dev_close(s_cfg.spk);
    s_spk_open = false;
    apply_volume((*s_cfg.volume));
}


static void play_chirp_pair(int a, int b)
{
    play_chirp(a);
    if (b > 0) {
        play_chirp(b);
    }
}


static void stop_playback(void)
{
    s_stop_play = true;
    while (s_playing) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_stop_play = false;
}


static bool message_at_end(const msg_t *m)
{
    if (!m || m->duration_ms <= 0) {
        return false;
    }
    return m->position_ms >= (m->duration_ms - 150);
}


static void restart_message_if_at_end(msg_t *m)
{
    if (!message_at_end(m)) {
        return;
    }
    m->position_ms = 0;
    save_position(m->seq, 0);
}

static int32_t snap_target_x(lv_obj_t *card, lv_obj_t *scroller)
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


static int carousel_center_index(void)
{
    if (!s_carousel_scroll || s_inbox_n <= 0) {
        return s_inbox_focus;
    }
    int32_t mid = lv_obj_get_scroll_x(s_carousel_scroll) + lv_obj_get_width(s_carousel_scroll) / 2;
    int best = 0;
    int32_t best_dist = INT32_MAX;
    for (int i = 0; i < s_inbox_n; i++) {
        lv_obj_t *ch = carousel_card_at(i);
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


static void refresh_card_visuals_from_scroll(void)
{
    if (!s_carousel_scroll || s_inbox_n <= 0) {
        return;
    }
    int visual = carousel_center_index();
    for (int i = 0; i < s_inbox_n; i++) {
        lv_obj_t *card = s_card_ui[i].card;
        if (!card) {
            continue;
        }
        if (i == visual) {
            lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_transform_scale(card, 256, 0);
        } else {
            lv_obj_set_style_opa(card, LV_OPA_50, 0);
            lv_obj_set_style_transform_scale(card, 230, 0);
        }
    }
}


static void scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
    if (v1_state_get() == ST_CAROUSEL) {
        refresh_card_visuals_from_scroll();
    }
}


static void snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    if (scroller != NULL) {
        lv_obj_scroll_to_x(scroller, s_snap_target, LV_ANIM_OFF);
    }
    s_scroll_lock = false;
    s_carousel_locked = true;
    ui_refresh_transport();
    (void)s_snap_t0;
}


static void snap_scroll_to(lv_obj_t *scroller, int32_t target)
{
    int32_t start = lv_obj_get_scroll_x(scroller);

    lv_anim_delete(scroller, scroll_x_exec);
    s_snap_target = target;
    s_scroll_lock = true;
    s_carousel_locked = false;
    ui_refresh_transport();
    s_snap_t0 = esp_timer_get_time();
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
    lv_anim_set_exec_cb(&a, scroll_x_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, snap_anim_done);
    lv_anim_start(&a);
}


static void carousel_snap_to_focus(void)
{
    lv_obj_t *card = carousel_card_at(s_inbox_focus);
    if (!card || !s_carousel_scroll) {
        return;
    }
    int32_t target = snap_target_x(card, s_carousel_scroll);
    if (lv_obj_get_scroll_x(s_carousel_scroll) == target) {
        s_scroll_lock = false;
        s_carousel_locked = true;
        ui_refresh_transport();
        return;
    }
    snap_scroll_to(s_carousel_scroll, target);
}


static void sync_focus_from_scroll(void)
{
    if (s_scroll_lock || !s_carousel_scroll || s_inbox_n <= 0) {
        return;
    }
    int32_t mid = lv_obj_get_scroll_x(s_carousel_scroll) + lv_obj_get_width(s_carousel_scroll) / 2;
    int best = s_inbox_focus;
    int32_t best_dist = -1;
    for (int i = 0; i < s_inbox_n; i++) {
        lv_obj_t *ch = carousel_card_at(i);
        if (!ch) {
            continue;
        }
        int32_t center = lv_obj_get_x(ch) + lv_obj_get_width(ch) / 2;
        int32_t dist = center > mid ? center - mid : mid - center;
        if (best_dist < 0 || dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    if (best == s_inbox_focus) {
        return;
    }
    stop_playback();
    s_inbox_focus = best;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_inbox_msgs[s_inbox_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", "/v1/session/view", js, s_session, &b);
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        v1_ui_request_chirp(784);
    }
}


static void set_transport_icon(bool playing)
{
    if (s_play_icon) {
        if (playing) {
            lv_obj_add_flag(s_play_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_play_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_pause_icon) {
        if (playing) {
            lv_obj_clear_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


static void refresh_card_focus_states(void)
{
    if (s_scroll_lock) {
        refresh_card_visuals_from_scroll();
    } else {
        for (int i = 0; i < s_inbox_n; i++) {
            lv_obj_t *card = s_card_ui[i].card;
            if (!card) {
                continue;
            }
            if (i == s_inbox_focus) {
                lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
                lv_obj_set_style_transform_scale(card, 256, 0);
            } else {
                lv_obj_set_style_opa(card, LV_OPA_50, 0);
                lv_obj_set_style_transform_scale(card, 230, 0);
            }
        }
    }
    for (int i = 0; i < s_inbox_n; i++) {
        lv_obj_t *card = s_card_ui[i].card;
        if (!card) {
            continue;
        }
        if (i == s_inbox_focus && !s_scroll_lock) {
            lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    bool transport = s_carousel_locked && !s_scroll_lock && s_inbox_n > 0;
    if (s_play_btn) {
        if (transport) {
            lv_obj_add_flag(s_play_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_play_btn, LV_OPA_COVER, 0);
        } else {
            lv_obj_remove_flag(s_play_btn, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_play_btn, LV_OPA_50, 0);
        }
    }
    if (s_bar) {
        if (transport) {
            lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_bar, LV_OPA_COVER, 0);
        } else {
            lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_opa(s_bar, LV_OPA_50, 0);
        }
    }
}

static lv_obj_t *make_pause_icon(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 20, 20);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = lv_obj_create(box);
    lv_obj_set_size(l, 4, 16);
    lv_obj_set_style_bg_color(l, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(l, LV_ALIGN_CENTER, -5, 0);
    lv_obj_t *r = lv_obj_create(box);
    lv_obj_set_size(r, 4, 16);
    lv_obj_set_style_bg_color(r, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(r, LV_ALIGN_CENTER, 5, 0);
    lv_obj_center(box);
    return box;
}


static void style_transport_slider(lv_obj_t *sl)
{
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x2A3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x4ECDC4), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xE8F0E8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 4, LV_PART_KNOB);
}


static void ui_refresh_transport(void)
{
    msg_t *m = focus_msg();
    refresh_card_focus_states();
    if (!m) {
        return;
    }
    if (s_bar && !s_scrubbing) {
        int dur = m->duration_ms > 0 ? m->duration_ms : 1;
        s_bar_sync = true;
        lv_slider_set_range(s_bar, 0, dur);
        lv_slider_set_value(s_bar, s_playing ? s_play_pos_ms : m->position_ms, LV_ANIM_OFF);
        s_bar_sync = false;
    }
    set_transport_icon(s_playing);
    if (m->seq != s_card_face_seq || m->read != s_card_face_read) {
        s_card_face_seq = m->seq;
        s_card_face_read = m->read;
    }
    if (s_count_lab) {
        char who[16];
        snprintf(who, sizeof(who), "%d/%d",
                 s_inbox_n > 0 ? s_inbox_focus + 1 : 0, s_inbox_n);
        lv_label_set_text(s_count_lab, who);
    }
}


static void playback_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_want_play) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        s_want_play = false;
        msg_t *m = focus_msg();
        if (!m || s_inbox_n <= 0) {
            continue;
        }
        if (s_playing) {
            stop_playback();
            continue;
        }
        http_buf_t b = { .buf = s_cfg.playback_buf, .cap = V1_PLAYBACK_BUF_CAP, .len = 0 };
        if (http_blob_get(m->seq, &b) != 200 || b.len < 64) {
            if (!v1_connect_online()) {
                v1_ui_set_toast(NULL, "can't play right now");
            }
            continue;
        }
        const uint8_t *pcm = b.buf;
        int pcm_len = b.len;
        if (pcm_len > 44 && memcmp(pcm, "RIFF", 4) == 0) {
            pcm += 44;
            pcm_len -= 44;
        }
        int skip = (V1_RECORD_TRIM_MS * V1_SAMPLE_RATE * 2) / 1000;
        if (skip > pcm_len) {
            skip = 0;
        }
        const uint8_t *pcm_base = pcm + skip;
        int pcm_base_len = pcm_len - skip;
        restart_message_if_at_end(m);
        int offset_bytes = (m->position_ms * V1_SAMPLE_RATE * 2) / 1000;
        if (offset_bytes > pcm_base_len) {
            offset_bytes = 0;
            m->position_ms = 0;
        }
        pcm = pcm_base + offset_bytes;
        pcm_len = pcm_base_len - offset_bytes;
        if (pcm_len <= 0) {
            m->position_ms = 0;
            save_position(m->seq, 0);
            pcm = pcm_base;
            pcm_len = pcm_base_len;
        }
        if (!s_spk_open) {
            bool opened = false;
            for (int try = 0; try < 8; try++) {
                if (esp_codec_dev_open(s_cfg.spk, &s_fs) == ESP_OK) {
                    opened = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(40));
            }
            if (!opened) {
                s_want_play = true;
                continue;
            }
            s_spk_open = true;
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        apply_volume((*s_cfg.volume));
        s_playing = true;
        s_play_pos_ms = m->position_ms;
        int pos_bytes = 0;
        while (pos_bytes < pcm_len && !s_stop_play) {
            int chunk = pcm_len - pos_bytes;
            if (chunk > 2048) {
                chunk = 2048;
            }
            if (esp_codec_dev_write(s_cfg.spk, (void *)(pcm + pos_bytes), chunk) != ESP_CODEC_DEV_OK) {
                break;
            }
            pos_bytes += chunk;
            s_play_pos_ms = m->position_ms + (pos_bytes * 1000) / (V1_SAMPLE_RATE * 2);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        s_playing = false;
        m->position_ms = s_play_pos_ms;
        save_position(m->seq, s_play_pos_ms);
        if (!s_stop_play) {
            mark_read(m->seq, s_play_pos_ms);
        }
        v1_ui_request_transport_refresh();
    }
}


static void shift_focus_to(int idx)
{
    if (s_inbox_n <= 0 || idx < 0 || idx >= s_inbox_n) {
        return;
    }
    if (idx == s_inbox_focus) {
        return;
    }
    stop_playback();
    msg_t *m = focus_msg();
    if (m) {
        save_position(m->seq, m->position_ms);
    }
    s_inbox_focus = idx;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_inbox_msgs[s_inbox_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", "/v1/session/view", js, s_session, &b);
    carousel_snap_to_focus();
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        v1_ui_request_chirp(784);
    }
    v1_ui_bump_activity();
}


static void on_carousel_card_click(lv_event_t *e)
{
    lv_obj_t *card = lv_event_get_current_target(e);
    lv_obj_t *scroller = s_carousel_scroll;
    intptr_t idx = (intptr_t)lv_event_get_user_data(e);
    if ((int)idx == s_inbox_focus || !card || !scroller) {
        return;
    }
    stop_playback();
    msg_t *m = focus_msg();
    if (m) {
        save_position(m->seq, m->position_ms);
    }
    s_inbox_focus = (int)idx;
    char js[32];
    snprintf(js, sizeof(js), "{\"seq\":%d}", s_inbox_msgs[s_inbox_focus].seq);
    uint8_t tmp[64];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", "/v1/session/view", js, s_session, &b);
    snap_scroll_to(scroller, snap_target_x(card, scroller));
    ui_refresh_transport();
    if (!s_want_play && !s_playing) {
        v1_ui_request_chirp(784);
    }
    v1_ui_bump_activity();
}


static void on_carousel_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL) {
        refresh_card_visuals_from_scroll();
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        sync_focus_from_scroll();
        carousel_snap_to_focus();
    }
}


static void on_play(lv_event_t *e)
{
    (void)e;
    if (s_playing) {
        stop_playback();
        v1_ui_request_chirp(660);
    } else {
        msg_t *m = focus_msg();
        restart_message_if_at_end(m);
        v1_ui_request_chirp(0);
        s_want_play = true;
    }
    v1_ui_bump_activity();
}


static void on_scrub(lv_event_t *e)
{
    if (s_bar_sync || !s_bar || !s_carousel_locked || s_scroll_lock) {
        return;
    }
    if (lv_event_get_target(e) != s_bar) {
        return;
    }
    msg_t *m = focus_msg();
    if (!m) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_scrubbing = true;
        if (s_playing) {
            stop_playback();
        }
        v1_ui_bump_activity();
        return;
    }
    if (code == LV_EVENT_VALUE_CHANGED) {
        int pos = (int)lv_slider_get_value(s_bar);
        m->position_ms = pos;
        s_play_pos_ms = pos;
        return;
    }
    if (code == LV_EVENT_RELEASED) {
        save_position(m->seq, m->position_ms);
        s_scrubbing = false;
        v1_ui_bump_activity();
    }
}

#define PLAY_ICON_W 24
#define PLAY_ICON_H 24

static uint16_t rgb565_from_hex(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}


static void play_tri_rot_pt(int pivot_x, int pivot_y, int x, int y, int rot_tenths, int *ox, int *oy)
{
    int dx = x - pivot_x;
    int dy = y - pivot_y;

    switch (rot_tenths) {
    case 900:
        *ox = pivot_x + dy;
        *oy = pivot_y - dx;
        break;
    case 1800:
        *ox = pivot_x - dx;
        *oy = pivot_y - dy;
        break;
    case 2700:
        *ox = pivot_x - dy;
        *oy = pivot_y + dx;
        break;
    default:
        *ox = x;
        *oy = y;
        break;
    }
}


static int play_tri_edge(int ax, int ay, int bx, int by, int cx, int cy)
{
    return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
}


static void fill_play_icon_fb(uint16_t *fb, uint32_t disk_hex)
{
    const uint16_t bg = rgb565_from_hex(disk_hex);
    const uint16_t fg = rgb565_from_hex(0xFFFFFF);
    int x0 = 12;
    int y0 = 4;
    int x1 = 5;
    int y1 = 19;
    int x2 = 19;
    int y2 = 19;
    int pivot_x = (x0 + x1 + x2) / 3;
    int pivot_y = (y0 + y1 + y2) / 3;
    int ax;
    int ay;
    int bx;
    int by;
    int cx;
    int cy;

    play_tri_rot_pt(pivot_x, pivot_y, x0, y0, 2700, &ax, &ay);
    play_tri_rot_pt(pivot_x, pivot_y, x1, y1, 2700, &bx, &by);
    play_tri_rot_pt(pivot_x, pivot_y, x2, y2, 2700, &cx, &cy);

    for (int y = 0; y < PLAY_ICON_H; y++) {
        for (int x = 0; x < PLAY_ICON_W; x++) {
            int w0 = play_tri_edge(ax, ay, bx, by, x, y);
            int w1 = play_tri_edge(bx, by, cx, cy, x, y);
            int w2 = play_tri_edge(cx, cy, ax, ay, x, y);
            bool inside = (w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0);
            fb[y * PLAY_ICON_W + x] = inside ? fg : bg;
        }
    }
}

static lv_obj_t *make_play_icon_buf(lv_obj_t *parent, uint32_t disk_hex, uint16_t *fb)
{
    fill_play_icon_fb(fb, disk_hex);
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, fb, PLAY_ICON_W, PLAY_ICON_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    return canvas;
}


static void refresh_offline_ribbon(void)
{
    v1_connect_refresh_offline_ribbon(s_session);
}


static void paint_ribbons(lv_obj_t *scr)
{
    v1_ui_paint_ribbons(scr, v1_connect_online(), s_session,
                        &s_ribbon_top, &s_ribbon_bot, &s_count_lab, &s_offline_lab, &s_toast);
    v1_connect_set_offline_lab(s_offline_lab);
    v1_ui_bind_toast(s_toast);
}


static void carousel_add_card(int msg_idx, int x)
{
    msg_t *m = &s_inbox_msgs[msg_idx];
    const card_grad_t *g = active_card_grad();
    int uid = v1_connect_user_index_by_label(m->from_label);
    carousel_card_ui_t *ui = &s_card_ui[msg_idx];
    uint32_t text_hex = g->light_ui ? 0x101418U : 0xF0F4F0U;

    memset(ui, 0, sizeof(*ui));

    lv_obj_t *card = lv_obj_create(s_carousel_scroll);
    ui->card = card;
    lv_obj_set_size(card, V1_SCROLL_CARD_W, V1_SCROLL_CARD_H);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_radius(card, V1_CARD_RADIUS, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    apply_card_grad_style(card);
    style_msg_card(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_carousel_card_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)msg_idx);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(card, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(card, lv_pct(50), 0);

    v1_ui_paint_user_portrait_aligned(card, uid, V1_CARD_PORTRAIT, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, sender_display_name(m));
    lv_obj_set_style_text_color(name, lv_color_hex(text_hex), 0);
#if defined(LV_FONT_MONTSERRAT_14) && LV_FONT_MONTSERRAT_14
    lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
#elif defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
#endif
    lv_obj_set_width(name, V1_SCROLL_CARD_W - V1_CARD_PORTRAIT - 24);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 10 + V1_CARD_PORTRAIT + 6, 0);
}


static void save_profile(void)
{
    int idx = v1_connect_user_index(s_session);
    if (idx < 0 || !s_session[0]) {
        return;
    }
    char js[96];
    char hex[8];
    uint32_t c = v1_connect_user_accent(idx);
    snprintf(hex, sizeof(hex), "#%06X", (unsigned)(c & 0xFFFFFF));
    snprintf(js, sizeof(js), "{\"avatar_slot\":%u,\"accent_hex\":\"%s\"}",
             (unsigned)v1_connect_user_avatar_slot(idx), hex);
    uint8_t tmp[128];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", "/v1/profile", js, s_session, &b);
}


static void paint_carousel(lv_obj_t *scr)
{
    const card_grad_t *g = active_card_grad();
    uint32_t disk_hex = g->light_ui ? 0x101418U : 0x3A4450U;
    int32_t keep_scroll = 0;
    int keep_focus = s_inbox_focus;
    bool restore_scroll = s_carousel_scroll != NULL;
    if (restore_scroll) {
        keep_scroll = lv_obj_get_scroll_x(s_carousel_scroll);
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    s_carousel_scroll = NULL;
    memset(s_card_ui, 0, sizeof(s_card_ui));
    s_bar = NULL;
    s_play_btn = NULL;
    s_play_icon = NULL;
    s_pause_icon = NULL;
    s_ribbon_top = NULL;
    s_ribbon_bot = NULL;
    s_count_lab = NULL;
    s_card_face_seq = -1;
    s_card_face_read = false;
    s_scroll_lock = false;
    s_carousel_locked = false;

    paint_ribbons(scr);
    s_carousel_ready_us = now_us();

    s_carousel_scroll = lv_obj_create(scr);
    lv_obj_set_pos(s_carousel_scroll, 0, V1_SCROLL_CARD_Y);
    lv_obj_set_size(s_carousel_scroll, V1_LCD_W, V1_SCROLL_CARD_H);
    lv_obj_set_style_bg_opa(s_carousel_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_carousel_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_carousel_scroll, 0, 0);
    lv_obj_add_flag(s_carousel_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_carousel_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_carousel_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_carousel_scroll, on_carousel_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_carousel_scroll, on_carousel_scroll, LV_EVENT_SCROLL_END, NULL);

    int x = V1_SCROLL_CARD_GAP;
    for (int i = 0; i < s_inbox_n; i++) {
        carousel_add_card(i, x);
        x += V1_SCROLL_CARD_W + V1_SCROLL_CARD_GAP;
    }
    lv_obj_t *end = lv_obj_create(s_carousel_scroll);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, x, 0);

    s_play_btn = lv_button_create(scr);
    lv_obj_set_size(s_play_btn, V1_CAROUSEL_PLAY, V1_CAROUSEL_PLAY);
    lv_obj_align(s_play_btn, LV_ALIGN_TOP_MID, 0, V1_CAROUSEL_TRANSPORT_Y);
    lv_obj_set_style_bg_opa(s_play_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_play_btn, 0, 0);
    lv_obj_set_style_border_width(s_play_btn, 0, 0);
    lv_obj_set_style_pad_all(s_play_btn, 0, 0);
    lv_obj_add_event_cb(s_play_btn, on_play, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disk = lv_obj_create(s_play_btn);
    lv_obj_set_size(disk, V1_CAROUSEL_DISK, V1_CAROUSEL_DISK);
    lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disk, lv_color_hex(disk_hex), 0);
    lv_obj_set_style_bg_opa(disk, g->light_ui ? LV_OPA_20 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disk, 0, 0);
    lv_obj_set_style_pad_all(disk, 0, 0);
    lv_obj_clear_flag(disk, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(disk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(disk);

    s_play_icon = make_play_icon_buf(s_play_btn, disk_hex, s_play_icon_fb);
    s_pause_icon = make_pause_icon(s_play_btn);
    lv_obj_add_flag(s_pause_icon, LV_OBJ_FLAG_HIDDEN);

    s_bar = lv_slider_create(scr);
    lv_obj_set_size(s_bar, V1_SCROLL_CARD_W - 16, 12);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, V1_CAROUSEL_TRANSPORT_Y + V1_CAROUSEL_PLAY + 4);
    style_transport_slider(s_bar);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_bar, on_scrub, LV_EVENT_RELEASED, NULL);

    ui_refresh_transport();
    if (s_inbox_n > 0) {
        if (keep_focus >= 0 && keep_focus < s_inbox_n) {
            s_inbox_focus = keep_focus;
        }
        if (restore_scroll) {
            s_scroll_lock = true;
            lv_obj_scroll_to_x(s_carousel_scroll, keep_scroll, LV_ANIM_OFF);
            s_scroll_lock = false;
            s_carousel_locked = true;
            ui_refresh_transport();
        } else {
            carousel_snap_to_focus();
        }
    } else {
        s_carousel_locked = true;
        ui_refresh_transport();
    }
    v1_ui_hook_scr(scr);
}


static lv_obj_t *carousel_card_at(int idx)
{
    if (idx < 0 || idx >= s_inbox_n) {
        return NULL;
    }
    return s_card_ui[idx].card;
}

static const char *sender_display_name(const msg_t *m)
{
    if (!m) {
        return "Family";
    }
    int uid = v1_connect_user_index_by_label(m->from_label);
    int un = v1_connect_user_count();
    user_t *users = v1_connect_users_mut();
    if (uid >= 0 && uid < un && users[uid].name[0]) {
        return users[uid].name;
    }
    if (m->from_label[0]) {
        return m->from_label;
    }
    return "Family";
}

static msg_t *focus_msg(void)
{
    if (s_inbox_n <= 0) {
        return NULL;
    }
    if (s_inbox_focus < 0) {
        s_inbox_focus = 0;
    }
    if (s_inbox_focus >= s_inbox_n) {
        s_inbox_focus = s_inbox_n - 1;
    }
    return &s_inbox_msgs[s_inbox_focus];
}

msg_t *v1_carousel_focus_msg(void) { return focus_msg(); }
msg_t *v1_carousel_msgs(void) { return s_inbox_msgs; }
int v1_carousel_msg_n(void) { return s_inbox_n; }
int v1_carousel_focus_idx(void) { return s_inbox_focus; }
void v1_carousel_set_focus_idx(int idx) { s_inbox_focus = idx; }

