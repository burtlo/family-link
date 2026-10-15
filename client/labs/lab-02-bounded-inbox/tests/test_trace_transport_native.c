#include "lab02/trace_emit.h"
#include "trace_host_stubs.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t s_clock;

int64_t esp_timer_get_time(void)
{
    return ++s_clock;
}

static void require_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "TRACE_NATIVE_FAIL %s\n", message);
        exit(1);
    }
}

static lab02_trace_stats_t stats(void)
{
    lab02_trace_stats_t value;
    lab02_trace_stats(&value);
    return value;
}

typedef struct {
    unsigned base;
} producer_args_t;

static void *producer(void *arg)
{
    producer_args_t *args = arg;
    char event[32];
    char message[24];
    char extra[80];
    for (unsigned i = 0; i < 32u; ++i) {
        snprintf(event, sizeof(event), "producer_%u", args->base);
        snprintf(message, sizeof(message), "msg_%u_%u", args->base, i);
        snprintf(extra, sizeof(extra), "producer=%u sequence=%u", args->base, i);
        lab02_trace_emit(event, args->base + i, (int)i, message, extra);
    }
    return NULL;
}

static atomic_bool s_start_gate;

static void *repeated_start(void *arg)
{
    bool *result = arg;
    while (!atomic_load(&s_start_gate)) {
        sched_yield();
    }
    *result = lab02_trace_start();
    return NULL;
}

static void *drain_trace(void *arg)
{
    (void)arg;
    lab02_trace_drain();
    return NULL;
}

static void *drain_trace_marked(void *arg)
{
    atomic_bool *done = arg;
    lab02_trace_drain();
    atomic_store(done, true);
    return NULL;
}

static void *reset_trace_marked(void *arg)
{
    atomic_bool *done = arg;
    lab02_trace_reset();
    atomic_store(done, true);
    return NULL;
}

static bool wait_for_true(const atomic_bool *value)
{
    for (unsigned i = 0; i < 100000u; ++i) {
        if (atomic_load(value)) {
            return true;
        }
        sched_yield();
    }
    return false;
}

