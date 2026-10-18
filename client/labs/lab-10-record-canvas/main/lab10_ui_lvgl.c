#include "lab10_ui_lvgl.h"

#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

#ifndef CONFIG_LAB10_SCRIPTED
#define CONFIG_LAB10_SCRIPTED 0
#endif

#if defined(LV_DRAW_BUF_ALIGN)
#define LAB10_CANVAS_BUFFER_ALIGNMENT LV_DRAW_BUF_ALIGN
#elif defined(CONFIG_LV_DRAW_BUF_ALIGN)
#define LAB10_CANVAS_BUFFER_ALIGNMENT CONFIG_LV_DRAW_BUF_ALIGN
#else
/* LVGL's documented default for draw-buffer start addresses. */
#define LAB10_CANVAS_BUFFER_ALIGNMENT 4u
#endif

_Static_assert(LAB10_CANVAS_BUFFER_ALIGNMENT > 0u &&
                   (LAB10_CANVAS_BUFFER_ALIGNMENT &
                    (LAB10_CANVAS_BUFFER_ALIGNMENT - 1u)) == 0u,
               "LVGL canvas buffer alignment must be a nonzero power of two");

static const char *TAG = "lab10_ui";
static lab10_ui_view_t *s_anim_view;
static atomic_bool s_pending_scripted_start = ATOMIC_VAR_INIT(false);
static atomic_bool s_scripted_start_delivery_required =
    ATOMIC_VAR_INIT(false);
static atomic_uint_least32_t s_pending_scripted_completion_points =
    ATOMIC_VAR_INIT(0u);
static atomic_int s_pending_capture_outcome =
    ATOMIC_VAR_INIT(LAB10_CAPTURE_OUTCOME_NONE);

static bool publish_pending_scripted_start(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong_explicit(
        &s_pending_scripted_start, &expected, true,
        memory_order_release, memory_order_relaxed);
}

static bool publish_pending_scripted_completion(uint32_t accepted_points)
{
    if (accepted_points == 0u) {
        return false;
    }

    uint_least32_t expected = 0u;
    return atomic_compare_exchange_strong_explicit(
        &s_pending_scripted_completion_points, &expected, accepted_points,
        memory_order_release, memory_order_relaxed);
}

bool lab10_ui_view_take_pending_scripted_start(void)
{
    const bool pending = atomic_exchange_explicit(
        &s_pending_scripted_start, false, memory_order_acq_rel);
    if (pending) {
        atomic_store_explicit(&s_scripted_start_delivery_required, false,
                              memory_order_release);
    }
    return pending;
}

bool lab10_ui_view_take_pending_scripted_completion(
    uint32_t *accepted_points)
{
    if (accepted_points == NULL) {
        return false;
    }

    const uint_least32_t pending = atomic_exchange_explicit(
        &s_pending_scripted_completion_points, 0u, memory_order_acq_rel);
    if (pending == 0u) {
        return false;
    }
    *accepted_points = (uint32_t)pending;
    return true;
}

static void publish_pending_scripted_events(lab10_ui_view_t *view)
{
    if (view == NULL) {
        return;
    }
    if (view->scripted_start_marker_pending &&
        publish_pending_scripted_start()) {
        view->scripted_start_marker_pending = false;
    }
    if (view->scripted_completion_marker_pending &&
        publish_pending_scripted_completion(
            view->scripted_completion_accepted_points)) {
        view->scripted_completion_marker_pending = false;
    }
}

static bool publish_pending_capture_outcome(
    lab10_capture_terminal_outcome_t outcome)
{
    if (outcome <= LAB10_CAPTURE_OUTCOME_NONE ||
        outcome > LAB10_CAPTURE_OUTCOME_FAILED) {
        return false;
    }

    int expected = LAB10_CAPTURE_OUTCOME_NONE;
    return atomic_compare_exchange_strong_explicit(
        &s_pending_capture_outcome, &expected, (int)outcome,
        memory_order_release, memory_order_relaxed);
}

bool lab10_ui_view_take_pending_terminal_outcome(
    lab10_capture_terminal_outcome_t *outcome)
{
    if (outcome == NULL) {
        return false;
    }

    const int pending = atomic_exchange_explicit(
        &s_pending_capture_outcome, LAB10_CAPTURE_OUTCOME_NONE,
        memory_order_acq_rel);
    if (pending <= LAB10_CAPTURE_OUTCOME_NONE ||
        pending > LAB10_CAPTURE_OUTCOME_FAILED) {
        return false;
    }
    *outcome = (lab10_capture_terminal_outcome_t)pending;
    return true;
}

