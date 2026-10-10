/*
 * h19 — Scrollable inbox list + screen transition showcase.
 *
 * Each row picks a list→detail transition; Back reverses it. Record entry uses a
 * red-circle bloom (list FAB) or ring iris wipe (physical BSP_BUTTON_MAIN) into a
 * drawing + palette recording stub.
 *
 * -- PASS h19 after one list→detail and one Back.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"

static const char *TAG = "h19";

#define SCR_W 320
#define SCR_H 240
#define TRANS_MS 400
#define RECORD_EXPAND_MS 380
#define RECORD_FADE_MS 240
#define RING_BAND_PX 18
#define RING_WIPE_MS 600
#define REC_BTN_D 68
#define RECORD_RED_HEX 0xE53935

/* Recording screen stub (320×240): 240×240 canvas + 80px palette rail */
#define REC_CANVAS_SZ 240
#define REC_RAIL_W 80
#define REC_SWATCH_D 44
#define REC_SWATCH_PITCH 36
#define REC_SWATCH_ZIG_X 11
#define REC_SWATCH_RAIL_MARGIN 10
#define REC_CANVAS_RADIUS 8

/* THEME-DARK + existing demo accents (no new global product tokens) */
#define FL_SURFACE_HEX 0x101418
#define FL_TEXT_PRIMARY_HEX 0xE8F0E8
#define FL_TEXT_SECONDARY_HEX 0xA8B0B8
#define FL_ACCENT_HEX 0xE8C040
#define INK_BLACK_HEX 0x000000
#define INK_WHITE_HEX 0xF8F8F8
#define INK_GOLD_HEX FL_ACCENT_HEX
#define INK_GREEN_HEX 0x4CAF50
#define INK_TEAL_HEX 0x3AB0A8
#define INK_BLUE_HEX 0x5AA0E8

#define REC_PALETTE_N 6
#define SWATCH_RING_GRAY_HEX 0xB8BEC6

/* ESP-BOX-3 active LCD vs bezel PTT (mm). Maps to SCR_W×SCR_H pixels. */
#define PANEL_W_MM 45
#define PANEL_H_MM 34
#define PHYS_REC_CENTER_BELOW_MM 6
#define PHYS_REC_BTN_D_MM 12

#define PHYS_REC_CX (SCR_W / 2)
#define PHYS_REC_CY ((SCR_H * (PANEL_H_MM + PHYS_REC_CENTER_BELOW_MM)) / PANEL_H_MM)
#define PHYS_REC_R0_PX ((PHYS_REC_BTN_D_MM * SCR_W) / (2 * PANEL_W_MM))

/* Strong ease-in-out: flatter start/end than cubic (easings.net easeInOutQuint).
 * For even slower ends, try LV_ANIM_SET_EASE_IN_OUT_EXPO. */
static void anim_set_ease_in_out(lv_anim_t *a)
{
    lv_anim_set_path_cb(a, lv_anim_path_custom_bezier3);
    LV_ANIM_SET_EASE_IN_OUT_QUINT(a);
}

typedef enum {
    H19_TRANS_CUT,
    H19_TRANS_FADE,
    H19_TRANS_PUSH_RIGHT,
    H19_TRANS_PUSH_LEFT,
    H19_TRANS_PUSH_UP,
    H19_TRANS_PUSH_DOWN,
    H19_TRANS_WIPE_RIGHT,
    H19_TRANS_WIPE_LEFT,
    H19_TRANS_WIPE_UP,
    H19_TRANS_WIPE_DOWN,
    H19_TRANS_COVER_RIGHT,
    H19_TRANS_COVER_LEFT,
    H19_TRANS_COVER_UP,
    H19_TRANS_COVER_DOWN,
    H19_TRANS_REVEAL,
} h19_trans_t;

typedef struct {
    const char *id;
    const char *row_title;
    h19_trans_t trans;
    const char *sent_at;
    int duration_ms;
    int position_ms;
    bool read;
} audio_msg_t;

/* List order: directionless, then right / left / up / down (push, wipe, cover), reveal. */
static const audio_msg_t s_msgs[] = {
    { "m01", "Dad - cut", H19_TRANS_CUT, "Sun 3:04 PM", 4500, 800, false },
    { "m02", "Mom - fade", H19_TRANS_FADE, "Sun 11:20 AM", 8200, 0, true },
    /* right */
    { "m03", "Mom - push right", H19_TRANS_PUSH_RIGHT, "Sat 8:41 PM", 2100, 0, false },
    { "m04", "Dad - wipe right", H19_TRANS_WIPE_RIGHT, "Sat 7:02 PM", 15300, 4000, true },
    { "m05", "Mom - cover right", H19_TRANS_COVER_RIGHT, "Fri 6:15 PM", 9800, 0, false },
    /* left */
    { "m06", "Mom - push left", H19_TRANS_PUSH_LEFT, "Thu 9:50 PM", 6400, 0, false },
    { "m07", "Dad - wipe left", H19_TRANS_WIPE_LEFT, "Thu 4:22 PM", 12100, 0, true },
    { "m08", "Mom - cover left", H19_TRANS_COVER_LEFT, "Wed 2:10 PM", 5200, 0, false },
    /* up */
    { "m09", "Mom - push up", H19_TRANS_PUSH_UP, "Tue 8:30 PM", 7200, 0, false },
    { "m10", "Dad - wipe up", H19_TRANS_WIPE_UP, "Tue 1:15 PM", 6100, 0, true },
    { "m11", "Mom - cover up", H19_TRANS_COVER_UP, "Mon 6:40 PM", 8900, 0, false },
    /* down */
    { "m12", "Mom - push down", H19_TRANS_PUSH_DOWN, "Sun 5:22 PM", 3800, 0, false },
    { "m13", "Dad - wipe down", H19_TRANS_WIPE_DOWN, "Sun 12:18 PM", 9900, 0, true },
    { "m14", "Mom - cover down", H19_TRANS_COVER_DOWN, "Sat 4:55 PM", 4600, 0, false },
    { "m15", "Mom - reveal", H19_TRANS_REVEAL, "Fri 7:07 PM", 6700, 0, false },
};

