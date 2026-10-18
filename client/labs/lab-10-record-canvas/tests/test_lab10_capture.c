#include "lab10_capture.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef LAB10_CAPTURE_TEST_HOOKS
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#endif

typedef struct {
    uint64_t now_us;
} fake_clock_t;

static unsigned g_allocation_attempts;

typedef struct {
    unsigned allocations;
    unsigned frees;
} allocator_probe_t;

static void *failing_alloc(size_t size, void *context)
{
    (void)size;
    (void)context;
    ++g_allocation_attempts;
    return NULL;
}

static void noop_free(void *memory, void *context)
{
    (void)memory;
    (void)context;
}

static void *counting_alloc(size_t size, void *context)
{
    allocator_probe_t *probe = context;
    ++probe->allocations;
    return malloc(size);
}

static void counting_free(void *memory, void *context)
{
    allocator_probe_t *probe = context;
    ++probe->frees;
    free(memory);
}

static void count_raw_report(const lab10_capture_raw_report_t *report,
                             void *context)
{
    assert(report != NULL);
    unsigned *count = context;
    ++*count;
}

static uint64_t fake_now(void *context)
{
    return ((fake_clock_t *)context)->now_us;
}

static lab10_capture_raw_report_t report(uint64_t timestamp_us,
                                         lab10_touch_phase_t phase,
                                         lab10_touch_route_t route, int16_t x,
                                         int16_t y, uint8_t touch_id,
                                         uint8_t color_index)
{
    return (lab10_capture_raw_report_t){
        .timestamp_us = timestamp_us,
        .display_x = x,
        .display_y = y,
        .touch_id = touch_id,
        .phase = phase,
        .route = route,
        .color_index = color_index,
    };
}

static lab10_capture_t *new_capture(fake_clock_t *clock)
{
    return lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = clock,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
}

static void submit_and_poll(lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            lab10_capture_raw_report_t input)
{
    assert(lab10_capture_submit(capture, session_id, &input) ==
           LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(lab10_capture_poll(capture, session_id) == 1);
}

static void submit_queue_overflow(lab10_capture_t *capture,
                                  lab10_capture_session_id_t session_id,
                                  uint64_t timestamp_us)
{
    for (uint8_t index = 0; index < LAB10_CAPTURE_RAW_FIFO_CAPACITY; ++index) {
        assert(lab10_capture_submit(
                   capture, session_id,
                   &(lab10_capture_raw_report_t){
                       .timestamp_us = timestamp_us + index,
                       .display_x = index,
                       .display_y = index,
                       .touch_id = 1,
                       .phase = index == 0 ? LAB10_PHASE_DOWN : LAB10_PHASE_MOVE,
                       .route = LAB10_ROUTE_CANVAS,
                       .color_index = LAB10_COLOR_WHITE,
                   }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    }
    assert(lab10_capture_submit(
               capture, session_id,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = timestamp_us +
                                   LAB10_CAPTURE_RAW_FIFO_CAPACITY,
                   .display_x = 16,
                   .display_y = 16,
                   .touch_id = 1,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_QUEUE_OVERFLOW);
}

static void assert_queue_overflow_terminal(
    lab10_capture_t *capture, lab10_capture_session_id_t session_id);

#ifdef LAB10_CAPTURE_TEST_HOOKS
typedef struct {
    atomic_bool pause_before_admission;
    atomic_bool submit_before_admission;
    atomic_bool pause_next_submit;
    atomic_bool submit_admitted;
    atomic_bool terminalization_started;
    atomic_bool release_submit;
} concurrency_gate_t;

static void init_concurrency_gate(concurrency_gate_t *gate)
{
    atomic_init(&gate->pause_before_admission, false);
    atomic_init(&gate->submit_before_admission, false);
    atomic_init(&gate->pause_next_submit, false);
    atomic_init(&gate->submit_admitted, false);
    atomic_init(&gate->terminalization_started, false);
    atomic_init(&gate->release_submit, false);
}

static void pause_before_admission_submit(void *context)
{
    concurrency_gate_t *gate = context;
    if (!atomic_exchange_explicit(&gate->pause_before_admission, false,
                                  memory_order_acq_rel)) {
        return;
    }
    atomic_store_explicit(&gate->submit_before_admission, true,
                          memory_order_release);
    while (!atomic_load_explicit(&gate->release_submit, memory_order_acquire)) {
        sched_yield();
    }
}

static void wait_for_flag(const atomic_bool *flag)
{
    for (unsigned int attempt = 0; attempt < 1000000u; ++attempt) {
        if (atomic_load_explicit(flag, memory_order_acquire)) {
            return;
        }
        sched_yield();
    }
    assert(!"concurrency test barrier timed out");
}

static void pause_admitted_submit(void *context)
{
    concurrency_gate_t *gate = context;
    if (!atomic_exchange_explicit(&gate->pause_next_submit, false,
                                  memory_order_acq_rel)) {
        return;
    }
    atomic_store_explicit(&gate->submit_admitted, true, memory_order_release);
    while (!atomic_load_explicit(&gate->release_submit, memory_order_acquire)) {
        sched_yield();
    }
}

static void signal_terminalization(void *context)
{
    concurrency_gate_t *gate = context;
    atomic_store_explicit(&gate->terminalization_started, true,
                          memory_order_release);
}

static lab10_capture_t *new_capture_with_gate(fake_clock_t *clock,
                                               concurrency_gate_t *gate)
{
    return lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = clock,
        .initial_color_index = LAB10_COLOR_WHITE,
        .test_submit_admitted_hook = pause_admitted_submit,
        .test_submit_admitted_context = gate,
        .test_terminalization_hook = signal_terminalization,
        .test_terminalization_context = gate,
    });
}

static lab10_capture_t *new_capture_with_pre_admission_gate(
    fake_clock_t *clock, concurrency_gate_t *gate)
{
    return lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = clock,
        .initial_color_index = LAB10_COLOR_WHITE,
        .test_submit_before_admission_hook = pause_before_admission_submit,
        .test_submit_before_admission_context = gate,
    });
}