static void release_view_resources(lab10_ui_view_t *view)
{
    if (view == NULL) {
        return;
    }
    if (view->poll_timer != NULL) {
        lv_timer_delete(view->poll_timer);
        view->poll_timer = NULL;
    }
    if (view->root != NULL) {
        /* All child widgets are owned by root and are deleted with it. */
        lv_obj_delete(view->root);
        view->root = NULL;
    }
    if (view->canvas_buffer != NULL) {
        heap_caps_free(view->canvas_buffer);
        view->canvas_buffer = NULL;
    }
    if (view->renderer != NULL) {
        lab10_render_destroy(view->renderer);
        view->renderer = NULL;
    }
    if (view->capture != NULL) {
        if (view->capture_session_id != LAB10_CAPTURE_INVALID_SESSION_ID) {
            if (lab10_capture_status(view->capture, view->capture_session_id) ==
                LAB10_CAPTURE_STATUS_ACTIVE) {
                (void)lab10_capture_cancel(view->capture,
                                            view->capture_session_id);
            }
            (void)lab10_capture_release_result(view->capture,
                                                view->capture_session_id);
        }
        lab10_capture_destroy(view->capture);
        view->capture = NULL;
    }
    if (s_anim_view == view) {
        s_anim_view = NULL;
    }
    *view = (lab10_ui_view_t){0};
}

static bool view_init_failed(lab10_ui_view_t *view, const char *resource)
{
    ESP_LOGE(TAG, "UI initialization failed resource=%s", resource);
    release_view_resources(view);
    return false;
}

static bool canvas_buffer_is_aligned(const void *buffer)
{
    const uintptr_t alignment = LAB10_CANVAS_BUFFER_ALIGNMENT;
    return buffer != NULL && ((uintptr_t)buffer % alignment) == 0u;
}

static void *allocate_canvas_buffer(uint32_t bytes, const char **region)
{
    for (size_t attempt = 0u;
         attempt < lab10_canvas_memory_attempt_count(); ++attempt) {
        const lab10_canvas_memory_region_t memory_region =
            lab10_canvas_memory_attempt(attempt);
        const uint32_t caps = memory_region == LAB10_CANVAS_MEMORY_PSRAM
                                  ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                  : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        void *buffer = heap_caps_aligned_alloc(
            LAB10_CANVAS_BUFFER_ALIGNMENT, bytes, caps);
        if (buffer != NULL && canvas_buffer_is_aligned(buffer)) {
            *region = memory_region == LAB10_CANVAS_MEMORY_PSRAM
                          ? "psram"
                          : "internal";
            return buffer;
        }
        if (buffer != NULL) {
            ESP_LOGW(TAG, "discarding misaligned %s canvas buffer=%p",
                     memory_region == LAB10_CANVAS_MEMORY_PSRAM ? "PSRAM"
                                                                  : "internal",
                     buffer);
            heap_caps_free(buffer);
        }
    }
    *region = "none";
    return NULL;
}

static lab10_render_mode_t configured_render_mode(void)
{
#if defined(CONFIG_LAB10_RENDER_MODE_SEGMENT)
    return LAB10_RENDER_MODE_SEGMENT;
#elif defined(CONFIG_LAB10_RENDER_MODE_BOUNDED_BATCH)
    return LAB10_RENDER_MODE_BOUNDED_BATCH;
#elif defined(CONFIG_LAB10_RENDER_MODE_WHOLE_FRAME)
    return LAB10_RENDER_MODE_WHOLE_FRAME;
#else
    return LAB10_RENDER_MODE_SEGMENT;
#endif
}

static uint64_t capture_clock_now_us(void *context)
{
    (void)context;
    const int64_t now = esp_timer_get_time();
    return now >= 0 ? (uint64_t)now : 0;
}

static void make_plain(lv_obj_t *object)
{
    lv_obj_remove_style_all(object);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static void make_circle(lv_obj_t *object, uint32_t color, int16_t diameter)
{
    make_plain(object);
    lv_obj_set_size(object, diameter, diameter);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
}

static void refresh_swatch(lab10_ui_view_t *view, uint8_t index)
{
    if (view == NULL || index >= LAB10_SWATCH_COUNT || view->swatches[index] == NULL) {
        return;
    }
    lv_obj_t *swatch = view->swatches[index];
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0xB8BEC6), 0);
    lv_obj_set_style_border_opa(swatch, LV_OPA_COVER, 0);
    if (index == lab10_ui_selected_color(&view->model)) {
        lv_obj_set_style_outline_width(swatch, 2, 0);
        lv_obj_set_style_outline_color(swatch, lv_color_hex(0xE8C040), 0);
        lv_obj_set_style_outline_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_pad(swatch, 1, 0);
    } else {
        lv_obj_set_style_outline_width(swatch, 0, 0);
    }
    /* Only the changed swatch rectangles are invalidated. */
    lv_obj_invalidate(swatch);
}

static void refresh_palette(lab10_ui_view_t *view, uint8_t previous, uint8_t next)
{
    refresh_swatch(view, previous);
    if (next != previous) {
        refresh_swatch(view, next);
    }
}

static void on_palette_changed(uint8_t previous, uint8_t next, void *context)
{
    lab10_ui_view_t *view = context;
    refresh_palette(view, previous, next);
}

