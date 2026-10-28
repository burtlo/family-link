#include "lab10_capture.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum {
    LAB10_CAPTURE_LIFECYCLE_UNINITIALIZED = 0,
    LAB10_CAPTURE_LIFECYCLE_ACTIVE,
    LAB10_CAPTURE_LIFECYCLE_TERMINATING,
    LAB10_CAPTURE_LIFECYCLE_TERMINAL,
};

/*
 * The gate is a generation-aware admission CAS. UINT64_MAX is transiently
 * owned by the producer while it increments in_flight_producers; zero closes
 * admission. All usable session generations are full-width 64-bit values
 * below that private claim marker. The count is never reset during a session
 * reset; terminalization closes the gate and drains it first.
 */
#define LAB10_CAPTURE_ADMISSION_CLAIMED UINT64_MAX

struct lab10_capture {
    lab10_capture_config_t config;
    lab10_capture_raw_report_observer_fn raw_report_observer;
    void *raw_report_observer_context;
    lab10_capture_point_t *points;
    lab10_capture_raw_report_t raw_fifo[LAB10_CAPTURE_RAW_FIFO_CAPACITY];

    _Atomic uint32_t queue_head;
    _Atomic uint32_t queue_tail;
    _Atomic uint32_t raw_report_count;
    _Atomic uint32_t queue_overflow_count;
    _Atomic uint32_t queue_depth_high_water;
    _Atomic uint32_t lifecycle;
    _Atomic uint32_t in_flight_producers;
    _Atomic bool overflow_latched;
    bool allocator_configuration_valid;

    _Atomic lab10_capture_session_id_t session_id;
    _Atomic lab10_capture_session_id_t admission_gate;
    uint64_t start_timestamp_us;
    uint64_t last_timestamp_us;
    bool has_last_timestamp;

    lab10_capture_status_t status;
    lab10_capture_terminal_outcome_t terminal_outcome;
    lab10_capture_failure_reason_t failure_reason;
    lab10_capture_stats_t stats;

    size_t point_count;
    uint8_t selected_color_index;
    bool active_stroke;
    uint8_t active_touch_id;
    uint32_t active_stroke_id;
    uint32_t next_stroke_id;
    bool has_last_point;
    bool last_processed_was_move;
    lab10_capture_point_t last_point;
};

static void *default_alloc(size_t size, void *context)
{
    (void)context;
    return malloc(size);
}

static void default_free(void *memory, void *context)
{
    (void)context;
    free(memory);
}

static void clear_queue(lab10_capture_t *capture)
{
    atomic_store_explicit(&capture->queue_tail, 0, memory_order_release);
    atomic_store_explicit(&capture->queue_head, 0, memory_order_release);
    capture->stats.queue_depth_current = 0;
}

static uint32_t queue_depth(const lab10_capture_t *capture)
{
    const uint32_t head =
        atomic_load_explicit(&capture->queue_head, memory_order_acquire);
    const uint32_t tail =
        atomic_load_explicit(&capture->queue_tail, memory_order_acquire);
    return head - tail;
}

