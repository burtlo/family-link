#include "lab10_audio.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    unsigned allocations;
    unsigned frees;
    unsigned outstanding;
    unsigned fail_after;
} allocator_probe_t;

static void *probe_alloc(size_t size, void *context)
{
    allocator_probe_t *probe = context;
    ++probe->allocations;
    if (probe->fail_after != 0 && probe->allocations > probe->fail_after) {
        return NULL;
    }
    void *memory = malloc(size);
    if (memory != NULL) {
        ++probe->outstanding;
    }
    return memory;
}

static void probe_free(void *memory, void *context)
{
    allocator_probe_t *probe = context;
    if (memory != NULL) {
        assert(probe->outstanding > 0);
        --probe->outstanding;
        ++probe->frees;
        free(memory);
    }
}

static lab10_audio_model_t *new_model(void)
{
    lab10_audio_model_t *model = lab10_audio_create(NULL);
    assert(model != NULL);
    return model;
}

static lab10_audio_session_token_t start_model(
    lab10_audio_model_t *model, lab10_audio_scenario_id_t scenario_id)
{
    lab10_audio_session_token_t session_token =
        LAB10_AUDIO_INVALID_SESSION_TOKEN;
    assert(lab10_audio_start(model, scenario_id, &session_token));
    assert(session_token != LAB10_AUDIO_INVALID_SESSION_TOKEN);
    return session_token;
}

static void assert_clean_result(const lab10_audio_result_t *result)
{
    assert(result != NULL);
    assert(result->expectation_met);
    assert(result->resources_released);
    assert(!result->hardware_audio_health_measured);
    assert(result->metrics.provenance == LAB10_AUDIO_PROVENANCE_HOST);
    assert(result->metrics.missing_reason != NULL);
    assert(result->metrics.live_resources_after_terminal == 0);
    assert(result->metrics.audio_buffer_occupancy_frames == 0);
    assert(result->metrics.drawing_queue_depth == 0);
    if (result->cleanup_timestamps_recorded) {
        assert(result->cleanup_timestamp_us >= result->terminal_timestamp_us);
        assert(result->metrics.cleanup_time_us ==
               result->cleanup_timestamp_us - result->terminal_timestamp_us);
    } else {
        assert(result->terminal_timestamp_us == 0);
        assert(result->cleanup_timestamp_us == 0);
        assert(result->metrics.cleanup_time_us == 0);
    }
}

static void test_scenario_catalog_and_all_scenarios(void)
{
    for (int scenario_id = LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL;
         scenario_id < LAB10_AUDIO_SCENARIO_COUNT; ++scenario_id) {
        const lab10_audio_scenario_t *scenario =
            lab10_audio_scenario((lab10_audio_scenario_id_t)scenario_id);
        assert(scenario != NULL);
        assert(scenario->id != NULL && scenario->id[0] != '\0');
        assert(lab10_audio_scenario_id_name(
                   (lab10_audio_scenario_id_t)scenario_id) == scenario->id);
        assert(lab10_audio_expected_outcome_name(scenario->expected_outcome) !=
               NULL);
    }

    lab10_audio_model_t *model = new_model();
    const lab10_audio_scenario_id_t runnable[] = {
        LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL,
        LAB10_AUDIO_SCENARIO_SPARSE_DRAWING,
        LAB10_AUDIO_SCENARIO_CONTINUOUS_DRAWING,
        LAB10_AUDIO_SCENARIO_DENSE_DRAWING,
        LAB10_AUDIO_SCENARIO_RAPID_COLOR_SWITCHING,
        LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO,
        LAB10_AUDIO_SCENARIO_CANCEL_DURING_AUDIO,
        LAB10_AUDIO_SCENARIO_AUDIO_CAPTURE_FAILURE,
    };
    for (size_t index = 0; index < sizeof(runnable) / sizeof(runnable[0]);
         ++index) {
        lab10_audio_result_t result;
        assert(lab10_audio_run_scenario(model, runnable[index], &result));
        assert_clean_result(&result);
        assert(result.scenario_id == runnable[index]);
    }
    lab10_audio_destroy(model);
}

