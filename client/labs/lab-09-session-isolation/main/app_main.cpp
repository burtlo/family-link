#include "bsp/esp-box-3.h"
#include "esp_attr.h"
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
#include "probe.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_LAB09_BUILD_ID
#define CONFIG_LAB09_BUILD_ID "lab09-fw-v1-manual"
#endif
#ifndef CONFIG_LAB09_BASE_URL
#define CONFIG_LAB09_BASE_URL "http://127.0.0.1:8769"
#endif

namespace
{

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
constexpr int kStepDebounceUs = 200000;

static_assert(kStatusY + kStatusH == kStageY, "status meets stage");
static_assert(kStageY + kStageH == kGuideY, "stage meets guide");
static_assert(kGuideY + kGuideH == kControlsY, "guide meets controls");
static_assert(kControlsY + kControlsH == kScreenH, "controls fill the screen");
static_assert(kStageY + kStageH <= kGuideY, "stage does not cover the guide");

const char *kGuideText =
    "This is a signed-in probe, not a product inbox.\n"
    "Four operations: fetch the inbox, fetch one image, write playback state, and update the profile.\n"
    "Each one keeps the operation id and session generation from the moment you submit it.\n"
    "The screen shows whose session is active and whether a result was applied.\n"
    "Mute starts each operation for user A. After mute, a late result stays stale or cancelled, "
    "user A's bytes stay hidden, the display is off, and the late result does not restore the stage.\n"
    "Sign out before the result arrives and the result is rejected. The stage stays signed out.\n"
    "Sign in as user B before user A's result arrives. A's result is not applied to B's cache, "
    "and B's session generation is different from A's.\n"
    "If the fixture session expires before the response, nothing is applied, the stage is not restored, "
    "and there is one terminal that is not success.\n"
    "Duplicate and out-of-order inbox hints do not create duplicate card ids.\n"
    "Each button names the action you are about to take. Scripted mode runs the decided scenarios without taps.\n";

enum Action {
    kSignInA = 1,
    kFetchInbox = 2,
    kFetchMedia = 3,
    kWritePlayback = 4,
    kUpdateProfile = 5,
    kMute = 6,
    kSignOut = 7,
    kSignInB = 8
};

struct Step {
    int action;
    const char *caption;
};

const Step kSteps[] = {
    {kSignInA, "Sign in as user A"},
    {kFetchInbox, "Fetch inbox"},
    {kFetchMedia, "Fetch media"},
    {kWritePlayback, "Write playback state"},
    {kUpdateProfile, "Update profile"},
    {kMute, "Mute"},
    {kSignOut, "Sign out"},
    {kSignInB, "Sign in as user B"},
};
constexpr int kStepCount = (int)(sizeof(kSteps) / sizeof(kSteps[0]));

struct Exchange {
    int perform_ok;
    int http_status;
    int body_len;
    char body[lab09::kImageBytes + 1];
};

struct Pending {
    int ready;
    int sign_in;
    int perform_ok;
    int http_status;
    int body_len;
    char user[16];
    lab07::ScopedTicket ticket;
};

lab09::Probe s_probe;
/* One response buffer. Scripted mode and the HTTP worker do not run together.
   The worker leaves the bytes here until the UI drain has applied or rejected them. */
EXT_RAM_BSS_ATTR Exchange s_exchange;
#ifdef CONFIG_LAB09_SCRIPTED
EXT_RAM_BSS_ATTR Exchange s_media_held;
#endif
lv_obj_t *s_status = nullptr;
lv_obj_t *s_stage = nullptr;
lv_obj_t *s_guide = nullptr;
lv_obj_t *s_controls = nullptr;
lv_obj_t *s_step_label = nullptr;
lv_obj_t *s_unmute = nullptr;
int s_step_index = 0;
int64_t s_last_step_us = 0;
char s_handle[40];
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Pending s_pending;
int s_job_action = 0;
int s_job_ready = 0;
lab07::ScopedTicket s_job_ticket;
TaskHandle_t s_worker = nullptr;

void wipe_handle()
{
    memset(s_handle, 0, sizeof(s_handle));
}

extern "C" esp_err_t lab09_http_event(esp_http_client_event_t *event)
{
    Exchange *out;
    int room;
    int count;
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    out = (Exchange *)event->user_data;
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }
    room = lab09::kImageBytes - out->body_len;
    count = event->data_len < room ? event->data_len : room;
    if (count > 0) {
        memcpy(out->body + out->body_len, event->data, (size_t)count);
        out->body_len += count;
        out->body[out->body_len] = 0;
    }
    return ESP_OK;
}