static void update_queue_high_water(lab10_capture_t *capture, uint32_t depth)
{
    uint32_t previous = atomic_load_explicit(&capture->queue_depth_high_water,
                                             memory_order_relaxed);
    while (depth > previous &&
           !atomic_compare_exchange_weak_explicit(
               &capture->queue_depth_high_water, &previous, depth,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static bool session_matches(const lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id)
{
    return capture != NULL && session_id != LAB10_CAPTURE_INVALID_SESSION_ID &&
           atomic_load_explicit(&capture->session_id, memory_order_acquire) ==
               session_id;
}

static bool timestamp_reached_duration_cap(const lab10_capture_t *capture,
                                           uint64_t timestamp_us)
{
    return timestamp_us >= capture->start_timestamp_us &&
           timestamp_us - capture->start_timestamp_us >=
               LAB10_CAPTURE_DURATION_CAP_US;
}

static bool clock_reached_duration_cap(const lab10_capture_t *capture)
{
    if (capture->config.clock_now_us == NULL) {
        return false;
    }
    return timestamp_reached_duration_cap(
        capture, capture->config.clock_now_us(capture->config.clock_context));
}

static bool queue_overflow_latched(const lab10_capture_t *capture)
{
    return atomic_load_explicit(&capture->overflow_latched,
                                memory_order_acquire);
}

static void reset_session_state(lab10_capture_t *capture)
{
    /*
     * start() closes admission before calling this function.  A terminal
     * session has already drained its admitted producers, while a paused
     * pre-admission producer cannot pass the closed generation gate.  The
     * in-flight count is therefore already zero, but it is deliberately not
     * reset here: an old producer must never be able to increment a counter
     * that has been reused for a newer session.  The overflow latch and queue
     * fields are reset only after that same admission boundary is closed.
     */
    memset(capture->raw_fifo, 0, sizeof(capture->raw_fifo));
    atomic_store_explicit(&capture->queue_head, 0, memory_order_relaxed);
    atomic_store_explicit(&capture->queue_tail, 0, memory_order_relaxed);
    atomic_store_explicit(&capture->raw_report_count, 0, memory_order_relaxed);
    atomic_store_explicit(&capture->queue_overflow_count, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&capture->queue_depth_high_water, 0,
                          memory_order_relaxed);
    atomic_store_explicit(&capture->lifecycle,
                          LAB10_CAPTURE_LIFECYCLE_UNINITIALIZED,
                          memory_order_relaxed);
    atomic_store_explicit(&capture->overflow_latched, false,
                          memory_order_relaxed);

    capture->start_timestamp_us = 0;
    capture->last_timestamp_us = 0;
    capture->has_last_timestamp = false;
    capture->status = LAB10_CAPTURE_STATUS_UNINITIALIZED;
    capture->terminal_outcome = LAB10_CAPTURE_OUTCOME_NONE;
    capture->failure_reason = LAB10_CAPTURE_FAILURE_NONE;
    capture->stats = (lab10_capture_stats_t){0};
    capture->point_count = 0;
    capture->selected_color_index = LAB10_COLOR_WHITE;
    capture->active_stroke = false;
    capture->active_touch_id = 0;
    capture->active_stroke_id = 0;
    capture->next_stroke_id = 0;
    capture->has_last_point = false;
    capture->last_processed_was_move = false;
    capture->last_point = (lab10_capture_point_t){0};
}

static void close_producer_admission(lab10_capture_t *capture)
{
    for (;;) {
        lab10_capture_session_id_t expected =
            atomic_load_explicit(&capture->admission_gate, memory_order_acquire);
        if (expected == LAB10_CAPTURE_INVALID_SESSION_ID) {
            return;
        }
        if (expected == LAB10_CAPTURE_ADMISSION_CLAIMED) {
            /*
             * A producer has won the admission CAS but has not yet released
             * the short reservation.  Terminalization may wait here; the
             * producer never waits and will either publish the reservation or
             * complete its admitted bounded section.
             */
            continue;
        }
        if (atomic_compare_exchange_weak_explicit(
                &capture->admission_gate, &expected,
                LAB10_CAPTURE_INVALID_SESSION_ID, memory_order_acq_rel,
                memory_order_acquire)) {
            return;
        }
    }
}

static void open_producer_admission(lab10_capture_t *capture,
                                    lab10_capture_session_id_t session_id)
{
    atomic_store_explicit(&capture->admission_gate, session_id,
                          memory_order_release);
}

static void wait_for_admitted_producers(lab10_capture_t *capture)
{
    while (atomic_load_explicit(&capture->in_flight_producers,
                                memory_order_acquire) != 0) {
    }
}

static void release_point_arena(lab10_capture_t *capture)
{
    if (capture->points == NULL) {
        capture->point_count = 0;
        return;
    }
    capture->config.free(capture->points, capture->config.allocator_context);
    capture->points = NULL;
    capture->point_count = 0;
}

static bool claim_terminalization(lab10_capture_t *capture)
{
    uint32_t expected = LAB10_CAPTURE_LIFECYCLE_ACTIVE;
    if (!atomic_compare_exchange_strong_explicit(
            &capture->lifecycle, &expected,
            LAB10_CAPTURE_LIFECYCLE_TERMINATING, memory_order_acq_rel,
            memory_order_acquire)) {
        return false;
    }

    close_producer_admission(capture);

#ifdef LAB10_CAPTURE_TEST_HOOKS
    if (capture->config.test_terminalization_hook != NULL) {
        capture->config.test_terminalization_hook(
            capture->config.test_terminalization_context);
    }
#endif
    /*
     * A producer is admitted only after its second ACTIVE observation.  Once
     * admitted, it owns this bounded section through the FIFO write or the
     * overflow latch.  Terminal cleanup waits for that section before it can
     * clear the queue.
     */
    wait_for_admitted_producers(capture);
    return true;
}

static bool finish_capture(lab10_capture_t *capture,
                           lab10_capture_terminal_outcome_t outcome,
                           lab10_capture_failure_reason_t failure_reason)
{
    if (!claim_terminalization(capture)) {
        return false;
    }

    /* A producer-side overflow latched before publication outranks all exits. */
    if (queue_overflow_latched(capture)) {
        outcome = LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW;
        failure_reason = LAB10_CAPTURE_FAILURE_NONE;
    }
    if (outcome == LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW) {
        ++capture->stats.trace_loss_count;
        capture->stats.queue_overflow_count = atomic_load_explicit(
            &capture->queue_overflow_count, memory_order_acquire);
    }

    clear_queue(capture);

    capture->active_stroke = false;
    capture->active_touch_id = 0;
    capture->active_stroke_id = 0;
    capture->has_last_point = false;
    capture->last_processed_was_move = false;
    capture->status = LAB10_CAPTURE_STATUS_TERMINAL;
    capture->terminal_outcome = outcome;
    capture->failure_reason = failure_reason;
    capture->stats.status = capture->status;
    capture->stats.terminal_outcome = outcome;
    capture->stats.failure_reason = failure_reason;
    ++capture->stats.terminal_event_count;
    atomic_store_explicit(&capture->lifecycle,
                          LAB10_CAPTURE_LIFECYCLE_TERMINAL,
                          memory_order_release);
    return true;
}

static bool finish_duration_cap(lab10_capture_t *capture)
{
    const bool terminalized =
        finish_capture(capture, LAB10_CAPTURE_OUTCOME_DURATION_CAP,
                       LAB10_CAPTURE_FAILURE_NONE);
    if (terminalized &&
        capture->terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP) {
        ++capture->stats.duration_cap_hit_count;
    }
    return terminalized;
}

static bool begin_submit(lab10_capture_t *capture,
                         lab10_capture_session_id_t session_id)
{
    if (atomic_load_explicit(&capture->lifecycle, memory_order_acquire) !=
            LAB10_CAPTURE_LIFECYCLE_ACTIVE ||
        atomic_load_explicit(&capture->session_id, memory_order_acquire) !=
            session_id) {
        return false;
    }

#ifdef LAB10_CAPTURE_TEST_HOOKS
    if (capture->config.test_submit_before_admission_hook != NULL) {
        capture->config.test_submit_before_admission_hook(
            capture->config.test_submit_before_admission_context);
    }
#endif

    lab10_capture_session_id_t expected_gate =
        atomic_load_explicit(&capture->admission_gate, memory_order_acquire);
    if (expected_gate != session_id) {
        return false;
    }

    const lab10_capture_session_id_t claimed_gate =
        LAB10_CAPTURE_ADMISSION_CLAIMED;
    if (!atomic_compare_exchange_strong_explicit(
            &capture->admission_gate, &expected_gate, claimed_gate,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }

    atomic_fetch_add_explicit(&capture->in_flight_producers, 1,
                              memory_order_acq_rel);
    atomic_store_explicit(&capture->admission_gate, session_id,
                          memory_order_release);
    if (atomic_load_explicit(&capture->lifecycle, memory_order_acquire) !=
            LAB10_CAPTURE_LIFECYCLE_ACTIVE ||
        atomic_load_explicit(&capture->session_id, memory_order_acquire) !=
            session_id) {
        atomic_fetch_sub_explicit(&capture->in_flight_producers, 1,
                                  memory_order_release);
        return false;
    }
#ifdef LAB10_CAPTURE_TEST_HOOKS
    if (capture->config.test_submit_admitted_hook != NULL) {
        capture->config.test_submit_admitted_hook(
            capture->config.test_submit_admitted_context);
    }
#endif
    return true;
}

static void end_submit(lab10_capture_t *capture)
{
    atomic_fetch_sub_explicit(&capture->in_flight_producers, 1,
                              memory_order_release);
}

static bool latch_queue_overflow(lab10_capture_t *capture)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &capture->overflow_latched, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return false;
    }
    atomic_fetch_add_explicit(&capture->queue_overflow_count, 1,
                              memory_order_relaxed);
    return true;
}

static void finish_allocation_failure(lab10_capture_t *capture,
                                      lab10_capture_failure_reason_t reason)
{
    ++capture->stats.allocation_failure_count;
    finish_capture(capture, LAB10_CAPTURE_OUTCOME_FAILED, reason);
    release_point_arena(capture);
}

static void record_rejection(lab10_capture_t *capture,
                             lab10_capture_reject_reason_t reason)
{
    ++capture->stats.rejected_report_count;
    capture->last_processed_was_move = false;
    switch (reason) {
        case LAB10_CAPTURE_REJECT_RAIL_TOUCH:
            ++capture->stats.rejected_rail_touch_count;
            break;
        case LAB10_CAPTURE_REJECT_OUTSIDE_DISPLAY:
            ++capture->stats.rejected_outside_display_count;
            break;
        case LAB10_CAPTURE_REJECT_SECOND_TOUCH:
        case LAB10_CAPTURE_REJECT_WRONG_TOUCH:
            ++capture->stats.rejected_second_touch_count;
            break;
        case LAB10_CAPTURE_REJECT_INVALID_TIMESTAMP:
            ++capture->stats.invalid_timestamp_count;
            break;
        case LAB10_CAPTURE_REJECT_INVALID_COORDINATE:
            ++capture->stats.invalid_coordinate_count;
            break;
        default:
            break;
    }
}

static bool route_matches_coordinates(const lab10_capture_raw_report_t *report)
{
    const bool on_display =
        lab10_display_contains(report->display_x, report->display_y);
    const bool on_canvas =
        lab10_canvas_contains(report->display_x, report->display_y);
    const bool on_rail = lab10_rail_contains(report->display_x, report->display_y);
    const int palette =
        lab10_palette_hit_test(report->display_x, report->display_y);

    switch (report->route) {
        case LAB10_ROUTE_OUTSIDE:
            return !on_display;
        case LAB10_ROUTE_CANVAS:
            return on_canvas;
        case LAB10_ROUTE_PALETTE:
            return on_rail && palette >= 0;
        case LAB10_ROUTE_RAIL_REJECTED:
            return on_rail && palette < 0;
        default:
            return false;
    }
}

static bool append_point(lab10_capture_t *capture,
                         const lab10_capture_raw_report_t *report,
                         uint8_t logical_x, uint8_t logical_y,
                         lab10_capture_point_boundary_t boundary)
{
    if (capture->point_count >= LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY) {
        ++capture->stats.point_cap_hit_count;
        /* Product P7: full sketch ignores further ink; recording continues. */
        return false;
    }

    lab10_capture_point_t point = {
        .timestamp_us = report->timestamp_us,
        .display_x = report->display_x,
        .display_y = report->display_y,
        .logical_x = logical_x,
        .logical_y = logical_y,
        .color_index = capture->selected_color_index,
        .touch_id = report->touch_id,
        .phase = report->phase,
        .stroke_id = capture->active_stroke_id,
        .boundary = boundary,
        .outcome = LAB10_CAPTURE_POINT_OUTCOME_ACCEPTED,
    };
    capture->points[capture->point_count++] = point;
    capture->last_point = point;
    capture->has_last_point = true;
    ++capture->stats.accepted_point_count;
    capture->stats.accepted_color_mask |=
        (uint8_t)(1u << point.color_index);
    if (point.display_x == 0 || point.display_x == LAB10_CANVAS_WIDTH - 1 ||
        point.display_y == 0 || point.display_y == LAB10_CANVAS_HEIGHT - 1) {
        ++capture->stats.edge_point_count;
    }

    return true;
}

static void close_stroke(lab10_capture_t *capture)
{
    if (!capture->active_stroke) {
        return;
    }
    ++capture->stats.stroke_boundary_count;
    capture->active_stroke = false;
    capture->active_touch_id = 0;
    capture->active_stroke_id = 0;
    capture->has_last_point = false;
    capture->last_processed_was_move = false;
}

static void process_report(lab10_capture_t *capture,
                           const lab10_capture_raw_report_t *report)
{
    if (queue_overflow_latched(capture)) {
        return;
    }

    if (report->timestamp_us < capture->start_timestamp_us ||
        (capture->has_last_timestamp &&
         report->timestamp_us < capture->last_timestamp_us)) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_INVALID_TIMESTAMP);
        return;
    }
    capture->last_timestamp_us = report->timestamp_us;
    capture->has_last_timestamp = true;

    if (timestamp_reached_duration_cap(capture, report->timestamp_us)) {
        (void)finish_duration_cap(capture);
        return;
    }

    if (capture->active_stroke &&
        report->touch_id != capture->active_touch_id) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_SECOND_TOUCH);
        return;
    }

    if (report->phase == LAB10_PHASE_PALETTE_CHANGE) {
        if (report->route != LAB10_ROUTE_PALETTE ||
            !route_matches_coordinates(report)) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_RAIL_TOUCH);
            return;
        }
        const int palette =
            lab10_palette_hit_test(report->display_x, report->display_y);
        if (palette < 0 || report->color_index >= LAB10_SWATCH_COUNT ||
            report->color_index != (uint8_t)palette) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_INVALID_COLOR);
            return;
        }
        ++capture->stats.palette_hit_count;
        if (capture->selected_color_index != report->color_index) {
            capture->selected_color_index = report->color_index;
            ++capture->stats.color_change_count;
        }
        capture->last_processed_was_move = false;
        return;
    }

    if (report->phase != LAB10_PHASE_DOWN &&
        report->phase != LAB10_PHASE_MOVE && report->phase != LAB10_PHASE_UP &&
        report->phase != LAB10_PHASE_LEAVE) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_INVALID_PHASE);
        return;
    }

    if (report->phase == LAB10_PHASE_UP || report->phase == LAB10_PHASE_LEAVE) {
        if (!capture->active_stroke) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_NO_ACTIVE_STROKE);
            return;
        }
        close_stroke(capture);
        return;
    }

    if (!route_matches_coordinates(report)) {
        if (report->route == LAB10_ROUTE_OUTSIDE) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_OUTSIDE_DISPLAY);
        } else if (report->route == LAB10_ROUTE_RAIL_REJECTED ||
                   report->route == LAB10_ROUTE_PALETTE) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_RAIL_TOUCH);
        } else {
            record_rejection(capture, LAB10_CAPTURE_REJECT_INVALID_COORDINATE);
        }
        return;
    }

    if (report->route != LAB10_ROUTE_CANVAS) {
        record_rejection(capture, report->route == LAB10_ROUTE_OUTSIDE
                                      ? LAB10_CAPTURE_REJECT_OUTSIDE_DISPLAY
                                      : LAB10_CAPTURE_REJECT_RAIL_TOUCH);
        return;
    }
    if (report->color_index >= LAB10_SWATCH_COUNT) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_INVALID_COLOR);
        return;
    }
    if (report->color_index != capture->selected_color_index) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_STALE_COLOR);
        return;
    }

    uint8_t logical_x = 0;
    uint8_t logical_y = 0;
    lab10_map_to_logical(report->display_x, report->display_y, &logical_x,
                         &logical_y);

    if (report->phase == LAB10_PHASE_DOWN) {
        if (capture->active_stroke) {
            record_rejection(capture, LAB10_CAPTURE_REJECT_ACTIVE_STROKE);
            return;
        }
        capture->active_stroke = true;
        capture->active_touch_id = report->touch_id;
        capture->next_stroke_id++;
        if (capture->next_stroke_id == 0) {
            capture->next_stroke_id = 1;
        }
        capture->active_stroke_id = capture->next_stroke_id;
        ++capture->stats.stroke_count;
        capture->last_processed_was_move = false;
        (void)append_point(capture, report, logical_x, logical_y,
                           LAB10_CAPTURE_POINT_BOUNDARY_START);
        return;
    }

    if (!capture->active_stroke) {
        record_rejection(capture, LAB10_CAPTURE_REJECT_NO_ACTIVE_STROKE);
        return;
    }

    const int dx = (int)report->display_x - (int)capture->last_point.display_x;
    const int dy = (int)report->display_y - (int)capture->last_point.display_y;
    const unsigned int distance_squared = (unsigned int)(dx * dx + dy * dy);
    if (capture->last_processed_was_move && capture->has_last_point &&
        capture->last_point.stroke_id == capture->active_stroke_id &&
        capture->last_point.color_index == capture->selected_color_index &&
        capture->last_point.logical_x == logical_x &&
        capture->last_point.logical_y == logical_y &&
        distance_squared <
            LAB10_CAPTURE_MIN_MOVE_DISTANCE_PX *
                LAB10_CAPTURE_MIN_MOVE_DISTANCE_PX) {
        ++capture->stats.coalesced_move_count;
        if (dx == 0 && dy == 0) {
            ++capture->stats.duplicate_point_count;
        }
        capture->last_processed_was_move = true;
        return;
    }

    capture->last_processed_was_move = true;
    (void)append_point(capture, report, logical_x, logical_y,
                       LAB10_CAPTURE_POINT_BOUNDARY_CONTINUE);
}

