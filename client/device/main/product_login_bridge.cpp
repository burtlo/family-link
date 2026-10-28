#include "product_hardware_buttons.h"
#include "product_network.h"
#include "product_stage_router.h"
#include "product_login_fw.h"
#include "product_send_fw.h"
#include "product_login_http_worker.h"
#include "product_inbox_fw.h"
#include "product_mem_probe.h"
#include "product_record_fw.h"
#include "product_split_pane.h"
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
lv_obj_t *s_login_lower_layer = nullptr;
lv_obj_t *s_roster_row = nullptr;
static constexpr int kRosterCardStride = 132 + 12;
int s_roster_scroll_gesture = 0;
int s_roster_press_scroll_x = 0;
int s_roster_programmatic_scroll = 0;
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
bool s_signed_in_seen = false;
int s_login_shown = -1;

static void wake_login_display(void)
{
    /* Blank uses backlight_off only (not bsp_display_enter_sleep). exit_sleep()
     * reconfigures BSP_LCD_TOUCH_INT (GPIO 3) and leaves GT911/TT21100 dead.
     * A press that began on the dark panel must not carry into the woken UI;
     * this is the only pointer reset in the product firmware. */
    lv_indev_t *indev = bsp_display_get_input_dev();
    (void)bsp_display_brightness_set(80);
    s_backlight_dark = false;
    if (indev != nullptr) {
        lv_indev_reset(indev, nullptr);
    }
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY wake dark=0");
}

static void blank_login_display(void)
{
    (void)bsp_display_backlight_off();
    s_backlight_dark = true;
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY blank dark=1");
}

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