#define MSG_N ((int)(sizeof(s_msgs) / sizeof(s_msgs[0])))

static lv_obj_t *s_root_scr;
static lv_obj_t *s_list_clip;
static lv_obj_t *s_list_wrap;
static lv_obj_t *s_detail_wrap;
static lv_obj_t *s_record_reveal_host;
static lv_obj_t *s_record_wrap;
static lv_obj_t *s_record_btn;
static lv_obj_t *s_red_bloom;
static lv_obj_t *s_ring_arc;
static lv_obj_t *s_rec_canvas;
static lv_obj_t *s_rec_rail;
static lv_obj_t *s_palette_swatches[REC_PALETTE_N];
static int s_palette_sel;
static volatile bool s_phys_stop_pending;
static int s_open = -1;
static bool s_saw_detail;
static bool s_saw_back;
static bool s_passed;
static bool s_trans_busy;
static bool s_showing_detail;
static bool s_on_record;
static bool s_recording;
static volatile bool s_phys_record_pending;
static int s_bloom_cx;
static int s_bloom_cy;
static int s_bloom_r1;
static int s_ring_cx;
static int s_ring_cy;
static int s_ring_ri_end;

static lv_obj_t *s_d_sender;
static lv_obj_t *s_d_when;
static lv_obj_t *s_d_meta;
static lv_obj_t *s_d_bar;
static lv_obj_t *s_d_clock;
static lv_obj_t *s_d_play;
static lv_obj_t *s_d_trans_label;
static int s_detail_pos;

static void fmt_clock(char *out, size_t cap, int pos_ms, int dur_ms)
{
    int p = pos_ms / 1000;
    int d = dur_ms / 1000;
    snprintf(out, cap, "%d:%02d / %d:%02d", p / 60, p % 60, d / 60, d % 60);
}

static void anim_wipe_left_width(void *obj, int32_t w)
{
    lv_obj_set_width((lv_obj_t *)obj, w);
    lv_obj_set_x((lv_obj_t *)obj, SCR_W - w);
}

static void anim_wipe_down_height(void *obj, int32_t h)
{
    lv_obj_set_height((lv_obj_t *)obj, h);
    lv_obj_set_y((lv_obj_t *)obj, SCR_H - h);
}

static void panel_reset_geometry(void)
{
    lv_obj_set_align(s_list_clip, LV_ALIGN_TOP_LEFT);
    lv_obj_set_align(s_list_wrap, LV_ALIGN_TOP_LEFT);
    lv_obj_set_align(s_detail_wrap, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_list_clip, 0, 0);
    lv_obj_set_pos(s_list_wrap, 0, 0);
    lv_obj_set_pos(s_detail_wrap, 0, 0);
    lv_obj_set_size(s_list_clip, SCR_W, SCR_H);
    lv_obj_set_size(s_list_wrap, SCR_W, SCR_H);
    lv_obj_set_size(s_detail_wrap, SCR_W, SCR_H);
    lv_obj_set_align(s_record_wrap, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_record_wrap, 0, 0);
    lv_obj_set_size(s_record_wrap, SCR_W, SCR_H);
    lv_obj_set_style_radius(s_list_clip, 0, 0);
    lv_obj_set_style_clip_corner(s_list_clip, false, 0);
    lv_obj_set_style_opa(s_list_clip, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_list_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_detail_wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale_x(s_list_wrap, 256, 0);
    lv_obj_set_style_transform_scale_y(s_list_wrap, 256, 0);
    lv_obj_set_style_transform_scale_x(s_detail_wrap, 256, 0);
    lv_obj_set_style_transform_scale_y(s_detail_wrap, 256, 0);
    lv_obj_set_style_transform_pivot_x(s_list_wrap, SCR_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_list_wrap, SCR_H / 2, 0);
    lv_obj_set_style_transform_pivot_x(s_detail_wrap, SCR_W / 2, 0);
    lv_obj_set_style_transform_pivot_y(s_detail_wrap, SCR_H / 2, 0);
    lv_obj_set_align(s_record_reveal_host, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_record_reveal_host, 0, 0);
    lv_obj_set_size(s_record_reveal_host, SCR_W, SCR_H);
    lv_obj_set_style_radius(s_record_reveal_host, 0, 0);
    lv_obj_set_style_clip_corner(s_record_reveal_host, false, 0);
    lv_obj_set_pos(s_record_wrap, 0, 0);
    lv_obj_set_size(s_record_wrap, SCR_W, SCR_H);
}

static int circle_cover_radius(int cx, int cy)
{
    const int corners[4][2] = { { 0, 0 }, { SCR_W, 0 }, { 0, SCR_H }, { SCR_W, SCR_H } };
    int max_r = 0;
    for (int i = 0; i < 4; i++) {
        int dx = corners[i][0] - cx;
        int dy = corners[i][1] - cy;
        int dist = (int)ceilf(sqrtf((float)(dx * dx + dy * dy)));
        if (dist > max_r) {
            max_r = dist;
        }
    }
    return max_r + 4;
}

static void red_bloom_set_radius(int r)
{
    if (r < 1) {
        r = 1;
    }
    const int d = r * 2;
    lv_obj_set_align(s_red_bloom, LV_ALIGN_TOP_LEFT);
    lv_obj_set_size(s_red_bloom, d, d);
    lv_obj_set_pos(s_red_bloom, s_bloom_cx - r, s_bloom_cy - r);
    lv_obj_set_style_radius(s_red_bloom, LV_RADIUS_CIRCLE, 0);
}

static void anim_red_bloom_radius(void *obj, int32_t r)
{
    (void)obj;
    red_bloom_set_radius(r);
}

static void cancel_panel_anims(void)
{
    lv_anim_delete(s_list_wrap, NULL);
    lv_anim_delete(s_list_clip, NULL);
    lv_anim_delete(s_detail_wrap, NULL);
    lv_anim_delete(s_red_bloom, NULL);
    lv_anim_delete(s_record_reveal_host, NULL);
}

static void show_list_only(void)
{
    panel_reset_geometry();
    lv_obj_remove_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_detail_wrap);
    lv_obj_move_background(s_record_reveal_host);
    lv_obj_move_foreground(s_list_clip);
    lv_obj_move_foreground(s_record_btn);
    s_showing_detail = false;
    s_on_record = false;
}