typedef struct {
    lab10_capture_t *capture;
    lab10_capture_session_id_t session_id;
    lab10_capture_raw_report_t report;
    lab10_capture_submit_result_t result;
} submit_thread_args_t;

static void *submit_thread_main(void *context)
{
    submit_thread_args_t *args = context;
    args->result = lab10_capture_submit(args->capture, args->session_id,
                                         &args->report);
    return NULL;
}

typedef enum {
    TERMINAL_BOUNDARY_STOP = 0,
    TERMINAL_BOUNDARY_CANCEL,
    TERMINAL_BOUNDARY_COMPLETE,
    TERMINAL_BOUNDARY_DURATION,
} terminal_boundary_operation_t;

typedef struct {
    lab10_capture_t *capture;
    lab10_capture_session_id_t session_id;
    terminal_boundary_operation_t operation;
    atomic_bool done;
    bool result;
} terminal_thread_args_t;

static void *terminal_thread_main(void *context)
{
    terminal_thread_args_t *args = context;
    switch (args->operation) {
    case TERMINAL_BOUNDARY_STOP:
        args->result = lab10_capture_stop(args->capture, args->session_id);
        break;
    case TERMINAL_BOUNDARY_CANCEL:
        args->result = lab10_capture_cancel(args->capture, args->session_id);
        break;
    case TERMINAL_BOUNDARY_COMPLETE:
        args->result = lab10_capture_complete(args->capture, args->session_id);
        break;
    case TERMINAL_BOUNDARY_DURATION:
        (void)lab10_capture_poll(args->capture, args->session_id);
        args->result = true;
        break;
    }
    atomic_store_explicit(&args->done, true, memory_order_release);
    return NULL;
}

