#include "v1_ui_common.h"

#include "v1_api.h"
#include "v1_carousel.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"

#include "http_bearer.h"

#include "board.h"
#include "esp_timer.h"
#include "lvgl.h"

#if __has_include("assets/avatars/avatars.h")
#include "assets/avatars/avatars.h"
#endif

#include <stdio.h>
#include <string.h>

static volatile bool s_repaint;
static volatile bool s_transport_dirty;
static volatile int s_chirp_hz;
static volatile int s_chirp_hz2;

static lv_obj_t *s_status;
static lv_obj_t *s_pin_slots[V1_PIN_DIGITS];
static lv_obj_t *s_toast;
static void (*s_activity_cb)(void);
static bool s_privacy_screen_off;

void v1_ui_set_privacy_screen_off(bool off)
{
    s_privacy_screen_off = off;
}

bool v1_ui_privacy_screen_off(void)
{
    return s_privacy_screen_off;
}

void v1_ui_init(void)
{
    s_repaint = false;
    s_transport_dirty = false;
    s_chirp_hz = 0;
    s_chirp_hz2 = 0;
}

void v1_ui_set_activity_cb(void (*cb)(void))
{
    s_activity_cb = cb;
}

void v1_ui_bump_activity(void)
{
    if (s_activity_cb) {
        s_activity_cb();
    }
}

void v1_ui_request_repaint(void)
{
    s_repaint = true;
}

bool v1_ui_repaint_pending(void)
{
    return s_repaint;
}

void v1_ui_clear_repaint(void)
{
    s_repaint = false;
}

void v1_ui_request_transport_refresh(void)
{
    s_transport_dirty = true;
}

bool v1_ui_transport_dirty(void)
{
    return s_transport_dirty;
}

void v1_ui_clear_transport_dirty(void)
{
    s_transport_dirty = false;
}

static void on_scr_activity(lv_event_t *e)
{
    (void)e;
    if (s_activity_cb) {
        s_activity_cb();
    }
}

void v1_ui_hook_scr(lv_obj_t *scr)
{
    lv_obj_add_event_cb(scr, on_scr_activity, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, on_scr_activity, LV_EVENT_CLICKED, NULL);
}

void v1_ui_bind_status(lv_obj_t *status)
{
    s_status = status;
}

void v1_ui_set_status(lv_obj_t *status, const char *t, uint32_t color)
{
    lv_obj_t *lab = status ? status : s_status;
    if (lab) {
        lv_label_set_text(lab, t);
        lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
    }
}

void v1_ui_bind_pin_slots(lv_obj_t *s0, lv_obj_t *s1, lv_obj_t *s2, lv_obj_t *s3)
{
    s_pin_slots[0] = s0;
    s_pin_slots[1] = s1;
    s_pin_slots[2] = s2;
    s_pin_slots[3] = s3;
}

void v1_ui_refresh_dots(size_t elen)
{
    if (elen > V1_PIN_DIGITS) {
        elen = V1_PIN_DIGITS;
    }
    for (int i = 0; i < V1_PIN_DIGITS; i++) {
        lv_obj_t *lab = s_pin_slots[i];
        if (!lab) {
            continue;
        }
        if (i < (int)elen) {
            lv_label_set_text(lab, "*");
            lv_obj_set_style_text_color(lab, lv_color_hex(V1_UI_ACCENT), 0);
        } else {
            lv_label_set_text(lab, "_");
            lv_obj_set_style_text_color(lab, lv_color_hex(0x788898), 0);
        }
    }
}

void v1_ui_bind_toast(lv_obj_t *toast)
{
    s_toast = toast;
}

void v1_ui_clear_widget_binds(void)
{
    s_status = NULL;
    memset(s_pin_slots, 0, sizeof(s_pin_slots));
    s_toast = NULL;
}

void v1_ui_set_toast(lv_obj_t *toast, const char *msg)
{
    lv_obj_t *lab = toast ? toast : s_toast;
    if (lab) {
        lv_label_set_text(lab, msg ? msg : "");
    }
}

void v1_ui_request_chirp(int hz)
{
    if (hz > 0) {
        s_chirp_hz = hz;
        s_chirp_hz2 = 0;
    }
}

void v1_ui_request_chirp_pair(int a, int b)
{
    if (a > 0) {
        s_chirp_hz = a;
        s_chirp_hz2 = b > 0 ? b : 0;
    }
}

bool v1_ui_take_chirp(int *hz, int *hz2)
{
    if (!s_chirp_hz) {
        return false;
    }
    if (hz) {
        *hz = s_chirp_hz;
    }
    if (hz2) {
        *hz2 = s_chirp_hz2;
    }
    s_chirp_hz = 0;
    s_chirp_hz2 = 0;
    return true;
}

static const lv_image_dsc_t *avatar_image(uint8_t slot)
{
#if defined(AVATAR_SLOT_COUNT) && AVATAR_SLOT_COUNT > 0
    if (slot >= 1 && slot <= AVATAR_SLOT_COUNT) {
        return avatar_slots[slot - 1];
    }
#endif
    return NULL;
}