static void on_transition(lab10_screen_state_t from, lab10_screen_state_t to,
                          uint32_t generation, void *context)
{
    (void)from;
    (void)to;
    (void)generation;
    (void)context;
}

static void on_render_publish_pixel(const lab10_render_pixel_event_t *event,
                                    void *context)
{
    lab10_ui_view_t *view = context;
    if (event == NULL || view == NULL || view->canvas == NULL ||
        view->canvas_buffer == NULL) {
        return;
    }
    const lv_color_t color = lv_color_hex(lab10_color_rgb(event->color_index));
    const int32_t display_x = (int32_t)event->logical_x * 2;
    const int32_t display_y = (int32_t)event->logical_y * 2;
    /* One logical pixel is presented as a bounded 2x2 display block. */
    for (int32_t dy = 0; dy < 2; ++dy) {
        for (int32_t dx = 0; dx < 2; ++dx) {
            lv_canvas_set_px(view->canvas, display_x + dx, display_y + dy,
                             color, LV_OPA_COVER);
        }
    }
}

static void on_render_invalidation(
    const lab10_render_invalidation_t *invalidation, void *context)
{
    lab10_ui_view_t *view = context;
    if (invalidation == NULL || view == NULL || view->canvas == NULL) {
        return;
    }
    lv_area_t area = {
        .x1 = (int32_t)invalidation->min_x * 2,
        .y1 = (int32_t)invalidation->min_y * 2,
        .x2 = ((int32_t)invalidation->max_x + 1) * 2 - 1,
        .y2 = ((int32_t)invalidation->max_y + 1) * 2 - 1,
    };
    (void)lv_obj_invalidate_area(view->canvas, &area);
}

