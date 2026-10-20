#include "sdkconfig.h"
#include "lab05_peak_model.h"

#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_LAB05_SCRIPTED
#define LAB05_PEAK_MODE_TEXT "scripted"
#else
#define LAB05_PEAK_MODE_TEXT "manual"
#endif

#if CONFIG_LAB05_PEAK_REPLACE
#define LAB05_PEAK_CONTROL_TEXT "replace"
#else
#define LAB05_PEAK_CONTROL_TEXT "persistent"
#endif

/* Hang bound so a stalled LVGL tick cannot sit forever. It is not the
 * 400 ms cover-down length and it is not a frame-budget pass limit.
 */
#define LAB05_PEAK_ANIM_HANG_MS 2000u
#define LAB05_PEAK_FRAME_CAP 32u

#ifndef CONFIG_LAB05_BUILD_ID
#define CONFIG_LAB05_BUILD_ID "unknown"
#endif

enum {
    LAB05_UI_OVERLAP = 1,
    LAB05_UI_REPLACE,
    LAB05_UI_REPLACE_ALLOC,
    LAB05_UI_ALLOC_FAIL,
    LAB05_UI_STALE,
    LAB05_UI_CANCEL
};

typedef struct {
    uint64_t operation_id;
} lab05_worker_cmd;

typedef struct {
    uint64_t operation_id;
} lab05_page_result;

typedef struct {
    uint8_t code;
} lab05_ui_cmd;

typedef struct {
    int used;
    uint64_t operation_id;
    int lvgl_ok;
    uint32_t lvgl_total_bytes;
    uint32_t lvgl_high_water_bytes;
    uint32_t lvgl_largest_free_bytes;
    uint32_t internal_largest_free_bytes;
    uint32_t psram_largest_free_bytes;
    uint32_t anim_frame_us[LAB05_PEAK_FRAME_CAP];
    uint8_t anim_frame_count;
    uint8_t anim_ran;
    uint8_t anim_wait_timeout;
    uint8_t memory_captured;
} lab05_peak_stored_sample;

static lab05_peak_session s_session;
static lab05_peak_stored_sample s_samples[LAB05_PEAK_MAX_OPERATIONS];
static size_t s_sample_count;
static size_t s_emitted;
static lab05_peak_stored_sample *s_active_sample;
static int64_t s_last_frame_us;
static volatile int s_anim_done;
static TaskHandle_t s_anim_waiter;
static volatile int s_boot_go;
static volatile int s_worker_shutdown;
static volatile int s_audio_exited;
static volatile int s_page_exited;
static int s_audio_started;
static int s_page_started;
static int s_required_ui_failed;
static int s_cleanup_failed;
static int s_audio_running;
static unsigned s_audio_accepted;
static unsigned s_page_accepted;
static unsigned s_audio_overflow;
static unsigned s_page_overflow;
static unsigned s_page_result_overflow;
static unsigned s_manual_overflow;
static int s_steady_view_held;
static int s_incoming_view_held;
static lab05_peak_handle s_steady_view_handle;
static lab05_peak_handle s_incoming_view_handle;

static QueueHandle_t s_audio_q;
static QueueHandle_t s_page_q;
static QueueHandle_t s_page_result_q;
static QueueHandle_t s_manual_q;
static lv_obj_t *s_screen;
static lv_obj_t *s_steady_canvas;
static lv_obj_t *s_incoming;
static lv_obj_t *s_incoming_canvas;
static lv_obj_t *s_labels[LAB05_PEAK_PAGE_RECORDS];
static char s_summary_text[LAB05_PEAK_PAGE_RECORDS][LAB05_PEAK_LABEL_LEN];

static void print_ready_ok(void)
{
    printf("LAB05_PEAK_READY build=%s mode=%s control=%s server=none\n",
           CONFIG_LAB05_BUILD_ID, LAB05_PEAK_MODE_TEXT, LAB05_PEAK_CONTROL_TEXT);
}

static void print_ready_failed(const char *status)
{
    printf("LAB05_PEAK_READY build=%s mode=%s control=%s server=none status=%s\n",
           CONFIG_LAB05_BUILD_ID, LAB05_PEAK_MODE_TEXT, LAB05_PEAK_CONTROL_TEXT, status);
}

static void print_harness_done(int ok)
{
    if (ok) {
        printf("LAB05_PEAK_HARNESS_DONE status=complete control=%s\n", LAB05_PEAK_CONTROL_TEXT);
    } else {
        printf("LAB05_PEAK_HARNESS_DONE status=failed control=%s\n", LAB05_PEAK_CONTROL_TEXT);
    }
}

static const uint8_t *handle_pixels(lab05_peak_handle handle)
{
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        return NULL;
    }
    if (!s_session.images[handle.slot].live || s_session.images[handle.slot].pixels == NULL) {
        return NULL;
    }
    if (s_session.images[handle.slot].generation != handle.generation) {
        return NULL;
    }
    return s_session.images[handle.slot].pixels;
}

static int steady_pixels_ok(void)
{
    return handle_pixels(s_session.steady) != NULL;
}