lab10_capture_t *lab10_capture_create(const lab10_capture_config_t *config)
{
    lab10_capture_t *capture = calloc(1, sizeof(*capture));
    if (capture == NULL) {
        return NULL;
    }
    capture->config.clock_now_us = config != NULL ? config->clock_now_us : NULL;
    capture->config.clock_context = config != NULL ? config->clock_context : NULL;
    capture->config.alloc = config != NULL && config->alloc != NULL
                                ? config->alloc
                                : default_alloc;
    capture->config.free = config != NULL && config->free != NULL
                               ? config->free
                               : default_free;
    capture->config.allocator_context =
        config != NULL ? config->allocator_context : NULL;
    capture->config.initial_color_index =
        config != NULL ? config->initial_color_index : LAB10_COLOR_WHITE;
#ifdef LAB10_CAPTURE_TEST_HOOKS
    capture->config.test_submit_before_admission_hook =
        config != NULL ? config->test_submit_before_admission_hook : NULL;
    capture->config.test_submit_before_admission_context =
        config != NULL ? config->test_submit_before_admission_context : NULL;
    capture->config.test_submit_admitted_hook =
        config != NULL ? config->test_submit_admitted_hook : NULL;
    capture->config.test_submit_admitted_context =
        config != NULL ? config->test_submit_admitted_context : NULL;
    capture->config.test_terminalization_hook =
        config != NULL ? config->test_terminalization_hook : NULL;
    capture->config.test_terminalization_context =
        config != NULL ? config->test_terminalization_context : NULL;
#endif
    if (config == NULL) {
        capture->config.initial_color_index = LAB10_COLOR_WHITE;
    }
    capture->allocator_configuration_valid =
        config == NULL ||
        ((config->alloc == NULL) == (config->free == NULL));
    reset_session_state(capture);
    return capture;
}