static void show_detail_only(void)
{
    panel_reset_geometry();
    lv_obj_add_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_list_clip);
    lv_obj_move_foreground(s_detail_wrap);
    s_showing_detail = true;
    s_on_record = false;
}

static void show_record_only(void)
{
    panel_reset_geometry();
    lv_obj_add_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_list_clip);
    lv_obj_move_foreground(s_record_reveal_host);
    s_showing_detail = false;
    s_on_record = true;
}

static void trans_done_cb(lv_anim_t *a)
{
    (void)a;
    if (s_showing_detail) {
        show_detail_only();
    } else {
        show_list_only();
    }
    s_trans_busy = false;
}

static void anim_i32(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void anim_x(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void anim_y(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

static void anim_w(void *obj, int32_t v)
{
    lv_obj_set_width((lv_obj_t *)obj, v);
}

static void anim_h(void *obj, int32_t v)
{
    lv_obj_set_height((lv_obj_t *)obj, v);
}

static void anim_scale256(void *obj, int32_t v)
{
    lv_obj_set_style_transform_scale_x((lv_obj_t *)obj, (int)v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)obj, (int)v, 0);
}

static void start_anim_cb(lv_anim_t *a, lv_obj_t *var, int32_t from, int32_t to, lv_anim_exec_xcb_t exec,
                          uint32_t ms, lv_anim_completed_cb_t done)
{
    lv_anim_init(a);
    lv_anim_set_var(a, var);
    lv_anim_set_values(a, from, to);
    lv_anim_set_duration(a, ms);
    anim_set_ease_in_out(a);
    lv_anim_set_exec_cb(a, exec);
    if (done != NULL) {
        lv_anim_set_completed_cb(a, done);
    }
    lv_anim_start(a);
}

static void start_anim(lv_anim_t *a, lv_obj_t *var, int32_t from, int32_t to, lv_anim_exec_xcb_t exec,
                      bool complete)
{
    start_anim_cb(a, var, from, to, exec, TRANS_MS, complete ? trans_done_cb : NULL);
}

static void record_stop_session(void);
static void record_stop_ring_reverse(void);

static void record_end_mock(void)
{
    s_recording = false;
    ESP_LOGI(TAG, "recording stopped (mock)");
}

static void record_begin_mock(void)
{
    s_recording = true;
    ESP_LOGI(TAG, "recording started (mock)");
}

static void record_stop_session(void)
{
    if (s_trans_busy || !s_on_record) {
        return;
    }
    record_end_mock();
    show_list_only();
}

static void record_bloom_fade_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_red_bloom, LV_OPA_COVER, 0);
    s_trans_busy = false;
}

static void record_bloom_expand_done(lv_anim_t *a)
{
    (void)a;
    show_record_only();
    record_begin_mock();
    lv_obj_move_foreground(s_red_bloom);
    lv_obj_set_style_opa(s_red_bloom, LV_OPA_COVER, 0);
    lv_anim_t fade;
    start_anim_cb(&fade, s_red_bloom, LV_OPA_COVER, LV_OPA_TRANSP, anim_i32, RECORD_FADE_MS,
                  record_bloom_fade_done);
}

static void record_prepare_ui(void)
{
}

static bool record_can_start(void)
{
    return !s_trans_busy && !s_showing_detail && !s_on_record;
}

static void record_start_transition(int cx, int cy, int r0)
{
    if (!record_can_start()) {
        return;
    }
    cancel_panel_anims();
    s_trans_busy = true;
    record_prepare_ui();

    s_bloom_cx = cx;
    s_bloom_cy = cy;
    s_bloom_r1 = circle_cover_radius(cx, cy);

    lv_obj_remove_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_red_bloom);
    lv_obj_set_style_opa(s_red_bloom, LV_OPA_COVER, 0);
    red_bloom_set_radius(r0);
    lv_obj_add_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t grow;
    start_anim_cb(&grow, s_red_bloom, r0, s_bloom_r1, anim_red_bloom_radius, RECORD_EXPAND_MS,
                  record_bloom_expand_done);
}

/*
 * Ring leading edge is circular; LVGL rounded clip on a moving aperture was unreliable on
 * device (list stayed visible inside). Reveal uses a bottom-up rectangular clip whose top
 * edge y = cy - ri — aligned with the inner ring at the horizontal center (PTT below panel).
 */
static void ring_wipe_apply(int cx, int cy, int ri)
{
    if (ri < 1) {
        ri = 1;
    }
    const int ro = ri + RING_BAND_PX;

    int y_line = cy - ri;
    if (y_line < 0) {
        y_line = 0;
    }

    if (y_line >= SCR_H) {
        lv_obj_add_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    } else {
        const int h_reveal = SCR_H - y_line;
        lv_obj_remove_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_align(s_record_reveal_host, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(s_record_reveal_host, 0, y_line);
        lv_obj_set_size(s_record_reveal_host, SCR_W, h_reveal);
        lv_obj_set_style_radius(s_record_reveal_host, 0, 0);
        lv_obj_set_style_clip_corner(s_record_reveal_host, false, 0);
        lv_obj_set_align(s_record_wrap, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(s_record_wrap, 0, -y_line);
        lv_obj_set_size(s_record_wrap, SCR_W, SCR_H);
    }

    lv_obj_set_align(s_ring_arc, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_ring_arc, cx - ro, cy - ro);
    lv_obj_set_size(s_ring_arc, ro * 2, ro * 2);
    lv_arc_set_bg_angles(s_ring_arc, 0, 360);
    lv_arc_set_angles(s_ring_arc, 0, 360);
}

static void anim_ring_inner_radius(void *obj, int32_t ri)
{
    (void)obj;
    ring_wipe_apply(s_ring_cx, s_ring_cy, ri);
}

static void record_ring_wipe_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    record_begin_mock();
    show_record_only();
    s_trans_busy = false;
}

static void record_start_ring_wipe(int cx, int cy, int outer_r0)
{
    if (!record_can_start()) {
        return;
    }
    cancel_panel_anims();
    s_trans_busy = true;
    record_prepare_ui();

    s_ring_cx = cx;
    s_ring_cy = cy;
    s_ring_ri_end = circle_cover_radius(cx, cy) + RING_BAND_PX;

    int ri_start = outer_r0 - RING_BAND_PX;
    if (ri_start < 1) {
        ri_start = 1;
    }

    lv_obj_remove_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_detail_wrap);
    lv_obj_move_background(s_list_clip);
    lv_obj_move_foreground(s_record_reveal_host);
    lv_obj_move_foreground(s_ring_arc);

    ring_wipe_apply(cx, cy, ri_start);

    lv_anim_t grow;
    start_anim_cb(&grow, s_record_reveal_host, ri_start, s_ring_ri_end, anim_ring_inner_radius,
                  RING_WIPE_MS, record_ring_wipe_done);
}