static void cover_exec(void *var, int32_t value)
{
    int64_t now;
    lv_obj_set_y((lv_obj_t *)var, value);
    now = esp_timer_get_time();
    if (s_active_sample != NULL && s_last_frame_us != 0 &&
        s_active_sample->anim_frame_count < LAB05_PEAK_FRAME_CAP) {
        int64_t delta = now - s_last_frame_us;
        if (delta < 0) {
            delta = 0;
        }
        if (delta > 0xFFFFFFFFLL) {
            delta = 0xFFFFFFFFLL;
        }
        s_active_sample->anim_frame_us[s_active_sample->anim_frame_count++] = (uint32_t)delta;
    }
    s_last_frame_us = now;
}

static void cover_done(lv_anim_t *animation)
{
    (void)animation;
    s_anim_done = 1;
    if (s_anim_waiter != NULL) {
        xTaskNotifyGive(s_anim_waiter);
    }
}

static void publish(QueueHandle_t queue, unsigned *overflow, unsigned *accepted,
                    const char *name, uint64_t operation_id)
{
    lab05_worker_cmd cmd;
    cmd.operation_id = operation_id;
    if (queue == NULL) {
        return;
    }
    if (xQueueSend(queue, &cmd, 0) == pdTRUE) {
        (*accepted)++;
        return;
    }
    (*overflow)++;
    printf("LAB05_PEAK_PUBLISH queue=%s status=rejected overflow_count=%u dropped_count=0 "
           "capacity=%u operation_id=%llu\n",
           name, *overflow, (unsigned)LAB05_PEAK_QUEUE_CAPACITY,
           (unsigned long long)operation_id);
}

static void publish_pair(uint64_t operation_id)
{
    s_audio_running = 1;
    publish(s_audio_q, &s_audio_overflow, &s_audio_accepted, "audio-stop", operation_id);
    publish(s_page_q, &s_page_overflow, &s_page_accepted, "page-apply", operation_id);
}

static lab05_peak_stored_sample *store_sample(uint64_t operation_id)
{
    lab05_peak_stored_sample *sample;
    if (s_sample_count >= LAB05_PEAK_MAX_OPERATIONS) {
        return NULL;
    }
    sample = &s_samples[s_sample_count++];
    memset(sample, 0, sizeof(*sample));
    sample->used = 1;
    sample->operation_id = operation_id;
    return sample;
}

static const lab05_peak_stored_sample *find_sample(uint64_t operation_id)
{
    size_t i;
    for (i = 0; i < s_sample_count; i++) {
        if (s_samples[i].used && s_samples[i].operation_id == operation_id) {
            return &s_samples[i];
        }
    }
    return NULL;
}

static void capture_memory(lab05_peak_stored_sample *sample, const lab05_peak_peak_view *view,
                           int locked)
{
    lv_mem_monitor_t mem;
    unsigned persistent = 0;
    unsigned transient = 0;
    int lvgl_ok = 0;
    memset(&mem, 0, sizeof(mem));
    if (sample == NULL || view == NULL) {
        return;
    }
    if (locked) {
        lv_mem_monitor(&mem);
        lvgl_ok = 1;
    } else if (bsp_display_lock(200)) {
        lv_mem_monitor(&mem);
        lvgl_ok = 1;
        bsp_display_unlock();
    }
    sample->lvgl_ok = lvgl_ok;
    if (lvgl_ok) {
        sample->lvgl_total_bytes = (uint32_t)mem.total_size;
        sample->lvgl_high_water_bytes = (uint32_t)mem.max_used;
        sample->lvgl_largest_free_bytes = (uint32_t)mem.free_biggest_size;
        printf("LAB05_PEAK_LEDGER ledger=lvgl_pool total_bytes=%u high_water_bytes=%u "
               "largest_free_bytes=%u\n",
               (unsigned)sample->lvgl_total_bytes, (unsigned)sample->lvgl_high_water_bytes,
               (unsigned)sample->lvgl_largest_free_bytes);
    } else {
        printf("LAB05_PEAK_LEDGER ledger=lvgl_pool total_bytes=unavailable "
               "high_water_bytes=unavailable largest_free_bytes=unavailable\n");
    }
    sample->internal_largest_free_bytes =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    sample->psram_largest_free_bytes =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    sample->memory_captured = 1;
    if (view->live_images >= 1u) {
        persistent = LAB05_PEAK_IMAGE_BYTES;
    }
    if (view->live_images >= 2u) {
        transient = LAB05_PEAK_IMAGE_BYTES;
    }
    printf("LAB05_PEAK_LEDGER ledger=pixel_transient live_rgb565_count=%u temp_image_bytes=%u "
           "persistent_bytes=%u transient_bytes=%u\n",
           view->live_images, view->pixel_bytes, persistent, transient);
}

static void teardown_incoming_locked(void)
{
    if (s_incoming == NULL) {
        return;
    }
    lv_anim_delete(s_incoming, cover_exec);
    lv_obj_delete(s_incoming);
    s_incoming = NULL;
    s_incoming_canvas = NULL;
}