static void set_label_text(lv_obj_t *label, const char *text)
{
    const char *current = lv_label_get_text(label);
    if (current != nullptr && strcmp(current, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
}

static int64_t capped_session_advance_target(const fl_product_login::SessionWriter &session, int64_t now_ms)
{
    const int64_t session_now = session.now_ms();
    const int64_t max_slice_ms = 250;
    if (now_ms > session_now + max_slice_ms) {
        return session_now + max_slice_ms;
    }
    return now_ms;
}

static void mute_diag_snapshot(const char *reason, int latched)
{
    ESP_LOGI("product_mute",
             "V2_MUTE_EDGE reason=%s latch=%d gpio=%d btn=%d gpio_ready=%d dark=%d stage=%s "
             "signed_in=%d wifi=%d link=%d roster_inflight=%d screen=%d",
             reason != nullptr ? reason : "?", latched, product_hardware_buttons_mute_gpio_level(),
             product_hardware_buttons_mute_button_level(), product_hardware_buttons_mute_gpio_ready(),
             s_backlight_dark ? 1 : 0, s_session.stage_name(), s_session.signed_in() != 0 ? 1 : 0,
             s_wifi_up ? 1 : 0, product_network_sta_up(), s_session.roster_inflight() ? 1 : 0,
             (int)product_stage_screen());
}

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

static void on_roster_card_activate(int index)
{
    const char *user_id;
    if (s_session.roster_updating() != 0) {
        ESP_LOGW("product_login", "roster tap ignored updating=%d", s_session.roster_updating());
        return;
    }
    if (index < 0 || index >= s_session.user_count()) {
        return;
    }
    if (s_roster_row != nullptr) {
        s_roster_programmatic_scroll = 1;
        lv_obj_scroll_to_x(s_roster_row, index * kRosterCardStride, LV_ANIM_OFF);
        s_roster_programmatic_scroll = 0;
    }
    user_id = s_session.user_at(index);
    s_session.select_pin_user(user_id, (int64_t)(esp_timer_get_time() / 1000LL));
}

static void on_roster_card_pressed(lv_event_t *event)
{
    (void)event;
    s_roster_scroll_gesture = 0;
    s_roster_press_scroll_x = s_roster_row != nullptr ? lv_obj_get_scroll_x(s_roster_row) : 0;
}

static void on_roster_card_clicked(lv_event_t *event)
{
    if (event == nullptr) {
        return;
    }
    if (s_roster_scroll_gesture != 0) {
        s_roster_scroll_gesture = 0;
        return;
    }
    on_roster_card_activate((int)(intptr_t)lv_event_get_user_data(event));
}

static void on_roster_row_scroll(lv_event_t *event)
{
    lv_event_code_t code;
    int count;
    int index;
    int scroll;
    int target;
    if (event == nullptr || s_roster_programmatic_scroll != 0 || s_roster_row == nullptr) {
        return;
    }
    code = lv_event_get_code(event);
    count = s_session.user_count();
    if (count <= 0) {
        return;
    }
    scroll = lv_obj_get_scroll_x(s_roster_row);
    if (scroll < s_roster_press_scroll_x - 4 || scroll > s_roster_press_scroll_x + 4) {
        s_roster_scroll_gesture = 1;
    }
    if (code == LV_EVENT_SCROLL) {
        return;
    }
    if (code != LV_EVENT_SCROLL_END) {
        return;
    }
    index = scroll;
    if (index < 0) {
        index = 0;
    }
    index = (index + (kRosterCardStride / 2)) / kRosterCardStride;
    if (index >= count) {
        index = count - 1;
    }
    target = index * kRosterCardStride;
    s_roster_programmatic_scroll = 1;
    if (lv_obj_get_scroll_x(s_roster_row) != target) {
        lv_obj_scroll_to_x(s_roster_row, target, LV_ANIM_OFF);
    }
    s_roster_programmatic_scroll = 0;
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
        lv_obj_set_hidden(s_roster_row, false);
        if (s_lower_label != nullptr) {
            lv_obj_set_hidden(s_lower_label, true);
        }
        return;
    }
    clear_roster_cards();
    product_mem_probe("roster_cleared", 0);
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
    lv_obj_set_clickable(s_roster_row, false);
    lv_obj_add_event_cb(s_roster_row, on_roster_row_scroll, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(s_roster_row, on_roster_row_scroll, LV_EVENT_SCROLL_END, nullptr);
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
        lv_obj_set_scrollable(card, false);
        lv_obj_set_clickable(card, true);
        lv_obj_add_event_cb(card, on_roster_card_pressed, LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(card, on_roster_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        circle = lv_obj_create(card);
        if (circle != nullptr) {
            lv_obj_set_size(circle, 66, 66);
            lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 16);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0xF4F4F4), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_clickable(circle, false);
            lv_obj_set_scrollable(circle, false);
            mark = lv_label_create(circle);
            if (mark != nullptr) {
                lv_label_set_text(mark, initials);
                lv_obj_set_style_text_color(mark, lv_color_hex(0x101418), 0);
                lv_obj_set_style_text_font(mark, &lv_font_montserrat_16, 0);
                lv_obj_center(mark);
                lv_obj_set_clickable(mark, false);
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
            lv_obj_set_clickable(label, false);
        }
    }
    s_roster_shown = count;
    s_roster_locked = s_session.roster_updating();
    if (s_roster_locked != 0) {
        lv_obj_set_scroll_dir(s_roster_row, LV_DIR_NONE);
    }
    if (s_lower_label != nullptr) {
        lv_obj_set_hidden(s_lower_label, true);
    }
    ESP_LOGI("product_login", "roster cards=%d focus=%s", count, s_roster_focus[0] ? s_roster_focus : "none");
    product_mem_probe("roster_built", count);
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

static void on_pin_key_activate(lv_event_t *event)
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
    lv_obj_set_scrollable(s_pin_pad, false);
    lv_obj_set_clickable(s_pin_pad, false);
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
            lv_obj_set_scrollable(key, false);
            lv_obj_set_clickable(key, true);
            lv_obj_add_event_cb(key, on_pin_key_activate, LV_EVENT_PRESSED, (void *)(intptr_t)text[0]);
            label = lv_label_create(key);
            if (label != nullptr) {
                lv_label_set_text(label, text);
                lv_obj_set_style_text_color(label, lv_color_hex(0x101418), 0);
                lv_obj_set_style_text_font(label, &lv_font_montserrat_32, 0);
                lv_obj_center(label);
                lv_obj_set_clickable(label, false);
            }
        }
    }
}