static void record_ring_wipe_reverse_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_add_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    show_list_only();
    s_trans_busy = false;
}

static void record_stop_ring_reverse(void)
{
    if (s_trans_busy || !s_on_record) {
        return;
    }
    record_end_mock();
    cancel_panel_anims();
    s_trans_busy = true;

    s_ring_cx = PHYS_REC_CX;
    s_ring_cy = PHYS_REC_CY;
    const int ri_end = circle_cover_radius(s_ring_cx, s_ring_cy) + RING_BAND_PX;
    int ri_start = PHYS_REC_R0_PX - RING_BAND_PX;
    if (ri_start < 1) {
        ri_start = 1;
    }

    lv_obj_remove_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_record_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_red_bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_detail_wrap);
    lv_obj_move_background(s_list_clip);
    lv_obj_remove_flag(s_record_reveal_host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ring_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_record_reveal_host);
    lv_obj_move_foreground(s_ring_arc);

    ring_wipe_apply(s_ring_cx, s_ring_cy, ri_end);

    lv_anim_t shrink;
    start_anim_cb(&shrink, s_record_reveal_host, ri_end, ri_start, anim_ring_inner_radius,
                  RING_WIPE_MS, record_ring_wipe_reverse_done);
}

static void prepare_forward(void)
{
    panel_reset_geometry();
    lv_obj_remove_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_detail_wrap);
    lv_obj_move_foreground(s_list_clip);
    s_showing_detail = true;
}