static void release_incoming_view(void)
{
    if (!s_incoming_view_held) {
        return;
    }
    (void)lab05_peak_release_view(&s_session, s_incoming_view_handle);
    s_incoming_view_held = 0;
}

static void release_steady_view(void)
{
    if (!s_steady_view_held) {
        return;
    }
    (void)lab05_peak_release_view(&s_session, s_steady_view_handle);
    s_steady_view_held = 0;
}

static int apply_page_labels_locked(void)
{
    unsigned i;
    for (i = 0; i < LAB05_PEAK_PAGE_RECORDS; i++) {
        if (s_labels[i] != NULL) {
            lv_label_set_text(s_labels[i], s_summary_text[i]);
        }
    }
    return 0;
}

static void apply_pending_page_results(void)
{
    lab05_page_result result;
    if (s_page_result_q == NULL) {
        return;
    }
    while (xQueueReceive(s_page_result_q, &result, 0) == pdTRUE) {
        if (!bsp_display_lock(50)) {
            if (xQueueSendToFront(s_page_result_q, &result, 0) != pdTRUE) {
                s_page_result_overflow++;
                printf("LAB05_PEAK_PUBLISH queue=page-result status=rejected reason=display_lock "
                       "overflow_count=%u dropped_count=0 capacity=%u operation_id=%llu\n",
                       s_page_result_overflow, (unsigned)LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY,
                       (unsigned long long)result.operation_id);
            } else {
                printf("LAB05_PEAK_PUBLISH queue=page-result status=requeued reason=display_lock "
                       "operation_id=%llu\n",
                       (unsigned long long)result.operation_id);
            }
            return;
        }
        (void)apply_page_labels_locked();
        bsp_display_unlock();
    }
}

static void wait_for_cover(void)
{
    int64_t deadline_us = esp_timer_get_time() + ((int64_t)LAB05_PEAK_ANIM_HANG_MS * 1000);

    apply_pending_page_results();
    while (!s_anim_done && esp_timer_get_time() < deadline_us) {
        int64_t remain_us = deadline_us - esp_timer_get_time();
        uint32_t slice_ms = 20u;
        if (remain_us < 20000) {
            slice_ms = (uint32_t)((remain_us + 999) / 1000);
            if (slice_ms == 0u) {
                slice_ms = 1u;
            }
        }
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(slice_ms)) != 0) {
            break;
        }
        apply_pending_page_results();
    }
}

static int run_cover(const uint8_t *pixels, lab05_peak_handle source, int end_y, unsigned duration_ms,
                     lab05_peak_stored_sample *sample, const lab05_peak_peak_view *view)
{
    lv_anim_t animation;
    if (pixels == NULL || s_screen == NULL || duration_ms == 0u) {
        return -1;
    }
    if (!bsp_display_lock(1000)) {
        return -1;
    }
    teardown_incoming_locked();
    release_incoming_view();
    s_incoming = lv_obj_create(s_screen);
    if (s_incoming == NULL) {
        bsp_display_unlock();
        return -1;
    }
    lv_obj_set_size(s_incoming, LAB05_PEAK_CANVAS_WIDTH, LAB05_PEAK_CANVAS_HEIGHT);
    lv_obj_set_pos(s_incoming, 0, LAB05_PEAK_INCOMING_Y_START);
    lv_obj_set_style_bg_color(s_incoming, lv_color_hex(0x26351C), 0);
    lv_obj_set_style_bg_opa(s_incoming, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_incoming, 0, 0);
    lv_obj_remove_flag(s_incoming, LV_OBJ_FLAG_SCROLLABLE);
    s_incoming_canvas = lv_canvas_create(s_incoming);
    if (s_incoming_canvas == NULL) {
        teardown_incoming_locked();
        bsp_display_unlock();
        return -1;
    }
    lv_canvas_set_buffer(s_incoming_canvas, (void *)pixels, LAB05_PEAK_IMAGE_SIDE,
                         LAB05_PEAK_IMAGE_SIDE, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_incoming_canvas, 8, 8);
    if (lab05_peak_retain_view(&s_session, source) != 0) {
        teardown_incoming_locked();
        bsp_display_unlock();
        return -1;
    }
    s_incoming_view_held = 1;
    s_incoming_view_handle = source;
    capture_memory(sample, view, 1);
    s_anim_waiter = xTaskGetCurrentTaskHandle();
    s_anim_done = 0;
    (void)ulTaskNotifyTake(pdTRUE, 0);
    s_active_sample = sample;
    s_last_frame_us = esp_timer_get_time();
    if (sample != NULL) {
        sample->anim_ran = 1;
    }
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_incoming);
    lv_anim_set_values(&animation, LAB05_PEAK_INCOMING_Y_START, end_y);
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_path_cb(&animation, lv_anim_path_linear);
    lv_anim_set_exec_cb(&animation, cover_exec);
    lv_anim_set_completed_cb(&animation, cover_done);
    lv_anim_start(&animation);
    bsp_display_unlock();
    publish_pair(view->operation_id);
    wait_for_cover();
    if (!s_anim_done) {
        if (sample != NULL) {
            sample->anim_wait_timeout = 1;
        }
        s_required_ui_failed = 1;
    }
    s_anim_waiter = NULL;
    s_active_sample = NULL;
    if (!bsp_display_lock(1000)) {
        /* Canvas still references the buffer. Leave it allocated. */
        s_required_ui_failed = 1;
        return -1;
    }
    teardown_incoming_locked();
    bsp_display_unlock();
    release_incoming_view();
    return s_required_ui_failed ? -1 : 0;
}