/** Decorative widgets on a clickable parent (roster card, etc.). */
static void v1_ui_pass_clicks_to_parent(lv_obj_t *obj)
{
    if (!obj) {
        return;
    }
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

void v1_ui_paint_geometry_face(lv_obj_t *parent, uint32_t accent, int sz)
{
    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_set_size(head, sz, sz);
    lv_obj_center(head);
    lv_obj_set_style_radius(head, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);
    v1_ui_pass_clicks_to_parent(head);
    int esz = sz / 7;
    if (esz < 3) {
        esz = 3;
    }
    lv_obj_t *e1 = lv_obj_create(head);
    lv_obj_set_size(e1, esz, esz);
    lv_obj_set_pos(e1, sz * 7 / 28, sz * 9 / 28);
    lv_obj_set_style_radius(e1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(e1, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(e1, 0, 0);
    lv_obj_set_style_pad_all(e1, 0, 0);
    lv_obj_clear_flag(e1, LV_OBJ_FLAG_SCROLLABLE);
    v1_ui_pass_clicks_to_parent(e1);
    lv_obj_t *e2 = lv_obj_create(head);
    lv_obj_set_size(e2, esz, esz);
    lv_obj_set_pos(e2, sz * 17 / 28, sz * 9 / 28);
    lv_obj_set_style_radius(e2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(e2, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(e2, 0, 0);
    lv_obj_set_style_pad_all(e2, 0, 0);
    lv_obj_clear_flag(e2, LV_OBJ_FLAG_SCROLLABLE);
    v1_ui_pass_clicks_to_parent(e2);
}

void v1_ui_paint_user_portrait_aligned(lv_obj_t *parent, int idx, int sz,
                                       lv_align_t align, int x_ofs, int y_ofs)
{
    uint8_t slot = v1_connect_user_avatar_slot(idx);
    const lv_image_dsc_t *img = avatar_image(slot);
    if (img) {
        lv_obj_t *av = lv_image_create(parent);
        lv_image_set_src(av, img);
        lv_obj_set_size(av, sz, sz);
        lv_obj_align(av, align, x_ofs, y_ofs);
        v1_ui_pass_clicks_to_parent(av);
        return;
    }
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, sz, sz);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    v1_ui_pass_clicks_to_parent(box);
    lv_obj_align(box, align, x_ofs, y_ofs);
    v1_ui_paint_geometry_face(box, v1_connect_user_accent(idx), sz);
}

void v1_ui_paint_user_portrait(lv_obj_t *parent, int idx, int sz)
{
    v1_ui_paint_user_portrait_aligned(parent, idx, sz, LV_ALIGN_CENTER, 0, 0);
}

void v1_ui_paint_face_sized(lv_obj_t *parent, int idx, int x, int y, int sz)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, sz + 4, sz + 4);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    v1_ui_paint_user_portrait(box, idx, sz);
}

void v1_ui_paint_face(lv_obj_t *parent, int idx, int x, int y)
{
    v1_ui_paint_face_sized(parent, idx, x, y, 24);
}

void v1_ui_style_list_row(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(V1_UI_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(V1_UI_CARD_PRESS), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

void v1_ui_style_list_row_label(lv_obj_t *lab)
{
    lv_obj_set_style_text_color(lab, lv_color_hex(V1_UI_TEXT), 0);
}

void v1_ui_roster_row_layout(int n, int *y0, int *row_h, int *row_step, int *face_sz)
{
    *face_sz = V1_ROSTER_FACE_SZ;
    if (n <= 1) {
        *row_h = 78;
        *row_step = 82;
        *y0 = 76;
    } else if (n == 2) {
        *row_h = 78;
        *row_step = 82;
        *y0 = 40;
    } else if (n == 3) {
        *row_h = 68;
        *row_step = 72;
        *y0 = 28;
    } else {
        *row_h = 54;
        *row_step = 56;
        *y0 = 14;
    }
}

void v1_ui_paint_asterisk_icon(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 28, 28);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bar1 = lv_obj_create(box);
    lv_obj_set_size(bar1, 18, 4);
    lv_obj_set_style_bg_color(bar1, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar1, 0, 0);
    lv_obj_set_style_pad_all(bar1, 0, 0);
    lv_obj_clear_flag(bar1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar1);
    lv_obj_t *bar2 = lv_obj_create(box);
    lv_obj_set_size(bar2, 4, 18);
    lv_obj_set_style_bg_color(bar2, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar2, 0, 0);
    lv_obj_set_style_pad_all(bar2, 0, 0);
    lv_obj_clear_flag(bar2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar2);
    lv_obj_t *bar3 = lv_obj_create(box);
    lv_obj_set_size(bar3, 14, 4);
    lv_obj_set_style_bg_color(bar3, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar3, 0, 0);
    lv_obj_set_style_pad_all(bar3, 0, 0);
    lv_obj_set_style_transform_angle(bar3, 450, 0);
    lv_obj_clear_flag(bar3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar3);
    lv_obj_t *bar4 = lv_obj_create(box);
    lv_obj_set_size(bar4, 14, 4);
    lv_obj_set_style_bg_color(bar4, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(bar4, 0, 0);
    lv_obj_set_style_pad_all(bar4, 0, 0);
    lv_obj_set_style_transform_angle(bar4, 1350, 0);
    lv_obj_clear_flag(bar4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(bar4);
}

void v1_ui_paint_ribbons(lv_obj_t *scr, bool server_online, const char *session_user,
                         lv_obj_t **ribbon_top, lv_obj_t **ribbon_bot,
                         lv_obj_t **count_lab, lv_obj_t **offline_lab, lv_obj_t **toast)
{
    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_size(top, 320, V1_RIBBON_H);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x0C0E10), 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *off = lv_label_create(top);
    lv_label_set_text(off, "offline");
    lv_obj_set_style_text_color(off, lv_color_hex(0xE85A5A), 0);
    lv_obj_align(off, LV_ALIGN_LEFT_MID, 8, 0);
    if (server_online || !session_user || !session_user[0]) {
        lv_obj_add_flag(off, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *cnt = lv_label_create(top);
    lv_label_set_text(cnt, "");
    lv_obj_set_style_text_color(cnt, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(cnt, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot, 0, 240 - V1_RIBBON_H);
    lv_obj_set_size(bot, 320, V1_RIBBON_H);
    lv_obj_set_style_bg_color(bot, lv_color_hex(0x141A1E), 0);
    lv_obj_set_style_border_width(bot, 0, 0);
    lv_obj_set_style_pad_all(bot, 0, 0);
    lv_obj_clear_flag(bot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tst = lv_label_create(bot);
    lv_label_set_text(tst, "");
    lv_obj_set_style_text_color(tst, lv_color_hex(0xE8C040), 0);
    lv_obj_align(tst, LV_ALIGN_CENTER, 0, 0);

    if (ribbon_top) {
        *ribbon_top = top;
    }
    if (ribbon_bot) {
        *ribbon_bot = bot;
    }
    if (count_lab) {
        *count_lab = cnt;
    }
    if (offline_lab) {
        *offline_lab = off;
    }
    if (toast) {
        *toast = tst;
    }
}

void v1_ui_create_rec_disk(lv_obj_t *scr, lv_event_cb_t on_click, void *user_data,
                           lv_obj_t **btn_out, lv_obj_t **disk_out)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, V1_CAROUSEL_PLAY, V1_CAROUSEL_PLAY);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -V1_CAROUSEL_PLAY_PAD,
                 V1_CAROUSEL_HEADER_Y + V1_CAROUSEL_PLAY_PAD);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    if (on_click) {
        lv_obj_add_event_cb(btn, on_click, LV_EVENT_CLICKED, user_data);
    }

    const int disk_pad = 8;
    lv_obj_t *disk = lv_obj_create(btn);
    lv_obj_set_size(disk, V1_CAROUSEL_PLAY - 2 * disk_pad, V1_CAROUSEL_PLAY - 2 * disk_pad);
    lv_obj_center(disk);
    lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(disk, 0, 0);
    lv_obj_remove_flag(disk, LV_OBJ_FLAG_CLICKABLE);

    if (btn_out) {
        *btn_out = btn;
    }
    if (disk_out) {
        *disk_out = disk;
    }
}

lv_obj_t *v1_ui_paint_transparent_bar(lv_obj_t *scr, int y, int h)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_pos(bar, 0, y);
    lv_obj_set_size(bar, 320, h);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    return bar;
}

lv_obj_t *v1_ui_paint_icon_box_at(lv_obj_t *scr, int x, int y, int scale_pct)
{
    int w = 72 * scale_pct / 100;
    int h = 56 * scale_pct / 100;
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_size(box, w, h);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    return box;
}

void v1_ui_paint_mailbox_icon(lv_obj_t *parent, int scale_pct)
{
    int body_w = 44 * scale_pct / 100;
    int body_h = 32 * scale_pct / 100;
    int flap_w = 48 * scale_pct / 100;
    int flap_h = 14 * scale_pct / 100;
    int flap_y = 4 * scale_pct / 100;
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_size(body, body_w, body_h);
    lv_obj_align(body, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(body, 4 * scale_pct / 100, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *flap = lv_obj_create(parent);
    lv_obj_set_size(flap, flap_w, flap_h);
    lv_obj_align(flap, LV_ALIGN_TOP_MID, 0, flap_y);
    lv_obj_set_style_bg_color(flap, lv_color_hex(0xC8A030), 0);
    lv_obj_set_style_border_width(flap, 0, 0);
    lv_obj_set_style_pad_all(flap, 0, 0);
    lv_obj_set_style_transform_angle(flap, 450, 0);
    lv_obj_clear_flag(flap, LV_OBJ_FLAG_SCROLLABLE);
}

void v1_ui_style_conn_title_font(lv_obj_t *lab)
{
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_28, 0);
#else
    lv_obj_set_style_transform_pivot_x(lab, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(lab, lv_pct(50), 0);
    lv_obj_set_style_transform_scale(lab, 512, 0);
#endif
}

lv_obj_t *v1_ui_paint_message_panel(lv_obj_t *scr, int y, const char *headline,
                                    const char *sub, const char *hint)
{
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 288, hint ? 148 : 88);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(V1_UI_CARD), 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(V1_UI_CARD_PRESS), 0);
    lv_obj_set_style_pad_all(panel, 8, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *h = lv_label_create(panel);
    lv_label_set_text(h, headline);
    lv_obj_set_style_text_color(h, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_set_width(h, 264);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t *s = lv_label_create(panel);
    lv_label_set_text(s, sub);
    lv_obj_set_style_text_color(s, lv_color_hex(V1_UI_TEXT_MUT), 0);
    lv_obj_set_width(s, 264);
    lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 36);

    if (hint && hint[0]) {
        lv_obj_t *t = lv_label_create(panel);
        lv_label_set_text(t, hint);
        lv_obj_set_style_text_color(t, lv_color_hex(V1_UI_TEXT_DIM), 0);
        lv_obj_set_width(t, 264);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 88);
    }
    return panel;
}

/* --- sleep + settings (Phase 3) --- */

static int64_t s_activity_us;
static int64_t s_idle_track_us;
static int64_t s_sleep_enter_us;
static bool s_asleep;
static bool s_dimmed;

static lv_obj_t *s_sleep_glow;
static lv_obj_t *s_sleep_hint;
static lv_obj_t *s_sleep_badge;
static lv_obj_t *s_sleep_badge_lab;
static lv_obj_t *s_vol_slider;
static lv_obj_t *s_vol_val_lab;
static lv_obj_t *s_modal_dim;
static lv_obj_t *s_modal_panel;
static lv_obj_t *s_modal_body;
static bool s_modal_open;
static v1_setting_id_t s_modal_kind;

static int64_t sleep_now_us(void)
{
    return esp_timer_get_time();
}

void v1_ui_sleep_init(int64_t activity_us)
{
    s_activity_us = activity_us;
    s_idle_track_us = activity_us;
    s_asleep = false;
    s_dimmed = false;
}

void v1_ui_sleep_note_activity(void)
{
    s_activity_us = sleep_now_us();
}

void v1_ui_sleep_bump_idle(void)
{
    s_idle_track_us = sleep_now_us();
}

bool v1_ui_sleep_is_asleep(void)
{
    return s_asleep;
}

bool v1_ui_sleep_is_dimmed(void)
{
    return s_dimmed;
}

int64_t v1_ui_sleep_activity_us(void)
{
    return s_activity_us;
}

void v1_ui_sleep_set_dimmed(bool dimmed)
{
    s_dimmed = dimmed;
}

void v1_ui_sleep_enter(void)
{
    s_asleep = true;
    s_dimmed = false;
    s_sleep_enter_us = sleep_now_us();
}

void v1_ui_sleep_wake_from_asleep(bool request_full_repaint)
{
    if (s_asleep || s_dimmed) {
        bool was_asleep = s_asleep;
        s_asleep = false;
        s_dimmed = false;
        board_backlight_set(V1_BRIGHT_NORM);
        if (was_asleep && request_full_repaint) {
            v1_ui_request_repaint();
        }
    }
}

bool v1_ui_can_enter_sleep(state_t st)
{
    return st == ST_ROSTER || st == ST_PIN || st == ST_CAROUSEL || st == ST_SETTINGS;
}

static int unread_count(const v1_ui_sleep_cfg_t *cfg)
{
    int n = 0;
    if (!cfg || !cfg->msgs) {
        return 0;
    }
    for (int i = 0; i < cfg->msg_n; i++) {
        if (!cfg->msgs[i].read) {
            n++;
        }
    }
    return n;
}

static uint32_t sleep_accent(const v1_ui_sleep_cfg_t *cfg)
{
    if (cfg && cfg->session_signed_in && cfg->session_signed_in()) {
        int idx = v1_connect_user_index(cfg->session_user);
        if (idx >= 0) {
            return v1_connect_user_accent(idx);
        }
    }
    return V1_UI_ACCENT;
}

static lv_opa_t sleep_breathe_opa(int64_t ms_since_enter, lv_opa_t lo, lv_opa_t hi)
{
    int64_t t = ms_since_enter % V1_SLEEP_BREATHE_MS;
    int half = V1_SLEEP_BREATHE_MS / 2;
    int span = (int)hi - (int)lo;
    int v;
    if (t < half) {
        v = (int)lo + (int)(span * t / half);
    } else {
        v = (int)hi - (int)(span * (t - half) / half);
    }
    if (v < 0) {
        v = 0;
    }
    if (v > 255) {
        v = 255;
    }
    return (lv_opa_t)v;
}

static int sleep_breathe_brightness(int64_t ms_since_enter)
{
    int64_t t = ms_since_enter % V1_SLEEP_BREATHE_MS;
    int half = V1_SLEEP_BREATHE_MS / 2;
    int span = V1_BRIGHT_SLEEP_PEAK - V1_BRIGHT_SLEEP;
    if (t < half) {
        return V1_BRIGHT_SLEEP + (int)(span * t / half);
    }
    return V1_BRIGHT_SLEEP_PEAK - (int)(span * (t - half) / half);
}

static lv_opa_t sleep_hint_boost(int64_t ms_since_enter)
{
    int64_t phase = ms_since_enter % V1_SLEEP_HINT_MS;
    if (phase >= V1_SLEEP_HINT_PULSE_MS) {
        return 0;
    }
    int64_t t = phase;
    int half = V1_SLEEP_HINT_PULSE_MS / 2;
    int v;
    if (t < half) {
        v = (int)(80 * t / half);
    } else {
        v = (int)(80 * (V1_SLEEP_HINT_PULSE_MS - t) / half);
    }
    return (lv_opa_t)v;
}

void v1_ui_paint_sleep(lv_obj_t *scr, const v1_ui_sleep_cfg_t *cfg)
{
    s_sleep_glow = NULL;
    s_sleep_hint = NULL;
    s_sleep_badge = NULL;
    s_sleep_badge_lab = NULL;

    uint32_t accent = sleep_accent(cfg);
    int unread = (cfg && cfg->session_signed_in && cfg->session_signed_in()) ? unread_count(cfg) : 0;

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_SLEEP_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_sleep_glow = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sleep_glow);
    lv_obj_remove_flag(s_sleep_glow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sleep_glow, V1_LCD_W, 140);
    lv_obj_set_pos(s_sleep_glow, 0, 100);
    lv_obj_set_style_bg_color(s_sleep_glow, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_grad_color(s_sleep_glow, lv_color_hex(V1_UI_SLEEP_BG), 0);
    lv_obj_set_style_bg_grad_dir(s_sleep_glow, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_sleep_glow, LV_OPA_20, 0);

    s_sleep_hint = lv_obj_create(scr);
    lv_obj_remove_style_all(s_sleep_hint);
    lv_obj_remove_flag(s_sleep_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_sleep_hint, 72, 36);
    lv_obj_set_pos(s_sleep_hint, 124, 188);
    lv_obj_set_style_radius(s_sleep_hint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sleep_hint, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(s_sleep_hint, LV_OPA_0, 0);

    if (unread > 0) {
        s_sleep_badge = lv_obj_create(scr);
        lv_obj_remove_style_all(s_sleep_badge);
        lv_obj_remove_flag(s_sleep_badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_sleep_badge, 28, 28);
        lv_obj_align(s_sleep_badge, LV_ALIGN_TOP_MID, 0, 36);
        lv_obj_set_style_radius(s_sleep_badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_sleep_badge, lv_color_hex(accent), 0);
        s_sleep_badge_lab = lv_label_create(s_sleep_badge);
        char buf[8];
        if (unread > 9) {
            snprintf(buf, sizeof(buf), "9+");
        } else {
            snprintf(buf, sizeof(buf), "%d", unread);
        }
        lv_label_set_text(s_sleep_badge_lab, buf);
        lv_obj_set_style_text_color(s_sleep_badge_lab, lv_color_hex(V1_UI_SLEEP_BG), 0);
        lv_obj_center(s_sleep_badge_lab);
    }
    v1_ui_hook_scr(scr);
}

void v1_ui_refresh_sleep_anim(const v1_ui_sleep_cfg_t *cfg)
{
    (void)cfg;
    if (!s_asleep || !s_sleep_glow) {
        return;
    }
    int64_t ms = (sleep_now_us() - s_sleep_enter_us) / 1000;
    int glow = (int)sleep_breathe_opa(ms, LV_OPA_10, LV_OPA_30) + (int)sleep_hint_boost(ms);
    if (glow > (int)LV_OPA_COVER) {
        glow = (int)LV_OPA_COVER;
    }
    lv_obj_set_style_bg_opa(s_sleep_glow, (lv_opa_t)glow, 0);
    if (s_sleep_hint) {
        lv_obj_set_style_bg_opa(s_sleep_hint, sleep_hint_boost(ms), 0);
    }
    if (s_sleep_badge && cfg && cfg->session_signed_in && cfg->session_signed_in() && unread_count(cfg) > 0) {
        lv_obj_set_style_bg_opa(s_sleep_badge, sleep_breathe_opa(ms, LV_OPA_40, LV_OPA_80), 0);
    }
    int bl;
    if (ms < V1_SLEEP_FADE_MS) {
        int target = sleep_breathe_brightness(ms);
        bl = V1_BRIGHT_DIM + (target - V1_BRIGHT_DIM) * (int)ms / V1_SLEEP_FADE_MS;
    } else {
        bl = sleep_breathe_brightness(ms);
    }
    board_backlight_set(bl);
    lv_obj_invalidate(s_sleep_glow);
}

bool v1_ui_sleep_tick(int64_t now_us, state_t st, const v1_ui_sleep_cfg_t *cfg)
{
    if (s_privacy_screen_off) {
        return false;
    }
    int64_t idle = now_us - s_activity_us;
    if (!s_asleep && v1_ui_can_enter_sleep(st) && idle > (int64_t)V1_UI_SLEEP_MS * 1000) {
        v1_ui_sleep_enter();
        if (cfg && cfg->stop_playback) {
            cfg->stop_playback();
        }
        board_lvgl_lock(0);
        v1_ui_paint_sleep(lv_screen_active(), cfg);
        board_lvgl_unlock();
        board_lvgl_lock(0);
        v1_ui_refresh_sleep_anim(cfg);
        board_lvgl_unlock();
        return true;
    }
    if (s_asleep) {
        board_lvgl_lock(0);
        v1_ui_refresh_sleep_anim(cfg);
        board_lvgl_unlock();
    } else if (!s_dimmed && !s_asleep && s_activity_us > 0 && idle > (int64_t)V1_UI_DIM_MS * 1000) {
        s_dimmed = true;
        board_backlight_set(V1_BRIGHT_DIM);
    }
    return false;
}

static v1_ui_settings_cfg_t s_settings_cfg;
static bool s_settings_bound;

#define SETTINGS_PANEL_BG  0x1E242C
#define SETTINGS_PANEL_BD  0x5AA0E8
#define SETTINGS_INNER_W   (V1_MODAL_W - 2 * V1_MODAL_PAD)
#define SETTINGS_PICK_COLS 3
#define SETTINGS_GRAD_W    72
#define SETTINGS_GRAD_H    54

static const char *const s_setting_titles[V1_SETTINGS_CARD_N] = {
    "Logout", "Volume", "Auto-play new", "User icons", "User colors", "Card colors",
};

static const uint32_t s_accents[V1_ACCENT_COUNT] = {
    0x5AA0E8, 0xE8C040, 0x7AC47A, 0xC070E8,
    0xE87A9A, 0x7AD4E8, 0xD4A0E8, 0xE8A87A,
    0x4ECDC4, 0xFF6B6B,
};

void v1_ui_settings_bind(const v1_ui_settings_cfg_t *cfg)
{
    if (!cfg) {
        s_settings_bound = false;
        return;
    }
    s_settings_cfg = *cfg;
    s_settings_bound = true;
}

const char *v1_ui_setting_title(v1_setting_id_t id)
{
    if ((int)id < 0 || (int)id >= V1_SETTINGS_CARD_N) {
        return "";
    }
    return s_setting_titles[id];
}

void v1_ui_modal_invalidate(void)
{
    s_modal_dim = NULL;
    s_modal_panel = NULL;
    s_modal_body = NULL;
    s_vol_slider = NULL;
    s_vol_val_lab = NULL;
    s_modal_open = false;
}

bool v1_ui_modal_is_open(void)
{
    return s_modal_open;
}

void v1_ui_modal_close(void)
{
    if (s_modal_panel) {
        lv_obj_delete(s_modal_panel);
    }
    if (s_modal_dim) {
        lv_obj_delete(s_modal_dim);
    }
    v1_ui_modal_invalidate();
}

static void refresh_vol_label(void)
{
    if (!s_vol_val_lab || !s_settings_cfg.vol_notch || !s_settings_cfg.roomvol_codec) {
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", s_settings_cfg.roomvol_codec(*s_settings_cfg.vol_notch));
    lv_label_set_text(s_vol_val_lab, buf);
}

static void on_volume(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    int notch = (int)lv_slider_get_value(sl);
    if (s_settings_cfg.vol_notch) {
        *s_settings_cfg.vol_notch = notch;
    }
    if (s_settings_cfg.apply_volume && s_settings_cfg.roomvol_codec) {
        s_settings_cfg.apply_volume(s_settings_cfg.roomvol_codec(notch));
    }
    refresh_vol_label();
    v1_ui_bump_activity();
}

static void on_sign_out(lv_event_t *e)
{
    (void)e;
    v1_ui_modal_close();
    if (s_settings_cfg.stop_playback) {
        s_settings_cfg.stop_playback();
    }
    if (s_settings_cfg.session_user) {
        s_settings_cfg.session_user[0] = 0;
    }
    v1_state_post_goto(ST_ROSTER);
    v1_ui_request_repaint();
    v1_ui_bump_activity();
}

static void save_profile(const char *session_user)
{
    int idx = v1_connect_user_index(session_user);
    if (idx < 0 || !session_user || !session_user[0]) {
        return;
    }
    char js[128];
    char hex[8];
    uint32_t c = v1_connect_user_accent(idx);
    snprintf(hex, sizeof(hex), "#%06X", (unsigned)(c & 0xFFFFFF));
    snprintf(js, sizeof(js),
             "{\"avatar_slot\":%u,\"accent_hex\":\"%s\",\"autoplay_new\":%s}",
             (unsigned)v1_connect_user_avatar_slot(idx), hex,
             v1_connect_users()[idx].autoplay_new ? "true" : "false");
    uint8_t tmp[128];
    http_buf_t b = { .buf = tmp, .cap = sizeof(tmp) };
    (void)v1_api_http_json("PUT", "/v1/profile", js, session_user, &b);
}

static void on_autoplay_toggle(lv_event_t *e)
{
    if (!s_settings_bound || !s_settings_cfg.session_user) {
        return;
    }
    int idx = v1_connect_user_index(s_settings_cfg.session_user);
    if (idx < 0) {
        return;
    }
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    v1_connect_users_mut()[idx].autoplay_new = on;
    save_profile(s_settings_cfg.session_user);
    v1_ui_bump_activity();
}

static void mark_circle_selected(lv_obj_t *parent, lv_obj_t *chosen)
{
    if (!parent) {
        return;
    }
    uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(parent, i);
        if (!lv_obj_has_flag(ch, LV_OBJ_FLAG_CLICKABLE)) {
            continue;
        }
        lv_obj_set_style_border_width(ch, ch == chosen ? 2 : 0, 0);
        lv_obj_set_style_border_color(ch, lv_color_hex(V1_UI_TEXT), 0);
    }
}

static void on_accent_pick(lv_event_t *e)
{
    uint32_t color = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (!s_settings_bound) {
        return;
    }
    int idx = v1_connect_user_index(s_settings_cfg.session_user);
    if (idx < 0) {
        return;
    }
    v1_connect_users_mut()[idx].accent = color;
    save_profile(s_settings_cfg.session_user);
    mark_circle_selected(lv_obj_get_parent(lv_event_get_target(e)),
                         lv_event_get_target(e));
    v1_ui_bump_activity();
}

static void on_grad_pick(lv_event_t *e)
{
    uint8_t id = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    v1_carousel_set_card_grad(id);
    v1_carousel_save_card_grad();
    mark_circle_selected(lv_obj_get_parent(lv_event_get_target(e)),
                         lv_event_get_target(e));
    v1_ui_bump_activity();
}

static void on_avatar_pick(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_settings_bound) {
        return;
    }
    int idx = v1_connect_user_index(s_settings_cfg.session_user);
    if (idx < 0) {
        return;
    }
    v1_connect_users_mut()[idx].avatar_slot = (uint8_t)slot;
    save_profile(s_settings_cfg.session_user);
    mark_circle_selected(lv_obj_get_parent(lv_event_get_target(e)),
                         lv_event_get_target(e));
    v1_ui_bump_activity();
}

static void on_modal_close(lv_event_t *e)
{
    (void)e;
    v1_ui_modal_close();
    v1_ui_bump_activity();
}

static int settings_even_x(int count, int index, int item_w)
{
    if (count <= 1) {
        return (SETTINGS_INNER_W - item_w) / 2;
    }
    int gap = (SETTINGS_INNER_W - count * item_w) / (count - 1);
    if (gap < 4) {
        gap = 4;
    }
    return index * (item_w + gap);
}

static void settings_paint_face_slot(lv_obj_t *parent, int slot, uint32_t accent, int sz)
{
    if (slot <= 0) {
        v1_ui_paint_geometry_face(parent, accent, sz);
        return;
    }
    const lv_image_dsc_t *img = avatar_image((uint8_t)slot);
    if (img) {
        lv_obj_t *av = lv_image_create(parent);
        lv_image_set_src(av, img);
        lv_obj_set_size(av, sz, sz);
        lv_obj_center(av);
        return;
    }
    static const uint32_t fallback[V1_ACCENT_COUNT] = {
        0x5AA0E8, 0xE8C040, 0x7AC47A, 0xC070E8,
        0xE87A9A, 0x7AD4E8, 0xD4A0E8, 0xE8A87A,
        0x4ECDC4, 0xFF6B6B,
    };
    v1_ui_paint_geometry_face(parent, fallback[(slot - 1) % V1_ACCENT_COUNT], sz);
}

static int paint_pick_grid(lv_obj_t *content, int y0, int item_n, int item_sz,
                           void (*paint_item)(lv_obj_t *btn, int i, void *ctx), void *ctx,
                           int selected)
{
    const int cols = SETTINGS_PICK_COLS;
    const int gap = 8;
    int rows = (item_n + cols - 1) / cols;
    for (int i = 0; i < item_n; i++) {
        int row = i / cols;
        int col = i % cols;
        int row_count = cols;
        int row_start = row * cols;
        if (row_start + row_count > item_n) {
            row_count = item_n - row_start;
        }
        int x = settings_even_x(row_count, col, item_sz);
        lv_obj_t *btn = lv_button_create(content);
        lv_obj_set_size(btn, item_sz, item_sz);
        lv_obj_set_pos(btn, x, y0 + row * (item_sz + gap));
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(btn, true, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(V1_UI_CARD), 0);
        lv_obj_set_style_border_width(btn, i == selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(V1_UI_TEXT), 0);
        paint_item(btn, i, ctx);
    }
    return rows * (item_sz + gap);
}

typedef struct {
    uint32_t accent;
} face_paint_ctx_t;

static void paint_face_btn(lv_obj_t *btn, int slot, void *ctx)
{
    face_paint_ctx_t *c = ctx;
    settings_paint_face_slot(btn, slot, c->accent, V1_PICK_CIRCLE_D - 6);
    lv_obj_add_event_cb(btn, on_avatar_pick, LV_EVENT_CLICKED, (void *)(intptr_t)slot);
}

static void paint_accent_btn(lv_obj_t *btn, int i, void *ctx)
{
    (void)ctx;
    lv_obj_set_style_bg_color(btn, lv_color_hex(s_accents[i]), 0);
    lv_obj_add_event_cb(btn, on_accent_pick, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)s_accents[i]);
}

static void paint_modal_body(lv_obj_t *scroll, v1_setting_id_t id)
{
    static const struct { uint8_t id; uint32_t top; uint32_t bot; } grads[V1_CARD_GRAD_N] = {
        {1, 0x5AA0E8, 0x101418}, {2, 0xE85A5A, 0xE8C040}, {5, 0xF0F2F5, 0x8898A8},
    };

    lv_obj_t *content = lv_obj_create(scroll);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, SETTINGS_INNER_W);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    s_modal_body = content;

    int y = 0;
    const char *session = s_settings_cfg.session_user;
    int me = v1_connect_user_index(session);
    uint32_t my_accent = (me >= 0) ? v1_connect_user_accent(me) : V1_UI_ACCENT;

    if (id == V1_SET_LOGOUT) {
        lv_obj_t *hint = lv_label_create(content);
        lv_label_set_text(hint, "Sign out of this box?");
        lv_obj_set_style_text_color(hint, lv_color_hex(V1_UI_TEXT), 0);
        lv_obj_set_width(hint, SETTINGS_INNER_W);
        lv_obj_set_pos(hint, 0, y);
        y += 28;
        lv_obj_t *out = lv_button_create(content);
        lv_obj_set_size(out, SETTINGS_INNER_W, V1_LIST_BTN_H);
        lv_obj_set_pos(out, 0, y);
        lv_obj_set_style_bg_color(out, lv_color_hex(V1_UI_CARD), 0);
        lv_obj_set_style_radius(out, 8, 0);
        lv_obj_t *out_lab = lv_label_create(out);
        lv_label_set_text(out_lab, "sign out");
        lv_obj_set_style_text_color(out_lab, lv_color_hex(V1_UI_TEXT), 0);
        lv_obj_center(out_lab);
        lv_obj_add_event_cb(out, on_sign_out, LV_EVENT_CLICKED, NULL);
        y += V1_LIST_BTN_H + V1_MODAL_PAD;
    } else if (id == V1_SET_VOLUME) {
        const int slider_w = SETTINGS_INNER_W - 40;
        const int slider_x = (SETTINGS_INNER_W - slider_w) / 2;
        s_vol_slider = lv_slider_create(content);
        lv_obj_set_size(s_vol_slider, slider_w, 22);
        lv_obj_set_pos(s_vol_slider, slider_x, y + 8);
        lv_slider_set_range(s_vol_slider, 0, V1_ROOMVOL_ON);
        if (s_settings_cfg.vol_notch) {
            lv_slider_set_value(s_vol_slider, *s_settings_cfg.vol_notch, LV_ANIM_OFF);
        }
        lv_obj_add_event_cb(s_vol_slider, on_volume, LV_EVENT_VALUE_CHANGED, NULL);
        s_vol_val_lab = lv_label_create(content);
        lv_obj_set_style_text_color(s_vol_val_lab, lv_color_hex(V1_UI_TEXT_DIM), 0);
        lv_obj_set_pos(s_vol_val_lab, slider_x + slider_w - 28, y + 36);
        refresh_vol_label();
        y += 64;
    } else if (id == V1_SET_AUTOPLAY) {
        lv_obj_t *hint = lv_label_create(content);
        lv_label_set_text(hint,
                           "When you are on the newest message and\ncaught up, play new mail automatically.");
        lv_obj_set_style_text_color(hint, lv_color_hex(V1_UI_TEXT_DIM), 0);
        lv_obj_set_width(hint, SETTINGS_INNER_W);
        lv_obj_set_pos(hint, 0, y);
        y += 40;
        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, SETTINGS_INNER_W, V1_LIST_BTN_H);
        lv_obj_set_pos(row, 0, y);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, "Auto-play new messages");
        lv_obj_set_style_text_color(lab, lv_color_hex(V1_UI_TEXT), 0);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        bool on = false;
        if (me >= 0) {
            on = v1_connect_users()[me].autoplay_new;
        }
        if (on) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw, on_autoplay_toggle, LV_EVENT_VALUE_CHANGED, NULL);
        y += V1_LIST_BTN_H + V1_MODAL_PAD;
    } else if (id == V1_SET_ICONS) {
        face_paint_ctx_t ctx = { .accent = my_accent };
        uint8_t my_slot = (me >= 0) ? v1_connect_user_avatar_slot(me) : 0;
        y += paint_pick_grid(content, y, V1_AVATAR_SLOTS + 1, V1_PICK_CIRCLE_D,
                             paint_face_btn, &ctx, (int)my_slot);
    } else if (id == V1_SET_COLORS) {
        int selected = 0;
        for (int i = 0; i < V1_ACCENT_COUNT; i++) {
            if (s_accents[i] == my_accent) {
                selected = i;
                break;
            }
        }
        y += paint_pick_grid(content, y, V1_ACCENT_COUNT, V1_PICK_CIRCLE_D,
                             paint_accent_btn, NULL, selected);
    } else if (id == V1_SET_CARD) {
        uint8_t card_grad = v1_carousel_card_grad();
        for (int i = 0; i < V1_CARD_GRAD_N; i++) {
            int x = settings_even_x(V1_CARD_GRAD_N, i, SETTINGS_GRAD_W);
            lv_obj_t *gb = lv_button_create(content);
            lv_obj_set_size(gb, SETTINGS_GRAD_W, SETTINGS_GRAD_H);
            lv_obj_set_pos(gb, x, y);
            lv_obj_set_style_bg_color(gb, lv_color_hex(grads[i].top), 0);
            lv_obj_set_style_bg_grad_color(gb, lv_color_hex(grads[i].bot), 0);
            lv_obj_set_style_bg_grad_dir(gb, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_radius(gb, 8, 0);
            lv_obj_set_style_border_width(gb, grads[i].id == card_grad ? 2 : 0, 0);
            lv_obj_set_style_border_color(gb, lv_color_hex(V1_UI_TEXT), 0);
            lv_obj_add_event_cb(gb, on_grad_pick, LV_EVENT_CLICKED,
                                (void *)(uintptr_t)grads[i].id);
        }
        y += SETTINGS_GRAD_H + V1_MODAL_PAD;
    }

    lv_obj_set_height(content, y > 0 ? y : 1);
}