static void fill_fifo_without_overflow(lab10_capture_t *capture,
                                       lab10_capture_session_id_t session_id,
                                       uint64_t timestamp_us)
{
    for (uint8_t index = 0; index < LAB10_CAPTURE_RAW_FIFO_CAPACITY; ++index) {
        assert(lab10_capture_submit(
                   capture, session_id,
                   &(lab10_capture_raw_report_t){
                       .timestamp_us = timestamp_us + index,
                       .display_x = index,
                       .display_y = index,
                       .touch_id = 1,
                       .phase = index == 0 ? LAB10_PHASE_DOWN : LAB10_PHASE_MOVE,
                       .route = LAB10_ROUTE_CANVAS,
                       .color_index = LAB10_COLOR_WHITE,
                   }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    }
}

static void test_admitted_submit_is_drained_before_terminal_cleanup(void)
{
    fake_clock_t clock = {.now_us = 12000};
    concurrency_gate_t gate;
    init_concurrency_gate(&gate);
    lab10_capture_t *capture = new_capture_with_gate(&clock, &gate);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    atomic_store_explicit(&gate.pause_next_submit, true, memory_order_release);

    submit_thread_args_t submit_args = {
        .capture = capture,
        .session_id = session,
        .report = report(clock.now_us, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS,
                         10, 10, 7, LAB10_COLOR_WHITE),
    };
    pthread_t submit_thread;
    assert(pthread_create(&submit_thread, NULL, submit_thread_main,
                          &submit_args) == 0);
    wait_for_flag(&gate.submit_admitted);

    terminal_thread_args_t terminal_args = {
        .capture = capture,
        .session_id = session,
        .operation = TERMINAL_BOUNDARY_STOP,
        .result = false,
    };
    atomic_init(&terminal_args.done, false);
    pthread_t terminal_thread;
    assert(pthread_create(&terminal_thread, NULL, terminal_thread_main,
                          &terminal_args) == 0);
    wait_for_flag(&gate.terminalization_started);
    assert(!atomic_load_explicit(&terminal_args.done, memory_order_acquire));

    atomic_store_explicit(&gate.release_submit, true, memory_order_release);
    assert(pthread_join(submit_thread, NULL) == 0);
    assert(pthread_join(terminal_thread, NULL) == 0);
    assert(submit_args.result == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(terminal_args.result);

    lab10_capture_stats_t stats;
    lab10_capture_raw_report_t pending[LAB10_CAPTURE_RAW_FIFO_CAPACITY];
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.raw_report_count == 1);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_STOPPED);
    assert(stats.terminal_event_count == 1 && stats.active_stroke_id == 0);
    assert(stats.queue_depth_current == 0);
    assert(lab10_capture_copy_pending_raw(capture, session, pending,
                                          sizeof(pending) / sizeof(pending[0])) ==
           0);
    assert(lab10_capture_submit(capture, session, &submit_args.report) ==
           LAB10_CAPTURE_SUBMIT_TERMINAL);
    lab10_capture_destroy(capture);
}

static void test_overflow_wins_at_admission_boundary(
    terminal_boundary_operation_t operation, uint64_t timestamp_us)
{
    fake_clock_t clock = {.now_us = timestamp_us};
    concurrency_gate_t gate;
    init_concurrency_gate(&gate);
    lab10_capture_t *capture = new_capture_with_gate(&clock, &gate);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    fill_fifo_without_overflow(capture, session, timestamp_us);
    atomic_store_explicit(&gate.pause_next_submit, true, memory_order_release);

    submit_thread_args_t submit_args = {
        .capture = capture,
        .session_id = session,
        .report = report(timestamp_us + LAB10_CAPTURE_RAW_FIFO_CAPACITY,
                         LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 16, 16, 1,
                         LAB10_COLOR_WHITE),
    };
    pthread_t submit_thread;
    assert(pthread_create(&submit_thread, NULL, submit_thread_main,
                          &submit_args) == 0);
    wait_for_flag(&gate.submit_admitted);

    if (operation == TERMINAL_BOUNDARY_DURATION) {
        clock.now_us += LAB10_CAPTURE_DURATION_CAP_US;
    }
    terminal_thread_args_t terminal_args = {
        .capture = capture,
        .session_id = session,
        .operation = operation,
        .result = false,
    };
    atomic_init(&terminal_args.done, false);
    pthread_t terminal_thread;
    assert(pthread_create(&terminal_thread, NULL, terminal_thread_main,
                          &terminal_args) == 0);
    wait_for_flag(&gate.terminalization_started);
    assert(!atomic_load_explicit(&terminal_args.done, memory_order_acquire));

    atomic_store_explicit(&gate.release_submit, true, memory_order_release);
    assert(pthread_join(submit_thread, NULL) == 0);
    assert(pthread_join(terminal_thread, NULL) == 0);
    assert(submit_args.result == LAB10_CAPTURE_SUBMIT_QUEUE_OVERFLOW);
    assert(terminal_args.result);
    assert_queue_overflow_terminal(capture, session);
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.raw_report_count == LAB10_CAPTURE_RAW_FIFO_CAPACITY + 1);
    if (operation == TERMINAL_BOUNDARY_DURATION) {
        assert(stats.duration_cap_hit_count == 0);
    }
    lab10_capture_destroy(capture);
}

static void test_pre_admission_submit_cannot_enter_new_session(void)
{
    fake_clock_t clock = {.now_us = 11500};
    concurrency_gate_t gate;
    init_concurrency_gate(&gate);
    lab10_capture_t *capture =
        new_capture_with_pre_admission_gate(&clock, &gate);
    assert(capture != NULL);
    const lab10_capture_session_id_t first_session =
        lab10_capture_start(capture);
    atomic_store_explicit(&gate.pause_before_admission, true,
                          memory_order_release);

    submit_thread_args_t submit_args = {
        .capture = capture,
        .session_id = first_session,
        .report = report(clock.now_us, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS,
                         15, 15, 9, LAB10_COLOR_WHITE),
    };
    pthread_t submit_thread;
    assert(pthread_create(&submit_thread, NULL, submit_thread_main,
                          &submit_args) == 0);
    wait_for_flag(&gate.submit_before_admission);

    assert(lab10_capture_stop(capture, first_session));
    const lab10_capture_session_id_t second_session =
        lab10_capture_start(capture);
    assert(second_session != first_session);

    atomic_store_explicit(&gate.release_submit, true, memory_order_release);
    assert(pthread_join(submit_thread, NULL) == 0);
    assert(submit_args.result == LAB10_CAPTURE_SUBMIT_STALE_SESSION ||
           submit_args.result == LAB10_CAPTURE_SUBMIT_TERMINAL);

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, second_session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_ACTIVE);
    assert(stats.raw_report_count == 0 && stats.queue_depth_current == 0);
    lab10_capture_raw_report_t pending;
    assert(lab10_capture_copy_pending_raw(capture, second_session, &pending, 1) ==
           0);
    assert(lab10_capture_stop(capture, second_session));
    assert(lab10_capture_get_stats(capture, second_session, &stats));
    assert(stats.terminal_event_count == 1);
    lab10_capture_destroy(capture);
}