void perform_exchange(const char *path, esp_http_client_method_t method, const char *json, int json_len,
                      const char *handle, Exchange *out)
{
    char url[128];
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    snprintf(url, sizeof(url), "%s%s", CONFIG_LAB09_BASE_URL, path);
    memset(out, 0, sizeof(*out));
    config.url = url;
    config.method = method;
    config.timeout_ms = 2000;
    config.event_handler = lab09_http_event;
    config.user_data = out;
    client = esp_http_client_init(&config);
    if (client == nullptr) {
        return;
    }
    if (handle != nullptr && handle[0] != 0) {
        char header[80];
        snprintf(header, sizeof(header), "Bearer %s", handle);
        esp_http_client_set_header(client, "Authorization", header);
        memset(header, 0, sizeof(header));
    }
    if (method == HTTP_METHOD_POST && json != nullptr) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, json, json_len);
    }
    if (esp_http_client_perform(client) == ESP_OK) {
        out->perform_ok = 1;
        out->http_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
}

bool take_handle(const char *body, int length)
{
    const char *mark = body == nullptr ? nullptr : strstr(body, "\"handle\"");
    int i;
    int n = 0;
    if (mark == nullptr) {
        return false;
    }
    mark = strchr(mark, ':');
    if (mark == nullptr) {
        return false;
    }
    mark += 1;
    while (*mark == ' ') {
        mark += 1;
    }
    if (*mark != '"') {
        return false;
    }
    mark += 1;
    for (i = 0; mark[i] != 0 && mark[i] != '"' && n < (int)sizeof(s_handle) - 1; ++i) {
        s_handle[n] = mark[i];
        n += 1;
    }
    s_handle[n] = 0;
    return n > 0 && (body + length) > mark;
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
    char stage[96];
    bool muted = s_probe.session().stage() == lab07::kStageMuted;
    const char *user = s_probe.active_user();
    if (s_status != nullptr) {
        snprintf(status, sizeof(status), "%s  power=%d", s_probe.session().stage_name(),
                 s_probe.session().display_power());
        lv_label_set_text(s_status, status);
    }
    if (s_stage != nullptr) {
        if (user[0] == 0) {
            snprintf(stage, sizeof(stage), "No session is active. Result %s.",
                     s_probe.applied_visible() ? "applied" : "not applied");
        } else {
            snprintf(stage, sizeof(stage), "Session %s. Result %s.", user,
                     s_probe.applied_visible() ? "applied" : "not applied");
        }
        lv_label_set_text(s_stage, stage);
    }
    if (s_status != nullptr) {
        muted ? lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_stage != nullptr) {
        muted ? lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN) : lv_obj_clear_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
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

void publish_pending(int sign_in, const char *user, const lab07::ScopedTicket &ticket)
{
    portENTER_CRITICAL(&s_mux);
    s_pending.ready = 1;
    s_pending.sign_in = sign_in;
    s_pending.perform_ok = s_exchange.perform_ok;
    s_pending.http_status = s_exchange.http_status;
    s_pending.body_len = s_exchange.body_len;
    strncpy(s_pending.user, user == nullptr ? "" : user, sizeof(s_pending.user) - 1);
    s_pending.user[sizeof(s_pending.user) - 1] = 0;
    s_pending.ticket = ticket;
    portEXIT_CRITICAL(&s_mux);
}

void drain_results(lv_timer_t *timer)
{
    (void)timer;
    if (!s_pending.ready) {
        return;
    }
    if (s_pending.sign_in) {
        if (lab09::sign_in_response_ok(s_pending.perform_ok, s_pending.http_status, s_exchange.body,
                                       s_pending.body_len, s_pending.user)) {
            s_probe.sign_in(s_pending.user, s_probe.session().now_ms());
        }
    } else if (s_pending.ticket.operation_id != 0) {
        s_probe.complete(s_pending.ticket, s_pending.perform_ok, s_pending.http_status, s_exchange.body,
                         s_pending.body_len, s_probe.session().now_ms());
    }
    memset(s_exchange.body, 0, sizeof(s_exchange.body));
    s_pending.ready = 0;
    render_properties();
}

void queue_job(int action, const lab07::ScopedTicket &ticket)
{
    portENTER_CRITICAL(&s_mux);
    s_job_action = action;
    s_job_ticket = ticket;
    s_job_ready = 1;
    portEXIT_CRITICAL(&s_mux);
    if (s_worker != nullptr) {
        xTaskNotifyGive(s_worker);
    }
}

void on_step(lv_event_t *event)
{
    int64_t now;
    lab07::ScopedTicket ticket;
    const Step *step;
    (void)event;
    now = esp_timer_get_time();
    if (now - s_last_step_us < kStepDebounceUs || s_step_index >= kStepCount) {
        return;
    }
    s_last_step_us = now;
    step = &kSteps[s_step_index];
    s_step_index += 1;
    set_step_caption();
    memset(&ticket, 0, sizeof(ticket));
    if (step->action == kMute) {
        s_probe.mute(s_probe.session().now_ms());
        render_properties();
        return;
    }
    if (step->action == kSignOut) {
        s_probe.sign_out(s_probe.session().now_ms());
        wipe_handle();
        render_properties();
        return;
    }
    if (step->action == kFetchInbox) {
        ticket = s_probe.begin("inbox", s_probe.session().now_ms());
    } else if (step->action == kFetchMedia) {
        ticket = s_probe.begin("media", s_probe.session().now_ms());
    } else if (step->action == kWritePlayback) {
        ticket = s_probe.begin("playback", s_probe.session().now_ms());
    } else if (step->action == kUpdateProfile) {
        ticket = s_probe.begin("profile", s_probe.session().now_ms());
    }
    if (step->action != kSignInA && step->action != kSignInB && ticket.operation_id == 0) {
        render_properties();
        return;
    }
    queue_job(step->action, ticket);
}

void on_unmute(lv_event_t *event)
{
    (void)event;
    s_probe.unmute(s_probe.session().now_ms());
    s_probe.sign_in("user-a", s_probe.session().now_ms());
    render_properties();
}

void worker(void *arg)
{
    (void)arg;
    while (true) {
        int action;
        lab07::ScopedTicket ticket;
        char handle[40];
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (s_pending.ready) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        portENTER_CRITICAL(&s_mux);
        action = s_job_action;
        ticket = s_job_ticket;
        strncpy(handle, s_handle, sizeof(handle) - 1);
        handle[sizeof(handle) - 1] = 0;
        s_job_ready = 0;
        portEXIT_CRITICAL(&s_mux);
        if (action == kSignInA || action == kSignInB) {
            const char *user = action == kSignInA ? "user-a" : "user-b";
            char json[64];
            int length = snprintf(json, sizeof(json), "{\"user\":\"%s\"}", user);
            perform_exchange("/session/sign-in", HTTP_METHOD_POST, json, length, nullptr, &s_exchange);
            if (lab09::sign_in_response_ok(s_exchange.perform_ok, s_exchange.http_status, s_exchange.body,
                                           s_exchange.body_len, user)) {
                take_handle(s_exchange.body, s_exchange.body_len);
            }
            publish_pending(1, user, ticket);
            memset(handle, 0, sizeof(handle));
            continue;
        }
        if (action == kFetchInbox) {
            perform_exchange("/inbox", HTTP_METHOD_POST, "{}", 2, handle, &s_exchange);
        } else if (action == kFetchMedia) {
            perform_exchange("/media", HTTP_METHOD_POST, "{}", 2, handle, &s_exchange);
        } else if (action == kWritePlayback) {
            perform_exchange("/playback", HTTP_METHOD_POST, "{}", 2, handle, &s_exchange);
        } else if (action == kUpdateProfile) {
            perform_exchange("/profile", HTTP_METHOD_POST, "{}", 2, handle, &s_exchange);
        } else {
            memset(handle, 0, sizeof(handle));
            continue;
        }
        publish_pending(0, "", ticket);
        memset(handle, 0, sizeof(handle));
    }
}

bool build_screen()
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *guide_label;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    s_status = lv_label_create(screen);
    s_stage = lv_label_create(screen);
    if (s_status == nullptr || s_stage == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_status, 4, 4);
    lv_obj_set_width(s_status, kScreenW - 8);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_stage, 4, kStageY + 8);
    lv_obj_set_width(s_stage, kScreenW - 8);
    lv_label_set_long_mode(s_stage, LV_LABEL_LONG_WRAP);
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
    s_controls = lv_button_create(screen);
    if (s_controls == nullptr) {
        return false;
    }
    lv_obj_set_pos(s_controls, 4, kControlsY + 4);
    lv_obj_set_size(s_controls, kScreenW - 8, kControlsH - 8);
    lv_obj_add_event_cb(s_controls, on_step, LV_EVENT_CLICKED, nullptr);
    s_step_label = lv_label_create(s_controls);
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
    render_properties();
    return true;
}