static void hide_pin_pad(void)
{
    s_pin_announced = 0;
    if (s_pin_pad != nullptr) {
        lv_obj_set_hidden(s_pin_pad, true);
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
    if ((kind == 1 || kind == 2) && (s_lower_kind == 1 || s_lower_kind == 2)) {
        s_lower_kind = kind;
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
        lv_obj_set_hidden(s_cooldown, true);
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
        lv_obj_set_scrollable(s_cooldown, false);
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
        set_label_text(s_cooldown_try, line);
    }
    lv_obj_set_hidden(s_cooldown, false);
}

static void render_pin_screen(void)
{
    static char heading[72];
    static char marks[24];
    static char status_line[40];
    static int open_shown = -1;
    int i;
    int digits;
    int phase;
    const char *name;
    bool open;
    bool lower_status;
    ensure_pin_pad();
    hide_cooldown();
    if (s_roster_row != nullptr) {
        clear_roster_cards();
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
        lv_obj_set_hidden(s_upper_label, false);
        set_label_text(s_upper_label, heading);
    }
    lower_status = s_session.pin_lower_is_status();
    if (lower_status) {
        hide_pin_pad();
        if (s_lower_label != nullptr) {
            if (!s_wifi_up || s_session.stage() == fl_product_login::kStageWifiUnavailable) {
                set_label_text(s_lower_label, "no Wi-Fi");
            } else {
                format_connecting(status_line, (int)sizeof(status_line));
                set_label_text(s_lower_label, status_line);
            }
            lv_obj_set_hidden(s_lower_label, false);
        }
        note_lower_kind(3);
    } else {
        if (s_lower_label != nullptr) {
            lv_obj_set_hidden(s_lower_label, true);
        }
        if (s_pin_pad != nullptr) {
            lv_obj_set_hidden(s_pin_pad, false);
        }
        note_lower_kind(2);
    }
    open = s_session.keypad_open();
    if (open != (open_shown != 0)) {
        open_shown = open ? 1 : 0;
        for (i = 0; i < 9; ++i) {
            if (s_pin_keys[i] == nullptr) {
                continue;
            }
            lv_obj_set_style_bg_opa(s_pin_keys[i], open ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
    if (!s_pin_announced) {
        s_pin_announced = 1;
        open_shown = -1;
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
}

static void flush_wifi_schedule(void)
{
    bool up;
    int reason;
    if (s_wifi_driver_fail_pending) {
        s_wifi_driver_fail_pending = false;
        s_wifi_driver_failed = true;
        ESP_LOGW("product_login", "wifi radio failed");
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
        product_mem_probe_heap("login_http_worker_ready", s_session.roster_inflight() ? 1 : 0);
        return;
    }
    static int64_t s_last_worker_warn_ms;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_last_worker_warn_ms >= 5000) {
        s_last_worker_warn_ms = now_ms;
        ESP_LOGW("product_login", "HTTP worker not started; retrying");
    }
}

/* Level 0/1 render of the signed-out screens from the session snapshot.
 * Idempotent; never changes session, shell, or another module. */
static void render_login_copy(void)
{
    static char status_line[40];
    const char *upper = "Loading hangout ...";
    bool wifi_down;
    bool show_cards;
    if (s_upper_label == nullptr || s_lower_label == nullptr) {
        return;
    }
    if (s_session.stage() == fl_product_login::kStageMuted) {
        hide_cooldown();
        hide_pin_pad();
        clear_roster_cards();
        lv_obj_set_hidden(s_upper_label, true);
        lv_obj_set_hidden(s_lower_label, true);
        return;
    }
    if (s_session.pin_phase() == fl_product_login::kPinPhaseCooldown) {
        hide_pin_pad();
        if (s_roster_row != nullptr) {
            lv_obj_set_hidden(s_roster_row, true);
        }
        lv_obj_set_hidden(s_upper_label, true);
        lv_obj_set_hidden(s_lower_label, true);
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
    } else if (s_session.stage() == fl_product_login::kStageRoster && s_session.user_count() > 0) {
        upper = "Sign in as ...";
    }
    lv_obj_set_hidden(s_upper_label, false);
    set_label_text(s_upper_label, upper);
    wifi_down = !s_wifi_up || s_session.stage() == fl_product_login::kStageWifiUnavailable;
    show_cards = !wifi_down && s_session.stage() == fl_product_login::kStageRoster &&
                 s_session.user_count() > 0 && s_session.pin_phase() == fl_product_login::kPinPhaseIdle;
    if (show_cards) {
        lv_obj_set_hidden(s_lower_label, true);
        show_roster_cards();
        note_lower_kind(1);
        return;
    }
    clear_roster_cards();
    lv_obj_set_hidden(s_lower_label, false);
    if (wifi_down) {
        set_label_text(s_lower_label, "no Wi-Fi");
        note_lower_kind(3);
        return;
    }
    if (s_session.roster_updating() == 0 && s_session.hangout_known() != 0 && s_session.user_count() == 0 &&
        s_session.stage() != fl_product_login::kStageConnecting) {
        set_label_text(s_lower_label, "No people in this hangout");
        note_lower_kind(4);
        return;
    }
    format_connecting(status_line, (int)sizeof(status_line));
    set_label_text(s_lower_label, status_line);
    note_lower_kind(3);
}

static void note_sign_in_edge(void)
{
    const bool signed_in = s_session.signed_in() != 0;
    if (signed_in == s_signed_in_seen) {
        return;
    }
    s_signed_in_seen = signed_in;
    if (!signed_in) {
        return;
    }
    product_inbox_on_signed_in();
    product_stage_on_signed_in();
    ESP_LOGI("product_login", "V2_PRODUCT_SIGNED_IN");
    product_mem_probe("signed_in", 0);
}

static void enter_muted(int64_t now_ms)
{
    if (!s_backlight_dark) {
        blank_login_display();
    }
    if (s_session.stage() == fl_product_login::kStageMuted) {
        return;
    }
    product_stage_on_mute();
    if (product_volume_active()) {
        /* The volume model signs the login session out itself. */
        (void)product_volume_on_mute();
    }
    if (s_session.stage() != fl_product_login::kStageMuted) {
        s_session.mute(now_ms);
    }
    product_inbox_on_mute();
    product_send_release_widgets();
    product_record_release_surface();
    product_login_http_on_mute();
    product_stage_set(FL_SHELL_STAGE_ROSTER);
    ESP_LOGI("product_login", "mute_latch latched=1 stage=%s", s_session.stage_name());
    product_mem_probe("mute_latched", 0);
}

static void leave_muted(int64_t now_ms)
{
    bool link_up;
    if (s_backlight_dark) {
        wake_login_display();
    }
    if (s_session.stage() != fl_product_login::kStageMuted) {
        return;
    }
    link_up = s_wifi_up && product_network_sta_up() != 0;
    flush_wifi_schedule();
    s_session.set_wifi(link_up, now_ms);
    product_login_http_on_mute();
    s_session.unmute(now_ms);
    if (!s_session.roster_inflight()) {
        (void)s_session.begin_roster_request(now_ms);
    }
    ensure_http_worker();
    product_login_http_service(&s_session, now_ms);
    ESP_LOGI("product_login", "mute_latch latched=0 stage=%s wifi=%d link=%d roster_inflight=%d",
             s_session.stage_name(), s_wifi_up ? 1 : 0, product_network_sta_up(),
             s_session.roster_inflight() ? 1 : 0);
    product_mem_probe("mute_released", 0);
}

/* The one presentation tick (LVGL task). Phase order is normative; see
 * docs/standards/device-presentation-invariants.md. No early return: the mute
 * latch and worker results must be serviced on every tick. */
void on_login_timer(lv_timer_t *timer)
{
    int64_t now_ms;
    (void)timer;
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_dot_ms >= 400) {
        s_dot_ms = now_ms;
        s_dot_phase = (s_dot_phase + 1) % 3;
    }
    /* 1. Input: buttons and mute latch (state transitions only). */
    product_hardware_buttons_poll();
    /* 2. Connectivity from the network task. */
    flush_wifi_schedule();
    /* 3. Login session and its worker. */
    ensure_http_worker();
    s_session.advance_to(capped_session_advance_target(s_session, now_ms));
    s_session.drain();
    product_login_http_service(&s_session, now_ms);
    note_sign_in_edge();
    /* 4. Feature services. */
    if (s_session.signed_in() != 0) {
        product_inbox_service(now_ms);
        product_volume_service(now_ms);
    } else {
        /* The inbox task stays up across mute. Drop finished results so its
         * result queue cannot fill and block it. */
        product_inbox_discard_results();
    }
    product_send_service(now_ms);
    product_record_service(now_ms);
    /* 5. Present: one owner decides which module is visible, then renders. */
    product_stage_present();
#if CONFIG_FAMILY_DEVICE_MUTE_TRACE
    if (s_backlight_dark) {
        static int64_t s_last_dark_trace_ms;
        if (now_ms - s_last_dark_trace_ms >= 2000) {
            s_last_dark_trace_ms = now_ms;
            mute_diag_snapshot("dark_tick", product_hardware_buttons_mute_latched());
        }
    }
#endif
}

bool build_login_upper(lv_obj_t *upper_slot)
{
    s_upper_label = lv_label_create(upper_slot);
    return s_upper_label != nullptr;
}

bool build_login_lower(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    s_login_lower_layer = lv_obj_create(lower_slot);
    if (s_login_lower_layer == nullptr) {
        return false;
    }
    lv_obj_set_size(s_login_lower_layer, 320, 160);
    lv_obj_set_pos(s_login_lower_layer, 0, 0);
    lv_obj_set_style_bg_opa(s_login_lower_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_login_lower_layer, 0, 0);
    lv_obj_set_style_pad_all(s_login_lower_layer, 0, 0);
    lv_obj_set_scrollable(s_login_lower_layer, false);
    lv_obj_set_clickable(s_login_lower_layer, false);
    s_lower_slot = s_login_lower_layer;
    s_lower_label = lv_label_create(s_lower_slot);
    if (s_lower_label == nullptr) {
        return false;
    }
    lv_obj_set_clickable(upper_slot, false);
    lv_obj_set_clickable(lower_slot, false);
    lv_obj_align(s_upper_label, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_center(s_lower_label);
    lv_obj_set_style_text_color(s_upper_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(s_lower_label, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_font(s_upper_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_font(s_lower_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
    return true;
}

} /* namespace */

fl_product_login::SessionWriter *product_login_session(void) { return &s_session; }

extern "C" bool product_login_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_session.boot(0, false);
    if (!build_login_upper(upper_slot)) {
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
    if (!build_login_lower(upper_slot, lower_slot)) {
        return false;
    }
    (void)product_hardware_buttons_init();
    product_login_on_mute_level(product_hardware_buttons_mute_latched());
    product_stage_present();
    s_login_timer = lv_timer_create(on_login_timer, 50, nullptr);
    if (s_login_timer == nullptr) {
        return false;
    }
    ESP_LOGI("product_login", "V2_PRODUCT_LOGIN_READY desk=present-v1 build=%s", FIRMWARE_BUILD_ID);
    return true;
}

extern "C" void product_split_pane_release(void)
{
    if (!product_split_pane_slots_live()) {
        return;
    }
    product_split_pane_destroy_slots();
    s_upper_label = nullptr;
    s_lower_label = nullptr;
    s_lower_slot = nullptr;
    s_login_lower_layer = nullptr;
    s_roster_row = nullptr;
    s_roster_shown = -1;
    s_roster_locked = -1;
    s_roster_focus[0] = '\0';
    s_pin_pad = nullptr;
    memset(s_pin_keys, 0, sizeof(s_pin_keys));
    s_pin_announced = 0;
    s_lower_kind = -1;
    s_login_shown = -1;
    product_inbox_forget_widgets();
    product_send_forget_widgets();
    product_volume_forget_widgets();
    product_mem_probe("split_pane_released", 0);
}

extern "C" void product_split_pane_rebuild(void)
{
    lv_obj_t *upper = nullptr;
    lv_obj_t *lower = nullptr;
    if (product_split_pane_slots_live()) {
        return;
    }
    if (!product_split_pane_create_slots() || !product_split_pane_slot_roots(&upper, &lower)) {
        ESP_LOGE("product_login", "split_pane_rebuild_failed");
        return;
    }
    if (!build_login_upper(upper) || !product_inbox_build_widgets(upper, lower) ||
        !product_send_build_widgets(upper, lower) || !product_volume_build_widgets(upper, lower) ||
        !build_login_lower(upper, lower)) {
        ESP_LOGE("product_login", "split_pane_widgets_failed");
    }
    product_mem_probe("split_pane_rebuilt", 0);
}


extern "C" void product_login_present(int visible)
{
    if (s_upper_label == nullptr || s_login_lower_layer == nullptr) {
        return;
    }
    if (visible) {
        if (s_login_shown != 1) {
            s_login_shown = 1;
            lv_obj_set_hidden(s_login_lower_layer, false);
        }
        render_login_copy();
        return;
    }
    if (s_login_shown == 0) {
        return;
    }
    s_login_shown = 0;
    hide_cooldown();
    hide_pin_pad();
    clear_roster_cards();
    lv_obj_set_hidden(s_upper_label, true);
    lv_obj_set_hidden(s_login_lower_layer, true);
}

extern "C" int product_login_is_signed_in(void)
{
    return s_session.signed_in() != 0 ? 1 : 0;
}

extern "C" void product_login_on_boot_pressed(void)
{
    s_session.press_boot((int64_t)(esp_timer_get_time() / 1000LL));
}

/* Sampled every tick. Reconciles display power and session to the latch level;
 * a no-op when they already agree. Rendering belongs to the present phase. */
extern "C" void product_login_on_mute_level(int latched)
{
    const int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    static int s_last_latched = -1;
    if (latched != s_last_latched) {
        mute_diag_snapshot("latch_edge", latched);
        s_last_latched = latched;
    }
    if (latched) {
        enter_muted(now_ms);
    } else {
        leave_muted(now_ms);
    }
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