void lab10_capture_destroy(lab10_capture_t *capture)
{
    if (capture == NULL) {
        return;
    }
    release_point_arena(capture);
    free(capture);
}

bool lab10_capture_set_raw_report_observer(
    lab10_capture_t *capture, lab10_capture_raw_report_observer_fn observer,
    void *context)
{
    if (capture == NULL || capture->status == LAB10_CAPTURE_STATUS_ACTIVE) {
        return false;
    }
    capture->raw_report_observer = observer;
    capture->raw_report_observer_context = context;
    return true;
}

uint64_t lab10_capture_now_us(const lab10_capture_t *capture)
{
    if (capture == NULL || capture->config.clock_now_us == NULL) {
        return 0;
    }
    return capture->config.clock_now_us(capture->config.clock_context);
}

lab10_capture_session_id_t lab10_capture_start(lab10_capture_t *capture)
{
    if (capture == NULL) {
        return LAB10_CAPTURE_INVALID_SESSION_ID;
    }
    if (capture->status == LAB10_CAPTURE_STATUS_ACTIVE) {
        /* A live operation must terminalize through stop/cancel/complete. */
        return atomic_load_explicit(&capture->session_id, memory_order_acquire);
    }

    close_producer_admission(capture);
    lab10_capture_session_id_t session_id =
        atomic_load_explicit(&capture->session_id, memory_order_relaxed);
    if (session_id >= LAB10_CAPTURE_SESSION_GENERATION_MAX) {
        /* Never reuse an earlier generation after the 64-bit boundary. */
        return LAB10_CAPTURE_INVALID_SESSION_ID;
    }
    ++session_id;
    release_point_arena(capture);
    reset_session_state(capture);
    atomic_store_explicit(&capture->session_id, session_id,
                          memory_order_release);
    atomic_store_explicit(&capture->lifecycle, LAB10_CAPTURE_LIFECYCLE_ACTIVE,
                          memory_order_release);
    capture->stats.selected_color_index = capture->config.initial_color_index;
    capture->selected_color_index = capture->config.initial_color_index;
    if (capture->config.initial_color_index >= LAB10_SWATCH_COUNT ||
        !capture->allocator_configuration_valid) {
        finish_capture(capture, LAB10_CAPTURE_OUTCOME_FAILED,
                       LAB10_CAPTURE_FAILURE_INVALID_CONFIGURATION);
        return session_id;
    }

    capture->points = capture->config.alloc(
        sizeof(*capture->points) * LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
        capture->config.allocator_context);
    if (capture->points == NULL) {
        finish_allocation_failure(capture, LAB10_CAPTURE_FAILURE_ALLOCATION);
        return session_id;
    }
    memset(capture->points, 0,
           sizeof(*capture->points) * LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY);
    capture->start_timestamp_us = lab10_capture_now_us(capture);
    capture->status = LAB10_CAPTURE_STATUS_ACTIVE;
    capture->stats.status = capture->status;
    capture->stats.selected_color_index = capture->selected_color_index;
    open_producer_admission(capture, session_id);
    return session_id;
}