static int bind_steady_locked(const uint8_t *pixels)
{
    if (s_screen == NULL || pixels == NULL) {
        return -1;
    }
    if (s_steady_canvas == NULL) {
        s_steady_canvas = lv_canvas_create(s_screen);
        if (s_steady_canvas == NULL) {
            return -1;
        }
        lv_obj_set_pos(s_steady_canvas, 8, 28);
    }
    lv_canvas_set_buffer(s_steady_canvas, (void *)pixels, LAB05_PEAK_IMAGE_SIDE,
                         LAB05_PEAK_IMAGE_SIDE, LV_COLOR_FORMAT_RGB565);
    if (!s_steady_view_held) {
        if (lab05_peak_retain_view(&s_session, s_session.steady) != 0) {
            lv_obj_delete(s_steady_canvas);
            s_steady_canvas = NULL;
            return -1;
        }
        s_steady_view_held = 1;
        s_steady_view_handle = s_session.steady;
    }
    return 0;
}

static int bind_steady_pixels(const uint8_t *pixels)
{
    int rc;
    if (!bsp_display_lock(1000)) {
        return -1;
    }
    rc = bind_steady_locked(pixels);
    bsp_display_unlock();
    return rc;
}

static int drop_steady_view(void)
{
    if (s_steady_canvas == NULL) {
        return 0;
    }
    if (!bsp_display_lock(1000)) {
        s_required_ui_failed = 1;
        return -1;
    }
    lv_obj_delete(s_steady_canvas);
    s_steady_canvas = NULL;
    bsp_display_unlock();
    release_steady_view();
    return 0;
}

static void on_peak(lab05_peak_session *session, const lab05_peak_peak_view *view, void *ctx)
{
    lab05_peak_stored_sample *sample;
    const uint8_t *pixels;
    (void)ctx;
    if (session == NULL || view == NULL) {
        s_required_ui_failed = 1;
        return;
    }
    sample = store_sample(view->operation_id);
    if (sample == NULL) {
        s_required_ui_failed = 1;
        return;
    }
    /* Only overlap and cancel notify while a cover should be drawn.
     * A later scenario must not replay a clock left from the previous cover.
     */
    if ((view->scenario == LAB05_PEAK_SCENARIO_OVERLAP ||
         view->scenario == LAB05_PEAK_SCENARIO_CANCEL) &&
        view->animation_elapsed_ms > 0u) {
        pixels = NULL;
        if (session->has_incoming) {
            pixels = handle_pixels(session->incoming);
        }
        if (pixels == NULL ||
            run_cover(pixels, session->incoming, view->incoming_y, view->animation_elapsed_ms, sample,
                      view) != 0) {
            s_required_ui_failed = 1;
        }
        return;
    }
    pixels = handle_pixels(session->steady);
    if (view->live_images > 0u && pixels != NULL && bind_steady_pixels(pixels) != 0) {
        s_required_ui_failed = 1;
    }
    if (view->scenario == LAB05_PEAK_SCENARIO_REPLACE && view->live_images > 0u) {
        publish_pair(view->operation_id);
    }
    capture_memory(sample, view, 0);
}

static const char *trace_class(lab05_peak_scenario scenario)
{
    if (scenario == LAB05_PEAK_SCENARIO_OVERLAP || scenario == LAB05_PEAK_SCENARIO_REPLACE) {
        return "exploratory";
    }
    if (lab05_peak_scenario_is_experiment_input(scenario)) {
        return "decided";
    }
    return "none";
}

static void print_anim_field(const lab05_peak_stored_sample *sample)
{
    uint8_t i;
    if (sample == NULL || !sample->anim_ran || sample->anim_frame_count == 0u) {
        printf("anim_frame_us=unmeasured");
        return;
    }
    printf("anim_frame_us=");
    for (i = 0; i < sample->anim_frame_count; i++) {
        if (i > 0u) {
            printf(",");
        }
        printf("%u", (unsigned)sample->anim_frame_us[i]);
    }
    printf(" anim_frame_us_count=%u", (unsigned)sample->anim_frame_count);
}

