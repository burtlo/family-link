#!/usr/bin/env python3
"""Phase 3 extraction: split x02_product_shell.c into v1 modules + x02_main.c."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MONO = ROOT / "firmware/demos/x02_product_shell.c"
MAIN = ROOT / "firmware/v1/x02_main.c"
CAR = ROOT / "firmware/v1/v1_carousel.c"
REC = ROOT / "firmware/v1/v1_record.c"
SHIM = ROOT / "firmware/demos/x02_product_shell.c"

text = MONO.read_text(encoding="utf-8")
lines = text.splitlines(keepends=True)

# Functions to move (exact static/function names)
TO_CAROUSEL = {
    "card_grad_id_valid", "active_card_grad", "nvs_load_card_grad", "nvs_save_card_grad",
    "carousel_card_at", "sender_display_name", "apply_card_grad_style", "style_msg_card",
    "save_position", "mark_read", "http_blob_get", "focus_msg", "stop_playback",
    "message_at_end", "restart_message_if_at_end", "snap_target_x", "carousel_center_index",
    "refresh_card_visuals_from_scroll", "scroll_x_exec", "snap_anim_done", "snap_scroll_to",
    "carousel_snap_to_focus", "sync_focus_from_scroll", "set_transport_icon",
    "refresh_card_focus_states", "make_pause_icon", "style_transport_slider",
    "ui_refresh_transport", "playback_task", "shift_focus_to", "on_carousel_card_click",
    "on_carousel_scroll", "on_play", "on_scrub", "rgb565_from_hex", "play_tri_rot_pt",
    "play_tri_edge", "fill_play_icon_fb", "make_play_icon_buf", "carousel_add_card",
    "save_profile", "paint_carousel", "refresh_offline_ribbon", "paint_ribbons",
    "roomvol_codec", "apply_volume", "chirp_fade_edges", "chirp_write_pcm", "play_chirp",
    "play_chirp_pair",
}

TO_RECORD = {
    "wav_header", "pcm_peak", "post_wav", "record_take", "pcm_buffer_ready",
    "record_task_fn", "on_pick_btn", "paint_pick", "paint_record_overlay",
}

TO_UI = {
    "on_accent_pick", "on_grad_pick", "on_avatar_pick", "refresh_vol_label", "on_volume",
    "on_sign_out", "paint_settings", "can_enter_sleep", "session_signed_in", "unread_count",
    "sleep_accent", "sleep_breathe_brightness", "sleep_breathe_opa", "sleep_hint_boost",
    "paint_sleep", "refresh_sleep_anim",
}

TO_MAIN = {
    "now_us", "bump_idle", "on_auth_ok", "on_ws_inbox", "note_activity", "set_toast",
    "on_scr_activity", "hook_scr", "mute_latched", "signed_out_pre_auth",
    "paint_user_portrait_aligned", "paint_face_sized", "paint_asterisk_icon",
    "http_json_timeout", "http_json", "reload_inbox", "request_chirp", "request_chirp_pair",
    "on_circle_up", "on_boot_press", "paint", "ui_task", "app_main", "sntp_wait",
    "roster_row_layout", "style_list_row", "style_list_row_label",
}


def find_functions(src: str) -> dict[str, tuple[int, int]]:
    """Return name -> (start_line_0based, end_line_0based_exclusive)."""
    pat = re.compile(
        r"^(?:(static)\s+)?(?:(void|bool|int|size_t|msg_t\s\*|lv_obj_t\s\*)\s+)?"
        r"(\w+)\s*\([^;]*\)\s*\{",
        re.MULTILINE,
    )
    matches = list(pat.finditer(src))
    out: dict[str, tuple[int, int]] = {}
    line_starts = [0] + [m.start() + 1 for m in re.finditer(r"\n", src)]
    for i, m in enumerate(matches):
        name = m.group(3)
        start = src[: m.start()].count("\n")
        end = matches[i + 1].start() if i + 1 < len(matches) else len(src)
        end_line = src[:end].count("\n")
        out[name] = (start, end_line)
    return out


funcs = find_functions(text)

CAR_HEADER = '''#include "v1_carousel.h"

#include "pass.h"
#include "v1_api.h"
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

static const esp_codec_dev_sample_info_t s_fs = {
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

'''

REC_HEADER = '''#include "v1_record.h"

#include "v1_api.h"
#include "v1_carousel.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "http_bearer.h"
#include "who.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "v1_record";

#define CHUNK 640

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = V1_SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static v1_record_cfg_t s_cfg;
static char *s_session;
static char s_send_to[16];
static bool s_send_all;
static volatile bool s_stop_record;
static volatile bool s_cancel_pick;
static int64_t s_pick_open_us;
static uint8_t *s_pcm;

static int64_t now_us(void) { return esp_timer_get_time(); }

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

void v1_record_init(const v1_record_cfg_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
        s_session = cfg->session_user;
    }
}

void v1_record_start_task(void)
{
    xTaskCreate(record_task_fn, "rec", 8192, NULL, 5, NULL);
}

void v1_record_paint_pick(lv_obj_t *scr) { paint_pick(scr); }
void v1_record_paint_overlay(lv_obj_t *scr) { paint_record_overlay(scr); }

void v1_record_on_circle_stop(void) { s_stop_record = true; }

void v1_record_on_shoulder_cancel(void)
{
    s_cancel_pick = true;
    s_stop_record = true;
    if (s_cfg.play_chirp_pair) {
        s_cfg.play_chirp_pair(523, 392);
    }
    if (v1_state_get() == ST_PICK) {
        v1_state_post_goto(ST_CAROUSEL);
        v1_ui_request_repaint();
    }
}

void v1_record_set_send_all(bool all) { s_send_all = all; }

void v1_record_set_send_to(const char *user_id)
{
    if (user_id) {
        strncpy(s_send_to, user_id, sizeof(s_send_to) - 1);
    }
}

void v1_record_open_pick(int64_t t_us)
{
    s_cancel_pick = false;
    s_pick_open_us = t_us;
    v1_state_post_goto(ST_PICK);
    v1_ui_request_repaint();
}

bool v1_record_tick_pick_timeout(int64_t now)
{
    if (v1_state_get() != ST_PICK || s_pick_open_us <= 0) {
        return false;
    }
    if ((now - s_pick_open_us) <= (int64_t)V1_UI_PICK_TIMEOUT_MS * 1000) {
        return false;
    }
    s_pick_open_us = 0;
    if (s_cfg.play_chirp_pair) {
        s_cfg.play_chirp_pair(523, 392);
    }
    v1_state_post_goto(ST_CAROUSEL);
    v1_ui_request_repaint();
    return true;
}

void v1_record_give_work(void)
{
    if (s_cfg.work_sem) {
        xSemaphoreGive(s_cfg.work_sem);
    }
}

'''


def extract_block(name: str) -> str:
    if name not in funcs:
        return f"/* MISSING {name} */\n"
    s, e = funcs[name]
    block = "".join(lines[s:e])
    return block


def xform_carousel(block: str) -> str:
    reps = [
        ("s_st ==", "v1_state_get() =="),
        ("s_st !=", "v1_state_get() !="),
        ("s_session_user", "s_session"),
        ("s_msgs", "s_inbox_msgs"),
        ("s_msg_n", "s_inbox_n"),
        ("s_focus", "s_inbox_focus"),
        ("s_volume", "(*s_cfg.volume)"),
        ("s_vol_notch", "(*s_cfg.vol_notch)"),
        ("s_buf", "s_cfg.playback_buf"),
        ("s_spk", "s_cfg.spk"),
        ("http_json(", "v1_api_http_json("),
        ("reload_inbox()", "v1_api_reload_inbox(s_session)"),
        ("request_repaint()", "v1_ui_request_repaint()"),
        ("request_transport_refresh()", "v1_ui_request_transport_refresh()"),
        ("request_chirp(", "v1_ui_request_chirp("),
        ("paint_user_portrait_aligned", "v1_ui_paint_user_portrait_aligned"),
        ("hook_scr(", "v1_ui_hook_scr("),
        ("note_activity()", "v1_ui_bump_activity()"),
        ("user_index(", "v1_connect_user_index("),
        ("user_index_by_label(", "v1_connect_user_index_by_label("),
        ("user_name(", "v1_connect_user_name("),
        ("s_user_n", "v1_connect_user_count()"),
        ("s_users", "v1_connect_users_mut()"),
        ("s_toast_pending = true;\n        strncpy(s_toast_msg,", "v1_ui_set_toast(NULL,"),
        ('sizeof(s_toast_msg) - 1)', '64)'),
        ("SNAP_SCROLL_MS_MAX", "V1_CAROUSEL_SNAP_MS_MAX"),
        ("SNAP_SCROLL_MS", "V1_CAROUSEL_SNAP_MS_MIN"),
        ("TRIM_MS", "V1_RECORD_TRIM_MS"),
        ("SAMPLE_RATE", "V1_SAMPLE_RATE"),
        ("BUF_CAP", "V1_PLAYBACK_BUF_CAP"),
        ("CHIRP_WRITE", "V1_CHIRP_WRITE"),
        ("CHIRP_VOL", "V1_CHIRP_VOL"),
        ("CHIRP_SAMPLES", "V1_CHIRP_SAMPLES"),
        ("CHIRP_DRAIN", "V1_CHIRP_DRAIN"),
        ("UI_TEXT", "V1_UI_TEXT"),
        ("SCROLL_CARD_W", "V1_SCROLL_CARD_W"),
        ("SCROLL_CARD_H", "V1_SCROLL_CARD_H"),
        ("SCROLL_CARD_GAP", "V1_SCROLL_CARD_GAP"),
        ("SCROLL_CARD_Y", "V1_SCROLL_CARD_Y"),
        ("CAROUSEL_TRANSPORT_Y", "V1_CAROUSEL_TRANSPORT_Y"),
        ("CAROUSEL_PLAY", "V1_CAROUSEL_PLAY"),
        ("CAROUSEL_DISK", "V1_CAROUSEL_DISK"),
        ("CARD_RADIUS", "V1_CARD_RADIUS"),
        ("CARD_PORTRAIT", "V1_CARD_PORTRAIT"),
        ("CARD_GRAD_N", "V1_CARD_GRAD_N"),
        ("LCD_W", "V1_LCD_W"),
        ("RIBBON_H", "V1_RIBBON_H"),
        ("MSG_MAX", "V1_MSG_MAX"),
    ]
    for a, b in reps:
        block = block.replace(a, b)
    return block


def xform_record(block: str) -> str:
    reps = [
        ("s_st !=", "v1_state_get() !="),
        ("s_st = ST_CAROUSEL", "v1_state_post_goto(ST_CAROUSEL)"),
        ("s_st = ST_RECORD", "v1_state_post_goto(ST_RECORD)"),
        ("s_session_user", "s_session"),
        ("s_work", "s_cfg.work_sem"),
        ("s_mic", "s_cfg.mic"),
        ("play_chirp_pair", "s_cfg.play_chirp_pair"),
        ("http_json(", "v1_api_http_json("),
        ("reload_inbox()", "v1_api_reload_inbox(s_session)"),
        ("request_repaint()", "v1_ui_request_repaint()"),
        ("focus_msg()", "v1_carousel_focus_msg()"),
        ("s_msgs", "v1_carousel_msgs()"),
        ("s_msg_n", "v1_carousel_msg_n()"),
        ("s_focus", "v1_carousel_focus_idx()"),
        ("paint_face_sized", "v1_ui_paint_face_sized"),
        ("paint_asterisk_icon", "v1_ui_paint_asterisk_icon"),
        ("style_list_row", "v1_ui_style_list_row"),
        ("style_list_row_label", "v1_ui_style_list_row_label"),
        ("roster_row_layout", "v1_ui_roster_row_layout"),
        ("hook_scr(", "v1_ui_hook_scr("),
        ("note_activity()", "v1_ui_bump_activity()"),
        ("s_toast_pending = true;\n        strncpy(s_toast_msg,", "v1_ui_set_toast(NULL,"),
        ("SILENCE_SEC", "V1_RECORD_SILENCE_SEC"),
        ("MAX_RECORD_SEC", "V1_RECORD_MAX_SEC"),
        ("TRIM_MS", "V1_RECORD_TRIM_MS"),
        ("SAMPLE_RATE", "V1_SAMPLE_RATE"),
        ("TALK_PEAK", "V1_TALK_PEAK"),
        ("s_user_n", "v1_connect_user_count()"),
        ("s_users", "v1_connect_users_mut()"),
    ]
    for a, b in reps:
        block = block.replace(a, b)
    return block


# Build carousel
car_body = ""
for name in sorted(TO_CAROUSEL, key=lambda n: funcs.get(n, (9999,))[0]):
    car_body += xform_carousel(extract_block(name)) + "\n"

CAR.write_text(CAR_HEADER + car_body, encoding="utf-8")
print(f"Wrote {CAR} ({len(CAR.read_text().splitlines())} lines)")

# Build record - fix play_chirp_pair calls after replacement
rec_body = ""
for name in sorted(TO_RECORD, key=lambda n: funcs.get(n, (9999,))[0]):
    rec_body += xform_record(extract_block(name)) + "\n"
rec_body = rec_body.replace("s_cfg.play_chirp_pair(", "if (s_cfg.play_chirp_pair) s_cfg.play_chirp_pair(")

REC.write_text(REC_HEADER + rec_body, encoding="utf-8")
print(f"Wrote {REC} ({len(REC.read_text().splitlines())} lines)")

# Write shim
SHIM.write_text(
    "/* x02 product shell — flash.py build id; logic in firmware/v1/x02_main.c */\n",
    encoding="utf-8",
)
print(f"Wrote shim {SHIM}")