int main(void)
{
    lab02_trace_init();

    /* xTaskCreateStatic success is real, repeated start is idempotent, and
     * concurrent callers can claim only one creation attempt. */
    trace_host_set_task_create_failure(false);
    unsigned creates = trace_host_task_create_count();
    require_true(lab02_trace_start(), "successful task creation");
    require_true(trace_host_last_task_stack_words() == LAB02_TRACE_TASK_STACK_WORDS,
                 "trace task stack depth wired");
    require_true(LAB02_TRACE_TASK_STACK_WORDS >= 4096u,
                 "trace task stack budget");
    require_true(lab02_trace_start(), "repeated start");
    require_true(trace_host_task_create_count() == creates + 1u,
                 "repeated start created one task");
    lab02_trace_stop();

    lab02_trace_reset();
    creates = trace_host_task_create_count();
    atomic_store(&s_start_gate, false);
    pthread_t starters[4];
    bool start_results[4] = {false, false, false, false};
    for (unsigned i = 0; i < 4u; ++i) {
        require_true(pthread_create(&starters[i], NULL, repeated_start,
                                    &start_results[i]) == 0, "starter thread");
    }
    atomic_store(&s_start_gate, true);
    for (unsigned i = 0; i < 4u; ++i) {
        pthread_join(starters[i], NULL);
        require_true(start_results[i], "concurrent start accepted");
    }
    require_true(trace_host_task_create_count() == creates + 1u,
                 "concurrent start created one task");
    lab02_trace_stop();

    trace_host_set_task_create_failure(true);
    require_true(!lab02_trace_start(), "task creation failure surfaced");
    lab02_trace_stats_t before_retry = stats();
    trace_host_set_task_create_failure(false);
    require_true(lab02_trace_start(), "start claim rolled back after failure");
    lab02_trace_stop();
    require_true(before_retry.start_failures > 0u, "start failure accounted");

    /* Reset is a lifecycle boundary: pre-reset records never drain later, but
     * cumulative loss evidence survives the queue/state reset. */
    lab02_trace_emit("before_reset", 1, 1, "old_message", "old_extra");
    lab02_trace_stats_t pre_reset = stats();
    lab02_trace_reset();
    lab02_trace_stats_t post_reset = stats();
    require_true(post_reset.stale_discarded == pre_reset.stale_discarded,
                 "reset defers stale queued record reconciliation");
    lab02_trace_drain();
    post_reset = stats();
    require_true(post_reset.stale_discarded > pre_reset.stale_discarded,
                 "drain accounts stale queued record");
    lab02_trace_emit("after_reset", 2, 2, "new_message", "new_extra");
    lab02_trace_drain();

    /* Typed values are copied before the producer returns; formatting is
     * deferred until drain and every applicable C5a field remains present. */
    lab02_trace_reset();
    char state[32] = "running";
    char outcome[32] = "success";
    char reason[64] = "completed";
    char receipt[33] = "receipt_old";
    char run_id[33] = "run_old";
    char event_name[32] = "whole_row";
    char message_id[24] = "message_old";
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_REVISION |
                  LAB02_TRACE_FIELD_SNAP_ID | LAB02_TRACE_FIELD_OP_ID |
                  LAB02_TRACE_FIELD_OUTCOME | LAB02_TRACE_FIELD_REASON |
                  LAB02_TRACE_FIELD_STATE | LAB02_TRACE_FIELD_PAGE_RECEIPT_ID |
                  LAB02_TRACE_FIELD_RUN_ID | LAB02_TRACE_FIELD_ELAPSED_US,
        .session_id = 7u,
        .revision = 8u,
        .snap_id = 9u,
        .op_id = 10u,
        .outcome = outcome,
        .reason = reason,
        .state = state,
        .page_receipt_id = receipt,
        .run_id = run_id,
        .elapsed_us = 11,
    };
    lab02_trace_emit_payload(event_name, 10u, 4, message_id, &payload);
    strcpy(event_name, "mutated_event");
    strcpy(message_id, "mutated_message");
    strcpy(state, "mutated_state");
    strcpy(outcome, "mutated_outcome");
    strcpy(reason, "mutated_reason");
    strcpy(receipt, "receipt_mutated");
    strcpy(run_id, "run_mutated");
    lab02_trace_drain();

    /* Saturation is observable and never blocks the producer. */
    lab02_trace_reset();
    lab02_trace_stats_t saturation_start = stats();
    for (unsigned i = 0; i < LAB02_TRACE_QUEUE_CAPACITY; ++i) {
        lab02_trace_emit("saturated", i, (int)i, "stable", "row");
    }
    lab02_trace_emit("overflow", 99, 99, "dropped", "row");
    lab02_trace_stats_t saturation = stats();
    require_true(saturation.admitted == LAB02_TRACE_QUEUE_CAPACITY,
                 "queue admission count");
    require_true(saturation.dropped - saturation_start.dropped == 1u &&
                 saturation.queue_full - saturation_start.queue_full == 1u,
                 "queue loss accounting");
    require_true(saturation.high_water == LAB02_TRACE_QUEUE_CAPACITY,
                 "queue high water");
    lab02_trace_drain();

    /* Two producers share the bounded admission path without borrowed storage races. */
    lab02_trace_reset();
    lab02_trace_stats_t concurrent_start = stats();
    pthread_t owner;
    pthread_t worker;
    producer_args_t owner_args = {.base = 10u};
    producer_args_t worker_args = {.base = 20u};
    require_true(pthread_create(&owner, NULL, producer, &owner_args) == 0,
                 "owner thread");
    require_true(pthread_create(&worker, NULL, producer, &worker_args) == 0,
                 "worker thread");
    pthread_join(owner, NULL);
    pthread_join(worker, NULL);
    lab02_trace_stats_t concurrent = stats();
    require_true((concurrent.admitted - concurrent_start.admitted) +
                     (concurrent.dropped - concurrent_start.dropped) == 64u,
                 "producer accounting conservation");
    require_true(concurrent.dropped > concurrent_start.dropped,
                 "concurrent saturation loss");
    lab02_trace_drain();

    /* Oversized data is rejected before queue admission. */
    lab02_trace_reset();
    lab02_trace_stats_t oversized_start = stats();
    char oversized[LAB02_TRACE_EXTRA_CAP + 1u];
    memset(oversized, 'x', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = '\0';
    lab02_trace_emit("oversized", 4, 4, "message", oversized);
    lab02_trace_stats_t oversized_stats = stats();
    require_true(oversized_stats.admitted == 0u &&
                 oversized_stats.oversized - oversized_start.oversized == 1u &&
                 oversized_stats.dropped - oversized_start.dropped == 1u,
                 "oversized loss accounting");
    lab02_trace_stats_t loss_before_reset = stats();
    lab02_trace_reset();
    lab02_trace_stats_t loss_after_reset = stats();
    require_true(loss_after_reset.dropped == loss_before_reset.dropped &&
                 loss_after_reset.oversized == loss_before_reset.oversized &&
                 loss_after_reset.queue_full == loss_before_reset.queue_full &&
                 loss_after_reset.stale_discarded == loss_before_reset.stale_discarded,
                 "reset preserves cumulative loss evidence");

    /* A stalled sink may stop the drain context, but it must not stop a
     * producer that only performs bounded copy/admission. */
    lab02_trace_reset();
    lab02_trace_emit("sink_blocked", 5, 5, "stable", "row");
    trace_host_set_sink_blocked(true);
    trace_host_reset_sink_observation();
    pthread_t drain;
    require_true(pthread_create(&drain, NULL, drain_trace, NULL) == 0,
                 "stalled drain thread");
    trace_host_wait_sink_entered();
    atomic_bool competing_drain_done = false;
    pthread_t competing_drain;
    require_true(pthread_create(&competing_drain, NULL, drain_trace_marked,
                                &competing_drain_done) == 0,
                 "competing drain thread");
    bool competing_drain_returned = wait_for_true(&competing_drain_done);
    if (!competing_drain_returned) {
        trace_host_set_sink_blocked(false);
    }
    pthread_join(competing_drain, NULL);
    require_true(competing_drain_returned,
                 "competing public drain does not wait or interleave");
    lab02_trace_stats_t stalled_start = stats();
    lab02_trace_emit("producer_while_sink_blocked", 6, 6, "stable", "row");
    lab02_trace_stats_t stalled_end = stats();
    require_true(stalled_end.admitted == stalled_start.admitted + 1u,
                 "producer admission independent of sink progress");
    trace_host_set_sink_blocked(false);
    pthread_join(drain, NULL);

    /* Reset must mark the lifecycle and return while the task owns a
     * blocked sink.  Static queue/task storage stays alive until stop can
     * safely reap the task after the sink is released. */
    lab02_trace_reset();
    lab02_trace_emit("task_sink_blocked", 7, 7, "stable", "row");
    trace_host_set_sink_blocked(true);
    trace_host_reset_sink_observation();
    require_true(lab02_trace_start(), "start task for reset stall");
    trace_host_wait_sink_entered();
    atomic_bool competing_task_drain_done = false;
    pthread_t competing_task_drain;
    require_true(pthread_create(&competing_task_drain, NULL, drain_trace_marked,
                                &competing_task_drain_done) == 0,
                 "task/public drain contention thread");
    bool competing_task_drain_returned = wait_for_true(&competing_task_drain_done);
    if (!competing_task_drain_returned) {
        trace_host_set_sink_blocked(false);
    }
    pthread_join(competing_task_drain, NULL);
    require_true(competing_task_drain_returned,
                 "public drain does not wait behind task sink");

    atomic_bool reset_done = false;
    pthread_t reset_thread;
    require_true(pthread_create(&reset_thread, NULL, reset_trace_marked,
                                &reset_done) == 0,
                 "stalled reset thread");
    bool reset_returned = wait_for_true(&reset_done);
    if (!reset_returned) {
        trace_host_set_sink_blocked(false);
    }
    pthread_join(reset_thread, NULL);
    require_true(reset_returned, "reset does not wait behind stalled sink");
    trace_host_set_sink_blocked(false);
    lab02_trace_stop();

    puts("TRACE_NATIVE_RESULT PASS");
    return 0;
}