static void test_audio_occupancy_and_accounting(void)
{
    lab10_audio_config_t config = lab10_audio_default_config();
    config.buffer_capacity_frames = 2;
    config.frame_bytes = 32;
    config.samples_per_frame = 7;
    config.frame_duration_us = 100;
    lab10_audio_model_t *model = lab10_audio_create(&config);
    assert(model != NULL);
    const lab10_audio_session_token_t session_token =
        start_model(model, LAB10_AUDIO_SCENARIO_CONTINUOUS_DRAWING);
    assert(lab10_audio_produce_frame(model, session_token, 10));
    assert(lab10_audio_produce_frame(model, session_token, 20));
    assert(!lab10_audio_produce_frame(model, session_token, 30));
    assert(lab10_audio_consume_frame(model, session_token, 40));
    assert(lab10_audio_consume_frame(model, session_token, 50));
    assert(!lab10_audio_consume_frame(model, session_token, 60));

    lab10_audio_metrics_t metrics;
    assert(lab10_audio_get_metrics(model, session_token, &metrics));
    assert(metrics.audio_buffer_high_water_frames == 2);
    assert(metrics.audio_buffer_high_water_bytes == 64);
    assert(metrics.audio_overrun_count == 1);
    assert(metrics.audio_underrun_count == 1);
    assert(metrics.audio_underrun_duration_us == 100);
    assert(metrics.audio_frame_count_produced == 2);
    assert(metrics.audio_frame_count_consumed == 2);
    assert(metrics.audio_sample_count_produced == 14);
    assert(metrics.audio_sample_count_consumed == 14);
    assert(metrics.audio_continuity_status ==
           LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN_AND_UNDERRUN);
    assert(!lab10_audio_complete(model, session_token, 69, 68));
    assert(lab10_audio_get_metrics(model, session_token, &metrics));
    assert(metrics.audio_error_count == 1);
    assert(lab10_audio_complete(model, session_token, 70, 75));
    assert(lab10_audio_get_metrics(model, session_token, &metrics));
    assert(metrics.cleanup_time_us == 5);
    assert(metrics.audio_buffer_occupancy_frames == 0);
    assert(!lab10_audio_complete(model, session_token, 80, 81));
    lab10_audio_destroy(model);
}

static void test_stale_worker_token_cannot_mutate_restarted_session(void)
{
    lab10_audio_model_t *model = new_model();
    const lab10_audio_session_token_t old_token =
        start_model(model, LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL);
    assert(lab10_audio_produce_frame(model, old_token, 10));
    assert(lab10_audio_complete(model, old_token, 20, 25));

    const lab10_audio_session_token_t new_token =
        start_model(model, LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL);
    assert(old_token != new_token);
    lab10_audio_metrics_t before;
    assert(lab10_audio_get_metrics(model, new_token, &before));

    lab10_audio_metrics_t ignored_metrics;
    lab10_audio_result_t ignored_result;
    assert(!lab10_audio_produce_frame(model, old_token, 100));
    assert(!lab10_audio_consume_frame(model, old_token, 101));
    assert(!lab10_audio_submit_drawing(model, old_token, 102, 1, 1));
    assert(lab10_audio_process_drawing(model, old_token, 103, 1) == 0);
    assert(!lab10_audio_stop(model, old_token, 104, 105));
    assert(!lab10_audio_cancel(model, old_token, 106, 107));
    assert(!lab10_audio_fail_capture(model, old_token, 108, 109));
    assert(!lab10_audio_complete(model, old_token, 110, 111));
    assert(!lab10_audio_get_metrics(model, old_token, &ignored_metrics));
    assert(!lab10_audio_get_result(model, old_token, &ignored_result));

    lab10_audio_metrics_t after;
    assert(lab10_audio_get_metrics(model, new_token, &after));
    assert(after.stale_worker_mutation_count ==
           before.stale_worker_mutation_count + 10u);
    assert(after.audio_buffer_occupancy_frames ==
           before.audio_buffer_occupancy_frames);
    assert(after.drawing_queue_depth == before.drawing_queue_depth);
    assert(after.audio_frame_count_produced == before.audio_frame_count_produced);
    assert(after.audio_frame_count_consumed == before.audio_frame_count_consumed);
    assert(after.audio_sample_count_produced ==
           before.audio_sample_count_produced);
    assert(after.audio_sample_count_consumed ==
           before.audio_sample_count_consumed);
    assert(after.drawing_event_count == before.drawing_event_count);
    assert(after.drawing_queue_overflow_count ==
           before.drawing_queue_overflow_count);
    assert(after.audio_error_count == before.audio_error_count);

    assert(lab10_audio_produce_frame(model, new_token, 10));
    assert(lab10_audio_consume_frame(model, new_token, 11));
    assert(lab10_audio_complete(model, new_token, 20, 25));
    lab10_audio_destroy(model);
}