static void prepare_back(void)
{
    panel_reset_geometry();
    lv_obj_remove_flag(s_list_clip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_detail_wrap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(s_list_clip);
    lv_obj_move_foreground(s_detail_wrap);
    s_showing_detail = false;
}

static void run_transition(h19_trans_t t, bool to_detail)
{
    lv_anim_t a;
    lv_anim_t b;

    if (s_trans_busy) {
        return;
    }
    cancel_panel_anims();
    s_trans_busy = true;

    if (t == H19_TRANS_CUT) {
        if (to_detail) {
            show_detail_only();
        } else {
            show_list_only();
        }
        s_trans_busy = false;
        return;
    }

    if (to_detail) {
        prepare_forward();
    } else {
        prepare_back();
    }

    switch (t) {
    case H19_TRANS_FADE:
        if (to_detail) {
            lv_obj_set_style_opa(s_detail_wrap, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(s_list_wrap, LV_OPA_COVER, 0);
            start_anim(&a, s_list_wrap, LV_OPA_COVER, LV_OPA_TRANSP, anim_i32, true);
        } else {
            lv_obj_set_style_opa(s_list_wrap, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(s_detail_wrap, LV_OPA_COVER, 0);
            start_anim(&a, s_detail_wrap, LV_OPA_COVER, LV_OPA_TRANSP, anim_i32, true);
        }
        break;

    case H19_TRANS_PUSH_RIGHT:
        if (to_detail) {
            lv_obj_set_x(s_list_wrap, 0);
            lv_obj_set_x(s_detail_wrap, SCR_W);
            start_anim(&a, s_list_wrap, 0, -SCR_W, anim_x, false);
            start_anim(&b, s_detail_wrap, SCR_W, 0, anim_x, true);
        } else {
            lv_obj_set_x(s_detail_wrap, 0);
            lv_obj_set_x(s_list_wrap, -SCR_W);
            start_anim(&a, s_detail_wrap, 0, SCR_W, anim_x, false);
            start_anim(&b, s_list_wrap, -SCR_W, 0, anim_x, true);
        }
        break;

    case H19_TRANS_PUSH_LEFT:
        if (to_detail) {
            lv_obj_set_x(s_list_wrap, 0);
            lv_obj_set_x(s_detail_wrap, -SCR_W);
            start_anim(&a, s_list_wrap, 0, SCR_W, anim_x, false);
            start_anim(&b, s_detail_wrap, -SCR_W, 0, anim_x, true);
        } else {
            lv_obj_set_x(s_detail_wrap, 0);
            lv_obj_set_x(s_list_wrap, SCR_W);
            start_anim(&a, s_detail_wrap, 0, -SCR_W, anim_x, false);
            start_anim(&b, s_list_wrap, SCR_W, 0, anim_x, true);
        }
        break;

    case H19_TRANS_PUSH_UP:
        if (to_detail) {
            lv_obj_set_y(s_list_wrap, 0);
            lv_obj_set_y(s_detail_wrap, SCR_H);
            start_anim(&a, s_list_wrap, 0, -SCR_H, anim_y, false);
            start_anim(&b, s_detail_wrap, SCR_H, 0, anim_y, true);
        } else {
            lv_obj_set_y(s_detail_wrap, 0);
            lv_obj_set_y(s_list_wrap, -SCR_H);
            start_anim(&a, s_detail_wrap, 0, SCR_H, anim_y, false);
            start_anim(&b, s_list_wrap, -SCR_H, 0, anim_y, true);
        }
        break;

    case H19_TRANS_PUSH_DOWN:
        if (to_detail) {
            lv_obj_set_y(s_list_wrap, 0);
            lv_obj_set_y(s_detail_wrap, -SCR_H);
            start_anim(&a, s_list_wrap, 0, SCR_H, anim_y, false);
            start_anim(&b, s_detail_wrap, -SCR_H, 0, anim_y, true);
        } else {
            lv_obj_set_y(s_detail_wrap, 0);
            lv_obj_set_y(s_list_wrap, SCR_H);
            start_anim(&a, s_detail_wrap, 0, -SCR_H, anim_y, false);
            start_anim(&b, s_list_wrap, SCR_H, 0, anim_y, true);
        }
        break;

    case H19_TRANS_WIPE_RIGHT:
        lv_obj_move_foreground(s_list_clip);
        lv_obj_set_pos(s_list_wrap, 0, 0);
        lv_obj_set_size(s_list_wrap, SCR_W, SCR_H);
        if (to_detail) {
            start_anim(&a, s_list_wrap, SCR_W, 0, anim_w, true);
        } else {
            lv_obj_set_width(s_list_wrap, 0);
            start_anim(&a, s_list_wrap, 0, SCR_W, anim_w, true);
        }
        break;

    case H19_TRANS_WIPE_LEFT:
        lv_obj_move_foreground(s_list_clip);
        lv_obj_set_y(s_list_wrap, 0);
        lv_obj_set_height(s_list_wrap, SCR_H);
        if (to_detail) {
            lv_obj_set_width(s_list_wrap, SCR_W);
            lv_obj_set_x(s_list_wrap, 0);
            start_anim(&a, s_list_wrap, SCR_W, 0, anim_wipe_left_width, true);
        } else {
            lv_obj_set_width(s_list_wrap, 0);
            lv_obj_set_x(s_list_wrap, SCR_W);
            start_anim(&a, s_list_wrap, 0, SCR_W, anim_wipe_left_width, true);
        }
        break;

    case H19_TRANS_WIPE_UP:
        lv_obj_move_foreground(s_list_clip);
        lv_obj_set_pos(s_list_wrap, 0, 0);
        lv_obj_set_width(s_list_wrap, SCR_W);
        lv_obj_set_height(s_list_wrap, SCR_H);
        if (to_detail) {
            start_anim(&a, s_list_wrap, SCR_H, 0, anim_h, true);
        } else {
            lv_obj_set_height(s_list_wrap, 0);
            start_anim(&a, s_list_wrap, 0, SCR_H, anim_h, true);
        }
        break;

    case H19_TRANS_WIPE_DOWN:
        lv_obj_move_foreground(s_list_clip);
        lv_obj_set_x(s_list_wrap, 0);
        lv_obj_set_width(s_list_wrap, SCR_W);
        if (to_detail) {
            lv_obj_set_height(s_list_wrap, SCR_H);
            lv_obj_set_y(s_list_wrap, 0);
            start_anim(&a, s_list_wrap, SCR_H, 0, anim_wipe_down_height, true);
        } else {
            lv_obj_set_height(s_list_wrap, 0);
            lv_obj_set_y(s_list_wrap, SCR_H);
            start_anim(&a, s_list_wrap, 0, SCR_H, anim_wipe_down_height, true);
        }
        break;

    case H19_TRANS_COVER_RIGHT:
        lv_obj_move_foreground(s_detail_wrap);
        if (to_detail) {
            lv_obj_set_x(s_detail_wrap, SCR_W);
            start_anim(&a, s_detail_wrap, SCR_W, 0, anim_x, true);
        } else {
            lv_obj_set_x(s_detail_wrap, 0);
            start_anim(&a, s_detail_wrap, 0, SCR_W, anim_x, true);
        }
        break;

    case H19_TRANS_COVER_LEFT:
        lv_obj_move_foreground(s_detail_wrap);
        if (to_detail) {
            lv_obj_set_x(s_detail_wrap, -SCR_W);
            start_anim(&a, s_detail_wrap, -SCR_W, 0, anim_x, true);
        } else {
            lv_obj_set_x(s_detail_wrap, 0);
            start_anim(&a, s_detail_wrap, 0, -SCR_W, anim_x, true);
        }
        break;

    case H19_TRANS_COVER_UP:
        lv_obj_move_foreground(s_detail_wrap);
        if (to_detail) {
            lv_obj_set_y(s_detail_wrap, -SCR_H);
            start_anim(&a, s_detail_wrap, -SCR_H, 0, anim_y, true);
        } else {
            lv_obj_set_y(s_detail_wrap, 0);
            start_anim(&a, s_detail_wrap, 0, -SCR_H, anim_y, true);
        }
        break;

    case H19_TRANS_COVER_DOWN:
        lv_obj_move_foreground(s_detail_wrap);
        if (to_detail) {
            lv_obj_set_y(s_detail_wrap, SCR_H);
            start_anim(&a, s_detail_wrap, SCR_H, 0, anim_y, true);
        } else {
            lv_obj_set_y(s_detail_wrap, 0);
            start_anim(&a, s_detail_wrap, 0, SCR_H, anim_y, true);
        }
        break;

    case H19_TRANS_REVEAL:
        lv_obj_move_foreground(s_list_clip);
        if (to_detail) {
            lv_obj_set_style_transform_scale_x(s_list_wrap, 256, 0);
            lv_obj_set_style_transform_scale_y(s_list_wrap, 256, 0);
            lv_obj_set_style_transform_scale_x(s_detail_wrap, 230, 0);
            lv_obj_set_style_transform_scale_y(s_detail_wrap, 230, 0);
            lv_obj_set_style_opa(s_detail_wrap, LV_OPA_COVER, 0);
            start_anim(&a, s_list_wrap, 256, 210, anim_scale256, false);
            start_anim(&b, s_detail_wrap, 230, 256, anim_scale256, false);
            lv_anim_t c;
            lv_anim_init(&c);
            lv_anim_set_var(&c, s_list_wrap);
            lv_anim_set_values(&c, LV_OPA_COVER, LV_OPA_TRANSP);
            lv_anim_set_duration(&c, TRANS_MS);
            anim_set_ease_in_out(&c);
            lv_anim_set_exec_cb(&c, anim_i32);
            lv_anim_set_completed_cb(&c, trans_done_cb);
            lv_anim_start(&c);
        } else {
            lv_obj_set_style_transform_scale_x(s_detail_wrap, 256, 0);
            lv_obj_set_style_transform_scale_y(s_detail_wrap, 256, 0);
            lv_obj_set_style_transform_scale_x(s_list_wrap, 230, 0);
            lv_obj_set_style_transform_scale_y(s_list_wrap, 230, 0);
            lv_obj_set_style_opa(s_list_wrap, LV_OPA_TRANSP, 0);
            start_anim(&a, s_detail_wrap, 256, 210, anim_scale256, false);
            start_anim(&b, s_list_wrap, 230, 256, anim_scale256, false);
            lv_anim_t c;
            lv_anim_init(&c);
            lv_anim_set_var(&c, s_list_wrap);
            lv_anim_set_values(&c, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_set_duration(&c, TRANS_MS);
            anim_set_ease_in_out(&c);
            lv_anim_set_exec_cb(&c, anim_i32);
            lv_anim_set_completed_cb(&c, trans_done_cb);
            lv_anim_start(&c);
        }
        break;

    default:
        if (to_detail) {
            show_detail_only();
        } else {
            show_list_only();
        }
        s_trans_busy = false;
        break;
    }
}

static void maybe_pass(void)
{
    if (!s_passed && s_saw_detail && s_saw_back) {
        s_passed = true;
        demo_pass("h19");
    }
}

static void fill_detail(int idx)
{
    if (idx < 0 || idx >= MSG_N) {
        return;
    }
    const audio_msg_t *m = &s_msgs[idx];
    lv_label_set_text(s_d_sender, m->row_title);
    lv_label_set_text(s_d_when, m->sent_at);
    lv_label_set_text(s_d_meta, m->read ? "read" : "unread");
    lv_obj_set_style_text_color(s_d_meta, lv_color_hex(m->read ? 0xA8B0B8 : 0xE8C040), 0);
    if (s_d_trans_label) {
        lv_label_set_text(s_d_trans_label, m->row_title);
    }
    lv_bar_set_range(s_d_bar, 0, m->duration_ms > 0 ? m->duration_ms : 1);
    lv_bar_set_value(s_d_bar, m->position_ms, LV_ANIM_OFF);
    char line[32];
    fmt_clock(line, sizeof(line), m->position_ms, m->duration_ms);
    lv_label_set_text(s_d_clock, line);
    lv_label_set_text(s_d_play, "Play");
}

static void nav_open_detail(int idx)
{
    if (s_trans_busy) {
        return;
    }
    s_open = idx;
    s_saw_detail = true;
    ESP_LOGI(TAG, "open %s (%s)", s_msgs[idx].id, s_msgs[idx].row_title);
    s_detail_pos = s_msgs[idx].position_ms;
    fill_detail(idx);
    run_transition(s_msgs[idx].trans, true);
}

static void nav_back_list(void)
{
    if (s_trans_busy || s_open < 0) {
        return;
    }
    s_saw_back = true;
    ESP_LOGI(TAG, "back to list");
    run_transition(s_msgs[s_open].trans, false);
    maybe_pass();
}

static void on_row(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    nav_open_detail(idx);
}

static void record_start_from_on_screen_fab(void)
{
    lv_area_t area;
    lv_obj_get_coords(s_record_btn, &area);
    const int cx = (area.x1 + area.x2) / 2;
    const int cy = (area.y1 + area.y2) / 2;
    ESP_LOGI(TAG, "record bloom on-screen fab (%d,%d)", cx, cy);
    record_start_transition(cx, cy, REC_BTN_D / 2);
}

static void record_start_from_physical_ptt(void)
{
    ESP_LOGI(TAG, "record ring wipe physical PTT (%d,%d) r0=%d", PHYS_REC_CX, PHYS_REC_CY, PHYS_REC_R0_PX);
    record_start_ring_wipe(PHYS_REC_CX, PHYS_REC_CY, PHYS_REC_R0_PX);
}

static void on_record_press(lv_event_t *e)
{
    (void)e;
    record_start_from_on_screen_fab();
}

static void physical_main_down(void *btn, void *u)
{
    (void)btn;
    (void)u;
    if (s_on_record && s_recording && !s_trans_busy) {
        if (!board_lvgl_lock(0)) {
            s_phys_stop_pending = true;
            return;
        }
        record_stop_ring_reverse();
        board_lvgl_unlock();
        return;
    }
    if (!record_can_start()) {
        return;
    }
    if (!board_lvgl_lock(0)) {
        s_phys_record_pending = true;
        return;
    }
    record_start_from_physical_ptt();
    board_lvgl_unlock();
}

static void init_physical_record_button(void)
{
    button_handle_t btns[BSP_BUTTON_NUM] = { 0 };
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) != ESP_OK) {
        ESP_LOGW(TAG, "physical buttons unavailable");
        return;
    }
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        ESP_LOGW(TAG, "BSP_BUTTON_MAIN missing (display up first?)");
        return;
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, physical_main_down, NULL);
    ESP_LOGI(TAG, "physical PTT -> ring wipe at (%d,%d)", PHYS_REC_CX, PHYS_REC_CY);
}

static void on_back(lv_event_t *e)
{
    (void)e;
    nav_back_list();
}

static void on_fake_play(lv_event_t *e)
{
    (void)e;
    if (s_open < 0) {
        return;
    }
    s_detail_pos += 800;
    if (s_detail_pos > s_msgs[s_open].duration_ms) {
        s_detail_pos = 0;
    }
    lv_bar_set_value(s_d_bar, s_detail_pos, LV_ANIM_ON);
    char line[32];
    fmt_clock(line, sizeof(line), s_detail_pos, s_msgs[s_open].duration_ms);
    lv_label_set_text(s_d_clock, line);
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *title, int w, int h)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_size(btn, w, h);
    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, title);
    lv_obj_center(lab);
    return btn;
}