static void test_non_reusing_generation_boundary_rejects_stale_submit(void)
{
    fake_clock_t clock = {.now_us = 12000};
    concurrency_gate_t gate;
    init_concurrency_gate(&gate);
    lab10_capture_t *capture =
        new_capture_with_pre_admission_gate(&clock, &gate);
    assert(capture != NULL);

    const lab10_capture_session_id_t seed_generation =
        LAB10_CAPTURE_SESSION_GENERATION_MAX - UINT64_C(3);
    assert(lab10_capture_test_set_session_generation(capture, seed_generation));
    const lab10_capture_session_id_t first_session =
        lab10_capture_start(capture);
    assert(first_session == LAB10_CAPTURE_SESSION_GENERATION_MAX - UINT64_C(2));

    atomic_store_explicit(&gate.pause_before_admission, true,
                          memory_order_release);
    submit_thread_args_t submit_args = {
        .capture = capture,
        .session_id = first_session,
        .report = report(clock.now_us, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS,
                         15, 15, 9, LAB10_COLOR_WHITE),
    };
    pthread_t submit_thread;
    assert(pthread_create(&submit_thread, NULL, submit_thread_main,
                          &submit_args) == 0);
    wait_for_flag(&gate.submit_before_admission);

    assert(lab10_capture_stop(capture, first_session));
    const lab10_capture_session_id_t second_session =
        lab10_capture_start(capture);
    assert(second_session == LAB10_CAPTURE_SESSION_GENERATION_MAX - UINT64_C(1));
    assert(second_session != first_session);

    atomic_store_explicit(&gate.release_submit, true, memory_order_release);
    assert(pthread_join(submit_thread, NULL) == 0);
    assert(submit_args.result == LAB10_CAPTURE_SUBMIT_STALE_SESSION);

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, second_session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_ACTIVE);
    assert(stats.raw_report_count == 0 && stats.queue_depth_current == 0);
    assert(lab10_capture_stop(capture, second_session));

    const lab10_capture_session_id_t final_session =
        lab10_capture_start(capture);
    assert(final_session == LAB10_CAPTURE_SESSION_GENERATION_MAX);
    assert(lab10_capture_stop(capture, final_session));

    /* The final generation is terminal: start() must not wrap or reuse. */
    assert(lab10_capture_start(capture) == LAB10_CAPTURE_INVALID_SESSION_ID);
    assert(lab10_capture_submit(capture, first_session, &submit_args.report) ==
           LAB10_CAPTURE_SUBMIT_STALE_SESSION);
    lab10_capture_destroy(capture);
}
#endif

static void assert_queue_overflow_terminal(
    lab10_capture_t *capture, lab10_capture_session_id_t session_id)
{
    lab10_capture_stats_t stats;
    lab10_capture_raw_report_t pending[LAB10_CAPTURE_RAW_FIFO_CAPACITY];
    assert(lab10_capture_get_stats(capture, session_id, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_TERMINAL);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW);
    assert(stats.queue_overflow_count == 1);
    assert(stats.trace_loss_count == 1);
    assert(stats.terminal_event_count == 1);
    assert(stats.queue_depth_current == 0);
    assert(stats.active_stroke_id == 0);
    assert(lab10_capture_copy_pending_raw(capture, session_id, pending,
                                          sizeof(pending) / sizeof(pending[0])) ==
           0);
}

typedef bool (*terminal_operation_fn)(lab10_capture_t *,
                                      lab10_capture_session_id_t);

static void test_overflow_precedes_explicit_terminal(
    terminal_operation_fn terminal_operation, uint64_t timestamp_us)
{
    fake_clock_t clock = {.now_us = timestamp_us};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    submit_queue_overflow(capture, session, timestamp_us);
    assert(terminal_operation(capture, session));
    assert(!terminal_operation(capture, session));
    assert_queue_overflow_terminal(capture, session);
    lab10_capture_destroy(capture);
}