static void test_zero_timestamp_cleanup_schedule_is_explicit(void)
{
    lab10_audio_model_t *model = new_model();
    const lab10_audio_session_token_t session_token =
        start_model(model, LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL);
    assert(lab10_audio_complete(model, session_token, 0, 0));

    lab10_audio_result_t result;
    assert(lab10_audio_get_result(model, session_token, &result));
    assert(result.cleanup_timestamps_recorded);
    assert(result.terminal_timestamp_us == 0);
    assert(result.cleanup_timestamp_us == 0);
    assert(result.metrics.cleanup_time_us == 0);
    lab10_audio_destroy(model);
}

static void test_drawing_audio_separation_and_latency(void)
{
    lab10_audio_config_t config = lab10_audio_default_config();
    config.drawing_queue_capacity = 2;
    lab10_audio_model_t *model = lab10_audio_create(&config);
    assert(model != NULL);
    const lab10_audio_session_token_t session_token =
        start_model(model, LAB10_AUDIO_SCENARIO_DENSE_DRAWING);
    assert(lab10_audio_produce_frame(model, session_token, 10));
    assert(lab10_audio_consume_frame(model, session_token, 11));
    assert(lab10_audio_submit_drawing(model, session_token, 12, 1, 1));
    assert(lab10_audio_submit_drawing(model, session_token, 13, 1, 2));
    assert(!lab10_audio_submit_drawing(model, session_token, 14, 2, 3));
    assert(lab10_audio_process_drawing(model, session_token, 20, 1) == 1);
    assert(lab10_audio_process_drawing(model, session_token, 30, 2) == 1);
    assert(lab10_audio_produce_frame(model, session_token, 31));
    assert(lab10_audio_consume_frame(model, session_token, 32));
    assert(lab10_audio_complete(model, session_token, 33, 40));

    lab10_audio_metrics_t metrics;
    assert(lab10_audio_get_metrics(model, session_token, &metrics));
    assert(metrics.audio_overrun_count == 0);
    assert(metrics.audio_underrun_count == 0);
    assert(metrics.drawing_queue_overflow_count == 1);
    assert(metrics.drawing_event_count == 2);
    assert(metrics.drawing_color_change_count == 1);
    assert(metrics.drawing_stroke_count == 1);
    assert(metrics.drawing_latency_us_p50 == 8);
    assert(metrics.drawing_latency_us_max == 17);
    assert(metrics.drawing_queue_high_water == 2);
    lab10_audio_destroy(model);
}