static void emit_operation(const lab05_peak_operation *op)
{
    const lab05_peak_stored_sample *sample;
    if (op == NULL) {
        return;
    }
    sample = find_sample(op->operation_id);
    printf("LAB05_PEAK_TRACE schema=lab05-peak-trace/v1 scenario=%s kind=%s control=%s terminal=%s "
           "operation_id=%llu terminal_count=%u live_image_count=%u class=%s reason=%s "
           "temp_image_bytes=%u ",
           lab05_peak_scenario_name(op->scenario), lab05_peak_op_kind_name(op->kind),
           lab05_peak_control_name(op->control),
           lab05_peak_terminal_name(op->terminal), (unsigned long long)op->operation_id,
           op->terminal_count, op->live_image_count, trace_class(op->scenario), op->reason,
           op->temp_image_bytes);
    if (sample != NULL && lab05_peak_heap_metrics_are_instrumented(sample->memory_captured)) {
        if (sample->lvgl_ok) {
            printf("lvgl_high_water_bytes=%u lvgl_largest_free_bytes=%u ",
                   (unsigned)sample->lvgl_high_water_bytes,
                   (unsigned)sample->lvgl_largest_free_bytes);
        } else {
            printf("lvgl_high_water_bytes=unavailable lvgl_largest_free_bytes=unavailable ");
        }
        printf("internal_largest_free_bytes=%u psram_largest_free_bytes=%u ",
               (unsigned)sample->internal_largest_free_bytes,
               (unsigned)sample->psram_largest_free_bytes);
        print_anim_field(sample);
        printf(" metrics_provenance=instrumented\n");
        return;
    }
    if (sample != NULL) {
        printf("lvgl_high_water_bytes=unmeasured lvgl_largest_free_bytes=unmeasured "
               "internal_largest_free_bytes=unmeasured psram_largest_free_bytes=unmeasured ");
        print_anim_field(sample);
        printf(" metrics_provenance=unmeasured\n");
        return;
    }
    printf("lvgl_high_water_bytes=unmeasured lvgl_largest_free_bytes=unmeasured "
           "internal_largest_free_bytes=unmeasured psram_largest_free_bytes=unmeasured "
           "anim_frame_us=unmeasured metrics_provenance=unmeasured\n");
}

static void emit_new(void)
{
    while (s_emitted < s_session.op_count) {
        emit_operation(&s_session.ops[s_emitted]);
        s_emitted++;
    }
}

static unsigned count_reps(lab05_peak_scenario scenario, lab05_peak_terminal terminal)
{
    size_t i;
    unsigned count = 0;
    for (i = 0; i < s_session.op_count; i++) {
        const lab05_peak_operation *op = &s_session.ops[i];
        if (op->scenario == scenario && op->kind == LAB05_PEAK_OP_REPETITION &&
            op->terminal == terminal && op->terminal_count == 1u) {
            count++;
        }
    }
    return count;
}

static int repetition_ids_unique(lab05_peak_scenario scenario)
{
    size_t i;
    size_t j;
    for (i = 0; i < s_session.op_count; i++) {
        if (s_session.ops[i].scenario != scenario || s_session.ops[i].kind != LAB05_PEAK_OP_REPETITION) {
            continue;
        }
        for (j = i + 1u; j < s_session.op_count; j++) {
            if (s_session.ops[j].scenario == scenario &&
                s_session.ops[j].kind == LAB05_PEAK_OP_REPETITION &&
                s_session.ops[j].operation_id == s_session.ops[i].operation_id) {
                return 0;
            }
        }
    }
    return 1;
}

static int ensure_steady_image(void)
{
    lab05_peak_handle handle;
    const uint8_t *pixels;
    if (steady_pixels_ok()) {
        return bind_steady_pixels(handle_pixels(s_session.steady));
    }
    if (lab05_peak_alloc_image(&s_session, 0u, &handle) != 0) {
        return -1;
    }
    s_session.steady = handle;
    pixels = handle_pixels(handle);
    if (pixels == NULL) {
        return -1;
    }
    return bind_steady_pixels(pixels);
}

