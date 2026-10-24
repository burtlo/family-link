#include "product_hardware_buttons.h"
#include "product_stage_router.h"
#include "product_login_fw.h"
#include "product_send_fw.h"
#include "product_login_http_worker.h"
#include "product_inbox_fw.h"
#include "product_record_fw.h"
#include "product_shell_fw.h"
#include "product_volume_fw.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "fl_product_login_session.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

fl_product_login::SessionWriter s_session;
lv_obj_t *s_upper_label = nullptr;
lv_obj_t *s_lower_label = nullptr;
lv_obj_t *s_lower_slot = nullptr;
lv_obj_t *s_roster_row = nullptr;
int s_roster_shown = -1;
int s_roster_locked = -1;
char s_roster_focus[fl_product_login::kMaxId];
lv_obj_t *s_cooldown = nullptr;
lv_obj_t *s_cooldown_try = nullptr;
int s_lower_kind = -1;
int64_t s_fade_ms = -1000;
int s_dot_phase = 0;
int64_t s_dot_ms = 0;
bool s_backlight_dark = false;
lv_obj_t *s_pin_pad = nullptr;
lv_obj_t *s_pin_keys[9];
int s_pin_announced = 0;
lv_timer_t *s_login_timer = nullptr;
bool s_wifi_up = false;
bool s_http_worker_started = false;
int s_wifi_last_reason = 0;
bool s_wifi_driver_failed = false;
volatile bool s_wifi_schedule_pending = false;
volatile bool s_wifi_scheduled_up = false;
volatile int s_wifi_scheduled_reason = 0;
volatile bool s_wifi_driver_fail_pending = false;

static void ensure_http_worker(void);
static void render_login_copy(void);

struct CardFill {
    const char *id;
    uint32_t fill;
};

const CardFill kCardFills[] = {
    {"card-bg-01", 0x5AA0E8}, {"card-bg-02", 0x67C587}, {"card-bg-03", 0xE8C040},
    {"card-bg-04", 0xD875A8}, {"card-bg-05", 0xE85B5B}, {"card-bg-06", 0x8B7CE8},
    {"card-bg-07", 0xE88840}, {"card-bg-08", 0x4EC4C8},
};

static uint32_t card_fill(const char *id)
{
    int i;
    for (i = 0; i < (int)(sizeof(kCardFills) / sizeof(kCardFills[0])); ++i) {
        if (id != nullptr && strcmp(id, kCardFills[i].id) == 0) {
            return kCardFills[i].fill;
        }
    }
    return kCardFills[0].fill;
}

static void initials_from_name(const char *name, char *out, int cap)
{
    int n = 0;
    const char *space;
    if (out == nullptr || cap < 2) {
        return;
    }
    if (name == nullptr || name[0] == '\0') {
        out[0] = '?';
        out[1] = '\0';
        return;
    }
    out[n++] = name[0];
    space = strchr(name, ' ');
    if (space != nullptr && space[1] != '\0' && n + 1 < cap) {
        out[n++] = space[1];
    }
    out[n] = '\0';
}

static void on_roster_card_clicked(lv_event_t *event)
{
    int index;
    const char *user_id;
    if (event == nullptr) {
        return;
    }
    if (s_session.roster_updating() != 0) {
        return;
    }
    index = (int)(intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= s_session.user_count()) {
        return;
    }
    if (s_roster_row != nullptr) {
        lv_obj_scroll_to_x(s_roster_row, index * (132 + 12), LV_ANIM_OFF);
    }
    user_id = s_session.user_at(index);
    s_session.select_pin_user(user_id, (int64_t)(esp_timer_get_time() / 1000LL));
}

static void clear_roster_cards(void)
{
    if (s_roster_row != nullptr) {
        lv_obj_delete(s_roster_row);
        s_roster_row = nullptr;
    }
    s_roster_shown = -1;
    s_roster_locked = -1;
    s_roster_focus[0] = '\0';
}

static bool roster_cards_current(void)
{
    const char *focus = s_session.focus_id();
    if (s_roster_row == nullptr || s_roster_shown != s_session.user_count() ||
        s_roster_locked != s_session.roster_updating()) {
        return false;
    }
    if (focus == nullptr) {
        focus = "";
    }
    return strcmp(s_roster_focus, focus) == 0;
}