static void test_stop_cancel_and_capture_failure_cleanup(void)
{
    const lab10_audio_scenario_id_t scenarios[] = {
        LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO,
        LAB10_AUDIO_SCENARIO_CANCEL_DURING_AUDIO,
        LAB10_AUDIO_SCENARIO_AUDIO_CAPTURE_FAILURE,
    };
    lab10_audio_model_t *model = new_model();
    for (size_t index = 0; index < sizeof(scenarios) / sizeof(scenarios[0]);
         ++index) {
        lab10_audio_result_t result;
        assert(lab10_audio_run_scenario(model, scenarios[index], &result));
        assert_clean_result(&result);
        assert(!lab10_audio_stop(model, result.session_token, 100000, 100001));
        assert(!lab10_audio_cancel(model, result.session_token, 100002, 100003));
    }
    lab10_audio_destroy(model);
}

static void test_allocator_bounds_and_failure(void)
{
    lab10_audio_config_t invalid = lab10_audio_default_config();
    invalid.buffer_capacity_frames = LAB10_AUDIO_MODEL_MAX_BUFFER_FRAMES + 1u;
    assert(lab10_audio_create(&invalid) == NULL);
    invalid = lab10_audio_default_config();
    invalid.frame_bytes = LAB10_AUDIO_MODEL_MAX_FRAME_BYTES + 1u;
    assert(lab10_audio_create(&invalid) == NULL);

    allocator_probe_t probe = {.fail_after = 1};
    lab10_audio_config_t config = lab10_audio_default_config();
    config.alloc = probe_alloc;
    config.free = probe_free;
    config.allocator_context = &probe;
    lab10_audio_model_t *model = lab10_audio_create(&config);
    assert(model != NULL);
    lab10_audio_result_t result;
    assert(lab10_audio_run_scenario(model,
                                    LAB10_AUDIO_SCENARIO_ALLOCATION_FAILURE,
                                    &result));
    assert_clean_result(&result);
    assert(result.observed_outcome == LAB10_AUDIO_OUTCOME_FAILED);
    assert(result.failure == LAB10_AUDIO_FAILURE_ALLOCATION);
    assert(result.metrics.allocation_attempt_count == 1);
    assert(result.metrics.allocation_failure_count == 1);
    lab10_audio_destroy(model);
    assert(probe.outstanding == 0);
    assert(probe.allocations == 2);
    assert(probe.frees == 1);
}

static void test_repeated_cycles_and_resource_balance(void)
{
    allocator_probe_t probe = {0};
    lab10_audio_config_t config = lab10_audio_default_config();
    config.alloc = probe_alloc;
    config.free = probe_free;
    config.allocator_context = &probe;
    lab10_audio_model_t *model = lab10_audio_create(&config);
    assert(model != NULL);
    for (unsigned cycle = 0; cycle < 100u; ++cycle) {
        lab10_audio_result_t result;
        assert(lab10_audio_run_scenario(
            model, LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL, &result));
        assert_clean_result(&result);
    }
    lab10_audio_destroy(model);
    assert(probe.outstanding == 0);
    assert(probe.allocations == probe.frees);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned participants;
    unsigned ready;
    bool released;
} audio_thread_gate_t;

typedef struct {
    lab10_audio_model_t *model;
    lab10_audio_session_token_t session_token;
    lab10_audio_session_token_t stale_session_token;
    audio_thread_gate_t *gate;
    atomic_uint current_calls;
    atomic_uint stale_calls;
    atomic_uint terminal_successes;
    atomic_bool terminal_finished;
} audio_thread_context_t;

static void audio_thread_gate_init(audio_thread_gate_t *gate,
                                   unsigned participants)
{
    assert(pthread_mutex_init(&gate->mutex, NULL) == 0);
    assert(pthread_cond_init(&gate->condition, NULL) == 0);
    gate->participants = participants;
    gate->ready = 0;
    gate->released = false;
}

static void audio_thread_gate_wait(audio_thread_gate_t *gate)
{
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    ++gate->ready;
    if (gate->ready == gate->participants) {
        assert(pthread_cond_broadcast(&gate->condition) == 0);
    }
    while (!gate->released) {
        assert(pthread_cond_wait(&gate->condition, &gate->mutex) == 0);
    }
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
}

