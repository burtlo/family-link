/* LAB-03/v1: LAB-02 bounded metadata, three retained card roots, one media owner.
 * Only the LVGL timer/callbacks mutate UI. Media and page workers have no handles.
 */
#include "media_core.h"
#include "fixture_hashes.h"
#include "wifi.h"
#include "lab02/inbox_model.h"
#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#if __has_include("lab03_build_id.h")
#include "lab03_build_id.h"
#else
#define LAB03_BUILD_ID CONFIG_LAB03_BUILD_ID
#endif
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifndef CONFIG_LAB03_SCRIPTED
#define CONFIG_LAB03_SCRIPTED 0
#endif
#define TRACE_DEPTH 256
#define CMD_DEPTH 16
#define UPDATE_MS 100
#define SUSTAIN_MS 600000

typedef struct {
    const char *event;
    int64_t t;
    uint32_t op, session, pos, progress, value, aux;
    char msg;
} trace_t;
typedef struct {
    uint32_t op, session;
    char msg;
} command_t;
typedef struct {
    unsigned page;
    uint32_t session;
} page_command_t;
typedef enum { IDLE,
               FETCHING,
               PLAYING,
               PAUSED,
               FAILED,
               STOPPED } stage_t;
typedef struct {
    uint32_t op, session, pos, rendered, count, sequence;
    char msg;
    stage_t stage;
    uint16_t pixels[120 * 120];
} snapshot_t;
static QueueHandle_t traces, commands, pages;
static SemaphoreHandle_t snapshot_lock, page_lock;
static snapshot_t *snapshot;
static lab02_inbox_model_t *inbox, *next_page;
static bool page_ready;
static uint32_t page_session;
static atomic_uint epoch = 0, session_id = 1, trace_drops = 0;
static atomic_bool paused = false;
enum { PH_IDLE,
       PH_AUDIO,
       PH_SKETCH,
       PH_PLAYING,
       PH_PAUSED,
       PH_TERMINAL };
static atomic_uint worker_phase, worker_op, worker_position, worker_outcome;
static unsigned applied_pages;
static uint16_t *raster, *display_pixels;
static uint8_t *audio, *sketch;
static atomic_uint alloc_attempts, alloc_failures;
static TaskHandle_t media_task, page_task;
static lv_obj_t *cards[3], *labels[3], *image, *status_label;
static lv_image_dsc_t image_dsc;
static int focus;
static unsigned ui_sequence, last_rendered, ui_op;
static char active_media_msg;
static atomic_uint runtime_alloc_failures;
static int64_t started_us;
static uint32_t worker_session;
static char presented_msg;
static unsigned presented_count;
static stage_t presented_stage;