static void test_real_points_color_and_boundaries(void)
{
    fake_clock_t clock = {.now_us = 1000};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    assert(lab10_capture_start(capture) == session);

    submit_and_poll(capture, session,
                    report(1000, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 10, 11,
                           1, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(1001, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 11, 11,
                           1, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(1002, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 11, 11,
                           1, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(1003, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 12, 11,
                           1, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(1004, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 13, 11,
                           1, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(1005, LAB10_PHASE_UP, LAB10_ROUTE_CANVAS, 13, 11, 1,
                           LAB10_COLOR_WHITE));

    submit_and_poll(capture, session,
                    report(1006, LAB10_PHASE_PALETTE_CHANGE,
                           LAB10_ROUTE_PALETTE, 269, 64, 1, LAB10_COLOR_RED));
    submit_and_poll(capture, session,
                    report(1007, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 239, 239,
                           1, LAB10_COLOR_RED));
    submit_and_poll(capture, session,
                    report(1008, LAB10_PHASE_LEAVE, LAB10_ROUTE_CANVAS, 239, 239,
                           1, LAB10_COLOR_RED));
    submit_and_poll(capture, session,
                    report(1009, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 0, 0, 1,
                           LAB10_COLOR_RED));

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.accepted_point_count == 5);
    assert(stats.coalesced_move_count == 2);
    assert(stats.duplicate_point_count == 1);
    assert(stats.stroke_count == 3);
    assert(stats.stroke_boundary_count == 2);
    assert(stats.palette_hit_count == 1);
    assert(stats.color_change_count == 1);
    assert(stats.edge_point_count == 2);
    assert(stats.rejected_report_count == 0);

    lab10_capture_point_t point;
    assert(lab10_capture_point_at(capture, session, 0, &point));
    assert(point.timestamp_us == 1000 && point.display_x == 10 &&
           point.display_y == 11 && point.logical_x == 5 && point.logical_y == 5);
    assert(point.stroke_id == 1 &&
           point.boundary == LAB10_CAPTURE_POINT_BOUNDARY_START &&
           point.outcome == LAB10_CAPTURE_POINT_OUTCOME_ACCEPTED);
    assert(lab10_capture_point_at(capture, session, 1, &point));
    assert(point.display_x == 11 && point.logical_x == 5 &&
           point.boundary == LAB10_CAPTURE_POINT_BOUNDARY_CONTINUE);
    assert(lab10_capture_point_at(capture, session, 2, &point));
    assert(point.display_x == 12 && point.logical_x == 6 &&
           point.stroke_id == 1);
    assert(lab10_capture_point_at(capture, session, 3, &point));
    assert(point.color_index == LAB10_COLOR_RED && point.stroke_id == 2 &&
           point.logical_x == 119 && point.logical_y == 119);
    assert(lab10_capture_point_at(capture, session, 4, &point));
    assert(point.color_index == LAB10_COLOR_RED && point.stroke_id == 3 &&
           point.logical_x == 0 && point.logical_y == 0);

    assert(lab10_capture_stop(capture, session));
    assert(!lab10_capture_stop(capture, session));
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.terminal_event_count == 1);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_STOPPED);
    lab10_capture_snapshot_t snapshot;
    assert(lab10_capture_snapshot(capture, session, &snapshot));
    assert(snapshot.points != NULL && snapshot.point_count == 5 &&
           snapshot.session_id == session && snapshot.immutable);
    assert(lab10_capture_release_result(capture, session));
    assert(!lab10_capture_snapshot(capture, session, &snapshot));
    assert(snapshot.points == NULL && snapshot.point_count == 0 &&
           snapshot.session_id == 0 && !snapshot.immutable);
    assert(!lab10_capture_release_result(capture, session));
    assert(!lab10_capture_point_at(capture, session, 0, &point));
    lab10_capture_destroy(capture);

    fake_clock_t cancel_clock = {.now_us = 1100};
    lab10_capture_t *cancel_capture = new_capture(&cancel_clock);
    const lab10_capture_session_id_t cancel_session =
        lab10_capture_start(cancel_capture);
    submit_and_poll(cancel_capture, cancel_session,
                    report(1100, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 4, 4, 1,
                           LAB10_COLOR_WHITE));
    assert(lab10_capture_cancel(cancel_capture, cancel_session));
    assert(!lab10_capture_cancel(cancel_capture, cancel_session));
    assert(lab10_capture_get_stats(cancel_capture, cancel_session, &stats));
    assert(stats.terminal_event_count == 1 &&
           stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_CANCELLED &&
           stats.active_stroke_id == 0);
    lab10_capture_destroy(cancel_capture);
}

static void test_routing_rejection_and_second_touch(void)
{
    fake_clock_t clock = {.now_us = 2000};
    lab10_capture_t *capture = new_capture(&clock);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    submit_and_poll(capture, session,
                    report(2000, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 20, 20,
                           3, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(2001, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 22, 20,
                           4, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(2002, LAB10_PHASE_MOVE, LAB10_ROUTE_CANVAS, 22, 20,
                           3, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(2003, LAB10_PHASE_DOWN, LAB10_ROUTE_RAIL_REJECTED,
                           240, 20, 3, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(2004, LAB10_PHASE_DOWN, LAB10_ROUTE_OUTSIDE, -1, 20,
                           3, LAB10_COLOR_WHITE));
    submit_and_poll(capture, session,
                    report(2005, LAB10_PHASE_PALETTE_CHANGE,
                           LAB10_ROUTE_PALETTE, 269, 64, 4, LAB10_COLOR_RED));
    submit_and_poll(capture, session,
                    report(2006, LAB10_PHASE_UP, LAB10_ROUTE_OUTSIDE, -1, 20, 3,
                           LAB10_COLOR_WHITE));

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.accepted_point_count == 2);
    assert(stats.rejected_report_count == 4);
    assert(stats.rejected_second_touch_count == 2);
    assert(stats.rejected_rail_touch_count == 1);
    assert(stats.rejected_outside_display_count == 1);
    assert(stats.palette_hit_count == 0);
    assert(stats.selected_color_index == LAB10_COLOR_WHITE);
    assert(stats.stroke_boundary_count == 1);
    lab10_capture_destroy(capture);
}

static void test_raw_fifo_and_queue_overflow(void)
{
    fake_clock_t clock = {.now_us = 3000};
    lab10_capture_t *capture = new_capture(&clock);
    unsigned observed_reports = 0;
    assert(lab10_capture_set_raw_report_observer(capture, count_raw_report,
                                                 &observed_reports));
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    for (uint8_t index = 0; index < LAB10_CAPTURE_RAW_FIFO_CAPACITY; ++index) {
        assert(lab10_capture_submit(
                   capture, session,
                   &(lab10_capture_raw_report_t){
                       .timestamp_us = 3000 + index,
                       .display_x = index,
                       .display_y = index,
                       .touch_id = 1,
                       .phase = index == 0 ? LAB10_PHASE_DOWN : LAB10_PHASE_MOVE,
                       .route = LAB10_ROUTE_CANVAS,
                       .color_index = LAB10_COLOR_WHITE,
                   }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    }
    lab10_capture_raw_report_t copied[LAB10_CAPTURE_RAW_FIFO_CAPACITY];
    assert(lab10_capture_copy_pending_raw(capture, session, copied,
                                          sizeof(copied) / sizeof(copied[0])) ==
           LAB10_CAPTURE_RAW_FIFO_CAPACITY);
    assert(copied[0].phase == LAB10_PHASE_DOWN && copied[0].display_x == 0);
    assert(copied[15].display_x == 15);
    assert(lab10_capture_submit(
               capture, session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = 3016,
                   .display_x = 16,
                   .display_y = 16,
                   .touch_id = 1,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_QUEUE_OVERFLOW);
    assert(lab10_capture_poll(capture, session) ==
           LAB10_CAPTURE_RAW_FIFO_CAPACITY);
    assert(observed_reports == LAB10_CAPTURE_RAW_FIFO_CAPACITY);

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.raw_report_count == 17);
    assert(stats.queue_depth_high_water == LAB10_CAPTURE_RAW_FIFO_CAPACITY);
    assert(stats.queue_overflow_count == 1);
    assert(stats.trace_loss_count == 1);
    assert(stats.terminal_event_count == 1);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_QUEUE_OVERFLOW);
    assert(stats.queue_depth_current == 0 && stats.active_stroke_id == 0);
    assert(lab10_capture_submit(capture, session, &copied[0]) ==
           LAB10_CAPTURE_SUBMIT_TERMINAL);
    lab10_capture_destroy(capture);
}

static void test_overflow_precedence_and_advanced_clock(void)
{
    test_overflow_precedes_explicit_terminal(lab10_capture_stop, 7000);
    test_overflow_precedes_explicit_terminal(lab10_capture_cancel, 8000);
    test_overflow_precedes_explicit_terminal(lab10_capture_complete, 9000);

    fake_clock_t clock = {.now_us = 10000};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    submit_queue_overflow(capture, session, clock.now_us);
    clock.now_us += LAB10_CAPTURE_DURATION_CAP_US;
    assert(lab10_capture_poll(capture, session) ==
           LAB10_CAPTURE_RAW_FIFO_CAPACITY);
    assert_queue_overflow_terminal(capture, session);
    assert(lab10_capture_poll(capture, session) == 0);
    lab10_capture_destroy(capture);
}

static void test_release_result_session_lifecycle(void)
{
    fake_clock_t clock = {.now_us = 11000};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t first_session =
        lab10_capture_start(capture);
    submit_and_poll(capture, first_session,
                    report(clock.now_us, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS,
                           10, 10, 1, LAB10_COLOR_WHITE));
    assert(lab10_capture_stop(capture, first_session));

    lab10_capture_snapshot_t snapshot;
    assert(lab10_capture_snapshot(capture, first_session, &snapshot));
    assert(snapshot.points != NULL && snapshot.point_count == 1 &&
           snapshot.session_id == first_session && snapshot.immutable);
    const lab10_capture_session_id_t stale_session =
        first_session == LAB10_CAPTURE_SESSION_GENERATION_MAX
            ? 1
            : first_session + 1;
    assert(!lab10_capture_release_result(capture, stale_session));

    const lab10_capture_session_id_t second_session =
        lab10_capture_start(capture);
    assert(second_session != first_session);
    assert(!lab10_capture_snapshot(capture, first_session, &snapshot));
    assert(!lab10_capture_release_result(capture, first_session));
    assert(lab10_capture_snapshot(capture, second_session, &snapshot));
    assert(snapshot.points != NULL && snapshot.point_count == 0 &&
           snapshot.session_id == second_session && !snapshot.immutable);

    assert(lab10_capture_stop(capture, second_session));
    assert(lab10_capture_snapshot(capture, second_session, &snapshot));
    assert(snapshot.points != NULL && snapshot.point_count == 0 &&
           snapshot.session_id == second_session && snapshot.immutable);
    assert(lab10_capture_release_result(capture, second_session));
    assert(!lab10_capture_release_result(capture, second_session));
    assert(!lab10_capture_snapshot(capture, second_session, &snapshot));
    lab10_capture_destroy(capture);
}

static void test_caps_timestamps_and_stale_session(void)
{
    fake_clock_t clock = {.now_us = 4000};
    lab10_capture_t *capture = new_capture(&clock);
    const lab10_capture_session_id_t first_session =
        lab10_capture_start(capture);
    submit_and_poll(capture, first_session,
                    report(4000, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS, 10, 10,
                           1, LAB10_COLOR_WHITE));
    assert(lab10_capture_submit(
               capture, first_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = 3999,
                   .display_x = 12,
                   .display_y = 10,
                   .touch_id = 1,
                   .phase = LAB10_PHASE_MOVE,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(lab10_capture_poll(capture, first_session) == 1);
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, first_session, &stats));
    assert(stats.invalid_timestamp_count == 1 &&
           stats.accepted_point_count == 1);

    assert(lab10_capture_stop(capture, first_session));
    const lab10_capture_session_id_t second_session =
        lab10_capture_start(capture);
    assert(second_session != first_session);
    assert(lab10_capture_submit(
               capture, first_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = 4001,
                   .display_x = 10,
                   .display_y = 10,
                   .touch_id = 1,
                   .phase = LAB10_PHASE_DOWN,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_STALE_SESSION);

    assert(lab10_capture_submit(
               capture, second_session,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = UINT64_C(180000000) + 4000,
                   .display_x = 10,
                   .display_y = 10,
                   .touch_id = 1,
                   .phase = LAB10_PHASE_DOWN,
                   .route = LAB10_ROUTE_CANVAS,
                   .color_index = LAB10_COLOR_WHITE,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(lab10_capture_poll(capture, second_session) == 1);
    assert(lab10_capture_get_stats(capture, second_session, &stats));
    assert(stats.duration_cap_hit_count == 1 &&
           stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP);
    lab10_capture_destroy(capture);
}

static void test_point_cap(void)
{
    fake_clock_t clock = {.now_us = 5000};
    lab10_capture_t *capture = new_capture(&clock);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    for (uint32_t index = 0; index < LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY;
         ++index) {
        const bool first = index == 0;
        const int16_t x = first ? 0 : (index % 2 == 0 ? 0 : 10);
        const int16_t y = first ? 0 : (index % 2 == 0 ? 0 : 10);
        assert(lab10_capture_submit(
                   capture, session,
                   &(lab10_capture_raw_report_t){
                       .timestamp_us = 5000 + index,
                       .display_x = x,
                       .display_y = y,
                       .touch_id = 1,
                       .phase = first ? LAB10_PHASE_DOWN : LAB10_PHASE_MOVE,
                       .route = LAB10_ROUTE_CANVAS,
                       .color_index = LAB10_COLOR_WHITE,
                   }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
        assert(lab10_capture_poll(capture, session) == 1);
    }
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.accepted_point_count == LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY);
    assert(stats.point_cap_hit_count == 1);
    assert(stats.terminal_event_count == 1);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_POINT_CAP);
    assert(!lab10_capture_complete(capture, session));
    lab10_capture_destroy(capture);
}

static void test_allocation_failure(void)
{
    fake_clock_t clock = {.now_us = 6000};
    g_allocation_attempts = 0;
    lab10_capture_t *capture = lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = &clock,
        .alloc = failing_alloc,
        .free = noop_free,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID &&
           g_allocation_attempts == 1);
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_TERMINAL &&
           stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_FAILED &&
           stats.failure_reason == LAB10_CAPTURE_FAILURE_ALLOCATION &&
           stats.terminal_event_count == 1);
    assert(!lab10_capture_stop(capture, session));
    lab10_capture_destroy(capture);
}

static void assert_invalid_configuration(lab10_capture_config_t config,
                                          allocator_probe_t *probe)
{
    lab10_capture_t *capture = lab10_capture_create(&config);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_TERMINAL);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_FAILED);
    assert(stats.failure_reason == LAB10_CAPTURE_FAILURE_INVALID_CONFIGURATION);
    assert(stats.terminal_event_count == 1);
    assert(probe->allocations == 0 && probe->frees == 0);
    lab10_capture_snapshot_t snapshot;
    assert(!lab10_capture_snapshot(capture, session, &snapshot));
    assert(!lab10_capture_release_result(capture, session));
    assert(!lab10_capture_stop(capture, session));
    lab10_capture_destroy(capture);
}

static void test_invalid_configuration(void)
{
    fake_clock_t clock = {.now_us = 6250};
    allocator_probe_t invalid_color_probe = {0};
    assert_invalid_configuration(
        (lab10_capture_config_t){
            .clock_now_us = fake_now,
            .clock_context = &clock,
            .alloc = counting_alloc,
            .free = counting_free,
            .allocator_context = &invalid_color_probe,
            .initial_color_index = LAB10_SWATCH_COUNT,
        },
        &invalid_color_probe);

    allocator_probe_t mismatched_allocator_probe = {0};
    assert_invalid_configuration(
        (lab10_capture_config_t){
            .clock_now_us = fake_now,
            .clock_context = &clock,
            .alloc = counting_alloc,
            .allocator_context = &mismatched_allocator_probe,
            .initial_color_index = LAB10_COLOR_WHITE,
        },
        &mismatched_allocator_probe);

    allocator_probe_t reverse_mismatch_probe = {0};
    assert_invalid_configuration(
        (lab10_capture_config_t){
            .clock_now_us = fake_now,
            .clock_context = &clock,
            .free = counting_free,
            .allocator_context = &reverse_mismatch_probe,
            .initial_color_index = LAB10_COLOR_WHITE,
        },
        &reverse_mismatch_probe);
}

static void test_normal_completion(void)
{
    fake_clock_t clock = {.now_us = 6500};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    submit_and_poll(capture, session,
                    report(clock.now_us, LAB10_PHASE_DOWN, LAB10_ROUTE_CANVAS,
                           20, 20, 1, LAB10_COLOR_WHITE));
    assert(lab10_capture_complete(capture, session));
    assert(!lab10_capture_complete(capture, session));
    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_TERMINAL);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_COMPLETED);
    assert(stats.terminal_event_count == 1 && stats.active_stroke_id == 0);
    lab10_capture_destroy(capture);
}

int main(void)
{
    test_real_points_color_and_boundaries();
    test_routing_rejection_and_second_touch();
    test_raw_fifo_and_queue_overflow();
    test_overflow_precedence_and_advanced_clock();
    test_release_result_session_lifecycle();
    test_caps_timestamps_and_stale_session();
    test_point_cap();
    test_allocation_failure();
    test_invalid_configuration();
    test_normal_completion();
#ifdef LAB10_CAPTURE_TEST_HOOKS
    test_pre_admission_submit_cannot_enter_new_session();
    test_non_reusing_generation_boundary_rejects_stale_submit();
    test_admitted_submit_is_drained_before_terminal_cleanup();
    test_overflow_wins_at_admission_boundary(TERMINAL_BOUNDARY_STOP, 70000);
    test_overflow_wins_at_admission_boundary(TERMINAL_BOUNDARY_CANCEL, 80000);
    test_overflow_wins_at_admission_boundary(TERMINAL_BOUNDARY_COMPLETE, 90000);
    test_overflow_wins_at_admission_boundary(TERMINAL_BOUNDARY_DURATION, 100000);
#endif
    puts("LAB10_CAPTURE_NATIVE_TEST_PASS");
    return 0;
}