static void audio_thread_gate_release(audio_thread_gate_t *gate)
{
    assert(pthread_mutex_lock(&gate->mutex) == 0);
    while (gate->ready != gate->participants) {
        assert(pthread_cond_wait(&gate->condition, &gate->mutex) == 0);
    }
    gate->released = true;
    assert(pthread_cond_broadcast(&gate->condition) == 0);
    assert(pthread_mutex_unlock(&gate->mutex) == 0);
}

static void audio_thread_gate_destroy(audio_thread_gate_t *gate)
{
    assert(pthread_cond_destroy(&gate->condition) == 0);
    assert(pthread_mutex_destroy(&gate->mutex) == 0);
}

static void *audio_concurrent_producer(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    for (unsigned index = 0; index < 512u; ++index) {
        atomic_fetch_add(&context->current_calls, 1u);
        (void)lab10_audio_produce_frame(context->model,
                                        context->session_token, 100u);
    }
    return NULL;
}

static void *audio_concurrent_consumer(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    for (unsigned index = 0; index < 512u; ++index) {
        atomic_fetch_add(&context->current_calls, 1u);
        (void)lab10_audio_consume_frame(context->model,
                                        context->session_token, 100u);
    }
    return NULL;
}

static void *audio_concurrent_drawing(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    for (unsigned index = 0; index < 512u; ++index) {
        atomic_fetch_add(&context->current_calls, 1u);
        (void)lab10_audio_submit_drawing(
            context->model, context->session_token, 100u, index + 1u,
            (uint8_t)((index % 5u) + 1u));
        atomic_fetch_add(&context->current_calls, 1u);
        (void)lab10_audio_process_drawing(context->model,
                                           context->session_token, 100u, 1u);
    }
    return NULL;
}

static void *audio_concurrent_observer(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    for (unsigned index = 0; index < 512u; ++index) {
        lab10_audio_metrics_t metrics;
        lab10_audio_result_t result;
        assert(lab10_audio_get_metrics(context->model,
                                       context->session_token, &metrics));
        assert(lab10_audio_get_result(context->model,
                                      context->session_token, &result));
    }
    return NULL;
}

static void *audio_concurrent_stop(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    while (atomic_load(&context->current_calls) < 64u) {
    }
    if (lab10_audio_stop(context->model, context->session_token, 1000u,
                         1005u)) {
        atomic_fetch_add(&context->terminal_successes, 1u);
    }
    atomic_store(&context->terminal_finished, true);
    return NULL;
}

static void stale_call_counted(audio_thread_context_t *context, bool rejected)
{
    assert(rejected);
    atomic_fetch_add(&context->stale_calls, 1u);
}

static void run_one_stale_call_set(audio_thread_context_t *context)
{
    lab10_audio_metrics_t ignored_metrics;
    lab10_audio_result_t ignored_result;
    stale_call_counted(
        context,
        !lab10_audio_produce_frame(context->model,
                                   context->stale_session_token, 3000u));
    stale_call_counted(
        context,
        !lab10_audio_consume_frame(context->model,
                                   context->stale_session_token, 3001u));
    stale_call_counted(
        context,
        !lab10_audio_submit_drawing(context->model,
                                    context->stale_session_token, 3002u, 1u,
                                    1u));
    stale_call_counted(
        context,
        lab10_audio_process_drawing(context->model,
                                    context->stale_session_token, 3003u, 1u) ==
            0);
    stale_call_counted(
        context,
        !lab10_audio_stop(context->model, context->stale_session_token, 3004u,
                          3005u));
    stale_call_counted(
        context,
        !lab10_audio_cancel(context->model, context->stale_session_token, 3006u,
                            3007u));
    stale_call_counted(
        context,
        !lab10_audio_fail_capture(context->model,
                                  context->stale_session_token, 3008u, 3009u));
    stale_call_counted(
        context,
        !lab10_audio_complete(context->model,
                              context->stale_session_token, 3010u, 3011u));
    stale_call_counted(
        context,
        !lab10_audio_get_metrics(context->model,
                                 context->stale_session_token, &ignored_metrics));
    stale_call_counted(
        context,
        !lab10_audio_get_result(context->model,
                                context->stale_session_token, &ignored_result));
}