static void show_roster_cards(void)
{
    int i;
    int count;
    const char *focus;
    if (s_lower_slot == nullptr) {
        return;
    }
    if (roster_cards_current()) {
        lv_obj_remove_flag(s_roster_row, LV_OBJ_FLAG_HIDDEN);
        if (s_lower_label != nullptr) {
            lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    clear_roster_cards();
    count = s_session.user_count();
    if (count > fl_product_login::kMaxUsers) {
        count = fl_product_login::kMaxUsers;
    }
    focus = s_session.focus_id();
    if (focus == nullptr) {
        focus = "";
    }
    strncpy(s_roster_focus, focus, sizeof(s_roster_focus) - 1);
    s_roster_row = lv_obj_create(s_lower_slot);
    if (s_roster_row == nullptr) {
        return;
    }
    lv_obj_set_size(s_roster_row, 320, 160);
    lv_obj_set_pos(s_roster_row, 0, 0);
    lv_obj_set_style_bg_opa(s_roster_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_roster_row, 0, 0);
    lv_obj_set_style_pad_all(s_roster_row, 0, 0);
    lv_obj_set_style_pad_left(s_roster_row, (320 - 132) / 2, 0);
    lv_obj_set_style_pad_right(s_roster_row, (320 - 132) / 2, 0);
    lv_obj_set_style_pad_column(s_roster_row, 12, 0);
    lv_obj_set_flex_flow(s_roster_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_roster_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_roster_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_roster_row, LV_SCROLLBAR_MODE_OFF);
    for (i = 0; i < count; ++i) {
        const char *name = s_session.user_display_name_at(i);
        const bool focused = focus[0] != '\0' && strcmp(s_session.user_at(i), focus) == 0;
        char initials[4];
        lv_obj_t *card = lv_obj_create(s_roster_row);
        lv_obj_t *circle;
        lv_obj_t *mark;
        lv_obj_t *label;
        if (card == nullptr) {
            break;
        }
        initials_from_name(name, initials, (int)sizeof(initials));
        lv_obj_set_size(card, 132, 133);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(card_fill(s_session.user_card_background_at(i))), 0);
        lv_obj_set_style_bg_opa(card, focused ? LV_OPA_COVER : LV_OPA_60, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        if (s_session.roster_updating() == 0) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card, on_roster_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        }
        circle = lv_obj_create(card);
        if (circle != nullptr) {
            lv_obj_set_size(circle, 66, 66);
            lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 16);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0xF4F4F4), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
            mark = lv_label_create(circle);
            if (mark != nullptr) {
                lv_label_set_text(mark, initials);
                lv_obj_set_style_text_color(mark, lv_color_hex(0x101418), 0);
                lv_obj_set_style_text_font(mark, &lv_font_montserrat_16, 0);
                lv_obj_center(mark);
                lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
            }
        }
        label = lv_label_create(card);
        if (label != nullptr) {
            lv_label_set_text(label, name != nullptr && name[0] != '\0' ? name : s_session.user_at(i));
            lv_obj_set_width(label, 116);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x101418), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -12);
            lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    s_roster_shown = count;
    s_roster_locked = s_session.roster_updating();
    if (s_roster_locked != 0) {
        lv_obj_set_scroll_dir(s_roster_row, LV_DIR_NONE);
    }
    lv_obj_move_foreground(s_roster_row);
    if (s_lower_label != nullptr) {
        lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI("product_login", "roster cards=%d focus=%s", count, s_roster_focus[0] ? s_roster_focus : "none");
}

static bool pin_entry_open(void)
{
    int phase;
    if (s_session.signed_in() != 0 || s_session.stage() == fl_product_login::kStageMuted ||
        s_session.pin_phase() == fl_product_login::kPinPhaseCooldown) {
        return false;
    }
    phase = s_session.pin_phase();
    return phase == fl_product_login::kPinPhaseEntry || phase == fl_product_login::kPinPhaseChecking ||
           phase == fl_product_login::kPinPhaseWrong;
}

static const char *focused_display_name(void)
{
    int i;
    const char *focus = s_session.focus_id();
    if (focus == nullptr || focus[0] == '\0') {
        return "...";
    }
    for (i = 0; i < s_session.user_count(); ++i) {
        const char *name;
        if (strcmp(s_session.user_at(i), focus) != 0) {
            continue;
        }
        name = s_session.user_display_name_at(i);
        if (name != nullptr && name[0] != '\0') {
            return name;
        }
        return focus;
    }
    return focus;
}