static lv_obj_t *make_list_clip_host(lv_obj_t *parent)
{
    lv_obj_t *clip = lv_obj_create(parent);
    lv_obj_remove_style_all(clip);
    lv_obj_remove_flag(clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(clip, SCR_W, SCR_H);
    lv_obj_set_pos(clip, 0, 0);
    lv_obj_set_style_bg_opa(clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(clip, 0, 0);
    return clip;
}

static lv_obj_t *make_panel_wrap(lv_obj_t *parent)
{
    lv_obj_t *wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wrap, SCR_W, SCR_H);
    lv_obj_set_pos(wrap, 0, 0);
    lv_obj_set_style_bg_color(wrap, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
    return wrap;
}

static void paint_row(lv_obj_t *list, int idx, int y)
{
    const audio_msg_t *m = &s_msgs[idx];
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(row, 296, 52);
    lv_obj_set_pos(row, 12, y);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2A3038), 0);
    lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *dot = lv_obj_create(row);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, 10, 22);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(m->read ? 0x3A4048 : 0xE8C040), 0);

    lv_obj_t *who = lv_label_create(row);
    lv_label_set_text(who, m->row_title);
    lv_obj_set_style_text_color(who, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_pos(who, 28, 6);
    lv_label_set_long_mode(who, LV_LABEL_LONG_DOT);
    lv_obj_set_width(who, 200);

    lv_obj_t *when = lv_label_create(row);
    lv_label_set_text(when, m->sent_at);
    lv_obj_set_style_text_color(when, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(when, 28, 28);

    char dur[16];
    snprintf(dur, sizeof(dur), "%d:%02d", (m->duration_ms / 1000) / 60, (m->duration_ms / 1000) % 60);
    lv_obj_t *len = lv_label_create(row);
    lv_label_set_text(len, dur);
    lv_obj_set_style_text_color(len, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(len, LV_ALIGN_RIGHT_MID, -12, 0);
}

static void build_list(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "messages (tap = transition)");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A9298), 0);
    lv_obj_set_pos(title, 16, 8);

    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 320, 200);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_bottom(list, REC_BTN_D + 12, 0);

    const int row_h = 56;
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < MSG_N; i++) {
        paint_row(list, i, 4 + i * row_h);
    }
    lv_obj_t *end = lv_obj_create(list);
    lv_obj_remove_style_all(end);
    lv_obj_remove_flag(end, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(end, 1, 8);
    lv_obj_set_pos(end, 0, 4 + MSG_N * row_h);
}

