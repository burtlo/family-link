#define _POSIX_C_SOURCE 200809L

#include "lab10_capture.h"
#include "lab10_render.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    uint64_t now_us;
} fake_clock_t;

typedef struct {
    size_t allocations;
    size_t frees;
    size_t outstanding;
    size_t bytes_outstanding;
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t fail_after;
} allocator_probe_t;

typedef struct {
    size_t size;
} allocation_header_t;

static uint64_t fake_now(void *context)
{
    return ((fake_clock_t *)context)->now_us;
}

static void *probe_alloc(size_t size, void *context)
{
    allocator_probe_t *probe = context;
    ++probe->allocations;
    if (probe->fail_after != 0 && probe->allocations > probe->fail_after) {
        return NULL;
    }
    allocation_header_t *header = malloc(sizeof(*header) + size);
    if (header != NULL) {
        header->size = size;
        ++probe->outstanding;
        probe->bytes_outstanding += size;
        probe->bytes_allocated += size;
    }
    return header != NULL ? (void *)(header + 1) : NULL;
}

static void probe_free(void *memory, void *context)
{
    allocator_probe_t *probe = context;
    if (memory != NULL) {
        allocation_header_t *header = ((allocation_header_t *)memory) - 1;
        assert(probe->outstanding > 0);
        --probe->outstanding;
        assert(probe->bytes_outstanding >= header->size);
        probe->bytes_outstanding -= header->size;
        probe->bytes_freed += header->size;
        ++probe->frees;
        free(header);
    }
}

static lab10_capture_t *new_capture(fake_clock_t *clock,
                                    allocator_probe_t *allocator)
{
    return lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = clock,
        .alloc = probe_alloc,
        .free = probe_free,
        .allocator_context = allocator,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
}