static void trace(const char *event, uint32_t op, char msg, uint32_t pos,
                  uint32_t progress, uint32_t value, uint32_t aux)
{
    trace_t row = {.event = event, .t = esp_timer_get_time(), .op = op, .session = (media_task && xTaskGetCurrentTaskHandle() == media_task) ? worker_session : atomic_load(&session_id), .msg = msg, .pos = pos, .progress = progress, .value = value, .aux = aux};
    if (xQueueSend(traces, &row, 0) != pdPASS)
        atomic_fetch_add(&trace_drops, 1);
}
static void logger(void *arg)
{
    (void)arg;
    trace_t r;
    for (;;)
        if (xQueueReceive(traces, &r, portMAX_DELAY) == pdPASS) {
            printf("LAB03_TRACE {\"event\":\"%s\",\"t_us\":%lld,\"operation_id\":%lu,"
                   "\"session_id\":%lu,\"generation\":%lu,\"message_id\":\"%c\","
                   "\"position_ms\":%lu,\"rendered\":%lu,\"value\":%lu,\"aux\":%lu}\n",
                   r.event, (long long)r.t, (unsigned long)r.op, (unsigned long)r.session,
                   (unsigned long)r.op, r.msg ? r.msg : '-', (unsigned long)r.pos,
                   (unsigned long)r.progress, (unsigned long)r.value, (unsigned long)r.aux);
        }
}
static void allocation_failed(size_t bytes, uint32_t caps, const char *function)
{
    (void)bytes;
    (void)caps;
    (void)function;
    atomic_fetch_add(&runtime_alloc_failures, 1);
}
static void *allocate(size_t bytes)
{
    atomic_fetch_add(&alloc_attempts, 1);
    void *p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        atomic_fetch_add(&alloc_failures, 1);
    return p;
}
static bool current(command_t cmd)
{
    return cmd.op == atomic_load(&epoch) && cmd.session == atomic_load(&session_id);
}
static bool publish(command_t cmd, stage_t stage, uint32_t pos, unsigned rendered, unsigned count)
{
    if (!current(cmd)) {
        trace("stale_rejected", cmd.op, cmd.msg, pos, rendered, 0, 0);
        return false;
    }
    if (xSemaphoreTake(snapshot_lock, pdMS_TO_TICKS(2)) != pdTRUE) {
        trace("publish_deferred", cmd.op, cmd.msg, pos, rendered, 0, 0);
        return false;
    }
    snapshot->op = cmd.op;
    snapshot->session = cmd.session;
    snapshot->msg = cmd.msg;
    snapshot->stage = stage;
    snapshot->pos = pos;
    snapshot->rendered = rendered;
    snapshot->count = count;
    snapshot->sequence++;
    memcpy(snapshot->pixels, raster, L3_FRAME_BYTES);
    xSemaphoreGive(snapshot_lock);
    return true;
}
static bool digest_matches(const uint8_t *data, size_t len, const char *hash)
{
    uint8_t digest[32];
    char hex[65];
    if (mbedtls_sha256(data, len, digest, 0) != 0)
        return false;
    for (unsigned i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return strcmp(hex, hash) == 0;
}
/* Whole bounded media prefetch: network wait never runs in the UI or audio loop.
 * Each blocking read has a 500ms socket timeout and a 15s operation deadline.
 */
static void resources(command_t cmd, uint32_t pos, unsigned rendered);
static int fetch(command_t cmd, const char *asset, uint8_t *buf, size_t cap)
{
    char url[240], op[16];
    snprintf(url, sizeof(url), "%s/media/%c/%s", CONFIG_LAB03_SERVER_URL, cmd.msg, asset);
    snprintf(op, sizeof(op), "%lu", (unsigned long)cmd.op);
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 3000, .buffer_size = 1024};
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    atomic_store(&worker_phase, asset[0] == 'a' ? PH_AUDIO : PH_SKETCH);
    trace(asset[0] == 'a' ? "audio_fetch_start" : "sketch_fetch_start", cmd.op, cmd.msg, 0, 0, 0, 0);
    if (!http)
        return -1;
    esp_http_client_set_header(http, "X-Operation-ID", op);
    int len = -1;
    int64_t deadline = esp_timer_get_time() + 15000000;
    if (esp_http_client_open(http, 0) == ESP_OK) {
        esp_http_client_set_timeout_ms(http, 500);
        int64_t total = esp_http_client_fetch_headers(http);
        resources(cmd, 0, 0);
        if (total >= 0 && total <= (int64_t)cap && esp_http_client_get_status_code(http) == 200) {
            len = 0;
            while (len < total && current(cmd) && esp_timer_get_time() < deadline) {
                int n = esp_http_client_read(http, (char *)buf + len,
                                             (int)((total - len) > 1024 ? 1024 : (total - len)));
                if (n == -ESP_ERR_HTTP_EAGAIN) {
                    trace("network_read_wait", cmd.op, cmd.msg, 0, 0, len, 0);
                    continue; // Same HTTP body; retain bytes and the bounded operation deadline.
                }
                if (n <= 0) {
                    len = -1;
                    break;
                }
                len += n;
            }
            if (len != total || !current(cmd))
                len = -1;
        }
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    trace(asset[0] == 'a' ? "audio_fetch_end" : "sketch_fetch_end", cmd.op, cmd.msg, 0, 0,
          len >= 0 ? (uint32_t)len : 0, len < 0);
    return len;
}
static void resources(command_t cmd, uint32_t pos, unsigned rendered)
{
    trace("heap_total", cmd.op, cmd.msg, pos, rendered, heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    trace("internal_heap", cmd.op, cmd.msg, pos, rendered,
          heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    trace("psram_heap", cmd.op, cmd.msg, pos, rendered,
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    trace("heap_min_free", cmd.op, cmd.msg, pos, rendered,
          heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
          heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
    trace("worker_stack", cmd.op, cmd.msg, pos, rendered, uxTaskGetStackHighWaterMark(NULL), 0);
    trace("allocation", cmd.op, cmd.msg, pos, rendered, alloc_attempts, alloc_failures);
    trace("runtime_allocation_failures", cmd.op, cmd.msg, pos, rendered, atomic_load(&runtime_alloc_failures), 0);
    trace("image_counts", cmd.op, cmd.msg, pos, rendered, 3, 0);
    trace("resident_bytes", cmd.op, cmd.msg, pos, rendered, 3 * L3_FRAME_BYTES, L3_AUDIO_CAP + L3_SKETCH_CAP);
}
static void media_worker(void *arg)
{
    (void)arg;
    esp_codec_dev_handle_t codec = bsp_audio_codec_speaker_init();
    command_t cmd, pending_cmd = {0};
    bool pending = false;
    uint32_t pending_pos = 0;
    unsigned pending_rendered = 0, pending_count = 0;
    stage_t pending_stage = IDLE;
    for (;;) {
        if (xQueueReceive(commands, &cmd, pdMS_TO_TICKS(20)) != pdPASS) {
            if (pending && current(pending_cmd))
                pending = !publish(pending_cmd, pending_stage, pending_pos, pending_rendered, pending_count);
            else
                pending = false;
            continue;
        }
        pending = false;
        worker_session = cmd.session;
        atomic_store(&worker_phase, PH_IDLE);
        atomic_store(&worker_position, 0);
        atomic_store(&worker_op, cmd.op);
        unsigned rendered = 0;
        uint32_t pos = 0;
        uint16_t count = 0;
        bool opened = false;
        const char *terminal = "terminal_failed";
        trace("worker_start", cmd.op, cmd.msg, 0, 0, cmd.session, 0);
        if (!current(cmd)) {
            terminal = "terminal_cancelled";
            goto done;
        }
        memset(raster, 0xff, L3_FRAME_BYTES);
        publish(cmd, FETCHING, 0, 0, 0);
        trace("network_buffering_start", cmd.op, cmd.msg, 0, 0, 0, 0);
        int n = fetch(cmd, "audio", audio, L3_AUDIO_CAP);
        if (!current(cmd)) {
            terminal = "terminal_cancelled";
            goto done;
        }
        if (n != L3_AUDIO_CAP || !digest_matches(audio, n, L3_HASH_TONE_PCM))
            goto done;
        if (cmd.msg != 'C') {
            n = fetch(cmd, "sketch", sketch, L3_SKETCH_CAP);
            if (!current(cmd)) {
                terminal = "terminal_cancelled";
                goto done;
            }
            const char *hash = (cmd.msg == 'B' || cmd.msg == 'F') ? L3_HASH_DENSE_SKETCH : L3_HASH_SHORT_SKETCH;
            int64_t decode = esp_timer_get_time();
            bool valid = n >= 0 && digest_matches(sketch, n, hash) && l3_validate_sketch(sketch, n, &count);
            trace("decode_us", cmd.op, cmd.msg, 0, 0, (uint32_t)(esp_timer_get_time() - decode), 0);
            if (!valid)
                goto done;
        }
        trace("network_buffering_end", cmd.op, cmd.msg, 0, 0, 0, 0);
        trace("media_ready", cmd.op, cmd.msg, 0, 0, count, L3_AUDIO_CAP);
        if (!codec)
            goto done;
        esp_codec_dev_sample_info_t fmt = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16};
        if (esp_codec_dev_open(codec, &fmt) != ESP_CODEC_DEV_OK)
            goto done;
        opened = true;
        esp_codec_dev_set_out_vol(codec, 35);
        esp_codec_dev_set_out_mute(codec, false);
        bool was_paused = false, pause_published = false;
        int64_t last_publish = 0, last_resource = 0;
        for (unsigned offset = 0; offset < L3_AUDIO_CAP && current(cmd);) {
            bool pause = atomic_load(&paused);
            if (pause) {
                if (!was_paused) {
                    esp_codec_dev_set_out_mute(codec, true);
                    trace("pause_worker_effect", cmd.op, cmd.msg, pos, rendered, 0, 0);
                    atomic_store(&worker_phase, PH_PAUSED);
                    was_paused = true;
                    pause_published = false;
                }
                if (!pause_published)
                    pause_published = publish(cmd, PAUSED, pos, rendered, count);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (was_paused) {
                esp_codec_dev_set_out_mute(codec, false);
                trace("resume_worker_effect", cmd.op, cmd.msg, pos, rendered, 0, 0);
                was_paused = false;
            }
            int64_t busy = esp_timer_get_time();
            if (esp_codec_dev_write(codec, audio + offset, 640) != ESP_CODEC_DEV_OK)
                goto done;
            if (!offset)
                trace("first_codec_write", cmd.op, cmd.msg, 0, 0, 0, 0);
            offset += 640;
            pos = offset / 32;
            atomic_store(&worker_position, pos);
            atomic_store(&worker_phase, PH_PLAYING); // submitted PCM position; DAC/acoustic delay unknown
            int64_t raster_start = esp_timer_get_time();
            unsigned work = l3_render(raster, sketch, count, &rendered, pos);
            unsigned lo = rendered, hi = count;
            while (lo < hi) {
                unsigned mid = (lo + hi) / 2;
                if (l3_due(sketch, mid) <= pos)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            unsigned due = lo;
            trace("raster_us", cmd.op, cmd.msg, pos, rendered, (uint32_t)(esp_timer_get_time() - raster_start), work);
            trace("renderer_backlog", cmd.op, cmd.msg, pos, rendered, due - rendered,
                  due > rendered ? pos - l3_due(sketch, rendered) : 0);
            trace("worker_busy_us", cmd.op, cmd.msg, pos, rendered, (uint32_t)(esp_timer_get_time() - busy), 0);
            int64_t now = esp_timer_get_time();
            if (now - last_publish >= UPDATE_MS * 1000 || offset == L3_AUDIO_CAP) {
                publish(cmd, PLAYING, pos, rendered, count);
                last_publish = now;
            }
            if (now - last_resource >= 1000000) {
                resources(cmd, pos, rendered);
                last_resource = now;
            }
        }
        terminal = current(cmd) ? "terminal_completed" : "terminal_cancelled";
    done:
        if (opened) {
            esp_codec_dev_set_out_mute(codec, true);
            esp_codec_dev_close(codec);
        }
        if (!current(cmd))
            terminal = "terminal_cancelled";
        trace(terminal, cmd.op, cmd.msg, pos, rendered, cmd.session, 0);
        if (current(cmd)) {
            pending_cmd = cmd;
            pending_pos = pos;
            pending_rendered = rendered;
            pending_count = count;
            pending_stage = strcmp(terminal, "terminal_failed") == 0 ? FAILED : STOPPED;
            pending = !publish(cmd, pending_stage, pos, rendered, count);
        } else
            trace("stale_rejected", cmd.op, cmd.msg, pos, rendered, cmd.session, 0);
        resources(cmd, pos, rendered);
        atomic_store(&worker_outcome, strcmp(terminal, "terminal_completed") == 0 ? 1 : strcmp(terminal, "terminal_cancelled") == 0 ? 2
                                                                                                                                    : 3);
        atomic_store(&worker_phase, PH_TERMINAL);
    }
}
static void page_worker(void *arg)
{
    (void)arg;
    page_command_t request;
    char *body = allocate(4096);
    for (;;)
        if (xQueueReceive(pages, &request, portMAX_DELAY) == pdPASS) {
            uint32_t owner = request.session;
            unsigned page = request.page;
            if (owner != atomic_load(&session_id)) {
                trace("page_stale_rejected", 0, 0, 0, 0, owner, 0);
                continue;
            }
            char url[240];
            snprintf(url, sizeof(url), "%s/inbox?page=%u", CONFIG_LAB03_SERVER_URL, page);
            esp_http_client_config_t cfg = {.url = url, .timeout_ms = 500};
            esp_http_client_handle_t http = esp_http_client_init(&cfg);
            int n = -1;
            if (http && body && esp_http_client_open(http, 0) == ESP_OK) {
                int64_t total = esp_http_client_fetch_headers(http);
                if (total > 0 && total < 4096 && esp_http_client_get_status_code(http) == 200)
                    n = esp_http_client_read_response(http, body, (int)total);
            }
            if (http) {
                esp_http_client_close(http);
                esp_http_client_cleanup(http);
            }
            if (n > 0 && owner == atomic_load(&session_id) && xSemaphoreTake(page_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                page_session = owner;
                page_ready = owner == atomic_load(&session_id) && lab02_inbox_model_parse(body, n, next_page);
                xSemaphoreGive(page_lock);
                trace("page_ready", 0, 0, 0, 0, page, owner);
            } else
                trace("page_failed", 0, 0, 0, 0, page, owner);
        }
}
static char focused(void)
{
    const lab02_inbox_item_t *it = lab02_inbox_model_item(inbox, focus);
    return it ? it->message_id[0] : 'A';
}
static void bind_cards(void)
{
    for (int s = 0; s < 3; s++) {
        int idx = focus + s - 1;
        const lab02_inbox_item_t *it = lab02_inbox_model_item(inbox, idx);
        lv_obj_set_pos(cards[s], (s - 1) * 144 + 94, 26);
        if (it) {
            lv_obj_remove_flag(cards[s], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(labels[s], it->sender_name);
        } else
            lv_obj_add_flag(cards[s], LV_OBJ_FLAG_HIDDEN);
    }
}
static void move_focus(int idx)
{
    if (idx < 0 || idx >= inbox->count)
        return;
    focus = idx;
    bind_cards();
    trace("focus", atomic_load(&epoch), focused(), 0, 0, idx, 0);
}
static void stop_media(const char *reason)
{
    uint32_t op = atomic_load(&epoch);
    trace(reason, op, active_media_msg, 0, 0, 0, 0);
    atomic_fetch_add(&epoch, 1);
    atomic_store(&paused, false);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label, "Stopped");
    trace("stop_ui_effect", op, active_media_msg, 0, 0, 0, 0);
}
static void play(char msg)
{
    command_t cmd = {.op = atomic_load(&epoch) + 1, .session = atomic_load(&session_id), .msg = msg};
    // Queue acceptance and publication occur in the sole UI owner.
    if (uxQueueSpacesAvailable(commands) == 0) {
        trace("intent_rejected", cmd.op, msg, 0, 0, 0, 0);
        return;
    }
    uint32_t previous = atomic_load(&epoch);
    char previous_msg = active_media_msg;
    if (previous)
        trace("switch_request", previous, previous_msg, 0, 0, cmd.op, 0);
    atomic_store(&epoch, cmd.op);
    atomic_store(&paused, false);
    active_media_msg = msg;
    trace("intent_play", cmd.op, msg, 0, 0, 0, 0);
    if (xQueueSend(commands, &cmd, 0) != pdPASS) {
        trace("intent_rejected", cmd.op, msg, 0, 0, 0, 0);
        return;
    }
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status_label, "Loading media");
    if (previous)
        trace("switch_ui_effect", previous, previous_msg, 0, 0, cmd.op, 0);
}
static void pause_media(bool value)
{
    atomic_store(&paused, value);
    trace(value ? "pause_request" : "resume_request", atomic_load(&epoch), active_media_msg, 0, 0, 0, 0);
}
static void action(lv_event_t *e)
{
    int64_t begin = esp_timer_get_time();
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    if (which == 0)
        move_focus((focus + 1) % inbox->count);
    if (which == 1)
        play(focused());
    if (which == 2)
        pause_media(!atomic_load(&paused));
    if (which == 3)
        stop_media("stop_request");
    if (which == 4) {
        stop_media("signout_request");
        atomic_fetch_add(&session_id, 1);
    }
    trace("callback_us", atomic_load(&epoch), focused(), 0, 0, esp_timer_get_time() - begin, 0);
}
static void scripted(uint32_t ms)
{
#if CONFIG_LAB03_SCRIPTED
    static unsigned step = 0, rapid = 0, sustain = 0, page_before = 0;
    static uint32_t entered = 0, rapid_next = 0, sustained_start = 0;
    unsigned op = atomic_load(&worker_op), phase = atomic_load(&worker_phase);
    unsigned confirmed_op = atomic_load(&worker_op);
    unsigned position = atomic_load(&worker_position), current_op = atomic_load(&epoch);
    bool owns = op == current_op && op == confirmed_op;
    bool settled = phase == PH_TERMINAL && uxQueueMessagesWaiting(commands) == 0;
    bool advance = false;
    if (step >= 29)
        return;
    if ((step < 25 && ms - entered > 25000) || (step == 27 && ms - entered > 5000)) {
        stop_media("stop_request");
        trace("harness_failed", current_op, active_media_msg, 0, 0, step, phase);
        step = 30;
        return;
    }
    switch (step) {
        case 0:
            if (ms >= 500) {
                move_focus(0);
                play('A');
                advance = true;
            }
            break;
        case 1:
            if (owns && phase == PH_PLAYING && position >= 600) {
                pause_media(true);
                advance = true;
            }
            break;
        case 2:
            if (owns && phase == PH_PAUSED)
                advance = true;
            break;
        case 3:
            if (ms - entered >= 600) {
                pause_media(false);
                advance = true;
            }
            break;
        case 4:
            if (owns && phase == PH_PLAYING && position >= 1200) {
                move_focus(1);
                play('B');
                advance = true;
            }
            break;
        case 5:
            if (owns && phase == PH_PLAYING && position >= 400) {
                rapid_next = ms;
                advance = true;
            }
            break;
        case 6:
            if (rapid < 60 && ms >= rapid_next) {
                char msg = rapid % 3 == 1 ? 'B' : 'A';
                move_focus(msg == 'B' ? 1 : 0);
                play(msg);
                trace("rapid_switch", atomic_load(&epoch), msg, 0, 0, rapid++, 0);
                rapid_next = ms + 80;
            }
            if (rapid == 60) {
                stop_media("stop_request");
                advance = true;
            }
            break;
        case 7:
            if (settled) {
                play('E');
                advance = true;
            }
            break;
        case 8:
            if (owns && phase == PH_AUDIO)
                advance = true;
            break;
        case 9:
            if (ms - entered >= 40) {
                stop_media("cancel_audio_request");
                advance = true;
            }
            break;
        case 10:
            if (settled) {
                play('F');
                advance = true;
            }
            break;
        case 11:
            if (owns && phase == PH_SKETCH)
                advance = true;
            break;
        case 12:
            if (ms - entered >= 40) {
                stop_media("cancel_sketch_request");
                advance = true;
            }
            break;
        case 13:
            if (settled) {
                move_focus(0);
                play('A');
                advance = true;
            }
            break;
        case 14:
            if (owns && phase == PH_PLAYING && position >= 200) {
                page_command_t page = {.page = 1, .session = atomic_load(&session_id)};
                page_before = applied_pages;
                if (xQueueSend(pages, &page, 0) == pdPASS)
                    advance = true;
                else
                    trace("page_rejected", 0, 0, 0, 0, 0, 0);
            }
            break;
        case 15:
            if (applied_pages > page_before) {
                move_focus(1);
                play('B');
                advance = true;
            }
            break;
        case 16:
            if (owns && phase == PH_PLAYING && position >= 200) {
                stop_media("signout_request");
                atomic_fetch_add(&session_id, 1);
                advance = true;
            }
            break;
        case 17:
            if (settled) {
                play('D');
                advance = true;
            }
            break;
        case 18:
            if (owns && phase == PH_TERMINAL && atomic_load(&worker_outcome) == 3) {
                move_focus(0);
                play('C');
                advance = true;
            }
            break;
        case 19:
            if (owns && phase == PH_TERMINAL && atomic_load(&worker_outcome) == 1) {
                move_focus(1);
                play('B');
                advance = true;
            }
            break;
        case 20:
            if (owns && phase == PH_PLAYING && position >= 600) {
                pause_media(true);
                advance = true;
            }
            break;
        case 21:
            if (owns && phase == PH_PAUSED) {
                move_focus(0);
                advance = true;
            }
            break;
        case 22:
            if (ms - entered >= 600) {
                pause_media(false);
                advance = true;
            }
            break;
        case 23:
            if (owns && phase == PH_PLAYING && position >= 1000) {
                move_focus(1);
                advance = true;
            }
            break;
        case 24:
            if (owns && phase == PH_TERMINAL && atomic_load(&worker_outcome) == 1)
                advance = true;
            break;
        case 25:
            sustained_start = ms;
            trace("sustained_begin", current_op, active_media_msg, 0, 0, SUSTAIN_MS, 0);
            advance = true;
            break;
        case 26:
            if (ms - sustained_start >= SUSTAIN_MS) {
                stop_media("stop_request");
                advance = true;
            } else if (settled && owns) {
                char msg = sustain % 2 ? 'A' : 'B';
                move_focus(msg == 'B' ? 1 : 2);
                play(msg);
                trace("sustained_cycle", atomic_load(&epoch), msg, 0, 0, sustain++, 0);
            }
            break;
        case 27:
            if (settled)
                advance = true;
            break;
        case 28:
            trace("harness_done", current_op, active_media_msg, 0, 0, atomic_load(&trace_drops), sustain);
            advance = true;
            break;
    }
    if (advance) {
        trace("scenario_step", atomic_load(&epoch), active_media_msg, 0, 0, step, phase);
        step++;
        entered = ms;
    }
#else
    (void)ms;
#endif
}
static void tick(lv_timer_t *timer)
{
    (void)timer;
    int64_t begin = esp_timer_get_time();
    scripted((begin - started_us) / 1000);
    if (xSemaphoreTake(page_lock, 0) == pdTRUE) {
        if (page_ready && page_session != atomic_load(&session_id)) {
            page_ready = false;
            trace("page_stale_rejected", 0, 0, 0, 0, page_session, 0);
        }
        if (page_ready) {
            char keep = focused();
            memcpy(inbox, next_page, sizeof(*inbox));
            page_ready = false;
            for (unsigned i = 0; i < inbox->count; i++)
                if (inbox->items[i].message_id[0] == keep)
                    focus = i;
            applied_pages++;
            bind_cards();
            trace("page_applied", atomic_load(&epoch), keep, 0, 0, inbox->count, 0);
        }
        xSemaphoreGive(page_lock);
    }
    if (xSemaphoreTake(snapshot_lock, 0) == pdTRUE) {
        snapshot_t *s = snapshot;
        if (s->sequence != ui_sequence && s->op == atomic_load(&epoch) && s->session == atomic_load(&session_id)) {
            ui_sequence = s->sequence;
            if (ui_op != s->op) {
                ui_op = s->op;
                last_rendered = 0;
            }
            memcpy(display_pixels, s->pixels, L3_FRAME_BYTES);
            lv_obj_set_parent(image, cards[1]);
            if (focused() == s->msg && s->count) {
                lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
                lv_obj_invalidate(image);
                if (last_rendered == 0 && s->rendered)
                    trace("first_frame_ui", s->op, s->msg, s->pos, s->rendered, 0, 0);
                // One bounded batch; host expands per-stroke lateness at this presentation timestamp.
                if (s->rendered > last_rendered)
                    trace("reveal_batch", s->op, s->msg, s->pos, last_rendered, s->rendered, s->count);
                last_rendered = s->rendered;
            } else
                lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
            if (presented_stage == PAUSED && s->stage == PLAYING)
                trace("resume_ui_effect", s->op, s->msg, s->pos, s->rendered, 0, 0);
            presented_msg = s->msg;
            presented_count = s->count;
            presented_stage = s->stage;
            static const char *names[] = {"Idle", "Loading media", "Playing", "Paused", "Asset failed", "Stopped"};
            lv_label_set_text(status_label, names[s->stage]);
            trace("ui_reconcile", s->op, s->msg, s->pos, s->rendered, s->stage, s->count);
            if (s->stage == PAUSED)
                trace("pause_ui_effect", s->op, s->msg, s->pos, s->rendered, 0, 0);
        }
        xSemaphoreGive(snapshot_lock);
    }
    if (presented_msg == focused() && presented_count && ui_op == atomic_load(&epoch))
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    static unsigned ticks = 0;
    if (++ticks % 50 == 0) {
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        trace("lvgl_heap", atomic_load(&epoch), focused(), 0, 0, m.max_used, m.free_biggest_size);
        trace("ui_stack", atomic_load(&epoch), focused(), 0, 0, uxTaskGetStackHighWaterMark(NULL), 0);
        trace("trace_loss", 0, 0, 0, 0, atomic_load(&trace_drops), 0);
    }
    trace("callback_us", atomic_load(&epoch), focused(), 0, 0, esp_timer_get_time() - begin, 0);
}
void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(allocation_failed));
    traces = xQueueCreate(TRACE_DEPTH, sizeof(trace_t));
    commands = xQueueCreate(CMD_DEPTH, sizeof(command_t));
    pages = xQueueCreate(2, sizeof(page_command_t));
    snapshot_lock = xSemaphoreCreateMutex();
    page_lock = xSemaphoreCreateMutex();
    if (!traces || !commands || !pages || !snapshot_lock || !page_lock) {
        printf("LAB03_INIT_FAILED queues\n");
        return;
    }
    if (xTaskCreate(logger, "l3_trace", 4096, NULL, 1, NULL) != pdPASS) {
        printf("LAB03_INIT_FAILED logger\n");
        return;
    }
    snapshot = allocate(sizeof(*snapshot));
    raster = allocate(L3_FRAME_BYTES);
    display_pixels = allocate(L3_FRAME_BYTES);
    audio = allocate(L3_AUDIO_CAP);
    sketch = allocate(L3_SKETCH_CAP);
    inbox = allocate(sizeof(*inbox));
    next_page = allocate(sizeof(*next_page));
    if (!snapshot || !raster || !display_pixels || !audio || !sketch || !inbox || !next_page) {
        trace("allocation", 0, 0, 0, 0, alloc_attempts, alloc_failures);
        return;
    }
    if (!lab03_board_wifi_connect()) {
        printf("LAB03_INIT_FAILED transport\n");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    if (!bsp_display_start()) {
        printf("LAB03_INIT_FAILED display\n");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000))
        return;
    lv_obj_t *screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (unsigned i = 0; i < 6; i++) {
        snprintf(inbox->items[i].message_id, 24, "%c", 'A' + i);
        snprintf(inbox->items[i].sender_name, 32, "Fixture %c", 'A' + i);
    }
    inbox->count = 6;
    for (unsigned i = 0; i < 3; i++) {
        cards[i] = lv_obj_create(screen);
        lv_obj_set_size(cards[i], 132, 156);
        lv_obj_remove_flag(cards[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(cards[i], 4, 0);
        labels[i] = lv_label_create(cards[i]);
    }
    bind_cards();
    image_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image_dsc.header.w = 120;
    image_dsc.header.h = 120;
    image_dsc.header.stride = 240;
    image_dsc.data_size = L3_FRAME_BYTES;
    image_dsc.data = (const uint8_t *)display_pixels;
    image = lv_image_create(cards[1]);
    lv_image_set_src(image, &image_dsc);
    lv_obj_set_pos(image, 0, 22);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    status_label = lv_label_create(screen);
    lv_obj_set_pos(status_label, 6, 3);
    lv_label_set_text(status_label, "Ready");
    const char *buttons[] = {"Next", "Play", "Pause", "Stop", "Out"};
    for (unsigned i = 0; i < 5; i++) {
        lv_obj_t *b = lv_button_create(screen);
        lv_obj_set_size(b, 60, 38);
        lv_obj_set_pos(b, i * 64, 198);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, buttons[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, action, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    started_us = esp_timer_get_time();
    lv_timer_create(tick, 20, NULL);
    bsp_display_unlock();
    if (xTaskCreate(media_worker, "l3_media", 8192, NULL, 5, &media_task) != pdPASS ||
        xTaskCreate(page_worker, "l3_page", 8192, NULL, 3, &page_task) != pdPASS) {
        printf("LAB03_INIT_FAILED worker\n");
        return;
    }
    printf("LAB03_READY build=%s fixture=lab03-media/v1 harness=%d trace_schema=lab03-trace/v1\n",
           LAB03_BUILD_ID, CONFIG_LAB03_SCRIPTED);
}
