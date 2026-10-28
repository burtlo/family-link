#pragma once

/*
 * LAB-10 L10-03 bounded capture contract.
 *
 * This component is intentionally LVGL-free.  The UI owns routing and calls
 * lab10_capture_submit() with an immutable copy of the routed report.  The
 * submit path is a non-blocking SPSC producer; lab10_capture_poll() is the
 * single state-machine owner and consumer.  Terminalization may wait for an
 * already-admitted bounded producer section, but never waits in the producer.
 * Accepted points are retained in a bounded immutable result arena for later
 * rendering/instrumentation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lab10_ui.h"

#define LAB10_CAPTURE_RAW_FIFO_CAPACITY 16u
#define LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY 4096u
#define LAB10_CAPTURE_DURATION_CAP_US UINT64_C(180000000)
#define LAB10_CAPTURE_MIN_MOVE_DISTANCE_PX 2u

/*
 * Session identity is deliberately wider than every counter in this
 * component. Zero is invalid, UINT64_MAX is reserved internally as the
 * admission-gate claim marker, and the generation never wraps or reuses a
 * value. Once the final representable generation is consumed, start() fails
 * closed instead of returning an earlier identity.
 */
typedef uint64_t lab10_capture_session_id_t;

#define LAB10_CAPTURE_INVALID_SESSION_ID UINT64_C(0)
#define LAB10_CAPTURE_SESSION_GENERATION_MAX (UINT64_MAX - UINT64_C(1))

typedef struct lab10_capture lab10_capture_t;

typedef uint64_t (*lab10_capture_clock_now_us_fn)(void *context);
typedef void *(*lab10_capture_alloc_fn)(size_t size, void *context);
typedef void (*lab10_capture_free_fn)(void *memory, void *context);

#ifdef LAB10_CAPTURE_TEST_HOOKS
typedef void (*lab10_capture_test_hook_fn)(void *context);
#endif

typedef struct {
    lab10_capture_clock_now_us_fn clock_now_us;
    void *clock_context;
    lab10_capture_alloc_fn alloc;
    lab10_capture_free_fn free;
    void *allocator_context;
    uint8_t initial_color_index;
#ifdef LAB10_CAPTURE_TEST_HOOKS
    /* Native-only barriers make admission-boundary races deterministic. */
    lab10_capture_test_hook_fn test_submit_before_admission_hook;
    void *test_submit_before_admission_context;
    lab10_capture_test_hook_fn test_submit_admitted_hook;
    void *test_submit_admitted_context;
    lab10_capture_test_hook_fn test_terminalization_hook;
    void *test_terminalization_context;
#endif
} lab10_capture_config_t;

typedef enum {
    LAB10_CAPTURE_STATUS_UNINITIALIZED = 0,
    LAB10_CAPTURE_STATUS_ACTIVE,
    LAB10_CAPTURE_STATUS_TERMINAL,
} lab10_capture_status_t;

typedef enum {
    LAB10_CAPTURE_OUTCOME_NONE = 0,
    LAB10_CAPTURE_OUTCOME_COMPLETED,
    LAB10_CAPTURE_OUTCOME_STOPPED,
    LAB10_CAPTURE_OUTCOME_CANCELLED,
    LAB10_CAPTURE_OUTCOME_DURATION_CAP,
    LAB10_CAPTURE_OUTCOME_POINT_CAP,
    LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW,
    LAB10_CAPTURE_OUTCOME_FAILED,
} lab10_capture_terminal_outcome_t;

typedef enum {
    LAB10_CAPTURE_FAILURE_NONE = 0,
    LAB10_CAPTURE_FAILURE_ALLOCATION,
    LAB10_CAPTURE_FAILURE_INVALID_CONFIGURATION,
} lab10_capture_failure_reason_t;

typedef enum {
    LAB10_CAPTURE_SUBMIT_ENQUEUED = 0,
    LAB10_CAPTURE_SUBMIT_QUEUE_OVERFLOW,
    LAB10_CAPTURE_SUBMIT_TERMINAL,
    LAB10_CAPTURE_SUBMIT_STALE_SESSION,
} lab10_capture_submit_result_t;

typedef enum {
    LAB10_CAPTURE_POINT_BOUNDARY_START = 0,
    LAB10_CAPTURE_POINT_BOUNDARY_CONTINUE,
} lab10_capture_point_boundary_t;

typedef enum {
    LAB10_CAPTURE_POINT_OUTCOME_ACCEPTED = 0,
} lab10_capture_point_outcome_t;

typedef enum {
    LAB10_CAPTURE_REJECT_NONE = 0,
    LAB10_CAPTURE_REJECT_OUTSIDE_DISPLAY,
    LAB10_CAPTURE_REJECT_RAIL_TOUCH,
    LAB10_CAPTURE_REJECT_SECOND_TOUCH,
    LAB10_CAPTURE_REJECT_NO_ACTIVE_STROKE,
    LAB10_CAPTURE_REJECT_WRONG_TOUCH,
    LAB10_CAPTURE_REJECT_ACTIVE_STROKE,
    LAB10_CAPTURE_REJECT_INVALID_TIMESTAMP,
    LAB10_CAPTURE_REJECT_INVALID_COORDINATE,
    LAB10_CAPTURE_REJECT_INVALID_PHASE,
    LAB10_CAPTURE_REJECT_INVALID_COLOR,
    LAB10_CAPTURE_REJECT_STALE_COLOR,
    LAB10_CAPTURE_REJECT_TERMINAL,
} lab10_capture_reject_reason_t;