static void *audio_concurrent_stale_worker(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    for (unsigned index = 0; index < 128u; ++index) {
        run_one_stale_call_set(context);
    }
    while (!atomic_load(&context->terminal_finished)) {
        run_one_stale_call_set(context);
    }
    for (unsigned index = 0; index < 128u; ++index) {
        run_one_stale_call_set(context);
    }
    return NULL;
}

static void *audio_concurrent_duplicate_stop(void *argument)
{
    audio_thread_context_t *context = argument;
    audio_thread_gate_wait(context->gate);
    while (atomic_load(&context->current_calls) < 64u) {
    }
    if (lab10_audio_stop(context->model, context->session_token, 2000u,
                         2005u)) {
        atomic_fetch_add(&context->terminal_successes, 1u);
    }
    atomic_store(&context->terminal_finished, true);
    return NULL;
}

static void join_audio_threads(pthread_t *threads, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        assert(pthread_join(threads[index], NULL) == 0);
    }
}

static void assert_audio_metrics_unchanged_except_stale(
    const lab10_audio_metrics_t *before, const lab10_audio_metrics_t *after)
{
    assert(after->provenance == before->provenance);
    assert(after->audio_continuity_status == before->audio_continuity_status);
    assert(after->audio_buffer_occupancy_frames ==
           before->audio_buffer_occupancy_frames);
    assert(after->audio_buffer_occupancy_bytes ==
           before->audio_buffer_occupancy_bytes);
    assert(after->audio_buffer_high_water_frames ==
           before->audio_buffer_high_water_frames);
    assert(after->audio_buffer_high_water_bytes ==
           before->audio_buffer_high_water_bytes);
    assert(after->audio_overrun_count == before->audio_overrun_count);
    assert(after->audio_underrun_count == before->audio_underrun_count);
    assert(after->audio_underrun_duration_us ==
           before->audio_underrun_duration_us);
    assert(after->audio_frame_count_produced ==
           before->audio_frame_count_produced);
    assert(after->audio_frame_count_consumed ==
           before->audio_frame_count_consumed);
    assert(after->audio_sample_count_produced ==
           before->audio_sample_count_produced);
    assert(after->audio_sample_count_consumed ==
           before->audio_sample_count_consumed);
    assert(after->audio_error_count == before->audio_error_count);
    assert(after->drawing_event_count == before->drawing_event_count);
    assert(after->drawing_queue_depth == before->drawing_queue_depth);
    assert(after->drawing_queue_high_water == before->drawing_queue_high_water);
    assert(after->drawing_queue_overflow_count ==
           before->drawing_queue_overflow_count);
    assert(after->drawing_color_change_count ==
           before->drawing_color_change_count);
    assert(after->drawing_stroke_count == before->drawing_stroke_count);
    assert(after->drawing_latency_us_p50 == before->drawing_latency_us_p50);
    assert(after->drawing_latency_us_p95 == before->drawing_latency_us_p95);
    assert(after->drawing_latency_us_p99 == before->drawing_latency_us_p99);
    assert(after->drawing_latency_us_max == before->drawing_latency_us_max);
    assert(after->retained_pcm_bytes == before->retained_pcm_bytes);
    assert(after->transient_pcm_bytes == before->transient_pcm_bytes);
    assert(after->retained_sketch_bytes == before->retained_sketch_bytes);
    assert(after->transient_sketch_bytes == before->transient_sketch_bytes);
    assert(after->retained_framebuffer_bytes ==
           before->retained_framebuffer_bytes);
    assert(after->transient_framebuffer_bytes ==
           before->transient_framebuffer_bytes);
    assert(after->largest_allocation_bytes == before->largest_allocation_bytes);
    assert(after->allocation_attempt_count == before->allocation_attempt_count);
    assert(after->allocation_failure_count == before->allocation_failure_count);
    assert(after->cleanup_time_us == before->cleanup_time_us);
    assert(after->live_resources_after_terminal ==
           before->live_resources_after_terminal);
}