static void submit_and_poll(lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            fake_clock_t *clock, lab10_touch_phase_t phase,
                            lab10_touch_route_t route, int16_t x, int16_t y,
                            uint8_t color)
{
    ++clock->now_us;
    assert(lab10_capture_submit(
               capture, session_id,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = clock->now_us,
                   .display_x = x,
                   .display_y = y,
                   .touch_id = 0,
                   .phase = phase,
                   .route = route,
                   .color_index = color,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(lab10_capture_poll(capture, session_id) == 1);
}

static uint64_t monotonic_us(void)
{
    struct timespec timestamp;
    assert(clock_gettime(CLOCK_MONOTONIC, &timestamp) == 0);
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000) +
           (uint64_t)timestamp.tv_nsec / UINT64_C(1000);
}

static void test_stop_cancel_color_and_release(void)
{
    fake_clock_t clock = {.now_us = 100};
    allocator_probe_t allocator = {0};
    lab10_capture_t *capture = new_capture(&clock, &allocator);
    assert(capture != NULL);

    const lab10_capture_session_id_t color_session =
        lab10_capture_start(capture);
    assert(color_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_and_poll(capture, color_session, &clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 10, 10, LAB10_COLOR_WHITE);
    submit_and_poll(capture, color_session, &clock,
                    LAB10_PHASE_PALETTE_CHANGE, LAB10_ROUTE_PALETTE, 269, 64,
                    LAB10_COLOR_RED);
    submit_and_poll(capture, color_session, &clock, LAB10_PHASE_MOVE,
                    LAB10_ROUTE_CANVAS, 20, 20, LAB10_COLOR_RED);
    submit_and_poll(capture, color_session, &clock, LAB10_PHASE_UP,
                    LAB10_ROUTE_CANVAS, 20, 20, LAB10_COLOR_RED);
    lab10_capture_stats_t color_stats;
    assert(lab10_capture_get_stats(capture, color_session, &color_stats));
    assert(color_stats.color_change_count == 1);
    assert(color_stats.stroke_count == 1);
    assert(color_stats.active_stroke_id == 0);
    assert(lab10_capture_complete(capture, color_session));

    lab10_capture_snapshot_t color_snapshot;
    assert(lab10_capture_snapshot(capture, color_session, &color_snapshot));
    assert(color_snapshot.immutable && color_snapshot.point_count == 2);
    assert(color_snapshot.points[0].stroke_id == color_snapshot.points[1].stroke_id);
    assert(color_snapshot.points[0].color_index == LAB10_COLOR_WHITE);
    assert(color_snapshot.points[1].color_index == LAB10_COLOR_RED);
    assert(lab10_capture_release_result(capture, color_session));
    assert(!lab10_capture_release_result(capture, color_session));

    const lab10_capture_session_id_t stop_session =
        lab10_capture_start(capture);
    assert(stop_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_and_poll(capture, stop_session, &clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 30, 30, LAB10_COLOR_WHITE);
    assert(lab10_capture_stop(capture, stop_session));
    assert(!lab10_capture_stop(capture, stop_session));
    assert(lab10_capture_submit(
               capture, stop_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = ++clock.now_us,
                   .display_x = 31,
                   .display_y = 31,
                   .touch_id = 0,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_TERMINAL);
    lab10_capture_stats_t stop_stats;
    assert(lab10_capture_get_stats(capture, stop_session, &stop_stats));
    assert(stop_stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_STOPPED);
    assert(stop_stats.terminal_event_count == 1);
    assert(stop_stats.active_stroke_id == 0);
    assert(stop_stats.queue_depth_current == 0);
    assert(lab10_capture_release_result(capture, stop_session));

    const lab10_capture_session_id_t cancel_session =
        lab10_capture_start(capture);
    assert(cancel_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_and_poll(capture, cancel_session, &clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 40, 40, LAB10_COLOR_WHITE);
    const uint64_t cancel_start_us = monotonic_us();
    assert(lab10_capture_cancel(capture, cancel_session));
    const uint64_t cancel_elapsed_us = monotonic_us() - cancel_start_us;
    assert(cancel_elapsed_us < UINT64_C(100000));
    assert(!lab10_capture_cancel(capture, cancel_session));
    lab10_capture_stats_t cancel_stats;
    assert(lab10_capture_get_stats(capture, cancel_session, &cancel_stats));
    assert(cancel_stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_CANCELLED);
    assert(cancel_stats.terminal_event_count == 1);
    assert(cancel_stats.active_stroke_id == 0);
    assert(cancel_stats.queue_depth_current == 0);
    assert(lab10_capture_release_result(capture, cancel_session));

    /* Reverse exit cancellation must drain an admitted-but-unpolled queue. */
    const lab10_capture_session_id_t active_exit_session =
        lab10_capture_start(capture);
    assert(active_exit_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    ++clock.now_us;
    assert(lab10_capture_submit(
               capture, active_exit_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = clock.now_us,
                   .display_x = 50,
                   .display_y = 50,
                   .touch_id = 0,
                   .phase = LAB10_PHASE_DOWN,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    lab10_capture_stats_t active_exit_before;
    assert(lab10_capture_get_stats(capture, active_exit_session,
                                   &active_exit_before));
    assert(active_exit_before.status == LAB10_CAPTURE_STATUS_ACTIVE);
    assert(active_exit_before.queue_depth_current == 1);
    assert(lab10_capture_cancel(capture, active_exit_session));
    lab10_capture_stats_t active_exit_after;
    assert(lab10_capture_get_stats(capture, active_exit_session,
                                   &active_exit_after));
    assert(active_exit_after.status == LAB10_CAPTURE_STATUS_TERMINAL);
    assert(active_exit_after.terminal_outcome == LAB10_CAPTURE_OUTCOME_CANCELLED);
    assert(active_exit_after.terminal_event_count == 1);
    assert(active_exit_after.queue_depth_current == 0);
    assert(lab10_capture_submit(
               capture, active_exit_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = ++clock.now_us,
                   .display_x = 51,
                   .display_y = 51,
                   .touch_id = 0,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_TERMINAL);
    assert(lab10_capture_release_result(capture, active_exit_session));

    const lab10_capture_session_id_t next_session = lab10_capture_start(capture);
    assert(next_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    assert(next_session != active_exit_session);
    assert(lab10_capture_status(capture, active_exit_session) ==
           LAB10_CAPTURE_STATUS_UNINITIALIZED);
    assert(lab10_capture_submit(
               capture, active_exit_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = ++clock.now_us,
                   .display_x = 52,
                   .display_y = 52,
                   .touch_id = 0,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_STALE_SESSION);
    assert(lab10_capture_cancel(capture, next_session));
    assert(lab10_capture_release_result(capture, next_session));
    puts("L10-09 active_exit_cancel_queue_drained=1 stale_session_rejected=1");

    lab10_capture_destroy(capture);
    assert(allocator.outstanding == 0);
    assert(allocator.bytes_outstanding == 0);
    assert(allocator.bytes_allocated == allocator.bytes_freed);
    printf("L10-09 capture_cancel_latency_us=%llu\n",
           (unsigned long long)cancel_elapsed_us);
}

static void test_duration_cap_and_repeated_memory_balance(void)
{
    fake_clock_t clock = {.now_us = 0};
    allocator_probe_t allocator = {0};
    lab10_capture_t *capture = new_capture(&clock, &allocator);
    assert(capture != NULL);

    const lab10_capture_session_id_t duration_session =
        lab10_capture_start(capture);
    assert(duration_session != LAB10_CAPTURE_INVALID_SESSION_ID);
    clock.now_us = LAB10_CAPTURE_DURATION_CAP_US;
    assert(lab10_capture_poll(capture, duration_session) == 0);
    lab10_capture_stats_t duration_stats;
    assert(lab10_capture_get_stats(capture, duration_session, &duration_stats));
    assert(duration_stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP);
    assert(duration_stats.duration_cap_hit_count == 1);
    assert(duration_stats.terminal_event_count == 1);
    assert(lab10_capture_release_result(capture, duration_session));

    for (unsigned cycle = 0; cycle < 32u; ++cycle) {
        const lab10_capture_session_id_t session = lab10_capture_start(capture);
        assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);
        submit_and_poll(capture, session, &clock, LAB10_PHASE_DOWN,
                        LAB10_ROUTE_CANVAS, (int16_t)(cycle % 100u), 50,
                        LAB10_COLOR_WHITE);
        assert(lab10_capture_complete(capture, session));
        lab10_capture_stats_t stats;
        assert(lab10_capture_get_stats(capture, session, &stats));
        assert(stats.terminal_event_count == 1);
        assert(stats.queue_depth_current == 0);
        assert(lab10_capture_release_result(capture, session));
        assert(allocator.outstanding == 0);
    }

    lab10_capture_destroy(capture);
    assert(allocator.outstanding == 0);
    assert(allocator.bytes_outstanding == 0);
    assert(allocator.bytes_allocated == allocator.bytes_freed);
    puts("L10-09 capture_duration_cap_and_memory_drift_bytes=0");
}

static void test_renderer_lock_failure_and_partial_allocation_cleanup(void)
{
    allocator_probe_t partial_allocator = {.fail_after = 2};
    assert(lab10_render_create(&(lab10_render_config_t){
               .alloc = probe_alloc,
               .free = probe_free,
               .allocator_context = &partial_allocator,
           }) == NULL);
    assert(partial_allocator.outstanding == 0);
    assert(partial_allocator.frees == 2);

    fake_clock_t clock = {.now_us = 100};
    lab10_capture_t *capture = lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = &clock,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_and_poll(capture, session, &clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 8, 8, LAB10_COLOR_WHITE);
    assert(lab10_capture_complete(capture, session));

    lab10_render_t *renderer = lab10_render_create(NULL);
    assert(renderer != NULL);
    assert(lab10_render_begin(renderer, session));
    assert(lab10_render_note_lock_state(renderer, session,
                                        LAB10_RENDER_LOCK_FAILED));
    lab10_render_metrics_t metrics;
    assert(lab10_render_get_metrics(renderer, session, &metrics));
    assert(metrics.lock_state == LAB10_RENDER_LOCK_FAILED);
    assert(metrics.lock_state_change_count == 1);
    const lab10_render_update_result_t update =
        lab10_render_update(renderer, capture, session);
    assert(update.valid && update.lock_state == LAB10_RENDER_LOCK_FAILED);
    assert(lab10_render_reset(renderer, session));
    assert(!lab10_render_update(renderer, capture, session).valid);
    lab10_render_destroy(renderer);
    assert(lab10_capture_release_result(capture, session));
    lab10_capture_destroy(capture);
    puts("L10-09 renderer_lock_failure_observed=1 partial_alloc_outstanding=0");
}

int main(void)
{
    test_stop_cancel_color_and_release();
    test_duration_cap_and_repeated_memory_balance();
    test_renderer_lock_failure_and_partial_allocation_cleanup();
    puts("LAB10_L109_QUALIFICATION_PASS");
    return 0;
}