#ifdef LAB10_CAPTURE_TEST_HOOKS
bool lab10_capture_test_set_session_generation(
    lab10_capture_t *capture, lab10_capture_session_id_t generation)
{
    if (capture == NULL || capture->status != LAB10_CAPTURE_STATUS_UNINITIALIZED ||
        generation >= LAB10_CAPTURE_SESSION_GENERATION_MAX) {
        return false;
    }
    close_producer_admission(capture);
    atomic_store_explicit(&capture->session_id, generation, memory_order_release);
    return true;
}
#endif

lab10_capture_submit_result_t lab10_capture_submit(
    lab10_capture_t *capture, lab10_capture_session_id_t session_id,
    const lab10_capture_raw_report_t *report)
{
    if (capture == NULL || report == NULL) {
        return LAB10_CAPTURE_SUBMIT_TERMINAL;
    }
    if (!session_matches(capture, session_id)) {
        return LAB10_CAPTURE_SUBMIT_STALE_SESSION;
    }
    if (queue_overflow_latched(capture)) {
        return LAB10_CAPTURE_SUBMIT_TERMINAL;
    }
    if (!begin_submit(capture, session_id)) {
        return session_matches(capture, session_id)
                   ? LAB10_CAPTURE_SUBMIT_TERMINAL
                   : LAB10_CAPTURE_SUBMIT_STALE_SESSION;
    }

    /*
     * The generation must still be current after admission and immediately
     * before any queue-visible operation.  The admitted count prevents a
     * terminalizer from resetting this session until this producer leaves.
     */
    if (atomic_load_explicit(&capture->session_id, memory_order_acquire) !=
        session_id) {
        const lab10_capture_submit_result_t result =
            session_matches(capture, session_id)
                ? LAB10_CAPTURE_SUBMIT_TERMINAL
                : LAB10_CAPTURE_SUBMIT_STALE_SESSION;
        atomic_fetch_sub_explicit(&capture->in_flight_producers, 1,
                                  memory_order_release);
        return result;
    }

    atomic_fetch_add_explicit(&capture->raw_report_count, 1,
                              memory_order_relaxed);
    if (queue_overflow_latched(capture)) {
        end_submit(capture);
        return LAB10_CAPTURE_SUBMIT_TERMINAL;
    }
    const uint32_t head =
        atomic_load_explicit(&capture->queue_head, memory_order_relaxed);
    const uint32_t tail =
        atomic_load_explicit(&capture->queue_tail, memory_order_acquire);
    if (head - tail >= LAB10_CAPTURE_RAW_FIFO_CAPACITY) {
        (void)latch_queue_overflow(capture);
        end_submit(capture);
        return LAB10_CAPTURE_SUBMIT_QUEUE_OVERFLOW;
    }

    capture->raw_fifo[head % LAB10_CAPTURE_RAW_FIFO_CAPACITY] = *report;
    atomic_store_explicit(&capture->queue_head, head + 1, memory_order_release);
    update_queue_high_water(capture, head + 1 - tail);
    end_submit(capture);
    return LAB10_CAPTURE_SUBMIT_ENQUEUED;
}