/* A raw report is copied into the FIFO before it is visible to the consumer. */
typedef struct {
    uint64_t timestamp_us;
    int16_t display_x;
    int16_t display_y;
    uint8_t touch_id;
    lab10_touch_phase_t phase;
    lab10_touch_route_t route;
    uint8_t color_index;
} lab10_capture_raw_report_t;

/* Consumer-side only; the observer must copy quickly into its own bounded sink. */
typedef void (*lab10_capture_raw_report_observer_fn)(
    const lab10_capture_raw_report_t *report, void *context);

/*
 * Accepted points are the only records that append canvas ink.  UP/LEAVE are
 * boundary reports and are represented by the stroke_id in the surrounding
 * raw report stream, not by an extra point.
 */
typedef struct {
    uint64_t timestamp_us;
    int16_t display_x;
    int16_t display_y;
    uint8_t logical_x;
    uint8_t logical_y;
    uint8_t color_index;
    uint8_t touch_id;
    lab10_touch_phase_t phase;
    uint32_t stroke_id;
    lab10_capture_point_boundary_t boundary;
    lab10_capture_point_outcome_t outcome;
} lab10_capture_point_t;

typedef struct {
    uint32_t raw_report_count;
    uint32_t accepted_point_count;
    uint32_t coalesced_move_count;
    uint32_t rejected_report_count;
    uint32_t rejected_rail_touch_count;
    uint32_t rejected_outside_display_count;
    uint32_t rejected_second_touch_count;
    uint32_t duplicate_point_count;
    uint32_t edge_point_count;
    uint32_t stroke_count;
    uint32_t stroke_boundary_count;
    uint32_t color_change_count;
    uint32_t palette_hit_count;
    uint32_t queue_depth_current;
    uint32_t queue_depth_high_water;
    uint32_t queue_overflow_count;
    uint32_t point_cap_hit_count;
    uint32_t duration_cap_hit_count;
    uint32_t terminal_event_count;
    uint32_t invalid_timestamp_count;
    uint32_t invalid_coordinate_count;
    uint32_t trace_loss_count;
    uint32_t allocation_failure_count;
    uint8_t selected_color_index;
    uint8_t accepted_color_mask;
    uint32_t active_stroke_id;
    lab10_capture_status_t status;
    lab10_capture_terminal_outcome_t terminal_outcome;
    lab10_capture_failure_reason_t failure_reason;
} lab10_capture_stats_t;

typedef struct {
    const lab10_capture_point_t *points;
    size_t point_count;
    lab10_capture_session_id_t session_id;
    bool immutable;
} lab10_capture_snapshot_t;

/* Create/destroy only the small session owner; start allocates the bounded point arena. */
lab10_capture_t *lab10_capture_create(const lab10_capture_config_t *config);
void lab10_capture_destroy(lab10_capture_t *capture);
bool lab10_capture_set_raw_report_observer(
    lab10_capture_t *capture, lab10_capture_raw_report_observer_fn observer,
    void *context);

uint64_t lab10_capture_now_us(const lab10_capture_t *capture);

/* A nonzero, never-reused 64-bit generation is required on every operation. */
lab10_capture_session_id_t lab10_capture_start(lab10_capture_t *capture);
lab10_capture_submit_result_t lab10_capture_submit(
    lab10_capture_t *capture, lab10_capture_session_id_t session_id,
    const lab10_capture_raw_report_t *report);
size_t lab10_capture_poll(lab10_capture_t *capture,
                          lab10_capture_session_id_t session_id);
bool lab10_capture_stop(lab10_capture_t *capture,
                        lab10_capture_session_id_t session_id);
bool lab10_capture_cancel(lab10_capture_t *capture,
                          lab10_capture_session_id_t session_id);
bool lab10_capture_complete(lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id);

lab10_capture_status_t lab10_capture_status(const lab10_capture_t *capture,
                                            lab10_capture_session_id_t session_id);
bool lab10_capture_get_stats(const lab10_capture_t *capture,
                             lab10_capture_session_id_t session_id,
                             lab10_capture_stats_t *stats);
bool lab10_capture_snapshot(const lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            lab10_capture_snapshot_t *snapshot);
bool lab10_capture_point_at(const lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            size_t index, lab10_capture_point_t *point);

/*
 * The point arena is the immutable session result.  A renderer may release it
 * after copying/presenting the result; the operation is session guarded so a
 * stale renderer cannot release a newer capture.  After release, snapshot()
 * and duplicate release calls return false.
 */
bool lab10_capture_release_result(lab10_capture_t *capture,
                                  lab10_capture_session_id_t session_id);

/* Copy, without consuming, the currently queued raw reports for diagnostics. */
size_t lab10_capture_copy_pending_raw(const lab10_capture_t *capture,
                                      lab10_capture_session_id_t session_id,
                                      lab10_capture_raw_report_t *reports,
                                      size_t capacity);

#ifdef LAB10_CAPTURE_TEST_HOOKS
/* Test-only seed; production builds expose no generation mutation hook. */
bool lab10_capture_test_set_session_generation(
    lab10_capture_t *capture, lab10_capture_session_id_t generation);
#endif

const char *lab10_capture_reject_reason_name(lab10_capture_reject_reason_t reason);
const char *lab10_capture_outcome_name(lab10_capture_terminal_outcome_t outcome);