static void drain_page_results_bounded(void)
{
    int i;
    for (i = 0; i < 20; i++) {
        UBaseType_t page_waiting = 0;
        UBaseType_t result_waiting = 0;
        apply_pending_page_results();
        if (s_page_q != NULL) {
            page_waiting = uxQueueMessagesWaiting(s_page_q);
        }
        if (s_page_result_q != NULL) {
            result_waiting = uxQueueMessagesWaiting(s_page_result_q);
        }
        if (page_waiting == 0 && result_waiting == 0) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_required_ui_failed = 1;
    printf("LAB05_PEAK_PUBLISH queue=page-result status=rejected reason=display_lock "
           "overflow_count=%u dropped_count=0 capacity=%u\n",
           s_page_result_overflow, (unsigned)LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY);
}

static int run_overlap_reps(unsigned reps, int check_total)
{
    unsigned before = count_reps(LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_TERMINAL_SUCCESS);
    unsigned missing = 0;
    unsigned duplicate_ids = 0;
    unsigned extra = 0;
    if (lab05_peak_run_overlap(&s_session, reps) != 0) {
        return -1;
    }
    if (check_total) {
        if (lab05_peak_overlap_completeness(&s_session, reps, &missing, &duplicate_ids, &extra) != 0) {
            return -1;
        }
    } else if (count_reps(LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_TERMINAL_SUCCESS) != before + reps) {
        return -1;
    }
    emit_new();
    drain_page_results_bounded();
    return s_required_ui_failed ? -1 : 0;
}

static int run_replace_once(int inject)
{
    uint64_t operation_id = s_session.next_operation_id++;
    const lab05_peak_operation *op;
    if (drop_steady_view() != 0) {
        return -1;
    }
    if (lab05_peak_run_replace(&s_session, operation_id, inject, NULL) != 0) {
        return -1;
    }
    op = lab05_peak_find_operation(&s_session, operation_id);
    if (op == NULL || op->terminal_count != 1u || op->control != LAB05_PEAK_CONTROL_REPLACE) {
        return -1;
    }
    if (inject) {
        if (op->terminal != LAB05_PEAK_TERMINAL_FAILURE || strcmp(op->reason, "alloc") != 0) {
            return -1;
        }
    } else if (op->terminal != LAB05_PEAK_TERMINAL_SUCCESS) {
        return -1;
    }
    emit_new();
    drain_page_results_bounded();
    if (s_required_ui_failed) {
        return -1;
    }
    if (inject && ensure_steady_image() != 0) {
        return -1;
    }
    return 0;
}

static int run_alloc_once(void)
{
    uint64_t operation_id = s_session.next_operation_id++;
    const lab05_peak_operation *op;
    if (drop_steady_view() != 0) {
        return -1;
    }
    if (lab05_peak_run_alloc_fail(&s_session, operation_id, NULL) != 0) {
        return -1;
    }
    op = lab05_peak_find_operation(&s_session, operation_id);
    if (op == NULL || op->terminal != LAB05_PEAK_TERMINAL_FAILURE || op->terminal_count != 1u ||
        strcmp(op->reason, "alloc") != 0) {
        return -1;
    }
    emit_new();
    drain_page_results_bounded();
    if (s_required_ui_failed) {
        return -1;
    }
    return ensure_steady_image();
}

static int run_stale_once(void)
{
    uint64_t operation_id = s_session.next_operation_id++;
    const lab05_peak_operation *op;
    if (drop_steady_view() != 0) {
        return -1;
    }
    if (lab05_peak_run_stale_callback(&s_session, operation_id, NULL) != 0) {
        return -1;
    }
    op = lab05_peak_find_operation(&s_session, operation_id);
    if (op == NULL || op->terminal != LAB05_PEAK_TERMINAL_STALE || op->terminal_count != 1u) {
        return -1;
    }
    emit_new();
    drain_page_results_bounded();
    if (s_required_ui_failed) {
        return -1;
    }
    return ensure_steady_image();
}

static int run_cancel_once(void)
{
    uint64_t animation_id = s_session.next_operation_id++;
    uint64_t audio_id = s_session.next_operation_id++;
    uint64_t page_id = s_session.next_operation_id++;
    const lab05_peak_operation *op;
    if (lab05_peak_run_cancel(&s_session, animation_id, audio_id, page_id) != 0) {
        return -1;
    }
    op = lab05_peak_find_operation(&s_session, animation_id);
    if (op == NULL || op->terminal != LAB05_PEAK_TERMINAL_CANCELLED || op->terminal_count != 1u ||
        op->live_image_count != LAB05_PEAK_STEADY_LIVE_IMAGES) {
        return -1;
    }
    emit_new();
    drain_page_results_bounded();
    return s_required_ui_failed ? -1 : 0;
}

static void audio_worker(void *unused)
{
    lab05_worker_cmd cmd;
    (void)unused;
    while (!s_worker_shutdown) {
        if (xQueueReceive(s_audio_q, &cmd, pdMS_TO_TICKS(50)) == pdTRUE) {
            (void)cmd.operation_id;
            s_audio_running = 0;
        }
    }
    s_audio_exited = 1;
    vTaskDelete(NULL);
}

static void page_worker(void *unused)
{
    lab05_worker_cmd cmd;
    (void)unused;
    while (!s_worker_shutdown) {
        lab05_page_result result;
        if (xQueueReceive(s_page_q, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        result.operation_id = cmd.operation_id;
        if (s_page_result_q == NULL || xQueueSend(s_page_result_q, &result, 0) != pdTRUE) {
            s_page_result_overflow++;
            printf("LAB05_PEAK_PUBLISH queue=page-result status=rejected overflow_count=%u "
                   "dropped_count=0 capacity=%u operation_id=%llu\n",
                   s_page_result_overflow, (unsigned)LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY,
                   (unsigned long long)cmd.operation_id);
            if (xQueueSendToFront(s_page_q, &cmd, 0) != pdTRUE) {
                printf("LAB05_PEAK_PUBLISH queue=page-apply status=rejected overflow_count=%u "
                       "dropped_count=0 capacity=%u operation_id=%llu\n",
                       s_page_overflow, (unsigned)LAB05_PEAK_QUEUE_CAPACITY,
                       (unsigned long long)cmd.operation_id);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    s_page_exited = 1;
    vTaskDelete(NULL);
}

static void on_manual(lv_event_t *event)
{
    lab05_ui_cmd cmd;
    cmd.code = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    if (s_manual_q == NULL || xQueueSend(s_manual_q, &cmd, 0) != pdTRUE) {
        s_manual_overflow++;
        printf("LAB05_PEAK_PUBLISH queue=manual status=rejected overflow_count=%u "
               "dropped_count=0 capacity=%u\n",
               s_manual_overflow, (unsigned)LAB05_PEAK_QUEUE_CAPACITY);
    }
}

static void add_button(lv_obj_t *screen, const char *text, int x, int y, unsigned code)
{
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_t *label;
    lv_obj_set_size(button, 74, 36);
    lv_obj_set_pos(button, x, y);
    lv_obj_add_event_cb(button, on_manual, LV_EVENT_CLICKED, (void *)(uintptr_t)code);
    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
}

static void init_summaries(void)
{
    unsigned i;
    for (i = 0; i < LAB05_PEAK_PAGE_RECORDS; i++) {
        snprintf(s_summary_text[i], sizeof(s_summary_text[i]), "summary-%02u", i + 1u);
    }
}

static int build_ui_locked(void)
{
    lv_obj_t *title;
    unsigned i;
    const uint8_t *pixels;
    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        return -1;
    }
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x10212D), 0);
    title = lv_label_create(s_screen);
    if (title == NULL) {
        return -1;
    }
    lv_label_set_text(title, "LAB-05 peak");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
    pixels = handle_pixels(s_session.steady);
    if (pixels == NULL || bind_steady_locked(pixels) != 0) {
        return -1;
    }
    for (i = 0; i < LAB05_PEAK_PAGE_RECORDS; i++) {
        s_labels[i] = lv_label_create(s_screen);
        if (s_labels[i] == NULL) {
            return -1;
        }
        lv_obj_set_pos(s_labels[i], 140, 28 + (int)i * 10);
        lv_label_set_text(s_labels[i], s_summary_text[i]);
    }
#if !CONFIG_LAB05_SCRIPTED
#if CONFIG_LAB05_PEAK_REPLACE
    add_button(s_screen, "Replace", 8, 198, LAB05_UI_REPLACE);
    add_button(s_screen, "R-alloc", 120, 198, LAB05_UI_REPLACE_ALLOC);
#else
    add_button(s_screen, "Overlap", 4, 198, LAB05_UI_OVERLAP);
    add_button(s_screen, "Alloc", 82, 198, LAB05_UI_ALLOC_FAIL);
    add_button(s_screen, "Stale", 160, 198, LAB05_UI_STALE);
    add_button(s_screen, "Cancel", 238, 198, LAB05_UI_CANCEL);
#endif
#endif
    lv_screen_load(s_screen);
    return 0;
}

static void manual_task(void *unused)
{
    lab05_ui_cmd cmd;
    (void)unused;
    while (!s_boot_go) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    for (;;) {
        int rc = -1;
        if (xQueueReceive(s_manual_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (cmd.code) {
        case LAB05_UI_OVERLAP:
            rc = run_overlap_reps(1u, 0);
            break;
        case LAB05_UI_REPLACE:
            rc = run_replace_once(0);
            break;
        case LAB05_UI_REPLACE_ALLOC:
            rc = run_replace_once(1);
            break;
        case LAB05_UI_ALLOC_FAIL:
            rc = run_alloc_once();
            break;
        case LAB05_UI_STALE:
            rc = run_stale_once();
            break;
        case LAB05_UI_CANCEL:
            rc = run_cancel_once();
            break;
        default:
            rc = -1;
            break;
        }
        if (rc != 0) {
            printf("LAB05_PEAK_SCENARIO status=failed code=%u\n", (unsigned)cmd.code);
        }
    }
}

static int scripted_persistent(void)
{
    if (run_overlap_reps(LAB05_PEAK_OVERLAP_REPETITIONS, 1) != 0) {
        return -1;
    }
    if (run_alloc_once() != 0 || run_stale_once() != 0 || run_cancel_once() != 0) {
        return -1;
    }
    return s_required_ui_failed ? -1 : 0;
}

static int scripted_replace(void)
{
    unsigned i;
    for (i = 0; i < LAB05_PEAK_OVERLAP_REPETITIONS; i++) {
        if (run_replace_once(0) != 0) {
            return -1;
        }
    }
    if (count_reps(LAB05_PEAK_SCENARIO_REPLACE, LAB05_PEAK_TERMINAL_SUCCESS) !=
            LAB05_PEAK_OVERLAP_REPETITIONS ||
        !repetition_ids_unique(LAB05_PEAK_SCENARIO_REPLACE)) {
        return -1;
    }
    return s_required_ui_failed ? -1 : 0;
}

static void stop_workers(void)
{
    int i;
    if (!s_audio_started) {
        s_audio_exited = 1;
    }
    if (!s_page_started) {
        s_page_exited = 1;
    }
    s_worker_shutdown = 1;
    for (i = 0; i < 50 && (!s_audio_exited || !s_page_exited); i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!s_audio_exited || !s_page_exited) {
        s_cleanup_failed = 1;
        return;
    }
    if (s_audio_q != NULL) {
        vQueueDelete(s_audio_q);
        s_audio_q = NULL;
    }
    if (s_page_q != NULL) {
        vQueueDelete(s_page_q);
        s_page_q = NULL;
    }
    if (s_page_result_q != NULL) {
        vQueueDelete(s_page_result_q);
        s_page_result_q = NULL;
    }
}

static void scripted_task(void *unused)
{
    int ok;
    (void)unused;
    while (!s_boot_go) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelay(pdMS_TO_TICKS(2500));
#if CONFIG_LAB05_PEAK_REPLACE
    ok = scripted_replace();
#else
    ok = scripted_persistent();
#endif
    emit_new();
    drain_page_results_bounded();
    if (!bsp_display_lock(1000)) {
        if (s_incoming != NULL || s_incoming_view_held ||
            (s_required_ui_failed && s_steady_canvas != NULL)) {
            s_cleanup_failed = 1;
        }
    } else {
        int failed_steady = s_required_ui_failed && s_steady_canvas != NULL;
        teardown_incoming_locked();
        if (failed_steady) {
            lv_obj_delete(s_steady_canvas);
            s_steady_canvas = NULL;
        }
        bsp_display_unlock();
        release_incoming_view();
        if (failed_steady) {
            release_steady_view();
            if (s_session.has_incoming) {
                (void)lab05_peak_delete_image(&s_session, s_session.incoming);
            }
            if (s_session.steady.valid) {
                (void)lab05_peak_delete_image(&s_session, s_session.steady);
            }
        }
    }
    stop_workers();
    printf("LAB05_PEAK_COUNTERS live_image_count=%u temp_image_bytes=%u "
           "audio_publish_accepted=%u page_publish_accepted=%u audio_publish_overflow=%u "
           "page_publish_overflow=%u duplicate_terminal_rejects=%u audio_running=%d\n",
           s_session.live_images, s_session.pixel_bytes, s_audio_accepted, s_page_accepted,
           s_audio_overflow, s_page_overflow, s_session.duplicate_terminal_rejects, s_audio_running);
    print_harness_done(ok == 0 && !s_required_ui_failed && !s_cleanup_failed);
    vTaskDelete(NULL);
}

static int start_workers(void)
{
    s_audio_q = xQueueCreate(LAB05_PEAK_QUEUE_CAPACITY, sizeof(lab05_worker_cmd));
    s_page_q = xQueueCreate(LAB05_PEAK_QUEUE_CAPACITY, sizeof(lab05_worker_cmd));
    s_page_result_q = xQueueCreate(LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY, sizeof(lab05_page_result));
    if (s_audio_q == NULL || s_page_q == NULL || s_page_result_q == NULL) {
        return -1;
    }
    if (xTaskCreate(audio_worker, "lab05_audio", 3072, NULL, 3, NULL) != pdPASS) {
        return -1;
    }
    s_audio_started = 1;
    if (xTaskCreate(page_worker, "lab05_page", 4096, NULL, 3, NULL) != pdPASS) {
        return -1;
    }
    s_page_started = 1;
    return 0;
}

void lab05_peak_harness_main(void)
{
    init_summaries();
    if (bsp_display_start() == NULL) {
        print_ready_failed("display_start_failed");
        return;
    }
    bsp_display_brightness_set(80);
    lab05_peak_session_init(&s_session);
    if (!steady_pixels_ok()) {
        print_ready_failed("init_failed");
        return;
    }
    if (start_workers() != 0) {
        stop_workers();
        print_ready_failed("init_failed");
        return;
    }
    lab05_peak_set_peak_observer(&s_session, on_peak, NULL);
    if (!bsp_display_lock(1000)) {
        stop_workers();
        print_ready_failed("display_lock_failed");
        return;
    }
    if (build_ui_locked() != 0) {
        bsp_display_unlock();
        stop_workers();
        print_ready_failed("init_failed");
        return;
    }
    bsp_display_unlock();
#if CONFIG_LAB05_SCRIPTED
    if (xTaskCreate(scripted_task, "lab05_peak", 8192, NULL, 4, NULL) != pdPASS) {
        stop_workers();
        print_ready_failed("init_failed");
        return;
    }
#else
    s_manual_q = xQueueCreate(LAB05_PEAK_QUEUE_CAPACITY, sizeof(lab05_ui_cmd));
    if (s_manual_q == NULL ||
        xTaskCreate(manual_task, "lab05_manual", 8192, NULL, 4, NULL) != pdPASS) {
        stop_workers();
        print_ready_failed("init_failed");
        return;
    }
#endif
    print_ready_ok();
    s_boot_go = 1;
    /* s_boot_go is volatile, so this read stays. Manual builds never take
     * the branch. The call keeps the completion literals in every peak image.
     */
    if (s_boot_go < 0) {
        print_harness_done(0);
    }
}
