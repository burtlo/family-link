#include "bsp/esp-box-3.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "session.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_LAB07_BUILD_ID
#define CONFIG_LAB07_BUILD_ID "lab07-fw-v1-manual"
#endif
#ifndef CONFIG_LAB07_HEALTH_URL
#define CONFIG_LAB07_HEALTH_URL "http://127.0.0.1:8767/health"
#endif
#ifndef CONFIG_LAB07_ROSTER_URL
#define CONFIG_LAB07_ROSTER_URL "http://127.0.0.1:8767/roster?script=eight"
#endif
#ifndef CONFIG_LAB07_HOLD_URL
#define CONFIG_LAB07_HOLD_URL "http://127.0.0.1:8767/roster?script=delayed"
#endif

namespace {
constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kStatusY = 0;
constexpr int kStatusH = 48;
constexpr int kStageY = 48;
constexpr int kStageH = 74;
constexpr int kControlsY = 122;
constexpr int kControlsH = 118;
constexpr int kCueH = 28;
static_assert(kStatusY + kStatusH == kStageY, "status sits above the stage");
static_assert(kStageY + kStageH == kControlsY, "stage sits above the controls");
static_assert(kControlsY + kControlsH == kScreenH, "controls end at the bottom of the screen");
static_assert(kCueH < kControlsH, "the tap-once label sits above the step button");

struct Control {
    int action;
    const char *label;
};

constexpr Control kControls[] = {
    {lab07::kManualWifiDown, "Wi-Fi down"},
    {lab07::kManualWifiReturn, "Wi-Fi return"},
    {lab07::kManualEmpty, "Empty roster"},
    {lab07::kManualOne, "One user"},
    {lab07::kManualEight, "Eight users"},
    {lab07::kManualAdd, "Add"},
    {lab07::kManualRemove, "Remove"},
    {lab07::kManualReorder, "Reorder"},
    {lab07::kManualFailOnce, "One failed check"},
    {lab07::kManualFailTwice, "Two failed checks"},
    {lab07::kManualInvalid, "Invalid HTTP 200"},
    {lab07::kManualHold, "Hold 1 second"},
    {lab07::kManualMute, "Mute"},
    {lab07::kManualUnmute, "Unmute stand-in"},
    {lab07::kManualIdleClock, "Idle clock"},
    {lab07::kManualStaleBeats, "Stale beats"},
};

lab07::SessionWriter s_session;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_stage = nullptr;
lv_obj_t *s_controls = nullptr;
lv_obj_t *s_dots = nullptr;
lv_obj_t *s_unmute = nullptr;
lv_obj_t *s_step_label = nullptr;
lv_obj_t *s_cue = nullptr;
int s_step_index = 0;
bool s_step_locked = false;
bool s_ignore_step_release = false;
int64_t s_last_step_us = 0;
int s_dot_phase = 0;
constexpr int kStepCount = (int)(sizeof(kControls) / sizeof(kControls[0]));
constexpr int64_t kStepDebounceUs = 700000;
bool s_hold_active = false;
int64_t s_hold_deadline_us = 0;
int64_t s_hold_model_ms = 0;
int s_trace_cursor = 0;
uint8_t s_pixel_ledger[320 * 16];
uint32_t s_pixel_used = 0;
uint32_t s_pixel_high_water = 0;

void print_new_traces()
{
    int i;
    for (i = s_trace_cursor; i < s_session.event_count(); ++i) {
        char line[1024];
        lab07::format_trace_line(s_session.event_at(i), line, (int)sizeof(line));
        printf("%s\n", line);
    }
    s_trace_cursor = s_session.event_count();
}

void note_pixel(uint32_t used)
{
    s_pixel_used = used;
    if (used > s_pixel_high_water) {
        s_pixel_high_water = used;
    }
}

void sample_ledgers()
{
    lv_mem_monitor_t monitor;
    uint32_t largest_free;
    note_pixel(sizeof(s_pixel_ledger) / 2);
    lv_mem_monitor(&monitor);
    largest_free = (uint32_t)sizeof(s_pixel_ledger) - s_pixel_used;
    printf("LAB07_TRACE schema=lab07-trace/v1 event=resource_sample seq=1 monotonic_us=%lld "
           "session_id=%s operation_id=0 generation=%" PRIu32 " lvgl_high_water_bytes=%u "
           "lvgl_largest_free_block_bytes=%u pixel_high_water_bytes=%" PRIu32 " "
           "pixel_largest_free_block_bytes=%" PRIu32 "\n",
           (long long)esp_timer_get_time(), s_session.session_id(), s_session.generation(),
           (unsigned)monitor.max_used, (unsigned)monitor.free_biggest_size, s_pixel_high_water,
           largest_free);
}

void render_status()
{
    char line[160];
    if (s_status == nullptr) {
        return;
    }
    snprintf(line, sizeof(line), "stage=%s users=%d\nfocus=%s\nstale=%d inflight=%d touch=%d",
             s_session.stage_name(), s_session.user_count(),
             s_session.focus_id()[0] ? s_session.focus_id() : "none", s_session.stale(),
             s_session.inflight_requests(), s_session.touch_during_wait());
    lv_label_set_text(s_status, line);
}

void clear_stage()
{
    if (s_stage == nullptr) {
        return;
    }
    lv_obj_clean(s_stage);
    s_dots = nullptr;
}

void on_roster_touch(lv_event_t *event)
{
    (void)event;
    s_session.note_touch_during_wait();
    render_status();
}

void render_cards()
{
    lv_obj_t *title;
    lv_obj_t *row;
    int i;
    if (s_hold_active) {
        lv_obj_t *card = lv_button_create(s_stage);
        lv_obj_t *label;
        const char *who = s_session.focus_id()[0] ? s_session.focus_id() : "su-01";
        char line[64];
        if (card == nullptr) {
            return;
        }
        lv_obj_set_pos(card, 4, 4);
        lv_obj_set_size(card, kScreenW - 8, kStageH - 8);
        label = lv_label_create(card);
        if (label != nullptr) {
            snprintf(line, sizeof(line), "Sign in as %s", who);
            lv_label_set_text(label, line);
            lv_obj_center(label);
        }
        lv_obj_add_event_cb(card, on_roster_touch, LV_EVENT_PRESSED, nullptr);
        return;
    }
    title = lv_label_create(s_stage);
    if (title == nullptr) {
        return;
    }
    lv_label_set_text(title, "Sign in as ...");
    lv_obj_set_pos(title, 4, 0);
    row = lv_obj_create(s_stage);
    if (row == nullptr) {
        return;
    }
    lv_obj_set_pos(row, 0, 18);
    lv_obj_set_size(row, kScreenW - 8, 52);
    lv_obj_set_scroll_dir(row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_AUTO);
    for (i = 0; i < s_session.user_count(); ++i) {
        lv_obj_t *card = lv_button_create(row);
        lv_obj_t *label;
        if (card == nullptr) {
            return;
        }
        lv_obj_set_pos(card, i * 156, 2);
        lv_obj_set_size(card, 148, 46);
        if (strcmp(s_session.user_at(i), s_session.focus_id()) == 0) {
            lv_obj_set_style_border_width(card, 3, 0);
        }
        label = lv_label_create(card);
        if (label != nullptr) {
            lv_label_set_text(label, s_session.user_at(i));
            lv_obj_center(label);
        }
        lv_obj_add_event_cb(card, on_roster_touch, LV_EVENT_PRESSED, nullptr);
    }
}

void set_step_caption()
{
    char line[96];
    if (s_step_label == nullptr) {
        return;
    }
    if (s_step_locked) {
        lv_label_set_text(s_step_label, "Keep holding");
        if (s_cue != nullptr) {
            lv_label_set_text(s_cue, "Until touch=1");
        }
        return;
    }
    if (s_cue != nullptr) {
        lv_label_set_text(s_cue, "Tap each once");
    }
    if (s_step_index >= kStepCount) {
        lv_label_set_text(s_step_label, "All steps sent");
        return;
    }
    snprintf(line, sizeof(line), "%d/%d  %s", s_step_index + 1, kStepCount, kControls[s_step_index].label);
    lv_label_set_text(s_step_label, line);
}

bool accept_step_tap()
{
    int64_t now = esp_timer_get_time();
    if (s_step_locked || s_step_index >= kStepCount) {
        return false;
    }
    if (s_last_step_us != 0 && now - s_last_step_us < kStepDebounceUs) {
        return false;
    }
    s_last_step_us = now;
    return true;
}

void render_stage()
{
    clear_stage();
    if (s_session.stage() == lab07::kStageMuted) {
        lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
        if (s_status != nullptr) {
            lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_stage != nullptr) {
            lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_controls != nullptr) {
            lv_obj_add_flag(s_controls, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_unmute != nullptr) {
            lv_obj_remove_flag(s_unmute, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xf4f1ea), 0);
    if (s_status != nullptr) {
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_stage != nullptr) {
        lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_controls != nullptr) {
        lv_obj_remove_flag(s_controls, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_unmute != nullptr) {
        lv_obj_add_flag(s_unmute, LV_OBJ_FLAG_HIDDEN);
    }
    render_status();
    set_step_caption();
    if (s_session.stage() == lab07::kStageConnecting) {
        s_dots = lv_label_create(s_stage);
        if (s_dots != nullptr) {
            lv_label_set_text(s_dots, "Checking the hangout ...");
            lv_obj_set_pos(s_dots, 8, 24);
        }
    } else if (s_session.stage() == lab07::kStageWifiUnavailable) {
        lv_obj_t *label = lv_label_create(s_stage);
        if (label != nullptr) {
            lv_label_set_text(label, "The station is not connected.");
            lv_obj_set_pos(label, 8, 24);
        }
    } else if (s_session.stage() == lab07::kStageEmptyHangout) {
        lv_obj_t *label = lv_label_create(s_stage);
        if (label != nullptr) {
            lv_label_set_text(label, "The hangout has no one to show.");
            lv_obj_set_pos(label, 8, 24);
        }
    } else if (s_session.stage() == lab07::kStageRoster) {
        render_cards();
    }
}

void render()
{
    if (!bsp_display_lock(0)) {
        return;
    }
    render_stage();
    bsp_display_unlock();
}

void begin_hold_from_finger(bool from_press)
{
    int64_t deadline = lab07::apply_manual_control(&s_session, lab07::kManualHold);
    if (deadline <= 0) {
        return;
    }
    s_hold_model_ms = deadline;
    s_hold_deadline_us = esp_timer_get_time() + ((int64_t)lab07::kRequestTimeoutMs * 1000);
    s_hold_active = true;
    s_step_locked = true;
    if (from_press) {
        s_ignore_step_release = true;
    }
    s_last_step_us = esp_timer_get_time();
    /* The finger is still down as the request becomes in flight. */
    s_session.note_touch_during_wait();
    render();
    print_new_traces();
}

void on_step_pressed(lv_event_t *event)
{
    (void)event;
    if (s_hold_active) {
        s_session.note_touch_during_wait();
        render_status();
        return;
    }
    if (s_step_locked || s_step_index >= kStepCount) {
        return;
    }
    if (kControls[s_step_index].action != lab07::kManualHold) {
        return;
    }
    begin_hold_from_finger(true);
}

void on_step(lv_event_t *event)
{
    int action;
    int64_t deadline;
    (void)event;
    if (s_ignore_step_release) {
        s_ignore_step_release = false;
        return;
    }
    if (s_step_index < kStepCount && kControls[s_step_index].action == lab07::kManualUnmute) {
        return;
    }
    if (!accept_step_tap()) {
        return;
    }
    action = kControls[s_step_index].action;
    if (action == lab07::kManualHold) {
        begin_hold_from_finger(false);
        return;
    }
    deadline = lab07::apply_manual_control(&s_session, action);
    (void)deadline;
    s_hold_active = false;
    s_step_index += 1;
    render();
    print_new_traces();
}

void on_unmute(lv_event_t *event)
{
    int64_t now = esp_timer_get_time();
    (void)event;
    if (s_step_locked) {
        return;
    }
    if (s_last_step_us != 0 && now - s_last_step_us < kStepDebounceUs) {
        return;
    }
    lab07::apply_manual_control(&s_session, lab07::kManualUnmute);
    s_hold_active = false;
    if (s_step_index < kStepCount && kControls[s_step_index].action == lab07::kManualUnmute) {
        s_step_index += 1;
    }
    s_last_step_us = esp_timer_get_time();
    render();
    print_new_traces();
}

void service_hold()
{
    if (!s_hold_active) {
        return;
    }
    if (esp_timer_get_time() < s_hold_deadline_us) {
        return;
    }
    lab07::complete_manual_hold(&s_session, s_hold_model_ms);
    s_hold_active = false;
    s_step_locked = false;
    s_last_step_us = esp_timer_get_time();
    if (s_step_index < kStepCount && kControls[s_step_index].action == lab07::kManualHold) {
        s_step_index += 1;
    }
    render();
    print_new_traces();
}

void animate_connecting(lv_timer_t *timer)
{
    (void)timer;
    if (s_dots == nullptr || s_session.stage() != lab07::kStageConnecting) {
        return;
    }
    s_dot_phase = (s_dot_phase + 1) % 4;
    if (s_dot_phase == 0) {
        lv_label_set_text(s_dots, "Checking the hangout");
    } else if (s_dot_phase == 1) {
        lv_label_set_text(s_dots, "Checking the hangout .");
    } else if (s_dot_phase == 2) {
        lv_label_set_text(s_dots, "Checking the hangout ..");
    } else {
        lv_label_set_text(s_dots, "Checking the hangout ...");
    }
}

int http_once(const char *url, bool pad_to_timeout)
{
    int64_t started = esp_timer_get_time();
    int64_t elapsed;
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    config.url = url;
    config.timeout_ms = lab07::kRequestTimeoutMs;
    config.method = HTTP_METHOD_GET;
    client = esp_http_client_init(&config);
    if (client != nullptr) {
        esp_http_client_perform(client);
        esp_http_client_cleanup(client);
    }
    elapsed = (esp_timer_get_time() - started) / 1000;
    if (pad_to_timeout && elapsed < lab07::kRequestTimeoutMs) {
        vTaskDelay(pdMS_TO_TICKS((int)(lab07::kRequestTimeoutMs - elapsed)));
        elapsed = (esp_timer_get_time() - started) / 1000;
    }
    return (int)elapsed;
}

void scripted_harness()
{
    int i;
    int hold_ms;
    int health_ms;
    int roster_ms;
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    lab07::run_scripted_catalog(&s_session);
    render();
    for (i = 0; i < s_session.event_count(); ++i) {
        char line[1024];
        const lab07::TerminalRecord &record = s_session.event_at(i);
        if (strcmp(record.cls, "decided") != 0 && strcmp(record.scenario, "R-wire") != 0 &&
            strcmp(record.scenario, "R-stale-clock") != 0) {
            continue;
        }
        lab07::format_trace_line(record, line, (int)sizeof(line));
        printf("%s\n", line);
    }
    s_trace_cursor = s_session.event_count();
    sample_ledgers();
    hold_ms = http_once(CONFIG_LAB07_HOLD_URL, true);
    printf("LAB07_TRACE schema=lab07-trace/v1 event=device_timeout seq=1 monotonic_us=%lld "
           "session_id=%s operation_id=0 generation=%" PRIu32 " elapsed_ms=%d request_timeout_ms=%d\n",
           (long long)esp_timer_get_time(), s_session.session_id(), s_session.generation(), hold_ms,
           lab07::kRequestTimeoutMs);
    health_ms = http_once(CONFIG_LAB07_HEALTH_URL, false);
    printf("LAB07_TRACE schema=lab07-trace/v1 event=device_http seq=2 monotonic_us=%lld "
           "session_id=%s operation_id=0 generation=%" PRIu32 " kind=health elapsed_ms=%d "
           "request_timeout_ms=%d\n",
           (long long)esp_timer_get_time(), s_session.session_id(), s_session.generation(), health_ms,
           lab07::kRequestTimeoutMs);
    roster_ms = http_once(CONFIG_LAB07_ROSTER_URL, false);
    printf("LAB07_TRACE schema=lab07-trace/v1 event=device_http seq=3 monotonic_us=%lld "
           "session_id=%s operation_id=0 generation=%" PRIu32 " kind=roster elapsed_ms=%d "
           "request_timeout_ms=%d\n",
           (long long)esp_timer_get_time(), s_session.session_id(), s_session.generation(), roster_ms,
           lab07::kRequestTimeoutMs);
    printf("LAB07_HARNESS_DONE status=complete\n");
}

bool build_screen()
{
    lv_obj_t *screen = lv_screen_active();
    if (screen == nullptr) {
        return false;
    }
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf4f1ea), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    s_status = lv_label_create(screen);
    s_stage = lv_obj_create(screen);
    s_controls = lv_obj_create(screen);
    s_unmute = lv_button_create(screen);
    if (s_status == nullptr || s_stage == nullptr || s_controls == nullptr || s_unmute == nullptr) {
        return false;
    }
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_status, 2, kStatusY);
    lv_obj_set_width(s_status, kScreenW - 4);
    lv_obj_set_pos(s_stage, 0, kStageY);
    lv_obj_set_size(s_stage, kScreenW, kStageH);
    lv_obj_set_style_pad_all(s_stage, 0, 0);
    lv_obj_set_pos(s_controls, 0, kControlsY);
    lv_obj_set_size(s_controls, kScreenW, kControlsH);
    lv_obj_set_style_pad_all(s_controls, 0, 0);
    lv_obj_set_scroll_dir(s_controls, LV_DIR_NONE);
    lv_obj_clear_flag(s_controls, LV_OBJ_FLAG_SCROLLABLE);
    {
        s_cue = lv_label_create(s_controls);
        lv_obj_t *cue = s_cue;
        lv_obj_t *button;
        if (cue == nullptr) {
            return false;
        }
        lv_obj_set_pos(cue, 8, 4);
        lv_obj_set_width(cue, kScreenW - 16);
        lv_label_set_text(cue, "Tap each once");
        button = lv_button_create(s_controls);
        if (button == nullptr) {
            return false;
        }
        lv_obj_set_pos(button, 4, kCueH);
        lv_obj_set_size(button, kScreenW - 8, kControlsH - kCueH - 4);
        lv_obj_add_event_cb(button, on_step_pressed, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(button, on_step, LV_EVENT_CLICKED, nullptr);
        s_step_label = lv_label_create(button);
        if (s_step_label == nullptr) {
            return false;
        }
        lv_label_set_long_mode(s_step_label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(s_step_label, kScreenW - 24);
        lv_obj_center(s_step_label);
        set_step_caption();
    }
    lv_obj_set_pos(s_unmute, 8, 80);
    lv_obj_set_size(s_unmute, kScreenW - 16, 120);
    lv_obj_add_flag(s_unmute, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_unmute, on_unmute, LV_EVENT_CLICKED, nullptr);
    {
        lv_obj_t *label = lv_label_create(s_unmute);
        if (label == nullptr) {
            return false;
        }
        lv_label_set_text(label, "Unmute (lab stand-in)");
        lv_obj_center(label);
    }
    if (lv_timer_create(animate_connecting, 200, nullptr) == nullptr) {
        return false;
    }
    s_session.boot(0, true);
    render_stage();
    return true;
}

} // namespace

extern "C" void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    bool ready;
    if (display == nullptr) {
        printf("LAB07_INIT_FAILED status=display_start_failed\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB07_INIT_FAILED status=display_lock_failed\n");
        return;
    }
    ready = build_screen();
    bsp_display_unlock();
    if (!ready) {
        printf("LAB07_INIT_FAILED status=controls_init_failed\n");
        return;
    }
#ifdef CONFIG_LAB07_SCRIPTED
    printf("LAB07_READY build=%s mode=scripted fixture=lab07-health-roster/v1 harness=1\n",
           CONFIG_LAB07_BUILD_ID);
    scripted_harness();
#else
    printf("LAB07_READY build=%s mode=manual fixture=lab07-health-roster/v1 harness=0\n",
           CONFIG_LAB07_BUILD_ID);
#endif
    while (true) {
        service_hold();
        if (bsp_display_lock(20)) {
            lv_timer_handler();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