size_t lab10_capture_poll(lab10_capture_t *capture,
                          lab10_capture_session_id_t session_id)
{
    if (!session_matches(capture, session_id) ||
        capture->status != LAB10_CAPTURE_STATUS_ACTIVE) {
        return 0;
    }

    size_t processed = 0;
    if (!queue_overflow_latched(capture) && clock_reached_duration_cap(capture)) {
        (void)finish_duration_cap(capture);
        return 0;
    }

    for (;;) {
        const uint32_t tail =
            atomic_load_explicit(&capture->queue_tail, memory_order_relaxed);
        const uint32_t head =
            atomic_load_explicit(&capture->queue_head, memory_order_acquire);
        if (tail == head) {
            break;
        }
        lab10_capture_raw_report_t report =
            capture->raw_fifo[tail % LAB10_CAPTURE_RAW_FIFO_CAPACITY];
        atomic_store_explicit(&capture->queue_tail, tail + 1,
                              memory_order_release);
        if (capture->raw_report_observer != NULL) {
            capture->raw_report_observer(
                &report, capture->raw_report_observer_context);
        }
        process_report(capture, &report);
        ++processed;
        if (capture->status != LAB10_CAPTURE_STATUS_ACTIVE) {
            return processed;
        }
        if (queue_overflow_latched(capture)) {
            continue;
        }
        if (clock_reached_duration_cap(capture)) {
            (void)finish_duration_cap(capture);
            return processed;
        }
    }

    if (queue_overflow_latched(capture)) {
        (void)finish_capture(capture, LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW,
                             LAB10_CAPTURE_FAILURE_NONE);
    }
    capture->stats.queue_depth_current = queue_depth(capture);
    return processed;
}