static void on_touch_routed(const lab10_ui_touch_event_t *event, void *context)
{
    lab10_ui_view_t *view = context;
    if (event == NULL || view == NULL) {
        return;
    }
    if (view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    (void)lab10_capture_submit(
        view->capture, view->capture_session_id,
        &(lab10_capture_raw_report_t){
            .timestamp_us = lab10_capture_now_us(view->capture),
            .display_x = event->display_x,
            .display_y = event->display_y,
            .touch_id = event->touch_id,
            .phase = event->phase,
            .route = event->route,
            .color_index = event->color_index,
        });
}

void lab10_ui_view_scripted_poll(lab10_ui_view_t *view)
{
#if CONFIG_LAB10_SCRIPTED
    if (view == NULL || view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID) {
        return;
    }
    /* A one-slot mailbox may still contain the prior event; retry these
     * bounded latches on each timer poll without overwriting or waiting.
     */
    publish_pending_scripted_events(view);
    lab10_capture_stats_t stats;
    if (!lab10_capture_get_stats(view->capture, view->capture_session_id,
                                 &stats)) {
        return;
    }
    const bool capture_active =
        stats.status == LAB10_CAPTURE_STATUS_ACTIVE;
    if (capture_active &&
        lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    const bool completion_eligible =
        !capture_active &&
        stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP &&
        stats.duration_cap_hit_count == 1u && stats.terminal_event_count == 1u &&
        stats.accepted_point_count >=
            LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS &&
        stats.accepted_point_count < LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY &&
        stats.stroke_count == 1u && stats.stroke_boundary_count == 0u &&
        stats.selected_color_index == LAB10_COLOR_WHITE &&
        stats.color_change_count == 0u && stats.queue_overflow_count == 0u &&
        stats.trace_loss_count == 0u;
    const lab10_ui_scripted_capture_state_t capture_state =
        capture_active
            ? LAB10_UI_SCRIPTED_CAPTURE_ACTIVE
            : completion_eligible
                  ? LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP
                  : LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL;
    lab10_ui_scripted_event_t scripted_event;
    const lab10_ui_scripted_poll_result_t result =
        lab10_ui_scripted_schedule_poll(
            &view->scripted_schedule, lab10_capture_now_us(view->capture),
            capture_state, stats.accepted_point_count, &scripted_event);
    if (result == LAB10_UI_SCRIPTED_POLL_EVENT) {
        if (!view->scripted_started) {
            view->scripted_started = true;
            atomic_store_explicit(&s_scripted_start_delivery_required, true,
                                  memory_order_release);
            view->scripted_start_marker_pending = true;
        }
        publish_pending_scripted_events(view);
        (void)lab10_ui_handle_touch(&view->model, scripted_event.phase,
                                    scripted_event.display_x,
                                    scripted_event.display_y,
                                    scripted_event.touch_id);
    } else if (result == LAB10_UI_SCRIPTED_POLL_COMPLETE &&
               view->scripted_started) {
        if (!view->scripted_completion_marker_pending) {
            view->scripted_completion_marker_pending = true;
            view->scripted_completion_accepted_points =
                stats.accepted_point_count;
        }
        publish_pending_scripted_events(view);
    }
#else
    (void)view;
#endif
}

static bool event_point(lv_event_t *event, lv_point_t *point)
{
    if (event == NULL || point == NULL) {
        return false;
    }
    lv_indev_t *indev = lv_event_get_indev(event);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        return false;
    }
    lv_indev_get_point(indev, point);
    return true;
}

static void handle_touch_and_poll(lab10_ui_view_t *view,
                                  lab10_touch_phase_t phase, int16_t x,
                                  int16_t y)
{
    (void)lab10_ui_handle_touch(&view->model, phase, x, y, 0);
    /* Consume and render this report on the LVGL task without waiting for the
     * periodic timer. Capture FIFO and per-update renderer budgets stay fixed.
     */
    lab10_ui_view_poll(view);
}

static void on_canvas_event(lv_event_t *event)
{
    lab10_ui_view_t *view = lv_event_get_user_data(event);
    if (view == NULL || lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    lv_point_t point;
    if (!event_point(event, &point)) {
        return;
    }
    lab10_touch_phase_t phase = LAB10_PHASE_NONE;
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        phase = LAB10_PHASE_DOWN;
        break;
    case LV_EVENT_PRESSING:
        phase = LAB10_PHASE_MOVE;
        break;
    case LV_EVENT_RELEASED:
        phase = LAB10_PHASE_UP;
        break;
    case LV_EVENT_PRESS_LOST:
        phase = LAB10_PHASE_LEAVE;
        break;
    default:
        return;
    }
    /* The model rejects x >= 240 even if an input driver reports a wide point. */
    handle_touch_and_poll(view, phase, point.x, point.y);
}

static lab10_touch_phase_t touch_phase_from_event(lv_event_code_t code)
{
    switch (code) {
    case LV_EVENT_PRESSED:
        return LAB10_PHASE_DOWN;
    case LV_EVENT_PRESSING:
        return LAB10_PHASE_MOVE;
    case LV_EVENT_RELEASED:
        return LAB10_PHASE_UP;
    case LV_EVENT_PRESS_LOST:
        return LAB10_PHASE_LEAVE;
    default:
        return LAB10_PHASE_NONE;
    }
}

static void on_rail_event(lv_event_t *event)
{
    lab10_ui_view_t *view = lv_event_get_user_data(event);
    if (view == NULL || lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    const lab10_touch_phase_t phase =
        touch_phase_from_event(lv_event_get_code(event));
    if (phase == LAB10_PHASE_NONE) {
        return;
    }
    lv_point_t point;
    if (!event_point(event, &point)) {
        return;
    }
    /* Rail gaps become explicit rail_rejected reports; they never become ink. */
    handle_touch_and_poll(view, phase, point.x, point.y);
}

static void on_palette_event(lv_event_t *event)
{
    lab10_ui_view_t *view = lv_event_get_user_data(event);
    if (view == NULL || lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING ||
        lv_event_get_code(event) != LV_EVENT_PRESSED) {
        return;
    }
    lv_point_t point;
    if (!event_point(event, &point)) {
        return;
    }
    /* LVGL object hit testing is rectangular; the model owns the circle test. */
    handle_touch_and_poll(view, LAB10_PHASE_PALETTE_CHANGE, point.x, point.y);
}

static void anim_circle_size(void *object, int32_t radius)
{
    lab10_ui_view_t *view = s_anim_view;
    if (view == NULL || object == NULL) {
        return;
    }
    if (radius < 1) {
        radius = 1;
    }
    lv_obj_set_size(object, radius * 2, radius * 2);
    lv_obj_set_pos(object, view->transition_origin_x - radius,
                   view->transition_origin_y - radius);
}

static void entry_animation_done(lv_anim_t *animation)
{
    (void)animation;
    if (s_anim_view == NULL) {
        return;
    }
    lv_obj_set_hidden(s_anim_view->bloom, true);
    lv_obj_set_hidden(s_anim_view->ring, true);
    lab10_ui_finish_entry(&s_anim_view->model);
    if (s_anim_view->capture != NULL) {
        s_anim_view->capture_session_id =
            lab10_capture_start(s_anim_view->capture);
    }
    if (s_anim_view->capture_session_id != LAB10_CAPTURE_INVALID_SESSION_ID &&
        lab10_capture_status(s_anim_view->capture,
                             s_anim_view->capture_session_id) ==
            LAB10_CAPTURE_STATUS_ACTIVE) {
        if (s_anim_view->renderer == NULL ||
            !lab10_render_begin(s_anim_view->renderer,
                                s_anim_view->capture_session_id)) {
            (void)lab10_capture_cancel(s_anim_view->capture,
                                        s_anim_view->capture_session_id);
            lab10_ui_view_present_state(s_anim_view, LAB10_SCREEN_FAILED);
            s_anim_view = NULL;
            return;
        }
        s_anim_view->scripted_started = false;
        s_anim_view->scripted_start_marker_pending = false;
        s_anim_view->scripted_completion_marker_pending = false;
        s_anim_view->scripted_completion_accepted_points = 0u;
        if (!lab10_ui_scripted_schedule_init(
                &s_anim_view->scripted_schedule,
                lab10_capture_now_us(s_anim_view->capture),
                LAB10_CAPTURE_DURATION_CAP_US,
                LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
                LAB10_UI_SCRIPTED_MOVE_INTERVAL_US)) {
            (void)lab10_capture_cancel(s_anim_view->capture,
                                        s_anim_view->capture_session_id);
            lab10_ui_view_present_state(s_anim_view, LAB10_SCREEN_FAILED);
            s_anim_view = NULL;
            return;
        }
        lab10_ui_view_present_state(s_anim_view, LAB10_SCREEN_RECORDING);
    } else {
        lab10_ui_view_present_state(s_anim_view, LAB10_SCREEN_FAILED);
    }
    s_anim_view = NULL;
}

static void exit_animation_done(lv_anim_t *animation)
{
    (void)animation;
    if (s_anim_view == NULL) {
        return;
    }
    lv_obj_set_hidden(s_anim_view->bloom, true);
    lab10_ui_finish_exit(&s_anim_view->model);
    lab10_ui_view_present_state(s_anim_view, LAB10_SCREEN_IDLE);
    if (s_anim_view->capture != NULL &&
        s_anim_view->capture_session_id != LAB10_CAPTURE_INVALID_SESSION_ID) {
        const lab10_capture_session_id_t session_id =
            s_anim_view->capture_session_id;
        if (lab10_capture_status(s_anim_view->capture, session_id) ==
            LAB10_CAPTURE_STATUS_ACTIVE) {
            /* Reverse exit is a public cancellation boundary for live capture. */
            (void)lab10_capture_cancel(s_anim_view->capture, session_id);
        }
        if (s_anim_view->renderer != NULL) {
            (void)lab10_render_reset(s_anim_view->renderer,
                                     session_id);
        }
        (void)lab10_capture_release_result(
            s_anim_view->capture, session_id);
        s_anim_view->capture_session_id = LAB10_CAPTURE_INVALID_SESSION_ID;
    }
    s_anim_view = NULL;
}

static void start_circle_animation(lab10_ui_view_t *view, lv_obj_t *object,
                                   int16_t origin_x, int16_t origin_y,
                                   int32_t radius_start, int32_t radius_end,
                                   uint32_t duration, lv_anim_completed_cb_t ready_cb)
{
    s_anim_view = view;
    view->transition_origin_x = origin_x;
    view->transition_origin_y = origin_y;
    anim_circle_size(object, radius_start);
    lv_obj_set_hidden(object, false);
    lv_obj_move_foreground(object);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, radius_start, radius_end);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&animation, anim_circle_size);
    lv_anim_set_completed_cb(&animation, ready_cb);
    lv_anim_start(&animation);
}