static void on_pin_key_clicked(lv_event_t *event)
{
    char digit;
    if (event == nullptr || !s_session.keypad_open()) {
        return;
    }
    int64_t now_ms;
    digit = (char)(intptr_t)lv_event_get_user_data(event);
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (!s_session.enter_pin_digit(digit, now_ms)) {
        return;
    }
    if (s_session.pin_submitted()) {
        ensure_http_worker();
        product_login_http_service(&s_session, now_ms);
    }
}

static void ensure_pin_pad(void)
{
    int row;
    int col;
    if (s_pin_pad != nullptr || s_lower_slot == nullptr) {
        return;
    }
    s_pin_pad = lv_obj_create(s_lower_slot);
    if (s_pin_pad == nullptr) {
        return;
    }
    lv_obj_set_size(s_pin_pad, 320, 160);
    lv_obj_set_pos(s_pin_pad, 0, 0);
    lv_obj_set_style_bg_opa(s_pin_pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pin_pad, 0, 0);
    lv_obj_set_style_pad_all(s_pin_pad, 0, 0);
    lv_obj_remove_flag(s_pin_pad, LV_OBJ_FLAG_SCROLLABLE);
    for (row = 0; row < 3; ++row) {
        for (col = 0; col < 3; ++col) {
            int digit = row * 3 + col + 1;
            char text[2];
            lv_obj_t *key = lv_obj_create(s_pin_pad);
            lv_obj_t *label;
            s_pin_keys[digit - 1] = key;
            if (key == nullptr) {
                continue;
            }
            text[0] = (char)('0' + digit);
            text[1] = '\0';
            lv_obj_set_pos(key, 4 + col * 105, 4 + row * 52);
            lv_obj_set_size(key, 101, 48);
            lv_obj_set_style_radius(key, 8, 0);
            lv_obj_set_style_bg_color(key, lv_color_hex(0xD7E2EA), 0);
            lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(key, 0, 0);
            lv_obj_set_style_pad_all(key, 0, 0);
            lv_obj_remove_flag(key, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(key, on_pin_key_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)text[0]);
            label = lv_label_create(key);
            if (label != nullptr) {
                lv_label_set_text(label, text);
                lv_obj_set_style_text_color(label, lv_color_hex(0x101418), 0);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
                lv_obj_center(label);
                lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
}

static void hide_pin_pad(void)
{
    s_pin_announced = 0;
    if (s_pin_pad != nullptr) {
        lv_obj_add_flag(s_pin_pad, LV_OBJ_FLAG_HIDDEN);
    }
}

static void fade_lower_opa(void *obj, int32_t value)
{
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), 0);
}

static void note_lower_kind(int kind)
{
    int64_t now_ms;
    lv_anim_t anim;
    if (s_lower_slot == nullptr) {
        return;
    }
    if (s_lower_kind < 0) {
        s_lower_kind = kind;
        lv_obj_set_style_opa(s_lower_slot, LV_OPA_COVER, 0);
        return;
    }
    if (kind == s_lower_kind) {
        return;
    }
    s_lower_kind = kind;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_fade_ms < 200) {
        return;
    }
    s_fade_ms = now_ms;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_lower_slot);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, 90);
    lv_anim_set_exec_cb(&anim, fade_lower_opa);
    lv_anim_start(&anim);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_lower_slot);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, 90);
    lv_anim_set_delay(&anim, 90);
    lv_anim_set_exec_cb(&anim, fade_lower_opa);
    lv_anim_start(&anim);
}

static void format_connecting(char *out, int cap)
{
    int dots = (s_dot_phase % 3) + 1;
    int i;
    int n;
    if (out == nullptr || cap < 16) {
        return;
    }
    n = snprintf(out, (size_t)cap, "connecting\n");
    for (i = 0; i < dots && n + 1 < cap; ++i) {
        out[n++] = '.';
        out[n] = '\0';
    }
}