#ifdef CONFIG_LAB09_SCRIPTED
bool http_sign_in(const char *user)
{
    char json[64];
    int length = snprintf(json, sizeof(json), "{\"user\":\"%s\"}", user);
    perform_exchange("/session/sign-in", HTTP_METHOD_POST, json, length, nullptr, &s_exchange);
    if (!lab09::sign_in_response_ok(s_exchange.perform_ok, s_exchange.http_status, s_exchange.body,
                                    s_exchange.body_len, user) ||
        !take_handle(s_exchange.body, s_exchange.body_len)) {
        memset(s_exchange.body, 0, sizeof(s_exchange.body));
        return false;
    }
    s_probe.sign_in(user, s_probe.session().now_ms());
    memset(s_exchange.body, 0, sizeof(s_exchange.body));
    return s_probe.session().signed_in() == 1 && strcmp(s_probe.session().signed_user(), user) == 0;
}

bool fetch(const char *kind, const char *path, Exchange *exchange)
{
    lab07::ScopedTicket ticket = s_probe.begin(kind, s_probe.session().now_ms());
    if (ticket.operation_id == 0) {
        return false;
    }
    perform_exchange(path, HTTP_METHOD_POST, "{}", 2, s_handle, exchange);
    return s_probe.complete(ticket, exchange->perform_ok, exchange->http_status, exchange->body,
                            exchange->body_len, s_probe.session().now_ms()) == 1;
}

