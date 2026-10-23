#include "bsp/esp-box-3.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "desk_station.hpp"
#include "pin_catalog.hpp"
#include "pin_http.hpp"
#include "session.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_LAB08_BUILD_ID
#define CONFIG_LAB08_BUILD_ID "lab08-fw-v1-manual"
#endif
#ifndef CONFIG_LAB08_PIN_URL
#define CONFIG_LAB08_PIN_URL "http://127.0.0.1:8768/pin"
#endif
#ifndef CONFIG_LAB08_OBSERVE_URL
#define CONFIG_LAB08_OBSERVE_URL "http://127.0.0.1:8768/observe"
#endif
#ifndef CONFIG_LAB08_MALFORMED_URL
#define CONFIG_LAB08_MALFORMED_URL "http://127.0.0.1:8768/malformed"
#endif

namespace {

constexpr int kScreenW = 320;
constexpr int kScreenH = 240;
constexpr int kStatusY = 0;
constexpr int kStatusH = 28;
constexpr int kStageY = 28;
constexpr int kStageH = 78;
constexpr int kGuideY = 106;
constexpr int kGuideH = 72;
constexpr int kControlsY = 178;
constexpr int kControlsH = 62;
constexpr int kHttpBodyCap = 1024;
constexpr int kStepDebounceUs = 200000;

static_assert(kStatusY + kStatusH == kStageY, "status meets stage");
static_assert(kStageY + kStageH == kGuideY, "stage meets guide");
static_assert(kGuideY + kGuideH == kControlsY, "guide meets controls");
static_assert(kControlsY + kControlsH == kScreenH, "controls fill the screen");
static_assert(kStageY + kStageH <= kGuideY, "stage does not cover the guide");

const char *kGuideText =
    "Each digit from 1 to 9 shows as * immediately.\n"
    "After the fourth digit the screen says checking... and ignores more digits while that submission is pending.\n"
    "A wrong PIN clears the entry and shows wrong pin for 2 seconds. A timeout is not a wrong PIN.\n"
    "Five wrong PINs for one user show Too many attempts. Try again in the remaining seconds.\n"
    "Switching users starts the failure count at 0. Success clears it. Cooldown expiry clears it.\n"
    "BOOT with no digits returns to the roster. BOOT with 1 to 3 digits clears them and stays. After four digits BOOT is ignored.\n"
    "Sixty seconds after the last digit of a partial entry returns to the roster and discards the digits.\n"
    "Mute during verification turns the display off. A late success does not undo mute or reveal the PIN.\n"
    "A local cancel does not mean the server rolled back a side effect.\n"
    "Each button names the action you are about to take. Scripted mode runs the decided scenarios without taps.\n";

struct Step {
    int action;
    const char *caption;
};

const Step kSteps[] = {
    {lab08::kManualSelectUser, "Select user su-01"},
    {lab08::kManualDigit, "Enter digit 1"},
    {lab08::kManualBoot, "Press BOOT"},
    {lab08::kManualWrongPin, "Submit a wrong PIN"},
    {lab08::kManualMute, "Mute during checking"},
    {lab08::kManualUnmute, "Unmute"},
    {lab08::kManualSwitchUser, "Switch to user su-02"},
    {lab08::kManualAdvanceCopy, "Advance the wrong-pin copy"},
    {lab08::kManualAdvanceCooldown, "Advance the cooldown clock by one second"},
    {lab08::kManualAdvanceIdle, "Advance the partial-entry idle clock"},
};
constexpr int kStepCount = (int)(sizeof(kSteps) / sizeof(kSteps[0]));

struct HttpExchange {
    int perform_ok;
    int http_status;
    int body_len;
    char body[kHttpBodyCap];
};

struct PinJob {
    uint64_t operation_id;
    uint32_t generation;
    char session_id[16];
    char user[16];
    char pin[8];
    char key[68];
};

lab07::SessionWriter s_session;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_stage = nullptr;
lv_obj_t *s_mask = nullptr;
lv_obj_t *s_copy = nullptr;
lv_obj_t *s_guide = nullptr;
lv_obj_t *s_controls = nullptr;
lv_obj_t *s_step_label = nullptr;
lv_obj_t *s_unmute = nullptr;
int s_step_index = 0;
int64_t s_last_step_us = 0;
QueueHandle_t s_results = nullptr;
TaskHandle_t s_worker = nullptr;
portMUX_TYPE s_job_mux = portMUX_INITIALIZER_UNLOCKED;
PinJob s_job;
int s_job_ready = 0;

void wipe(void *memory, size_t bytes)
{
    memset(memory, 0, bytes);
}

extern "C" esp_err_t lab08_http_event(esp_http_client_event_t *event)
{
    HttpExchange *out;
    int room;
    int count;
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    out = (HttpExchange *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }
    room = kHttpBodyCap - 1 - out->body_len;
    count = event->data_len < room ? event->data_len : room;
    if (count > 0) {
        memcpy(out->body + out->body_len, event->data, (size_t)count);
        out->body_len += count;
        out->body[out->body_len] = 0;
    }
    return ESP_OK;
}

void perform_exchange(const char *url, esp_http_client_method_t method, const char *json, int json_len, int drop,
                      HttpExchange *out)
{
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    memset(out, 0, sizeof(*out));
    config.url = url;
    config.method = method;
    config.timeout_ms = 1000;
    config.event_handler = lab08_http_event;
    config.user_data = out;
    client = esp_http_client_init(&config);
    if (client == nullptr) {
        return;
    }
    if (method == HTTP_METHOD_POST) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        if (drop) {
            esp_http_client_set_header(client, "X-Lab-Drop", "1");
        }
        esp_http_client_set_post_field(client, json, json_len);
    }
    if (esp_http_client_perform(client) == ESP_OK) {
        out->perform_ok = 1;
        out->http_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
}

int observe_side_effects()
{
    HttpExchange exchange;
    const char *mark;
    perform_exchange(CONFIG_LAB08_OBSERVE_URL, HTTP_METHOD_GET, nullptr, 0, 0, &exchange);
    if (!exchange.perform_ok) {
        return -1;
    }
    mark = strstr(exchange.body, "\"side_effects\"");
    if (mark == nullptr) {
        return -1;
    }
    mark = strchr(mark, ':');
    if (mark == nullptr) {
        return -1;
    }
    return atoi(mark + 1);
}

void set_step_caption()
{
    char text[96];
    if (s_step_label == nullptr) {
        return;
    }
    if (s_step_index >= kStepCount) {
        lv_label_set_text(s_step_label, "Named actions are finished");
        return;
    }
    snprintf(text, sizeof(text), "%d/%d  %s", s_step_index + 1, kStepCount, kSteps[s_step_index].caption);
    lv_label_set_text(s_step_label, text);
}

void render_properties()
{
    char status[96];
    bool muted = s_session.stage() == lab07::kStageMuted;
    if (s_stage != nullptr) {
        lv_label_set_text(s_stage, s_session.stage_name());
    }
    if (s_mask != nullptr) {
        lv_label_set_text(s_mask, s_session.pin_mask());
    }
    if (s_copy != nullptr) {
        lv_label_set_text(s_copy, s_session.visible_copy());
    }
    if (s_status != nullptr) {
        snprintf(status, sizeof(status), "%s  wrong=%d  power=%d", s_session.stage_name(),
                 s_session.wrong_count(), s_session.display_power());
        lv_label_set_text(s_status, status);
    }
    if (s_status != nullptr) {
        muted ? lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_stage != nullptr) {
        muted ? lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mask != nullptr) {
        muted ? lv_obj_add_flag(s_mask, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_mask, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_copy != nullptr) {
        muted ? lv_obj_add_flag(s_copy, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_copy, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_guide != nullptr) {
        muted ? lv_obj_add_flag(s_guide, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_guide, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_controls != nullptr) {
        muted ? lv_obj_add_flag(s_controls, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_controls, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_unmute != nullptr) {
        muted ? lv_obj_clear_flag(s_unmute, LV_OBJ_FLAG_HIDDEN) : lv_obj_add_flag(s_unmute, LV_OBJ_FLAG_HIDDEN);
    }
}

lab08::PinTraceAccount s_loss;

void note_callback_us(int64_t started)
{
    char line[256];
    int formatted = snprintf(line, sizeof(line),
                             "LAB08_TRACE schema=lab08-trace/v1 event=input_callback seq=0 monotonic_us=%lld "
                             "session_id=%s operation_id=0 elapsed_us=%lld",
                             (long long)esp_timer_get_time(), s_session.session_id(),
                             (long long)(esp_timer_get_time() - started));
    int written;
    if (formatted < 0) {
        s_loss.malformed += 1;
        return;
    }
    if (formatted >= (int)sizeof(line)) {
        s_loss.oversized += 1;
        return;
    }
    written = printf("%s\n", line);
    if (written < formatted + 1) {
        s_loss.dropped += 1;
    }
}

void sample_resources()
{
    lv_mem_monitor_t monitor;
    char line[320];
    int formatted;
    int written;
    lv_mem_monitor(&monitor);
    formatted = snprintf(line, sizeof(line),
                         "LAB08_TRACE schema=lab08-trace/v1 event=resource_sample seq=1 monotonic_us=%lld "
                         "internal_largest_free_block_bytes=%u psram_largest_free_block_bytes=%u "
                         "lvgl_largest_free_block_bytes=%u",
                         (long long)esp_timer_get_time(),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                         (unsigned)monitor.free_biggest_size);
    if (formatted < 0) {
        s_loss.malformed += 1;
        return;
    }
    if (formatted >= (int)sizeof(line)) {
        s_loss.oversized += 1;
        return;
    }
    written = printf("%s\n", line);
    if (written < formatted + 1) {
        s_loss.dropped += 1;
    }
}

void render_properties();

void note_checking_frame()
{
    int64_t started = esp_timer_get_time();
    if (bsp_display_lock(50)) {
        render_properties();
        bsp_display_unlock();
    }
    note_callback_us(started);
}

void finish_harness(const char *status)
{
    sample_resources();
    printf("LAB08_TRACE schema=lab08-trace/v1 event=trace_loss dropped_events=%u "
           "oversized_events=%u malformed_rows=%u\n",
           (unsigned)s_loss.dropped, (unsigned)s_loss.oversized, (unsigned)s_loss.malformed);
    printf("LAB08_HARNESS_DONE status=%s\n", status);
}

void queue_submitted_pin()
{
    PinJob job;
    memset(&job, 0, sizeof(job));
    if (!s_session.copy_pin_request(job.user, (int)sizeof(job.user), job.pin, (int)sizeof(job.pin), job.key,
                                    (int)sizeof(job.key))) {
        return;
    }
    job.operation_id = s_session.pin_operation_id();
    job.generation = s_session.pin_key_generation();
    strncpy(job.session_id, s_session.session_id(), sizeof(job.session_id) - 1);
    portENTER_CRITICAL(&s_job_mux);
    s_job = job;
    s_job_ready = 1;
    portEXIT_CRITICAL(&s_job_mux);
    wipe(job.pin, sizeof(job.pin));
    if (s_worker != nullptr) {
        xTaskNotifyGive(s_worker);
    }
}

void on_step(lv_event_t *event)
{
    int64_t started = esp_timer_get_time();
    int action;
    (void)event;
    if (started - s_last_step_us < kStepDebounceUs) {
        return;
    }
    if (s_step_index >= kStepCount) {
        return;
    }
    action = kSteps[s_step_index].action;
    lab08::apply_manual_pin(&s_session, action);
    if (action == lab08::kManualWrongPin) {
        queue_submitted_pin();
    }
    s_step_index += 1;
    s_last_step_us = esp_timer_get_time();
    set_step_caption();
    render_properties();
    note_callback_us(started);
}

void on_unmute(lv_event_t *event)
{
    int64_t started = esp_timer_get_time();
    (void)event;
    lab08::apply_manual_pin(&s_session, lab08::kManualUnmute);
    if (s_step_index < kStepCount && kSteps[s_step_index].action == lab08::kManualUnmute) {
        s_step_index += 1;
        set_step_caption();
    }
    render_properties();
    note_callback_us(started);
}

void drain_results(lv_timer_t *timer)
{
    lab07::PinResult result;
    (void)timer;
    if (s_results != nullptr && xQueueReceive(s_results, &result, 0) == pdTRUE) {
        s_session.deliver_pin_completion(result, s_session.now_ms());
    }
    render_properties();
}

#ifndef CONFIG_LAB08_SCRIPTED
void pin_worker(void *arg)
{
    PinJob job;
    char json[192];
    HttpExchange exchange;
    lab07::PinResult result;
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&s_job_mux);
        job = s_job;
        s_job_ready = 0;
        portEXIT_CRITICAL(&s_job_mux);
        snprintf(json, sizeof(json), "{\"key\":\"%s\",\"user\":\"%s\",\"pin\":\"%s\"}", job.key, job.user, job.pin);
        wipe(job.pin, sizeof(job.pin));
        perform_exchange(CONFIG_LAB08_PIN_URL, HTTP_METHOD_POST, json, (int)strlen(json), 0, &exchange);
        wipe(json, sizeof(json));
        result = lab08::classify_pin_http(job.operation_id, job.generation, job.session_id, job.user,
                                          exchange.perform_ok, exchange.http_status, exchange.body,
                                          exchange.body_len);
        if (s_results != nullptr) {
            xQueueSend(s_results, &result, portMAX_DELAY);
        }
        wipe(&job, sizeof(job));
        wipe(&exchange, sizeof(exchange));
    }
}
#endif

bool build_screen()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *guide_label;
    lv_obj_t *button;
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), 0);
    s_status = lv_label_create(screen);
    s_stage = lv_label_create(screen);
    s_mask = lv_label_create(screen);
    s_copy = lv_label_create(screen);
    if (s_status == nullptr || s_stage == nullptr || s_mask == nullptr || s_copy == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_status, 8, 4);
    lv_obj_set_width(s_status, kScreenW - 16);
    lv_obj_set_pos(s_stage, 8, kStageY + 4);
    lv_obj_set_pos(s_mask, 8, kStageY + 24);
    lv_obj_set_pos(s_copy, 8, kStageY + 44);
    lv_obj_set_width(s_copy, kScreenW - 16);
    lv_label_set_long_mode(s_copy, LV_LABEL_LONG_WRAP);
    s_guide = lv_obj_create(screen);
    if (s_guide == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_guide, 0, kGuideY);
    lv_obj_set_size(s_guide, kScreenW, kGuideH);
    lv_obj_set_scrollbar_mode(s_guide, LV_SCROLLBAR_MODE_AUTO);
    guide_label = lv_label_create(s_guide);
    if (guide_label == nullptr) {
        return false;
    }
    lv_obj_set_width(guide_label, kScreenW - 16);
    lv_label_set_long_mode(guide_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(guide_label, kGuideText);
    s_controls = lv_obj_create(screen);
    if (s_controls == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_controls, 0, kControlsY);
    lv_obj_set_size(s_controls, kScreenW, kControlsH);
    button = lv_button_create(s_controls);
    if (button == nullptr) {
        return false;
    }
    lv_obj_set_pos(button, 4, 4);
    lv_obj_set_size(button, kScreenW - 8, kControlsH - 8);
    lv_obj_add_event_cb(button, on_step, LV_EVENT_CLICKED, nullptr);
    s_step_label = lv_label_create(button);
    if (s_step_label == nullptr) {
        return false;
    }
    lv_label_set_long_mode(s_step_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_step_label, kScreenW - 24);
    lv_obj_center(s_step_label);
    set_step_caption();
    s_unmute = lv_button_create(screen);
    if (s_unmute == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_unmute, 8, 80);
    lv_obj_set_size(s_unmute, kScreenW - 16, 80);
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
    if (lv_timer_create(drain_results, 50, nullptr) == nullptr) {
        return false;
    }
    s_session.boot(0, true);
    render_properties();
    return true;
}

#ifdef CONFIG_LAB08_SCRIPTED
bool post_saved(const char *user, const char *pin, const char *key, int drop, HttpExchange *exchange)
{
    char json[192];
    int length = snprintf(json, sizeof(json), "{\"key\":\"%s\",\"user\":\"%s\",\"pin\":\"%s\"}", key, user, pin);
    if (length <= 0 || length >= (int)sizeof(json)) {
        wipe(json, sizeof(json));
        return false;
    }
    perform_exchange(CONFIG_LAB08_PIN_URL, HTTP_METHOD_POST, json, length, drop, exchange);
    wipe(json, sizeof(json));
    return true;
}

void restore_after_unmute()
{
    static const char *kIds[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
    lab07::RosterResult result;
    if (!s_session.roster_inflight()) {
        return;
    }
    memset(&result, 0, sizeof(result));
    result.operation_id = s_session.roster_operation_id();
    result.http_status = 200;
    result.parse_ok = 1;
    result.valid = 1;
    lab07::fill_users(&result, kIds, 8);
    s_session.deliver_roster_completion(result, s_session.now_ms());
}

bool live_authentication(bool *ok_success, bool *lost_success, bool *bad_clean, bool *closed_clean, bool *mute_held)
{
    char user[16];
    char pin[8];
    char key[68];
    char session_id[16];
    HttpExchange exchange;
    lab07::PinResult result;
    uint64_t operation_id;
    uint32_t generation;
    int before;
    int after_drop;
    int after_replay;
    *ok_success = false;
    *lost_success = false;
    *bad_clean = false;
    *closed_clean = false;
    *mute_held = false;
    s_session.select_pin_user("su-02", s_session.now_ms());
    s_session.select_pin_user("su-01", s_session.now_ms());
    operation_id = s_session.begin_pin_request("su-01", "1234", s_session.now_ms());
    note_checking_frame();
    generation = s_session.pin_key_generation();
    strncpy(session_id, s_session.session_id(), sizeof(session_id) - 1);
    if (operation_id == 0 ||
        !s_session.copy_pin_request(user, (int)sizeof(user), pin, (int)sizeof(pin), key, (int)sizeof(key))) {
        return false;
    }
    if (!post_saved(user, pin, key, 0, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    result = lab08::classify_pin_http(operation_id, generation, session_id, user, exchange.perform_ok,
                                      exchange.http_status, exchange.body, exchange.body_len);
    if (result.outcome == lab07::kPinOutcomeSuccess) {
        s_session.arm_pin_scenario("K-ok");
    }
    s_session.deliver_pin_completion(result, s_session.now_ms());
    *ok_success = result.outcome == lab07::kPinOutcomeSuccess && s_session.authenticated() == 1 &&
                  s_session.terminal_count_for(operation_id) == 1 &&
                  strcmp(s_session.terminal_for(operation_id), "success") == 0;
    before = observe_side_effects();
    if (!post_saved(user, pin, key, 0, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    s_session.note_remote_observation(before < 0 ? 0 : before, "concurrent");
    s_session.publish("K-concurrent", "failure");
    if (!post_saved(user, "9999", key, 0, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    s_session.note_remote_observation(observe_side_effects() < 0 ? 0 : observe_side_effects(), "mismatch");
    s_session.publish("K-mismatch", "failure");
    wipe(pin, sizeof(pin));
    wipe(exchange.body, sizeof(exchange.body));

    s_session.select_pin_user("su-01", s_session.now_ms());
    operation_id = s_session.begin_pin_request("su-01", "1234", s_session.now_ms());
    generation = s_session.pin_key_generation();
    strncpy(session_id, s_session.session_id(), sizeof(session_id) - 1);
    if (operation_id == 0 ||
        !s_session.copy_pin_request(user, (int)sizeof(user), pin, (int)sizeof(pin), key, (int)sizeof(key)) ||
        !post_saved(user, pin, key, 1, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    result = lab08::classify_pin_http(operation_id, generation, session_id, user, exchange.perform_ok,
                                      exchange.http_status, exchange.body, exchange.body_len);
    s_session.deliver_pin_completion(result, s_session.now_ms());
    after_drop = observe_side_effects();
    if (!post_saved(user, pin, key, 0, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    after_replay = observe_side_effects();
    result = lab08::classify_pin_http(operation_id, generation, session_id, user, exchange.perform_ok,
                                      exchange.http_status, exchange.body, exchange.body_len);
    if (result.outcome == lab07::kPinOutcomeSuccess && result.side_effects == 1 && after_drop == after_replay) {
        s_session.arm_pin_scenario("K-lost");
    }
    s_session.deliver_pin_completion(result, s_session.now_ms());
    *lost_success = strcmp(s_session.terminal_for(operation_id), "success") == 0 &&
                    s_session.terminal_count_for(operation_id) == 1 && result.side_effects == 1;
    wipe(pin, sizeof(pin));

    s_session.select_pin_user("su-02", s_session.now_ms());
    operation_id = s_session.begin_pin_request("su-02", "5678", s_session.now_ms());
    generation = s_session.pin_key_generation();
    perform_exchange(CONFIG_LAB08_MALFORMED_URL, HTTP_METHOD_GET, nullptr, 0, 0, &exchange);
    result = lab08::classify_pin_http(operation_id, generation, s_session.session_id(), "su-02",
                                      exchange.perform_ok, exchange.http_status, exchange.body, exchange.body_len);
    s_session.deliver_pin_completion(result, s_session.now_ms());
    *bad_clean = result.outcome != lab07::kPinOutcomeSuccess && s_session.authenticated() == 0;
    if (s_session.pin_submitted()) {
        s_session.cancel_pin(s_session.now_ms());
    }

    s_session.select_pin_user("su-02", s_session.now_ms());
    operation_id = s_session.begin_pin_request("su-02", "5678", s_session.now_ms());
    generation = s_session.pin_key_generation();
    perform_exchange("http://127.0.0.1:1/pin", HTTP_METHOD_POST, "{}", 2, 0, &exchange);
    result = lab08::classify_pin_http(operation_id, generation, s_session.session_id(), "su-02",
                                      exchange.perform_ok, exchange.http_status, exchange.body, exchange.body_len);
    s_session.deliver_pin_completion(result, s_session.now_ms());
    *closed_clean = exchange.perform_ok == 0 && result.outcome != lab07::kPinOutcomeSuccess &&
                    s_session.authenticated() == 0;
    if (s_session.pin_submitted()) {
        s_session.cancel_pin(s_session.now_ms());
    }

    s_session.select_pin_user("su-01", s_session.now_ms());
    operation_id = s_session.begin_pin_request("su-01", "1234", s_session.now_ms());
    generation = s_session.pin_key_generation();
    strncpy(session_id, s_session.session_id(), sizeof(session_id) - 1);
    if (operation_id == 0 ||
        !s_session.copy_pin_request(user, (int)sizeof(user), pin, (int)sizeof(pin), key, (int)sizeof(key)) ||
        !post_saved(user, pin, key, 0, &exchange)) {
        wipe(pin, sizeof(pin));
        return false;
    }
    result = lab08::classify_pin_http(operation_id, generation, session_id, user, exchange.perform_ok,
                                      exchange.http_status, exchange.body, exchange.body_len);
    s_session.note_remote_observation(result.side_effects_valid ? result.side_effects : 0, "mute_server");
    s_session.arm_pin_scenario("K-mute-server");
    s_session.mute(s_session.now_ms());
    s_session.deliver_pin_completion(result, s_session.now_ms());
    *mute_held = s_session.stage() == lab07::kStageMuted && s_session.display_power() == 0 &&
                 s_session.digit_len() == 0 && s_session.authenticated() == 0 &&
                 s_session.terminal_count_for(operation_id) == 1;
    wipe(pin, sizeof(pin));
    wipe(&exchange, sizeof(exchange));
    return true;
}

void scripted_harness()
{
    bool ok_success = false;
    bool lost_success = false;
    bool bad_clean = false;
    bool closed_clean = false;
    bool mute_held = false;
    bool posted;
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    lab08::run_clock_catalog(&s_session);
    s_session.unmute(s_session.now_ms());
    restore_after_unmute();
    if (!join_desk_wifi(CONFIG_LAB08_WIFI_SSID, CONFIG_LAB08_WIFI_PASSWORD)) {
        lab08::emit_pin_traces(s_session, &s_loss);
        finish_harness("incomplete");
        return;
    }
    posted = live_authentication(&ok_success, &lost_success, &bad_clean, &closed_clean, &mute_held);
    lab08::emit_pin_traces(s_session, &s_loss);
    if (bsp_display_lock(1000)) {
        render_properties();
        bsp_display_unlock();
    }
    if (posted && ok_success && lost_success && bad_clean && closed_clean && mute_held) {
        finish_harness("complete");
        return;
    }
    finish_harness("incomplete");
}
#endif

} // namespace

extern "C" void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    bool ready;
    if (display == nullptr) {
        printf("LAB08_INIT_FAILED status=display_start_failed\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB08_INIT_FAILED status=display_lock_failed\n");
        return;
    }
    ready = build_screen();
    bsp_display_unlock();
    if (!ready) {
        printf("LAB08_INIT_FAILED status=display_start_failed\n");
        return;
    }
    s_results = xQueueCreate(4, sizeof(lab07::PinResult));
    if (s_results == nullptr) {
        printf("LAB08_INIT_FAILED status=display_start_failed\n");
        return;
    }
#ifndef CONFIG_LAB08_SCRIPTED
    if (xTaskCreate(pin_worker, "lab08_pin", 8192, nullptr, 5, &s_worker) != pdPASS) {
        printf("LAB08_INIT_FAILED status=display_start_failed\n");
        return;
    }
    printf("LAB08_READY build=%s mode=manual fixture=lab08-pin/v1 harness=0\n", CONFIG_LAB08_BUILD_ID);
#else
    printf("LAB08_READY build=%s mode=scripted fixture=lab08-pin/v1 harness=1\n", CONFIG_LAB08_BUILD_ID);
    scripted_harness();
#endif
    while (true) {
        if (bsp_display_lock(20)) {
            lv_timer_handler();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