static void hide_cooldown(void)
{
    if (s_cooldown != nullptr) {
        lv_obj_add_flag(s_cooldown, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_cooldown(void)
{
    int seconds;
    static char line[40];
    lv_obj_t *screen;
    if (s_upper_label == nullptr) {
        return;
    }
    if (s_cooldown == nullptr) {
        screen = lv_obj_get_screen(s_upper_label);
        s_cooldown = lv_obj_create(screen);
        if (s_cooldown == nullptr) {
            return;
        }
        lv_obj_set_size(s_cooldown, 320, 240);
        lv_obj_set_pos(s_cooldown, 0, 0);
        lv_obj_set_style_bg_color(s_cooldown, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(s_cooldown, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_cooldown, 0, 0);
        lv_obj_set_style_pad_all(s_cooldown, 0, 0);
        lv_obj_remove_flag(s_cooldown, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = lv_label_create(s_cooldown);
        s_cooldown_try = lv_label_create(s_cooldown);
        if (title != nullptr) {
            lv_label_set_text(title, "Too many attempts.");
            lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
            lv_obj_align(title, LV_ALIGN_CENTER, 0, -14);
        }
        if (s_cooldown_try != nullptr) {
            lv_obj_set_style_text_color(s_cooldown_try, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_cooldown_try, &lv_font_montserrat_16, 0);
            lv_obj_align(s_cooldown_try, LV_ALIGN_CENTER, 0, 14);
        }
    }
    seconds = (s_session.cooldown_remaining_ms() + 999) / 1000;
    if (seconds < 0) {
        seconds = 0;
    }
    snprintf(line, sizeof(line), "Try again in %d.", seconds);
    if (s_cooldown_try != nullptr) {
        lv_label_set_text(s_cooldown_try, line);
    }
    lv_obj_remove_flag(s_cooldown, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_cooldown);
}

static void render_pin_screen(void)
{
    static char heading[72];
    static char marks[24];
    static char status_line[40];
    int i;
    int digits;
    int phase;
    const char *name;
    bool open;
    bool lower_status;
    ensure_pin_pad();
    hide_cooldown();
    if (s_roster_row != nullptr) {
        lv_obj_add_flag(s_roster_row, LV_OBJ_FLAG_HIDDEN);
    }
    name = focused_display_name();
    phase = s_session.pin_phase();
    digits = s_session.digit_len();
    if (digits < 0) {
        digits = 0;
    }
    if (digits > 4) {
        digits = 4;
    }
    if (phase == fl_product_login::kPinPhaseChecking) {
        snprintf(marks, sizeof(marks), "checking...");
    } else {
        int pos = 0;
        for (i = 0; i < 4 && pos + 2 < (int)sizeof(marks); ++i) {
            if (i > 0) {
                marks[pos++] = ' ';
            }
            marks[pos++] = i < digits ? '*' : '_';
        }
        marks[pos] = '\0';
    }
    if (phase == fl_product_login::kPinPhaseWrong) {
        snprintf(heading, sizeof(heading), "wrong pin");
    } else {
        snprintf(heading, sizeof(heading), "Sign in as %s   %s", name, marks);
    }
    if (s_upper_label != nullptr) {
        lv_obj_remove_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_upper_label, heading);
        lv_obj_align(s_upper_label, LV_ALIGN_LEFT_MID, 12, 0);
    }
    lower_status = s_session.pin_lower_is_status();
    if (lower_status) {
        hide_pin_pad();
        if (s_lower_label != nullptr) {
            if (!s_wifi_up || s_session.stage() == fl_product_login::kStageWifiUnavailable) {
                lv_label_set_text(s_lower_label, "no Wi-Fi");
            } else {
                format_connecting(status_line, (int)sizeof(status_line));
                lv_label_set_text(s_lower_label, status_line);
            }
            lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_remove_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_center(s_lower_label);
        }
        note_lower_kind(3);
    } else {
        if (s_lower_label != nullptr) {
            lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_pin_pad != nullptr) {
            lv_obj_remove_flag(s_pin_pad, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_pin_pad);
        }
        note_lower_kind(2);
    }
    open = s_session.keypad_open();
    for (i = 0; i < 9; ++i) {
        if (s_pin_keys[i] == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_opa(s_pin_keys[i], open ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (!s_pin_announced) {
        s_pin_announced = 1;
        ESP_LOGI("product_login", "pin screen user=%s", name);
    }
}

static void apply_wifi_link(bool up)
{
    if (up) {
        s_wifi_last_reason = 0;
    }
    s_wifi_up = up;
    s_session.set_wifi(up, (int64_t)(esp_timer_get_time() / 1000LL));
    product_inbox_set_wifi(up);
    product_send_set_wifi(up ? 1 : 0);
    product_volume_on_wifi(up);
    if (up) {
        ensure_http_worker();
    }
    render_login_copy();
}

static void flush_wifi_schedule(void)
{
    bool up;
    int reason;
    if (s_wifi_driver_fail_pending) {
        s_wifi_driver_fail_pending = false;
        s_wifi_driver_failed = true;
        ESP_LOGW("product_login", "wifi radio failed");
        render_login_copy();
    }
    if (!s_wifi_schedule_pending) {
        return;
    }
    s_wifi_schedule_pending = false;
    up = s_wifi_scheduled_up;
    reason = s_wifi_scheduled_reason;
    if (!up && reason != 0) {
        s_wifi_last_reason = reason;
        ESP_LOGW("product_login", "wifi link down reason=%d radio_failed=%d", s_wifi_last_reason,
                 s_wifi_driver_failed ? 1 : 0);
    }
    apply_wifi_link(up);
}

static void ensure_http_worker(void)
{
    int64_t now_ms;
    if (!s_wifi_up || s_http_worker_started) {
        return;
    }
    if (product_login_http_worker_start(&s_session)) {
        s_http_worker_started = true;
        now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
        if (!s_session.roster_inflight() && s_session.stage() == fl_product_login::kStageConnecting) {
            (void)s_session.begin_roster_request(now_ms);
        }
        return;
    }
    static int64_t s_last_worker_warn_ms;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_last_worker_warn_ms >= 5000) {
        s_last_worker_warn_ms = now_ms;
        ESP_LOGW("product_login", "HTTP worker not started; retrying");
    }
}

static void render_login_copy(void)
{
    static int signed_in_announced = 0;
    static char status_line[40];
    const char *upper = "Loading hangout ...";
    bool wifi_down;
    bool show_cards;
    if (s_upper_label == nullptr || s_lower_label == nullptr) {
        return;
    }
    if (s_session.signed_in() != 0) {
        if (!signed_in_announced) {
            product_inbox_on_signed_in();
            product_stage_on_signed_in();
            signed_in_announced = 1;
            ESP_LOGI("product_login", "V2_PRODUCT_SIGNED_IN");
        }
        hide_cooldown();
        lv_obj_add_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        clear_roster_cards();
        hide_pin_pad();
        return;
    }
    signed_in_announced = 0;
    if (s_session.stage() == fl_product_login::kStageMuted) {
        hide_cooldown();
        hide_pin_pad();
        if (s_roster_row != nullptr) {
            lv_obj_add_flag(s_roster_row, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_session.pin_phase() == fl_product_login::kPinPhaseCooldown) {
        hide_pin_pad();
        if (s_roster_row != nullptr) {
            lv_obj_add_flag(s_roster_row, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        show_cooldown();
        return;
    }
    hide_cooldown();
    if (pin_entry_open()) {
        render_pin_screen();
        return;
    }
    hide_pin_pad();
    if (s_session.roster_updating() != 0) {
        upper = "Updating hangout ...";
    } else if (s_session.hangout_known() != 0) {
        upper = "Sign in as ...";
    }
    lv_obj_remove_flag(s_upper_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_upper_label, upper);
    lv_obj_align(s_upper_label, LV_ALIGN_LEFT_MID, 12, 0);
    wifi_down = !s_wifi_up || s_session.stage() == fl_product_login::kStageWifiUnavailable;
    show_cards = !wifi_down && s_session.stage() == fl_product_login::kStageRoster &&
                 s_session.user_count() > 0 && s_session.pin_phase() == fl_product_login::kPinPhaseIdle;
    if (show_cards) {
        lv_obj_add_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
        show_roster_cards();
        note_lower_kind(1);
        return;
    }
    clear_roster_cards();
    lv_obj_remove_flag(s_lower_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_lower_label);
    if (wifi_down) {
        lv_label_set_text(s_lower_label, "no Wi-Fi");
        note_lower_kind(3);
        return;
    }
    if (s_session.roster_updating() == 0 && s_session.hangout_known() != 0 && s_session.user_count() == 0 &&
        s_session.stage() != fl_product_login::kStageConnecting) {
        lv_label_set_text(s_lower_label, "No people in this hangout");
        note_lower_kind(4);
        return;
    }
    format_connecting(status_line, (int)sizeof(status_line));
    lv_label_set_text(s_lower_label, status_line);
    note_lower_kind(3);
}

void on_login_timer(lv_timer_t *timer)
{
    int64_t now_ms;
    (void)timer;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_dot_ms >= 400) {
        s_dot_ms = now_ms;
        s_dot_phase = (s_dot_phase + 1) % 3;
    }
    flush_wifi_schedule();
    ensure_http_worker();
    s_session.advance_to(now_ms);
    s_session.drain();
    product_login_http_service(&s_session, now_ms);
    if (s_session.signed_in() != 0) {
        product_inbox_service(now_ms);
        product_volume_service(now_ms);
    }
    product_record_service(now_ms,
                          product_shell_stage() == FL_SHELL_STAGE_RECORD && !product_volume_active()
                              ? 1
                              : 0);
    product_hardware_buttons_poll();
    render_login_copy();
}

} /* namespace */

fl_product_login::SessionWriter *product_login_session(void) { return &s_session; }

extern "C" bool product_login_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_session.boot(0, false);
    s_lower_slot = lower_slot;
    s_upper_label = lv_label_create(upper_slot);
    s_lower_label = lv_label_create(lower_slot);
    if (s_upper_label == nullptr || s_lower_label == nullptr) {
        return false;
    }
    if (!product_inbox_attach(upper_slot, lower_slot)) {
        return false;
    }
    if (!product_send_attach(upper_slot, lower_slot)) {
        ESP_LOGW("product_login", "product_send_attach_failed");
    }
    product_send_set_wifi(s_wifi_up ? 1 : 0);
    if (!product_volume_attach(upper_slot, lower_slot)) {
        ESP_LOGW("product_login", "product_volume_attach_failed");
    }
    product_volume_bind_models(product_inbox_session(), &s_session);
    lv_obj_center(s_upper_label);
    lv_obj_center(s_lower_label);
    lv_obj_set_style_text_color(s_upper_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_lower_label, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_font(s_upper_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(s_lower_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_move_foreground(s_upper_label);
    lv_obj_move_foreground(s_lower_label);
    (void)product_hardware_buttons_init();
    if (product_hardware_buttons_mute_latched()) {
        product_login_on_mute_level(1);
    } else {
        render_login_copy();
    }
    s_login_timer = lv_timer_create(on_login_timer, 50, nullptr);
    if (s_login_timer == nullptr) {
        return false;
    }
    ESP_LOGI("product_login", "V2_PRODUCT_LOGIN_READY build=%s", FAMILY_BUILD_ID);
    return true;
}

extern "C" int product_login_is_signed_in(void)
{
    return s_session.signed_in() != 0 ? 1 : 0;
}

extern "C" void product_login_on_boot_pressed(void)
{
    s_session.press_boot((int64_t)(esp_timer_get_time() / 1000LL));
}

extern "C" void product_login_on_mute_level(int latched)
{
    const int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    bool changed = false;
    if (latched) {
        if (s_session.stage() != fl_product_login::kStageMuted) {
            product_stage_on_mute();
            if (!(product_volume_active() && product_volume_on_mute() != 0)) {
                s_session.mute(now_ms);
                product_inbox_on_mute();
                product_shell_set_stage(FL_SHELL_STAGE_ROSTER);
            }
            changed = true;
        }
        if (!s_backlight_dark) {
            (void)bsp_display_backlight_off();
            s_backlight_dark = true;
            changed = true;
        }
        if (changed) {
            render_login_copy();
        }
        return;
    }
    if (s_session.stage() == fl_product_login::kStageMuted) {
        s_session.unmute(now_ms);
        changed = true;
    }
    if (s_backlight_dark) {
        (void)bsp_display_backlight_on();
        s_backlight_dark = false;
        changed = true;
    }
    if (changed) {
        render_login_copy();
    }
}

extern "C" void product_login_on_mute_pressed(void)
{
    product_login_on_mute_level(1);
}

extern "C" void product_login_schedule_wifi_link(bool up, int disconnect_reason)
{
    s_wifi_scheduled_up = up;
    s_wifi_scheduled_reason = disconnect_reason;
    s_wifi_schedule_pending = true;
}

extern "C" void product_login_schedule_wifi_driver_failed(void)
{
    s_wifi_driver_fail_pending = true;
}