static lv_obj_t *make_record_fab(lv_obj_t *parent)
{
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(btn, REC_BTN_D, REC_BTN_D);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(RECORD_RED_HEX), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 4, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xF8F8F8), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_80, 0);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(btn, on_record_press, LV_EVENT_CLICKED, NULL);
    return btn;
}

static const uint32_t s_palette_colors[REC_PALETTE_N] = {
    INK_BLACK_HEX,
    INK_WHITE_HEX,
    INK_GOLD_HEX,
    INK_GREEN_HEX,
    RECORD_RED_HEX,
    INK_BLUE_HEX,
};

static void palette_refresh_selection(void)
{
    for (int i = 0; i < REC_PALETTE_N; i++) {
        if (s_palette_swatches[i] == NULL) {
            continue;
        }
        lv_obj_set_style_border_width(s_palette_swatches[i], 2, 0);
        lv_obj_set_style_border_color(s_palette_swatches[i], lv_color_hex(SWATCH_RING_GRAY_HEX), 0);
        lv_obj_set_style_border_opa(s_palette_swatches[i], LV_OPA_COVER, 0);
        if (i == s_palette_sel) {
            lv_obj_set_style_outline_width(s_palette_swatches[i], 2, 0);
            lv_obj_set_style_outline_color(s_palette_swatches[i], lv_color_hex(FL_ACCENT_HEX), 0);
            lv_obj_set_style_outline_opa(s_palette_swatches[i], LV_OPA_COVER, 0);
            lv_obj_set_style_outline_pad(s_palette_swatches[i], 1, 0);
        } else {
            lv_obj_set_style_outline_width(s_palette_swatches[i], 0, 0);
        }
    }
}

static void on_palette_swatch(lv_event_t *e)
{
    s_palette_sel = (int)(intptr_t)lv_event_get_user_data(e);
    palette_refresh_selection();
}