bool lab10_capture_stop(lab10_capture_t *capture,
                        lab10_capture_session_id_t session_id)
{
    if (!session_matches(capture, session_id)) {
        return false;
    }
    return finish_capture(capture, LAB10_CAPTURE_OUTCOME_STOPPED,
                          LAB10_CAPTURE_FAILURE_NONE);
}

bool lab10_capture_cancel(lab10_capture_t *capture,
                          lab10_capture_session_id_t session_id)
{
    if (!session_matches(capture, session_id)) {
        return false;
    }
    return finish_capture(capture, LAB10_CAPTURE_OUTCOME_CANCELLED,
                          LAB10_CAPTURE_FAILURE_NONE);
}

bool lab10_capture_complete(lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id)
{
    if (!session_matches(capture, session_id)) {
        return false;
    }
    return finish_capture(capture, LAB10_CAPTURE_OUTCOME_COMPLETED,
                          LAB10_CAPTURE_FAILURE_NONE);
}

lab10_capture_status_t lab10_capture_status(const lab10_capture_t *capture,
                                            lab10_capture_session_id_t session_id)
{
    return session_matches(capture, session_id)
               ? capture->status
               : LAB10_CAPTURE_STATUS_UNINITIALIZED;
}

bool lab10_capture_get_stats(const lab10_capture_t *capture,
                             lab10_capture_session_id_t session_id,
                             lab10_capture_stats_t *stats)
{
    if (!session_matches(capture, session_id) || stats == NULL) {
        return false;
    }
    *stats = capture->stats;
    stats->raw_report_count = atomic_load_explicit(&capture->raw_report_count,
                                                   memory_order_acquire);
    stats->queue_depth_current = queue_depth(capture);
    stats->queue_depth_high_water = atomic_load_explicit(
        &capture->queue_depth_high_water, memory_order_acquire);
    stats->queue_overflow_count = atomic_load_explicit(
        &capture->queue_overflow_count, memory_order_acquire);
    stats->selected_color_index = capture->selected_color_index;
    stats->active_stroke_id = capture->active_stroke_id;
    stats->status = capture->status;
    stats->terminal_outcome = capture->terminal_outcome;
    stats->failure_reason = capture->failure_reason;
    return true;
}