static const char *outcome_text(lab10_screen_state_t state)
{
    switch (state) {
    case LAB10_SCREEN_STOPPED:
        return "stopped";
    case LAB10_SCREEN_CANCELLED:
        return "cancelled";
    case LAB10_SCREEN_FAILED:
        return "failed";
    case LAB10_SCREEN_COMPLETED:
        return "completed";
    case LAB10_SCREEN_RECORDING:
        return "recording";
    default:
        return "";
    }
}

bool lab10_ui_view_init(lab10_ui_view_t *view, lv_obj_t *parent)
{
    if (view == NULL || parent == NULL) {
        return false;
    }
    *view = (lab10_ui_view_t){0};
    view->fab_origin_x = LAB10_FAB_ORIGIN_X;
    view->fab_origin_y = LAB10_FAB_ORIGIN_Y;
    lab10_ui_model_init(&view->model);
    view->capture = lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = capture_clock_now_us,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
    if (view->capture == NULL) {
        return view_init_failed(view, "capture");
    }
    const lab10_render_mode_t render_mode = configured_render_mode();
    view->renderer = lab10_render_create(&(lab10_render_config_t){
        .mode = render_mode,
        .role = render_mode == LAB10_RENDER_MODE_DIRECT_POINT
                    ? LAB10_RENDER_ROLE_CONTROL
                    : LAB10_RENDER_ROLE_CANDIDATE,
        .max_points_per_update = CONFIG_LAB10_RENDER_MAX_POINTS_PER_UPDATE,
        .max_raster_ops_per_update =
            CONFIG_LAB10_RENDER_MAX_RASTER_OPS_PER_UPDATE,
        .max_publication_ops_per_update =
            CONFIG_LAB10_RENDER_MAX_PUBLICATION_OPS_PER_UPDATE,
    });
    if (view->renderer == NULL) {
        return view_init_failed(view, "renderer");
    }
    ESP_LOGI(TAG, "renderer mode=%s role=%s point_budget=%d raster_budget=%d publication_budget=%d",
             lab10_render_mode_name(render_mode),
             lab10_render_role_name(render_mode == LAB10_RENDER_MODE_DIRECT_POINT
                                        ? LAB10_RENDER_ROLE_CONTROL
                                        : LAB10_RENDER_ROLE_CANDIDATE),
             CONFIG_LAB10_RENDER_MAX_POINTS_PER_UPDATE,
             CONFIG_LAB10_RENDER_MAX_RASTER_OPS_PER_UPDATE,
             CONFIG_LAB10_RENDER_MAX_PUBLICATION_OPS_PER_UPDATE);
    if (!lab10_render_set_hooks(
            view->renderer,
            &(lab10_render_hooks_t){
                .on_publish_pixel = on_render_publish_pixel,
                .on_invalidation = on_render_invalidation,
                .context = view,
            })) {
        return view_init_failed(view, "renderer_hooks");
    }
    lab10_ui_set_hooks(&view->model, &(lab10_ui_hooks_t){
        .on_transition = on_transition,
        .on_palette_changed = on_palette_changed,
        .on_touch_routed = on_touch_routed,
        .context = view,
    });

    view->root = lv_obj_create(parent);
    if (view->root == NULL) {
        return view_init_failed(view, "root");
    }
    make_plain(view->root);
    lv_obj_set_size(view->root, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(view->root, 0, 0);
    lv_obj_set_style_bg_color(view->root, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(view->root, LV_OPA_COVER, 0);

    view->canvas = lv_canvas_create(view->root);
    if (view->canvas == NULL) {
        return view_init_failed(view, "canvas");
    }
    make_plain(view->canvas);
    lv_obj_set_size(view->canvas, LAB10_CANVAS_WIDTH, LAB10_CANVAS_HEIGHT);
    lv_obj_set_pos(view->canvas, 0, 0);
    lv_obj_set_style_bg_color(view->canvas, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(view->canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(view->canvas, 8, 0);
    lv_obj_set_style_clip_corner(view->canvas, true, 0);
    view->canvas_buffer_bytes = lv_canvas_buf_size(
        LAB10_CANVAS_WIDTH, LAB10_CANVAS_HEIGHT, LV_COLOR_DEPTH,
        LV_DRAW_BUF_STRIDE_ALIGN);
    if (view->canvas_buffer_bytes == 0u) {
        return view_init_failed(view, "canvas_buffer_size");
    }
    const char *canvas_memory_region = "none";
    view->canvas_buffer = allocate_canvas_buffer(view->canvas_buffer_bytes,
                                                 &canvas_memory_region);
    if (view->canvas_buffer != NULL) {
        ESP_LOGI(TAG, "canvas buffer allocated bytes=%lu region=%s alignment=%u buffer=%p",
                 (unsigned long)view->canvas_buffer_bytes, canvas_memory_region,
                 (unsigned)LAB10_CANVAS_BUFFER_ALIGNMENT, view->canvas_buffer);
        lv_canvas_set_buffer(view->canvas, view->canvas_buffer,
                             LAB10_CANVAS_WIDTH, LAB10_CANVAS_HEIGHT,
                             LV_COLOR_FORMAT_NATIVE);
        lv_canvas_fill_bg(view->canvas, lv_color_black(), LV_OPA_COVER);
    } else {
        ESP_LOGE(TAG, "canvas buffer allocation failed bytes=%lu preferred=psram fallback=internal",
                 (unsigned long)view->canvas_buffer_bytes);
        return view_init_failed(view, "canvas_buffer");
    }
    lv_obj_set_clickable(view->canvas, true);
    lv_obj_add_event_cb(view->canvas, on_canvas_event, LV_EVENT_PRESSED, view);
    lv_obj_add_event_cb(view->canvas, on_canvas_event, LV_EVENT_PRESSING, view);
    lv_obj_add_event_cb(view->canvas, on_canvas_event, LV_EVENT_RELEASED, view);
    lv_obj_add_event_cb(view->canvas, on_canvas_event, LV_EVENT_PRESS_LOST, view);

    view->rail = lv_obj_create(view->root);
    if (view->rail == NULL) {
        return view_init_failed(view, "rail");
    }
    make_plain(view->rail);
    lv_obj_set_size(view->rail, LAB10_RAIL_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(view->rail, LAB10_RAIL_X, 0);
    lv_obj_set_style_bg_color(view->rail, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(view->rail, LV_OPA_COVER, 0);
    lv_obj_set_clickable(view->rail, true);
    lv_obj_add_event_cb(view->rail, on_rail_event, LV_EVENT_PRESSED, view);
    lv_obj_add_event_cb(view->rail, on_rail_event, LV_EVENT_PRESSING, view);
    lv_obj_add_event_cb(view->rail, on_rail_event, LV_EVENT_RELEASED, view);
    lv_obj_add_event_cb(view->rail, on_rail_event, LV_EVENT_PRESS_LOST, view);

    for (uint8_t index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *geometry = lab10_swatch_geometry(index);
        if (geometry == NULL) {
            return view_init_failed(view, "swatch_geometry");
        }
        lv_obj_t *swatch = lv_obj_create(view->rail);
        if (swatch == NULL) {
            return view_init_failed(view, "swatch");
        }
        make_circle(swatch, geometry->color_rgb, LAB10_SWATCH_DIAMETER);
        lv_obj_set_pos(swatch, geometry->center_x - LAB10_RAIL_X - LAB10_SWATCH_RADIUS,
                       geometry->center_y - LAB10_SWATCH_RADIUS);
        lv_obj_set_clickable(swatch, true);
        lv_obj_add_event_cb(swatch, on_palette_event, LV_EVENT_PRESSED, view);
        view->swatches[index] = swatch;
    }
    for (uint8_t index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        refresh_swatch(view, index);
    }

    view->transition_layer = lv_obj_create(view->root);
    if (view->transition_layer == NULL) {
        return view_init_failed(view, "transition_layer");
    }
    make_plain(view->transition_layer);
    lv_obj_set_size(view->transition_layer, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(view->transition_layer, 0, 0);
    lv_obj_set_style_bg_opa(view->transition_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(view->transition_layer, false);

    view->bloom = lv_obj_create(view->transition_layer);
    if (view->bloom == NULL) {
        return view_init_failed(view, "bloom");
    }
    make_circle(view->bloom, 0xE53935, 44);
    lv_obj_set_hidden(view->bloom, true);

    view->ring = lv_arc_create(view->transition_layer);
    if (view->ring == NULL) {
        return view_init_failed(view, "ring");
    }
    lv_obj_remove_style_all(view->ring);
    lv_obj_set_scrollable(view->ring, false);
    lv_obj_set_clickable(view->ring, false);
    lv_obj_set_style_bg_opa(view->ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(view->ring, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(view->ring, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(view->ring, lv_color_hex(0xE53935), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(view->ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(view->ring, 0, 360);
    lv_arc_set_angles(view->ring, 0, 360);
    lv_obj_set_hidden(view->ring, true);

    view->outcome_panel = lv_obj_create(view->root);
    if (view->outcome_panel == NULL) {
        return view_init_failed(view, "outcome_panel");
    }
    make_plain(view->outcome_panel);
    lv_obj_set_size(view->outcome_panel, 200, 64);
    lv_obj_set_pos(view->outcome_panel, 20, 88);
    lv_obj_set_style_radius(view->outcome_panel, 8, 0);
    lv_obj_set_style_bg_color(view->outcome_panel, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(view->outcome_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(view->outcome_panel, 2, 0);
    lv_obj_set_style_border_color(view->outcome_panel, lv_color_hex(0xE8C040), 0);
    lv_obj_set_hidden(view->outcome_panel, true);
    view->outcome_label = lv_label_create(view->outcome_panel);
    if (view->outcome_label == NULL) {
        return view_init_failed(view, "outcome_label");
    }
    lv_obj_set_style_text_color(view->outcome_label, lv_color_hex(0xF8F8F8), 0);
    lv_label_set_text(view->outcome_label, "");
    lv_obj_center(view->outcome_label);
    return true;
}

void lab10_ui_view_deinit(lab10_ui_view_t *view)
{
    release_view_resources(view);
}

void lab10_ui_view_set_physical_ring_available(lab10_ui_view_t *view,
                                               bool available)
{
    if (view != NULL) {
        view->physical_ring_available = available;
    }
}

void lab10_ui_view_enter_fab_bloom(lab10_ui_view_t *view, int16_t origin_x,
                                   int16_t origin_y)
{
    if (view == NULL || s_anim_view != NULL ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_IDLE) {
        return;
    }
    lab10_ui_enter_fab_bloom(&view->model);
    view->fab_origin_x = origin_x;
    view->fab_origin_y = origin_y;
    const lab10_ui_circle_transition_t transition =
        lab10_ui_fab_entry_transition(origin_x, origin_y);
    start_circle_animation(view, view->bloom, transition.origin_x,
                           transition.origin_y, transition.radius_start,
                           transition.radius_end, 380, entry_animation_done);
}

void lab10_ui_view_enter_ring_wipe(lab10_ui_view_t *view, int16_t center_x,
                                   int16_t center_y)
{
    if (view == NULL || !view->physical_ring_available || s_anim_view != NULL ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_IDLE) {
        return;
    }
    lab10_ui_enter_ring_wipe(&view->model);
    start_circle_animation(view, view->ring, center_x, center_y, 18, 360, 600,
                           entry_animation_done);
}

void lab10_ui_view_present_state(lab10_ui_view_t *view,
                                 lab10_screen_state_t state)
{
    if (view == NULL) {
        return;
    }
    if (state == LAB10_SCREEN_RECORDING) {
        lv_obj_set_hidden(view->outcome_panel, true);
        return;
    }
    if (state == LAB10_SCREEN_STOPPED || state == LAB10_SCREEN_CANCELLED ||
        state == LAB10_SCREEN_FAILED || state == LAB10_SCREEN_COMPLETED) {
        lv_label_set_text(view->outcome_label, outcome_text(state));
        lv_obj_set_hidden(view->outcome_panel, false);
        lv_obj_move_foreground(view->outcome_panel);
    } else if (state == LAB10_SCREEN_IDLE) {
        lv_obj_set_hidden(view->outcome_panel, true);
    }
}

static lab10_screen_state_t capture_outcome_state(
    lab10_capture_terminal_outcome_t outcome)
{
    switch (outcome) {
    case LAB10_CAPTURE_OUTCOME_COMPLETED:
        return LAB10_SCREEN_COMPLETED;
    case LAB10_CAPTURE_OUTCOME_STOPPED:
        return LAB10_SCREEN_STOPPED;
    case LAB10_CAPTURE_OUTCOME_CANCELLED:
        return LAB10_SCREEN_CANCELLED;
    case LAB10_CAPTURE_OUTCOME_DURATION_CAP:
    case LAB10_CAPTURE_OUTCOME_POINT_CAP:
    case LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW:
    case LAB10_CAPTURE_OUTCOME_FAILED:
    case LAB10_CAPTURE_OUTCOME_NONE:
    default:
        return LAB10_SCREEN_FAILED;
    }
}

static void present_capture_outcome(lab10_ui_view_t *view)
{
    if (view == NULL || view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    lab10_capture_stats_t stats;
    if (!lab10_capture_get_stats(view->capture, view->capture_session_id,
                                 &stats) ||
        stats.terminal_event_count != 1) {
        return;
    }
    if (view->scripted_started &&
        atomic_load_explicit(&s_scripted_start_delivery_required,
                             memory_order_acquire)) {
        return;
    }
    /* Do not advance the UI until the one-slot handoff accepts this outcome.
     * If app_main has not drained the previous terminal yet, the regular UI
     * poll retries without overwriting it or blocking the LVGL owner.
     */
    if (!publish_pending_capture_outcome(stats.terminal_outcome)) {
        return;
    }
    lab10_ui_present_outcome(
        &view->model, capture_outcome_state(stats.terminal_outcome));
    lab10_ui_view_present_state(view, lab10_ui_state(&view->model));
}

void lab10_ui_view_stop_capture(lab10_ui_view_t *view)
{
    if (view == NULL || view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    if (lab10_capture_stop(view->capture, view->capture_session_id)) {
        present_capture_outcome(view);
    }
}

void lab10_ui_view_cancel_capture(lab10_ui_view_t *view)
{
    if (view == NULL || view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID ||
        lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING) {
        return;
    }
    if (lab10_capture_cancel(view->capture, view->capture_session_id)) {
        present_capture_outcome(view);
    }
}

lab10_capture_t *lab10_ui_view_capture(lab10_ui_view_t *view)
{
    return view != NULL ? view->capture : NULL;
}

void lab10_ui_view_exit_reverse(lab10_ui_view_t *view)
{
    if (view == NULL || s_anim_view != NULL ||
        (lab10_ui_state(&view->model) != LAB10_SCREEN_RECORDING &&
         lab10_ui_state(&view->model) != LAB10_SCREEN_STOPPED &&
         lab10_ui_state(&view->model) != LAB10_SCREEN_CANCELLED &&
         lab10_ui_state(&view->model) != LAB10_SCREEN_FAILED &&
         lab10_ui_state(&view->model) != LAB10_SCREEN_COMPLETED)) {
        return;
    }
    lab10_ui_exit_reverse(&view->model);
    const lab10_ui_circle_transition_t transition =
        lab10_ui_fab_exit_transition(view->fab_origin_x, view->fab_origin_y);
    start_circle_animation(view, view->bloom, transition.origin_x,
                           transition.origin_y, transition.radius_start,
                           transition.radius_end, 400, exit_animation_done);
}

void lab10_ui_view_poll(lab10_ui_view_t *view)
{
    if (view == NULL || view->capture == NULL ||
        view->capture_session_id == LAB10_CAPTURE_INVALID_SESSION_ID) {
        return;
    }
    if (lab10_capture_status(view->capture, view->capture_session_id) ==
        LAB10_CAPTURE_STATUS_ACTIVE) {
        (void)lab10_capture_poll(view->capture, view->capture_session_id);
    }
    if (view->renderer != NULL) {
        (void)lab10_render_note_lock_state(
            view->renderer, view->capture_session_id,
            LAB10_RENDER_LOCK_UI_TASK_OWNED);
        (void)lab10_render_update(view->renderer, view->capture,
                                  view->capture_session_id);
    }
    if (lab10_ui_state(&view->model) == LAB10_SCREEN_RECORDING &&
        lab10_capture_status(view->capture, view->capture_session_id) ==
            LAB10_CAPTURE_STATUS_TERMINAL) {
        present_capture_outcome(view);
    }
}