static void build_record(lv_obj_t *parent)
{
    s_rec_canvas = lv_obj_create(parent);
    lv_obj_remove_style_all(s_rec_canvas);
    lv_obj_remove_flag(s_rec_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_rec_canvas, 0, 0);
    lv_obj_set_size(s_rec_canvas, REC_CANVAS_SZ, REC_CANVAS_SZ);
    lv_obj_set_style_bg_color(s_rec_canvas, lv_color_hex(INK_BLACK_HEX), 0);
    lv_obj_set_style_bg_opa(s_rec_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_rec_canvas, REC_CANVAS_RADIUS, 0);
    lv_obj_set_style_clip_corner(s_rec_canvas, true, 0);
    lv_obj_set_style_border_width(s_rec_canvas, 0, 0);
    lv_obj_set_style_pad_all(s_rec_canvas, 0, 0);

    s_rec_rail = lv_obj_create(parent);
    lv_obj_remove_style_all(s_rec_rail);
    lv_obj_remove_flag(s_rec_rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_rec_rail, REC_CANVAS_SZ, 0);
    lv_obj_set_size(s_rec_rail, REC_RAIL_W, SCR_H);
    lv_obj_set_style_bg_color(s_rec_rail, lv_color_hex(FL_SURFACE_HEX), 0);
    lv_obj_set_style_bg_opa(s_rec_rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_rec_rail, 0, 0);
    lv_obj_set_style_pad_all(s_rec_rail, 0, 0);

    s_palette_sel = 1;
    const int x_center = (REC_RAIL_W - REC_SWATCH_D) / 2;
    const int y_bottom = SCR_H - REC_SWATCH_RAIL_MARGIN - REC_SWATCH_D;
    for (int step = 0; step < REC_PALETTE_N; step++) {
        const int idx = step;
        int x = x_center + ((step % 2 == 0) ? -REC_SWATCH_ZIG_X : REC_SWATCH_ZIG_X);
        const int y = y_bottom - step * REC_SWATCH_PITCH;
        if (x < 2) {
            x = 2;
        }
        if (x + REC_SWATCH_D > REC_RAIL_W - 2) {
            x = REC_RAIL_W - 2 - REC_SWATCH_D;
        }
        lv_obj_t *sw = lv_obj_create(s_rec_rail);
        lv_obj_remove_style_all(sw);
        lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(sw, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(sw, REC_SWATCH_D, REC_SWATCH_D);
        lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(s_palette_colors[idx]), 0);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
        lv_obj_set_pos(sw, x, y);
        lv_obj_add_event_cb(sw, on_palette_swatch, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
        s_palette_swatches[idx] = sw;
    }
    palette_refresh_selection();
}

static lv_obj_t *make_record_reveal_host(lv_obj_t *parent)
{
    lv_obj_t *host = lv_obj_create(parent);
    lv_obj_remove_style_all(host);
    lv_obj_remove_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(host, SCR_W, SCR_H);
    lv_obj_set_pos(host, 0, 0);
    lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(host, 0, 0);
    return host;
}

static lv_obj_t *make_ring_arc(lv_obj_t *parent)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(RECORD_RED_HEX), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, RING_BAND_PX, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_arc_set_range(arc, 0, 360);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 0);
    return arc;
}

static lv_obj_t *make_red_bloom(lv_obj_t *parent)
{
    lv_obj_t *bloom = lv_obj_create(parent);
    lv_obj_remove_style_all(bloom);
    lv_obj_remove_flag(bloom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bloom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(bloom, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(bloom, lv_color_hex(RECORD_RED_HEX), 0);
    lv_obj_set_style_bg_opa(bloom, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bloom, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(bloom, 0, 0);
    return bloom;
}

static void build_detail(lv_obj_t *parent)
{
    lv_obj_t *back = make_btn(parent, "Back", 72, 32);
    lv_obj_set_pos(back, 8, 6);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);

    s_d_meta = lv_label_create(parent);
    lv_label_set_text(s_d_meta, "unread");
    lv_obj_align(s_d_meta, LV_ALIGN_TOP_RIGHT, -12, 12);

    s_d_sender = lv_label_create(parent);
    lv_label_set_text(s_d_sender, "");
    lv_obj_set_style_text_color(s_d_sender, lv_color_hex(0xE8F0E8), 0);
#if defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(s_d_sender, &lv_font_montserrat_22, 0);
#endif
    lv_obj_align(s_d_sender, LV_ALIGN_TOP_LEFT, 16, 44);
    lv_label_set_long_mode(s_d_sender, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_d_sender, 288);

    s_d_trans_label = lv_label_create(parent);
    lv_label_set_text(s_d_trans_label, "");
    lv_obj_set_style_text_color(s_d_trans_label, lv_color_hex(0x5AA0E8), 0);
    lv_obj_align(s_d_trans_label, LV_ALIGN_TOP_LEFT, 16, 72);

    s_d_when = lv_label_create(parent);
    lv_label_set_text(s_d_when, "");
    lv_obj_set_style_text_color(s_d_when, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_d_when, LV_ALIGN_TOP_LEFT, 16, 92);

    s_d_bar = lv_bar_create(parent);
    lv_obj_set_size(s_d_bar, 288, 14);
    lv_obj_align(s_d_bar, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_set_style_bg_color(s_d_bar, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_bg_opa(s_d_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_d_bar, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_d_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_d_clock = lv_label_create(parent);
    lv_label_set_text(s_d_clock, "");
    lv_obj_set_style_text_color(s_d_clock, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(s_d_clock, LV_ALIGN_TOP_MID, 0, 140);

    lv_obj_t *play = make_btn(parent, "Play", 140, 48);
    lv_obj_align(play, LV_ALIGN_BOTTOM_MID, 0, -16);
    s_d_play = lv_obj_get_child(play, 0);
    lv_obj_add_event_cb(play, on_fake_play, LV_EVENT_CLICKED, NULL);
}

static void build_ui(void)
{
    s_root_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_root_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_root_scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_root_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_root_scr, 0, 0);

    s_list_clip = make_list_clip_host(s_root_scr);
    s_list_wrap = make_panel_wrap(s_list_clip);
    s_detail_wrap = make_panel_wrap(s_root_scr);
    s_record_reveal_host = make_record_reveal_host(s_root_scr);
    s_record_wrap = make_panel_wrap(s_record_reveal_host);
    s_red_bloom = make_red_bloom(s_root_scr);
    s_ring_arc = make_ring_arc(s_root_scr);
    build_list(s_list_wrap);
    build_detail(s_detail_wrap);
    build_record(s_record_wrap);
    s_record_btn = make_record_fab(s_list_wrap);
    lv_obj_move_foreground(s_record_btn);
    show_list_only();
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h19", "display");
        return;
    }
    if (!board_lvgl_lock(1000)) {
        demo_fail("h19", "lvgl lock");
        return;
    }
    build_ui();
#if LVGL_VERSION_MAJOR >= 9
    lv_screen_load(s_root_scr);
#else
    lv_scr_load(s_root_scr);
#endif
    board_lvgl_unlock();
    init_physical_record_button();
    ESP_LOGI(TAG, "%d transitions; FAB bloom + physical PTT ring wipe.", MSG_N);
    while (1) {
        if (s_phys_stop_pending && board_lvgl_lock(0)) {
            s_phys_stop_pending = false;
            record_stop_ring_reverse();
            board_lvgl_unlock();
        } else if (s_phys_record_pending && board_lvgl_lock(0)) {
            s_phys_record_pending = false;
            record_start_from_physical_ptt();
            board_lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