bool lab10_capture_snapshot(const lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            lab10_capture_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    *snapshot = (lab10_capture_snapshot_t){0};
    if (!session_matches(capture, session_id) || capture->points == NULL) {
        return false;
    }
    *snapshot = (lab10_capture_snapshot_t){
        .points = capture->points,
        .point_count = capture->point_count,
        .session_id = atomic_load_explicit(&capture->session_id,
                                           memory_order_acquire),
        .immutable = capture->status == LAB10_CAPTURE_STATUS_TERMINAL,
    };
    return true;
}

bool lab10_capture_point_at(const lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            size_t index, lab10_capture_point_t *point)
{
    if (!session_matches(capture, session_id) || point == NULL ||
        index >= capture->point_count || capture->points == NULL) {
        return false;
    }
    *point = capture->points[index];
    return true;
}

bool lab10_capture_release_result(lab10_capture_t *capture,
                                  lab10_capture_session_id_t session_id)
{
    if (!session_matches(capture, session_id) ||
        capture->status != LAB10_CAPTURE_STATUS_TERMINAL ||
        capture->points == NULL) {
        return false;
    }
    release_point_arena(capture);
    return true;
}

size_t lab10_capture_copy_pending_raw(const lab10_capture_t *capture,
                                      lab10_capture_session_id_t session_id,
                                      lab10_capture_raw_report_t *reports,
                                      size_t capacity)
{
    if (!session_matches(capture, session_id) || reports == NULL || capacity == 0) {
        return 0;
    }
    const uint32_t tail =
        atomic_load_explicit(&capture->queue_tail, memory_order_acquire);
    const uint32_t head =
        atomic_load_explicit(&capture->queue_head, memory_order_acquire);
    const uint32_t pending = head - tail;
    const size_t count = pending < capacity ? (size_t)pending : capacity;
    for (size_t index = 0; index < count; ++index) {
        reports[index] =
            capture->raw_fifo[(tail + (uint32_t)index) %
                              LAB10_CAPTURE_RAW_FIFO_CAPACITY];
    }
    return count;
}

const char *lab10_capture_reject_reason_name(lab10_capture_reject_reason_t reason)
{
    switch (reason) {
        case LAB10_CAPTURE_REJECT_OUTSIDE_DISPLAY:
            return "outside_display";
        case LAB10_CAPTURE_REJECT_RAIL_TOUCH:
            return "rail_touch";
        case LAB10_CAPTURE_REJECT_SECOND_TOUCH:
            return "second_touch";
        case LAB10_CAPTURE_REJECT_NO_ACTIVE_STROKE:
            return "no_active_stroke";
        case LAB10_CAPTURE_REJECT_WRONG_TOUCH:
            return "wrong_touch";
        case LAB10_CAPTURE_REJECT_ACTIVE_STROKE:
            return "active_stroke";
        case LAB10_CAPTURE_REJECT_INVALID_TIMESTAMP:
            return "invalid_timestamp";
        case LAB10_CAPTURE_REJECT_INVALID_COORDINATE:
            return "invalid_coordinate";
        case LAB10_CAPTURE_REJECT_INVALID_PHASE:
            return "invalid_phase";
        case LAB10_CAPTURE_REJECT_INVALID_COLOR:
            return "invalid_color";
        case LAB10_CAPTURE_REJECT_STALE_COLOR:
            return "stale_color";
        case LAB10_CAPTURE_REJECT_TERMINAL:
            return "terminal";
        case LAB10_CAPTURE_REJECT_NONE:
        default:
            return "none";
    }
}
const char *lab10_capture_outcome_name(lab10_capture_terminal_outcome_t outcome)
{
    switch (outcome) {
        case LAB10_CAPTURE_OUTCOME_COMPLETED:
            return "completed";
        case LAB10_CAPTURE_OUTCOME_STOPPED:
            return "stopped";
        case LAB10_CAPTURE_OUTCOME_CANCELLED:
            return "cancelled";
        case LAB10_CAPTURE_OUTCOME_DURATION_CAP:
            return "duration_cap";
        case LAB10_CAPTURE_OUTCOME_POINT_CAP:
            return "point_cap";
        case LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW:
            return "queue_overflow";
        case LAB10_CAPTURE_OUTCOME_FAILED:
            return "failed";
        case LAB10_CAPTURE_OUTCOME_NONE:
        default:
            return "none";
    }
}