int observe_user_b()
{
    const char *mark;
    int count;
    perform_exchange("/observe", HTTP_METHOD_GET, nullptr, 0, nullptr, &s_exchange);
    if (!s_exchange.perform_ok) {
        return -1;
    }
    mark = strstr(s_exchange.body, "\"user-b\"");
    if (mark == nullptr) {
        return -1;
    }
    mark = strchr(mark, ':');
    if (mark == nullptr) {
        return -1;
    }
    count = atoi(mark + 1);
    memset(s_exchange.body, 0, sizeof(s_exchange.body));
    return count;
}

struct TraceAccount {
    uint32_t dropped;
    uint32_t oversized;
    uint32_t malformed;
};

TraceAccount s_loss;

void write_accounted(const char *line, int formatted, int capacity)
{
    int written;
    if (line == nullptr || formatted < 0) {
        s_loss.malformed += 1;
        return;
    }
    if (formatted >= capacity) {
        s_loss.oversized += 1;
        return;
    }
    written = printf("%s\n", line);
    if (written < formatted + 1) {
        s_loss.dropped += 1;
    }
}

void sample_resources(const char *scenario)
{
    lv_mem_monitor_t monitor;
    char line[320];
    int formatted;
    lv_mem_monitor(&monitor);
    formatted = snprintf(line, sizeof(line),
                         "LAB09_TRACE schema=lab09-trace/v1 event=resource_sample seq=1 monotonic_us=%lld "
                         "scenario=%s internal_largest_free_block_bytes=%u "
                         "psram_largest_free_block_bytes=%u lvgl_largest_free_block_bytes=%u",
                         (long long)esp_timer_get_time(), scenario,
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                         (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                         (unsigned)monitor.free_biggest_size);
    write_accounted(line, formatted, (int)sizeof(line));
}

void print_traces()
{
    static char buffer[12288];
    int n = lab09::emit_scoped_traces(s_probe.session(), buffer, (int)sizeof(buffer));
    if (n < 0) {
        s_loss.oversized += 1;
        return;
    }
    if (n > 0) {
        write_accounted(buffer, n, (int)sizeof(buffer));
    }
}

void finish_harness(const char *status)
{
    sample_resources("end");
    printf("LAB09_TRACE schema=lab09-trace/v1 event=trace_loss dropped_events=%u "
           "oversized_events=%u malformed_rows=%u\n",
           (unsigned)s_loss.dropped, (unsigned)s_loss.oversized, (unsigned)s_loss.malformed);
    printf("LAB09_HARNESS_DONE status=%s\n", status);
}

void scripted_harness()
{
    const char *kinds[] = {"inbox", "media", "playback", "profile"};
    const char *paths[] = {"/inbox", "/media", "/playback", "/profile"};
    bool mute_ok = true;
    bool signout_ok = false;
    bool switch_ok = false;
    bool push_ok = false;
    bool expire_ok = false;
    bool signed_ok;
    int k;
    int confused = -1;
    int revoke_status = 0;
    int leave_status = 0;
    char observation[256];
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    if (!join_desk_wifi(CONFIG_LAB09_WIFI_SSID, CONFIG_LAB09_WIFI_PASSWORD)) {
        print_traces();
        finish_harness("incomplete");
        return;
    }
    sample_resources("signed_in");
    signed_ok = http_sign_in("user-a");
    s_probe.arm("I-mute");
    for (k = 0; signed_ok && k < 4; ++k) {
        lab07::ScopedTicket ticket = s_probe.begin(kinds[k], s_probe.session().now_ms());
        int stage;
        int64_t cancel_started = esp_timer_get_time();
        char cancel_line[192];
        int cancel_n;
        s_probe.mute(s_probe.session().now_ms());
        cancel_n = snprintf(cancel_line, sizeof(cancel_line),
                            "LAB09_TRACE schema=lab09-trace/v1 event=cancel_sample seq=%d monotonic_us=%lld "
                            "elapsed_us=%lld scenario=I-mute",
                            k, (long long)esp_timer_get_time(),
                            (long long)(esp_timer_get_time() - cancel_started));
        write_accounted(cancel_line, cancel_n, (int)sizeof(cancel_line));
        stage = s_probe.session().stage();
        perform_exchange(paths[k], HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
        s_probe.complete(ticket, s_exchange.perform_ok, s_exchange.http_status, s_exchange.body,
                         s_exchange.body_len,
                         s_probe.session().now_ms());
        mute_ok = mute_ok && ticket.operation_id != 0 && s_probe.image_bytes() == 0 &&
                  s_probe.session().stage() == stage && s_probe.session().display_power() == 0 &&
                  s_probe.session().terminal_count_for(ticket.operation_id) == 1 &&
                  (strcmp(s_probe.session().terminal_for(ticket.operation_id), "stale") == 0 ||
                   strcmp(s_probe.session().terminal_for(ticket.operation_id), "cancelled") == 0);
        s_probe.unmute(s_probe.session().now_ms());
        s_probe.sign_in("user-a", s_probe.session().now_ms());
    }
    if (signed_ok) {
        lab07::ScopedTicket ticket;
        s_probe.arm("I-signout");
        ticket = s_probe.begin("profile", s_probe.session().now_ms());
        s_probe.sign_out(s_probe.session().now_ms());
        perform_exchange("/profile", HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
        s_probe.complete(ticket, s_exchange.perform_ok, s_exchange.http_status, s_exchange.body,
                         s_exchange.body_len,
                         s_probe.session().now_ms());
        signout_ok = s_probe.session().stage() == lab07::kStageSignedOut &&
                     s_probe.session().terminal_count_for(ticket.operation_id) == 1 && s_probe.image_bytes() == 0;
        wipe_handle();
        if (http_sign_in("user-a")) {
            uint32_t generation_a;
            int applied_media;
            ticket = s_probe.begin("media", s_probe.session().now_ms());
            generation_a = ticket.generation;
            s_probe.arm("I-switch");
            perform_exchange("/media", HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
            s_media_held = s_exchange;
            wipe_handle();
            if (http_sign_in("user-b")) {
                int stage_b = s_probe.session().stage();
                uint32_t generation_b = s_probe.session().generation();
                applied_media = s_probe.complete(ticket, s_media_held.perform_ok, s_media_held.http_status,
                                                 s_media_held.body, s_media_held.body_len,
                                                 s_probe.session().now_ms());
                switch_ok = s_media_held.perform_ok == 1 && s_media_held.http_status == 200 &&
                            s_media_held.body_len == lab09::kImageBytes && applied_media == 0 &&
                            generation_b != generation_a && s_probe.image_bytes() == 0 &&
                            s_probe.applied_visible() == 0 && s_probe.session().stage() == stage_b &&
                            s_probe.session().generation() == generation_b &&
                            s_probe.session().terminal_count_for(ticket.operation_id) == 1 &&
                            strcmp(s_probe.session().terminal_for(ticket.operation_id), "success") != 0;
            }
        }
        wipe_handle();
        if (http_sign_in("user-a")) {
            s_probe.arm("I-push");
            push_ok = fetch("inbox", "/inbox", &s_exchange) && s_probe.duplicate_cards() == 0 &&
                      s_probe.card_count() == 12;
            perform_exchange("/profile", HTTP_METHOD_POST, "{\"user_id\":\"user-b\"}", 20, s_handle, &s_exchange);
            confused = observe_user_b();
            s_probe.arm("I-expire-client");
            {
                lab07::ScopedTicket ticket = s_probe.begin("playback", s_probe.session().now_ms());
                int stage = s_probe.session().stage();
                int expire_status;
                perform_exchange("/session/expire", HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
                expire_status = s_exchange.http_status;
                s_probe.complete(ticket, s_exchange.perform_ok, s_exchange.http_status,
                                 s_exchange.body, s_exchange.body_len,
                                 s_probe.session().now_ms());
                expire_ok = s_probe.image_bytes() >= 0 && s_probe.session().stage() == stage &&
                            s_probe.session().terminal_count_for(ticket.operation_id) == 1 &&
                            strcmp(s_probe.session().terminal_for(ticket.operation_id), "success") != 0 &&
                            s_probe.applied_visible() == 0;
                write_accounted(observation, lab07::format_scoped_observation("I-expire-http", "none", expire_status, 0, 0, observation, (int)sizeof(observation)),
                                (int)sizeof(observation));
            }
        }
    }
    print_traces();
    write_accounted(observation, lab07::format_scoped_observation("I-confused", "none", 0, confused < 0 ? 0 : confused, 0, observation, (int)sizeof(observation)),
                    (int)sizeof(observation));
    wipe_handle();
    if (http_sign_in("user-a")) {
        perform_exchange("/session/mute", HTTP_METHOD_POST, "{\"mode\":\"revoke-on-mute\"}", 28, s_handle,
                         &s_exchange);
        perform_exchange("/inbox", HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
        revoke_status = s_exchange.http_status;
    }
    wipe_handle();
    if (http_sign_in("user-a")) {
        perform_exchange("/session/mute", HTTP_METHOD_POST, "{\"mode\":\"leave-server-session\"}", 32, s_handle,
                         &s_exchange);
        perform_exchange("/inbox", HTTP_METHOD_POST, "{}", 2, s_handle, &s_exchange);
        leave_status = s_exchange.http_status;
    }
    write_accounted(observation, lab07::format_scoped_observation("I-revoke-branches", "revoke-on-mute", revoke_status, 0, revoke_status == 401 ? 1 : 0, observation, (int)sizeof(observation)),
                    (int)sizeof(observation));
    write_accounted(observation, lab07::format_scoped_observation("I-revoke-branches", "leave-server-session", leave_status, 0, leave_status == 200 ? 0 : 1, observation, (int)sizeof(observation)),
                    (int)sizeof(observation));
    s_probe.cleanup();
    wipe_handle();
    memset(s_exchange.body, 0, sizeof(s_exchange.body));
    if (bsp_display_lock(1000)) {
        render_properties();
        bsp_display_unlock();
    }
    if (signed_ok && mute_ok && signout_ok && switch_ok && push_ok && expire_ok) {
        finish_harness("complete");
        return;
    }
    finish_harness("incomplete");
}
#endif

} /* namespace */

extern "C" void app_main(void)
{
    lv_display_t *display = bsp_display_start();
    bool ready;
    if (display == nullptr) {
        printf("LAB09_INIT_FAILED status=display_start_failed\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        printf("LAB09_INIT_FAILED status=display_lock_failed\n");
        return;
    }
    ready = build_screen();
    bsp_display_unlock();
    if (!ready) {
        printf("LAB09_INIT_FAILED status=display_start_failed\n");
        return;
    }
#ifndef CONFIG_LAB09_SCRIPTED
    if (xTaskCreate(worker, "lab09_http", 8192, nullptr, 5, &s_worker) != pdPASS) {
        printf("LAB09_INIT_FAILED status=display_start_failed\n");
        return;
    }
    printf("LAB09_READY build=%s mode=manual fixture=lab09-session/v1 harness=0\n", CONFIG_LAB09_BUILD_ID);
#else
    printf("LAB09_READY build=%s mode=scripted fixture=lab09-session/v1 harness=1\n", CONFIG_LAB09_BUILD_ID);
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