void v1_ui_modal_show_setting(lv_obj_t *scr, v1_setting_id_t id)
{
    if (!scr || !s_settings_bound) {
        return;
    }
    if (s_modal_open) {
        v1_ui_modal_close();
    }
    s_modal_kind = id;

    s_modal_dim = lv_obj_create(scr);
    lv_obj_remove_style_all(s_modal_dim);
    lv_obj_set_size(s_modal_dim, V1_LCD_W, 240);
    lv_obj_set_pos(s_modal_dim, 0, 0);
    lv_obj_set_style_bg_color(s_modal_dim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal_dim, LV_OPA_50, 0);
    lv_obj_clear_flag(s_modal_dim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal_dim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_modal_dim, on_modal_close, LV_EVENT_CLICKED, NULL);

    s_modal_panel = lv_obj_create(scr);
    lv_obj_set_pos(s_modal_panel, V1_MODAL_INSET_X, V1_MODAL_INSET_Y);
    lv_obj_set_size(s_modal_panel, V1_MODAL_W, V1_MODAL_H);
    lv_obj_set_style_bg_color(s_modal_panel, lv_color_hex(SETTINGS_PANEL_BG), 0);
    lv_obj_set_style_bg_opa(s_modal_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_modal_panel, 12, 0);
    lv_obj_set_style_border_width(s_modal_panel, 2, 0);
    lv_obj_set_style_border_color(s_modal_panel, lv_color_hex(SETTINGS_PANEL_BD), 0);
    lv_obj_set_style_pad_all(s_modal_panel, 0, 0);
    lv_obj_clear_flag(s_modal_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_modal_panel);
    lv_label_set_text(title, v1_ui_setting_title(id));
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, V1_MODAL_PAD, 8);

    lv_obj_t *close_btn = lv_button_create(s_modal_panel);
    lv_obj_set_size(close_btn, V1_MODAL_CLOSE_SZ, V1_MODAL_CLOSE_SZ);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(V1_UI_CARD), 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_set_style_pad_all(close_btn, 0, 0);
    lv_obj_t *xlab = lv_label_create(close_btn);
    lv_label_set_text(xlab, "X");
    lv_obj_set_style_text_color(xlab, lv_color_hex(V1_UI_TEXT), 0);
    lv_obj_center(xlab);
    lv_obj_add_event_cb(close_btn, on_modal_close, LV_EVENT_CLICKED, NULL);

    lv_obj_t *scroll = lv_obj_create(s_modal_panel);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_pos(scroll, 0, V1_MODAL_HEADER_H);
    lv_obj_set_size(scroll, V1_MODAL_W, V1_MODAL_H - V1_MODAL_HEADER_H);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(scroll, V1_MODAL_PAD, 0);

    paint_modal_body(scroll, id);
    s_modal_open = true;
    v1_ui_bump_activity();
}