static void test_threaded_audio_ownership_and_restart(void)
{
    lab10_audio_config_t config = lab10_audio_default_config();
    config.buffer_capacity_frames = 4;
    config.drawing_queue_capacity = 4;
    lab10_audio_model_t *model = lab10_audio_create(&config);
    assert(model != NULL);

    const lab10_audio_session_token_t first_token =
        start_model(model, LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO);
    for (unsigned index = 0; index < config.buffer_capacity_frames; ++index) {
        assert(lab10_audio_produce_frame(model, first_token, 1u));
    }
    assert(!lab10_audio_produce_frame(model, first_token, 1u));
    for (unsigned index = 0; index < config.drawing_queue_capacity; ++index) {
        assert(lab10_audio_submit_drawing(model, first_token, 1u, index + 1u,
                                          1u));
    }
    assert(!lab10_audio_submit_drawing(model, first_token, 1u, 99u, 1u));

    audio_thread_gate_t first_gate;
    audio_thread_gate_init(&first_gate, 5u);
    audio_thread_context_t first_context = {
        .model = model,
        .session_token = first_token,
        .stale_session_token = LAB10_AUDIO_INVALID_SESSION_TOKEN,
        .gate = &first_gate,
    };
    atomic_init(&first_context.current_calls, 0u);
    atomic_init(&first_context.stale_calls, 0u);
    atomic_init(&first_context.terminal_successes, 0u);
    atomic_init(&first_context.terminal_finished, false);
    pthread_t first_threads[5];
    assert(pthread_create(&first_threads[0], NULL, audio_concurrent_producer,
                          &first_context) == 0);
    assert(pthread_create(&first_threads[1], NULL, audio_concurrent_consumer,
                          &first_context) == 0);
    assert(pthread_create(&first_threads[2], NULL, audio_concurrent_drawing,
                          &first_context) == 0);
    assert(pthread_create(&first_threads[3], NULL, audio_concurrent_observer,
                          &first_context) == 0);
    assert(pthread_create(&first_threads[4], NULL, audio_concurrent_stop,
                          &first_context) == 0);
    audio_thread_gate_release(&first_gate);
    join_audio_threads(first_threads, 5u);
    audio_thread_gate_destroy(&first_gate);

    assert(atomic_load(&first_context.terminal_successes) == 1u);
    lab10_audio_result_t first_result;
    assert(lab10_audio_get_result(model, first_token, &first_result));
    assert(first_result.observed_outcome == LAB10_AUDIO_OUTCOME_STOPPED);
    assert_clean_result(&first_result);
    assert(first_result.metrics.audio_buffer_high_water_frames <=
           config.buffer_capacity_frames);
    assert(first_result.metrics.drawing_queue_high_water <=
           config.drawing_queue_capacity);

    const lab10_audio_session_token_t second_token =
        start_model(model, LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO);
    assert(second_token != first_token);
    for (unsigned index = 0; index < config.buffer_capacity_frames; ++index) {
        assert(lab10_audio_produce_frame(model, second_token, 10u));
    }
    assert(!lab10_audio_produce_frame(model, second_token, 10u));
    for (unsigned index = 0; index < config.drawing_queue_capacity; ++index) {
        assert(lab10_audio_submit_drawing(model, second_token, 10u,
                                          index + 1u, (uint8_t)(index + 1u)));
    }
    assert(!lab10_audio_submit_drawing(model, second_token, 10u, 99u, 1u));

    audio_thread_gate_t second_gate;
    audio_thread_gate_init(&second_gate, 7u);
    audio_thread_context_t second_context = {
        .model = model,
        .session_token = second_token,
        .stale_session_token = first_token,
        .gate = &second_gate,
    };
    atomic_init(&second_context.current_calls, 0u);
    atomic_init(&second_context.stale_calls, 0u);
    atomic_init(&second_context.terminal_successes, 0u);
    atomic_init(&second_context.terminal_finished, false);
    pthread_t second_threads[7];
    assert(pthread_create(&second_threads[0], NULL,
                          audio_concurrent_stale_worker, &second_context) == 0);
    assert(pthread_create(&second_threads[1], NULL, audio_concurrent_producer,
                          &second_context) == 0);
    assert(pthread_create(&second_threads[2], NULL, audio_concurrent_consumer,
                          &second_context) == 0);
    assert(pthread_create(&second_threads[3], NULL, audio_concurrent_drawing,
                          &second_context) == 0);
    assert(pthread_create(&second_threads[4], NULL, audio_concurrent_observer,
                          &second_context) == 0);
    assert(pthread_create(&second_threads[5], NULL,
                          audio_concurrent_duplicate_stop, &second_context) ==
           0);
    assert(pthread_create(&second_threads[6], NULL,
                          audio_concurrent_duplicate_stop, &second_context) ==
           0);
    audio_thread_gate_release(&second_gate);
    join_audio_threads(second_threads, 7u);
    audio_thread_gate_destroy(&second_gate);

    assert(atomic_load(&second_context.terminal_successes) == 1u);
    assert(atomic_load(&second_context.terminal_finished));
    assert(atomic_load(&second_context.stale_calls) >= 2560u);

    lab10_audio_metrics_t before_final_stale;
    assert(lab10_audio_get_metrics(model, second_token, &before_final_stale));
    run_one_stale_call_set(&second_context);
    lab10_audio_metrics_t after_final_stale;
    assert(lab10_audio_get_metrics(model, second_token, &after_final_stale));
    assert(after_final_stale.stale_worker_mutation_count ==
           before_final_stale.stale_worker_mutation_count + 10u);
    assert_audio_metrics_unchanged_except_stale(&before_final_stale,
                                                &after_final_stale);

    lab10_audio_result_t second_result;
    assert(lab10_audio_get_result(model, second_token, &second_result));
    assert(second_result.session_token == second_token);
    assert(second_result.observed_outcome == LAB10_AUDIO_OUTCOME_STOPPED);
    assert_clean_result(&second_result);
    assert(second_result.metrics.stale_worker_mutation_count ==
           atomic_load(&second_context.stale_calls));
    assert(second_result.metrics.audio_overrun_count > 0);
    assert(second_result.metrics.drawing_queue_overflow_count > 0);
    assert(second_result.metrics.audio_buffer_high_water_frames <=
           config.buffer_capacity_frames);
    assert(second_result.metrics.drawing_queue_high_water <=
           config.drawing_queue_capacity);
    assert(!lab10_audio_complete(model, second_token, 4000u, 4001u));
    lab10_audio_result_t after_duplicate_terminal;
    assert(lab10_audio_get_result(model, second_token,
                                  &after_duplicate_terminal));
    assert(after_duplicate_terminal.observed_outcome ==
           LAB10_AUDIO_OUTCOME_STOPPED);
    assert(after_duplicate_terminal.cleanup_timestamp_us ==
           second_result.cleanup_timestamp_us);

    /* All concurrent workers above are joined before destroy by contract. */
    lab10_audio_destroy(model);
}

int main(void)
{
    test_scenario_catalog_and_all_scenarios();
    test_audio_occupancy_and_accounting();
    test_stale_worker_token_cannot_mutate_restarted_session();
    test_zero_timestamp_cleanup_schedule_is_explicit();
    test_drawing_audio_separation_and_latency();
    test_stop_cancel_and_capture_failure_cleanup();
    test_allocator_bounds_and_failure();
    test_repeated_cycles_and_resource_balance();
    test_threaded_audio_ownership_and_restart();
    puts("LAB10_AUDIO_NATIVE_TEST_PASS");
    return 0;
}
