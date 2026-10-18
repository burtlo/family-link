#include "lab10_audio.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t queued_at_us;
    uint32_t stroke_id;
    uint8_t color_index;
} lab10_audio_drawing_event_t;

static const char *const HOST_MODEL_MISSING_REASON =
    "host bounded model only; microphone, codec, DAC, speaker, DMA, and task "
    "scheduling were not exercised";

struct lab10_audio_model {
    atomic_flag state_lock;
    lab10_audio_config_t config;
    lab10_audio_scenario_id_t scenario_id;
    lab10_audio_session_token_t session_token;
    lab10_audio_session_token_t last_issued_session_token;
    uint8_t *pcm;
    uint16_t pcm_head;
    uint16_t pcm_tail;
    uint16_t pcm_depth;
    lab10_audio_drawing_event_t drawing_queue[LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE];
    uint16_t drawing_head;
    uint16_t drawing_tail;
    uint16_t drawing_depth;
    uint64_t drawing_latencies[LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE];
    size_t drawing_latency_count;
    uint64_t last_timestamp_us;
    bool has_timestamp;
    bool has_previous_color;
    uint8_t previous_color;
    bool has_previous_stroke;
    uint32_t previous_stroke;
    lab10_audio_status_t status;
    lab10_audio_outcome_t outcome;
    lab10_audio_failure_t failure;
    bool cleanup_timestamps_recorded;
    uint64_t terminal_timestamp_us;
    uint64_t cleanup_timestamp_us;
    lab10_audio_metrics_t metrics;
};

/*
 * One C11 atomic_flag serializes the complete model state. The lock is
 * intentionally platform-neutral: it has no host-threading or ESP-IDF
 * dependency. Every public operation takes it before validating a token or
 * touching queues, terminal state, metrics, or results. The API is non-recursive;
 * internal helpers are called only while the caller holds this lock.
 */
static void model_lock(lab10_audio_model_t *model)
{
    while (atomic_flag_test_and_set_explicit(&model->state_lock,
                                              memory_order_acquire)) {
    }
}

static void model_unlock(lab10_audio_model_t *model)
{
    atomic_flag_clear_explicit(&model->state_lock, memory_order_release);
}

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

static bool scenario_id_valid(lab10_audio_scenario_id_t scenario_id)
{
    return scenario_id >= LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL &&
           scenario_id < LAB10_AUDIO_SCENARIO_COUNT;
}

static bool config_valid(const lab10_audio_config_t *config)
{
    if (config == NULL || config->frame_bytes == 0 ||
        config->frame_bytes > LAB10_AUDIO_MODEL_MAX_FRAME_BYTES ||
        config->samples_per_frame == 0 || config->frame_duration_us == 0 ||
        config->buffer_capacity_frames == 0 ||
        config->buffer_capacity_frames > LAB10_AUDIO_MODEL_MAX_BUFFER_FRAMES ||
        config->drawing_queue_capacity == 0 ||
        config->drawing_queue_capacity > LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE ||
        config->transient_pcm_bytes > LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES ||
        config->retained_sketch_bytes > LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES ||
        config->transient_sketch_bytes > LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES ||
        config->retained_framebuffer_bytes > LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES ||
        config->transient_framebuffer_bytes > LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES) {
        return false;
    }
    if (config->frame_bytes > SIZE_MAX / config->buffer_capacity_frames) {
        return false;
    }
    if ((config->alloc == NULL) != (config->free == NULL)) {
        return false;
    }
    return true;
}

static void reset_run_state(lab10_audio_model_t *model)
{
    if (model->pcm != NULL) {
        model->config.free(model->pcm, model->config.allocator_context);
        model->pcm = NULL;
    }
    model->pcm_head = 0;
    model->pcm_tail = 0;
    model->pcm_depth = 0;
    model->drawing_head = 0;
    model->drawing_tail = 0;
    model->drawing_depth = 0;
    model->drawing_latency_count = 0;
    model->last_timestamp_us = 0;
    model->has_timestamp = false;
    model->has_previous_color = false;
    model->previous_color = 0;
    model->has_previous_stroke = false;
    model->previous_stroke = 0;
    model->status = LAB10_AUDIO_STATUS_UNINITIALIZED;
    model->outcome = LAB10_AUDIO_OUTCOME_NONE;
    model->failure = LAB10_AUDIO_FAILURE_NONE;
    model->cleanup_timestamps_recorded = false;
    model->terminal_timestamp_us = 0;
    model->cleanup_timestamp_us = 0;
    memset(&model->metrics, 0, sizeof(model->metrics));
    model->metrics.provenance = LAB10_AUDIO_PROVENANCE_HOST;
    model->metrics.missing_reason = HOST_MODEL_MISSING_REASON;
    model->metrics.transient_pcm_bytes = model->config.transient_pcm_bytes;
    model->metrics.retained_sketch_bytes = model->config.retained_sketch_bytes;
    model->metrics.transient_sketch_bytes = model->config.transient_sketch_bytes;
    model->metrics.retained_framebuffer_bytes =
        model->config.retained_framebuffer_bytes;
    model->metrics.transient_framebuffer_bytes =
        model->config.transient_framebuffer_bytes;
    model->metrics.audio_continuity_status = LAB10_AUDIO_CONTINUITY_MODEL_NONE;
}

static bool session_token_valid(lab10_audio_model_t *model,
                                lab10_audio_session_token_t session_token)
{
    if (model == NULL || model->status == LAB10_AUDIO_STATUS_UNINITIALIZED) {
        return false;
    }
    if (session_token == LAB10_AUDIO_INVALID_SESSION_TOKEN ||
        session_token != model->session_token) {
        ++model->metrics.stale_worker_mutation_count;
        return false;
    }
    return true;
}

static bool timestamp_valid(lab10_audio_model_t *model, uint64_t timestamp_us)
{
    if (model->has_timestamp && timestamp_us < model->last_timestamp_us) {
        ++model->metrics.audio_error_count;
        return false;
    }
    model->last_timestamp_us = timestamp_us;
    model->has_timestamp = true;
    return true;
}

static void update_audio_occupancy(lab10_audio_model_t *model)
{
    model->metrics.audio_buffer_occupancy_frames = model->pcm_depth;
    model->metrics.audio_buffer_occupancy_bytes =
        (size_t)model->pcm_depth * model->config.frame_bytes;
    if (model->pcm_depth > model->metrics.audio_buffer_high_water_frames) {
        model->metrics.audio_buffer_high_water_frames = model->pcm_depth;
    }
    const size_t high_water_bytes =
        (size_t)model->metrics.audio_buffer_high_water_frames *
        model->config.frame_bytes;
    if (high_water_bytes > model->metrics.audio_buffer_high_water_bytes) {
        model->metrics.audio_buffer_high_water_bytes = high_water_bytes;
    }
}

static void update_drawing_occupancy(lab10_audio_model_t *model)
{
    model->metrics.drawing_queue_depth = model->drawing_depth;
    if (model->drawing_depth > model->metrics.drawing_queue_high_water) {
        model->metrics.drawing_queue_high_water = model->drawing_depth;
    }
}

static void update_continuity(lab10_audio_model_t *model)
{
    const bool overrun = model->metrics.audio_overrun_count != 0;
    const bool underrun = model->metrics.audio_underrun_count != 0;
    if (overrun && underrun) {
        model->metrics.audio_continuity_status =
            LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN_AND_UNDERRUN;
    } else if (overrun) {
        model->metrics.audio_continuity_status =
            LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN;
    } else if (underrun) {
        model->metrics.audio_continuity_status =
            LAB10_AUDIO_CONTINUITY_MODEL_UNDERRUN;
    } else {
        model->metrics.audio_continuity_status =
            LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS;
    }
}

static int compare_latency(const void *left, const void *right)
{
    const uint64_t left_value = *(const uint64_t *)left;
    const uint64_t right_value = *(const uint64_t *)right;
    if (left_value < right_value) {
        return -1;
    }
    if (left_value > right_value) {
        return 1;
    }
    return 0;
}

static uint64_t percentile(const uint64_t *values, size_t count,
                           unsigned percentile_value)
{
    if (count == 0) {
        return 0;
    }
    uint64_t sorted[LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE];
    memcpy(sorted, values, count * sizeof(sorted[0]));
    qsort(sorted, count, sizeof(sorted[0]), compare_latency);
    const size_t rank = (count * percentile_value + 99u) / 100u;
    const size_t index = rank == 0 ? 0 : rank - 1;
    return sorted[index < count ? index : count - 1];
}

static void finish_metrics(lab10_audio_model_t *model)
{
    update_audio_occupancy(model);
    update_drawing_occupancy(model);
    model->metrics.drawing_latency_us_p50 =
        percentile(model->drawing_latencies, model->drawing_latency_count, 50);
    model->metrics.drawing_latency_us_p95 =
        percentile(model->drawing_latencies, model->drawing_latency_count, 95);
    model->metrics.drawing_latency_us_p99 =
        percentile(model->drawing_latencies, model->drawing_latency_count, 99);
    model->metrics.drawing_latency_us_max = 0;
    for (size_t index = 0; index < model->drawing_latency_count; ++index) {
        if (model->drawing_latencies[index] >
            model->metrics.drawing_latency_us_max) {
            model->metrics.drawing_latency_us_max =
                model->drawing_latencies[index];
        }
    }
    if (model->failure == LAB10_AUDIO_FAILURE_CAPTURE ||
        model->failure == LAB10_AUDIO_FAILURE_ALLOCATION) {
        model->metrics.audio_continuity_status = LAB10_AUDIO_CONTINUITY_UNMEASURED;
    } else {
        update_continuity(model);
    }
}

static bool terminal_timestamps_valid(const lab10_audio_model_t *model,
                                      uint64_t terminal_timestamp_us,
                                      uint64_t cleanup_timestamp_us)
{
    if ((model->has_timestamp &&
         terminal_timestamp_us < model->last_timestamp_us) ||
        cleanup_timestamp_us < terminal_timestamp_us) {
        return false;
    }
    return true;
}

static void record_terminal_timestamps(lab10_audio_model_t *model,
                                       uint64_t terminal_timestamp_us,
                                       uint64_t cleanup_timestamp_us)
{
    /* Zero is a valid first timestamp; no special sentinel is used here. */
    model->terminal_timestamp_us = terminal_timestamp_us;
    model->cleanup_timestamp_us = cleanup_timestamp_us;
    model->cleanup_timestamps_recorded = true;
    model->last_timestamp_us = cleanup_timestamp_us;
    model->has_timestamp = true;
    model->metrics.cleanup_time_us = cleanup_timestamp_us -
                                     terminal_timestamp_us;
}

static bool terminalize(lab10_audio_model_t *model,
                        lab10_audio_session_token_t session_token,
                        lab10_audio_outcome_t outcome,
                        lab10_audio_failure_t failure,
                        uint64_t terminal_timestamp_us,
                        uint64_t cleanup_timestamp_us)
{
    if (!session_token_valid(model, session_token)) {
        return false;
    }
    if (model->status != LAB10_AUDIO_STATUS_ACTIVE) {
        return false;
    }
    if (!terminal_timestamps_valid(model, terminal_timestamp_us,
                                   cleanup_timestamp_us)) {
        ++model->metrics.audio_error_count;
        return false;
    }
    model->outcome = outcome;
    model->failure = failure;
    model->status = LAB10_AUDIO_STATUS_TERMINAL;
    if (model->pcm != NULL) {
        model->config.free(model->pcm, model->config.allocator_context);
        model->pcm = NULL;
    }
    model->pcm_head = 0;
    model->pcm_tail = 0;
    model->pcm_depth = 0;
    model->drawing_head = 0;
    model->drawing_tail = 0;
    model->drawing_depth = 0;
    model->metrics.live_resources_after_terminal = 0;
    finish_metrics(model);
    model->metrics.audio_buffer_occupancy_frames = 0;
    model->metrics.audio_buffer_occupancy_bytes = 0;
    model->metrics.drawing_queue_depth = 0;
    record_terminal_timestamps(model, terminal_timestamp_us,
                               cleanup_timestamp_us);
    return true;
}

static bool expected_result(const lab10_audio_model_t *model)
{
    const lab10_audio_scenario_t *scenario =
        lab10_audio_scenario(model->scenario_id);
    if (scenario == NULL || model->status != LAB10_AUDIO_STATUS_TERMINAL) {
        return false;
    }
    switch (scenario->expected_outcome) {
    case LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS:
        return model->outcome == LAB10_AUDIO_OUTCOME_COMPLETED &&
               model->metrics.audio_continuity_status ==
                   scenario->expected_continuity &&
               model->metrics.audio_overrun_count == 0 &&
               model->metrics.audio_underrun_count == 0;
    case LAB10_AUDIO_EXPECT_COMPLETED_WITH_DRAWING_PRESSURE:
        return model->outcome == LAB10_AUDIO_OUTCOME_COMPLETED &&
               model->metrics.audio_continuity_status ==
                   scenario->expected_continuity &&
               model->metrics.drawing_queue_overflow_count > 0;
    case LAB10_AUDIO_EXPECT_STOPPED_AND_CLEAN:
        return model->outcome == LAB10_AUDIO_OUTCOME_STOPPED &&
               model->metrics.live_resources_after_terminal == 0;
    case LAB10_AUDIO_EXPECT_CANCELLED_AND_CLEAN:
        return model->outcome == LAB10_AUDIO_OUTCOME_CANCELLED &&
               model->metrics.live_resources_after_terminal == 0;
    case LAB10_AUDIO_EXPECT_CAPTURE_FAILED_AND_CLEAN:
        return model->outcome == LAB10_AUDIO_OUTCOME_FAILED &&
               model->failure == LAB10_AUDIO_FAILURE_CAPTURE &&
               model->metrics.live_resources_after_terminal == 0;
    case LAB10_AUDIO_EXPECT_ALLOCATION_FAILED_AND_CLEAN:
        return model->outcome == LAB10_AUDIO_OUTCOME_FAILED &&
               model->failure == LAB10_AUDIO_FAILURE_ALLOCATION &&
               model->metrics.live_resources_after_terminal == 0;
    }
    return false;
}

lab10_audio_config_t lab10_audio_default_config(void)
{
    return (lab10_audio_config_t){
        .frame_bytes = 256u,
        .samples_per_frame = 128u,
        .frame_duration_us = 8000u,
        .buffer_capacity_frames = 8u,
        .drawing_queue_capacity = 16u,
        .transient_pcm_bytes = 256u,
        .retained_sketch_bytes = 4096u,
        .transient_sketch_bytes = 256u,
        .retained_framebuffer_bytes = 28800u,
        .transient_framebuffer_bytes = 28800u,
        .alloc = NULL,
        .free = NULL,
        .allocator_context = NULL,
    };
}

static const lab10_audio_scenario_t SCENARIOS[LAB10_AUDIO_SCENARIO_COUNT] = {
    [LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL] = {
        "no-stroke-control", "audio with no canvas events",
        LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_SPARSE_DRAWING] = {
        "sparse-drawing", "few drawing events with balanced audio",
        LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_CONTINUOUS_DRAWING] = {
        "continuous-drawing", "one drawing event per audio tick",
        LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_DENSE_DRAWING] = {
        "dense-drawing", "drawing queue pressure while audio remains balanced",
        LAB10_AUDIO_EXPECT_COMPLETED_WITH_DRAWING_PRESSURE,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, true, false},
    [LAB10_AUDIO_SCENARIO_RAPID_COLOR_SWITCHING] = {
        "rapid-color-switching", "frequent color changes between drawing events",
        LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO] = {
        "stop-during-audio", "stop while the bounded audio model is active",
        LAB10_AUDIO_EXPECT_STOPPED_AND_CLEAN,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_CANCEL_DURING_AUDIO] = {
        "cancel-during-audio", "cancel while the bounded audio model is active",
        LAB10_AUDIO_EXPECT_CANCELLED_AND_CLEAN,
        LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS, false, false},
    [LAB10_AUDIO_SCENARIO_AUDIO_CAPTURE_FAILURE] = {
        "audio-capture-failure", "inject an audio capture failure",
        LAB10_AUDIO_EXPECT_CAPTURE_FAILED_AND_CLEAN,
        LAB10_AUDIO_CONTINUITY_UNMEASURED, false, false},
    [LAB10_AUDIO_SCENARIO_ALLOCATION_FAILURE] = {
        "allocation-failure", "fail bounded PCM allocation at start",
        LAB10_AUDIO_EXPECT_ALLOCATION_FAILED_AND_CLEAN,
        LAB10_AUDIO_CONTINUITY_UNMEASURED, false, false},
};

const lab10_audio_scenario_t *lab10_audio_scenario(
    lab10_audio_scenario_id_t scenario_id)
{
    return scenario_id_valid(scenario_id) ? &SCENARIOS[scenario_id] : NULL;
}

const char *lab10_audio_scenario_id_name(lab10_audio_scenario_id_t scenario_id)
{
    const lab10_audio_scenario_t *scenario = lab10_audio_scenario(scenario_id);
    return scenario == NULL ? "invalid" : scenario->id;
}

const char *lab10_audio_expected_outcome_name(
    lab10_audio_expected_outcome_t expected_outcome)
{
    switch (expected_outcome) {
    case LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS:
        return "completed-audio-continuous";
    case LAB10_AUDIO_EXPECT_COMPLETED_WITH_DRAWING_PRESSURE:
        return "completed-with-drawing-pressure";
    case LAB10_AUDIO_EXPECT_STOPPED_AND_CLEAN:
        return "stopped-and-clean";
    case LAB10_AUDIO_EXPECT_CANCELLED_AND_CLEAN:
        return "cancelled-and-clean";
    case LAB10_AUDIO_EXPECT_CAPTURE_FAILED_AND_CLEAN:
        return "capture-failed-and-clean";
    case LAB10_AUDIO_EXPECT_ALLOCATION_FAILED_AND_CLEAN:
        return "allocation-failed-and-clean";
    }
    return "invalid";
}

const char *lab10_audio_outcome_name(lab10_audio_outcome_t outcome)
{
    switch (outcome) {
    case LAB10_AUDIO_OUTCOME_NONE:
        return "none";
    case LAB10_AUDIO_OUTCOME_COMPLETED:
        return "completed";
    case LAB10_AUDIO_OUTCOME_STOPPED:
        return "stopped";
    case LAB10_AUDIO_OUTCOME_CANCELLED:
        return "cancelled";
    case LAB10_AUDIO_OUTCOME_FAILED:
        return "failed";
    }
    return "invalid";
}

const char *lab10_audio_failure_name(lab10_audio_failure_t failure)
{
    switch (failure) {
    case LAB10_AUDIO_FAILURE_NONE:
        return "none";
    case LAB10_AUDIO_FAILURE_CAPTURE:
        return "capture";
    case LAB10_AUDIO_FAILURE_ALLOCATION:
        return "allocation";
    case LAB10_AUDIO_FAILURE_INVALID_CONFIGURATION:
        return "invalid-configuration";
    }
    return "invalid";
}

const char *lab10_audio_continuity_name(lab10_audio_continuity_t continuity)
{
    switch (continuity) {
    case LAB10_AUDIO_CONTINUITY_MODEL_NONE:
        return "none";
    case LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS:
        return "model-continuous";
    case LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN:
        return "model-overrun";
    case LAB10_AUDIO_CONTINUITY_MODEL_UNDERRUN:
        return "model-underrun";
    case LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN_AND_UNDERRUN:
        return "model-overrun-and-underrun";
    case LAB10_AUDIO_CONTINUITY_UNMEASURED:
        return "unmeasured";
    }
    return "invalid";
}

lab10_audio_model_t *lab10_audio_create(const lab10_audio_config_t *config)
{
    lab10_audio_config_t effective =
        config == NULL ? lab10_audio_default_config() : *config;
    if (!config_valid(&effective)) {
        return NULL;
    }
    if (effective.alloc == NULL) {
        effective.alloc = default_alloc;
        effective.free = default_free;
    }
    lab10_audio_model_t *model =
        effective.alloc(sizeof(*model), effective.allocator_context);
    if (model == NULL) {
        return NULL;
    }
    memset(model, 0, sizeof(*model));
    atomic_flag_clear_explicit(&model->state_lock, memory_order_release);
    model->config = effective;
    model->scenario_id = LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL;
    model->status = LAB10_AUDIO_STATUS_UNINITIALIZED;
    reset_run_state(model);
    return model;
}

void lab10_audio_destroy(lab10_audio_model_t *model)
{
    if (model == NULL) {
        return;
    }
    /* The caller has already quiesced and joined every worker. */
    model_lock(model);
    reset_run_state(model);
    const lab10_audio_free_fn free_fn = model->config.free;
    void *const allocator_context = model->config.allocator_context;
    model_unlock(model);
    free_fn(model, allocator_context);
}

bool lab10_audio_start(lab10_audio_model_t *model,
                       lab10_audio_scenario_id_t scenario_id,
                       lab10_audio_session_token_t *session_token)
{
    if (session_token != NULL) {
        *session_token = LAB10_AUDIO_INVALID_SESSION_TOKEN;
    }
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool started = false;
    if (session_token == NULL || !scenario_id_valid(scenario_id) ||
        model->status == LAB10_AUDIO_STATUS_ACTIVE) {
        goto done;
    }
    if (model->last_issued_session_token == LAB10_AUDIO_SESSION_TOKEN_MAX) {
        goto done;
    }
    const lab10_audio_session_token_t new_session_token =
        model->last_issued_session_token + UINT64_C(1);
    reset_run_state(model);
    model->session_token = new_session_token;
    model->last_issued_session_token = new_session_token;
    *session_token = new_session_token;
    model->scenario_id = scenario_id;
    model->status = LAB10_AUDIO_STATUS_ACTIVE;
    ++model->metrics.allocation_attempt_count;
    const size_t pcm_bytes =
        model->config.frame_bytes * model->config.buffer_capacity_frames;
    model->metrics.largest_allocation_bytes = pcm_bytes;
    model->pcm = model->config.alloc(pcm_bytes, model->config.allocator_context);
    if (model->pcm == NULL) {
        ++model->metrics.allocation_failure_count;
        model->failure = LAB10_AUDIO_FAILURE_ALLOCATION;
        model->outcome = LAB10_AUDIO_OUTCOME_FAILED;
        model->status = LAB10_AUDIO_STATUS_TERMINAL;
        model->metrics.audio_continuity_status = LAB10_AUDIO_CONTINUITY_UNMEASURED;
        /* Allocation fails before the deterministic terminal schedule starts. */
        model->metrics.cleanup_time_us = 0;
        model->metrics.live_resources_after_terminal = 0;
        started = true;
        goto done;
    }
    model->metrics.retained_pcm_bytes = pcm_bytes;
    update_audio_occupancy(model);
    update_drawing_occupancy(model);
    started = true;

done:
    model_unlock(model);
    return started;
}

bool lab10_audio_produce_frame(lab10_audio_model_t *model,
                               lab10_audio_session_token_t session_token,
                               uint64_t timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool produced = false;
    if (!session_token_valid(model, session_token) ||
        model->status != LAB10_AUDIO_STATUS_ACTIVE ||
        !timestamp_valid(model, timestamp_us)) {
        goto done;
    }
    if (model->pcm_depth == model->config.buffer_capacity_frames) {
        ++model->metrics.audio_overrun_count;
        update_continuity(model);
        goto done;
    }
    model->pcm_head = (uint16_t)((model->pcm_head + 1u) %
                                 model->config.buffer_capacity_frames);
    ++model->pcm_depth;
    ++model->metrics.audio_frame_count_produced;
    model->metrics.audio_sample_count_produced += model->config.samples_per_frame;
    update_audio_occupancy(model);
    produced = true;

done:
    model_unlock(model);
    return produced;
}

bool lab10_audio_consume_frame(lab10_audio_model_t *model,
                               lab10_audio_session_token_t session_token,
                               uint64_t timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool consumed = false;
    if (!session_token_valid(model, session_token) ||
        model->status != LAB10_AUDIO_STATUS_ACTIVE ||
        !timestamp_valid(model, timestamp_us)) {
        goto done;
    }
    if (model->pcm_depth == 0) {
        ++model->metrics.audio_underrun_count;
        model->metrics.audio_underrun_duration_us +=
            model->config.frame_duration_us;
        update_continuity(model);
        goto done;
    }
    model->pcm_tail = (uint16_t)((model->pcm_tail + 1u) %
                                 model->config.buffer_capacity_frames);
    --model->pcm_depth;
    ++model->metrics.audio_frame_count_consumed;
    model->metrics.audio_sample_count_consumed += model->config.samples_per_frame;
    update_audio_occupancy(model);
    consumed = true;

done:
    model_unlock(model);
    return consumed;
}

bool lab10_audio_submit_drawing(lab10_audio_model_t *model,
                                lab10_audio_session_token_t session_token,
                                uint64_t timestamp_us, uint32_t stroke_id,
                                uint8_t color_index)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool submitted = false;
    if (!session_token_valid(model, session_token) ||
        model->status != LAB10_AUDIO_STATUS_ACTIVE ||
        !timestamp_valid(model, timestamp_us)) {
        goto done;
    }
    if (model->drawing_depth == model->config.drawing_queue_capacity) {
        ++model->metrics.drawing_queue_overflow_count;
        goto done;
    }
    model->drawing_queue[model->drawing_head] = (lab10_audio_drawing_event_t){
        .queued_at_us = timestamp_us,
        .stroke_id = stroke_id,
        .color_index = color_index,
    };
    model->drawing_head = (uint16_t)((model->drawing_head + 1u) %
                                     model->config.drawing_queue_capacity);
    ++model->drawing_depth;
    ++model->metrics.drawing_event_count;
    if (model->has_previous_color && model->previous_color != color_index) {
        ++model->metrics.drawing_color_change_count;
    }
    model->previous_color = color_index;
    model->has_previous_color = true;
    if (model->has_previous_stroke && model->previous_stroke != stroke_id) {
        ++model->metrics.drawing_stroke_count;
    } else if (!model->has_previous_stroke) {
        ++model->metrics.drawing_stroke_count;
    }
    model->previous_stroke = stroke_id;
    model->has_previous_stroke = true;
    update_drawing_occupancy(model);
    submitted = true;

done:
    model_unlock(model);
    return submitted;
}

size_t lab10_audio_process_drawing(lab10_audio_model_t *model,
                                   lab10_audio_session_token_t session_token,
                                   uint64_t timestamp_us, size_t max_events)
{
    if (model == NULL) {
        return 0;
    }
    model_lock(model);
    size_t processed = 0;
    if (!session_token_valid(model, session_token) ||
        model->status != LAB10_AUDIO_STATUS_ACTIVE ||
        !timestamp_valid(model, timestamp_us)) {
        goto done;
    }
    while (model->drawing_depth != 0 && processed < max_events) {
        const lab10_audio_drawing_event_t event =
            model->drawing_queue[model->drawing_tail];
        model->drawing_tail = (uint16_t)((model->drawing_tail + 1u) %
                                          model->config.drawing_queue_capacity);
        --model->drawing_depth;
        uint64_t latency = 0;
        if (timestamp_us >= event.queued_at_us) {
            latency = timestamp_us - event.queued_at_us;
        } else {
            ++model->metrics.audio_error_count;
        }
        if (model->drawing_latency_count <
            LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE) {
            model->drawing_latencies[model->drawing_latency_count++] = latency;
        }
        ++processed;
    }
    update_drawing_occupancy(model);

done:
    model_unlock(model);
    return processed;
}

bool lab10_audio_stop(lab10_audio_model_t *model,
                      lab10_audio_session_token_t session_token,
                      uint64_t terminal_timestamp_us,
                      uint64_t cleanup_timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    const bool stopped =
        terminalize(model, session_token, LAB10_AUDIO_OUTCOME_STOPPED,
                    LAB10_AUDIO_FAILURE_NONE, terminal_timestamp_us,
                    cleanup_timestamp_us);
    model_unlock(model);
    return stopped;
}

bool lab10_audio_cancel(lab10_audio_model_t *model,
                        lab10_audio_session_token_t session_token,
                        uint64_t terminal_timestamp_us,
                        uint64_t cleanup_timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    const bool cancelled =
        terminalize(model, session_token, LAB10_AUDIO_OUTCOME_CANCELLED,
                    LAB10_AUDIO_FAILURE_NONE, terminal_timestamp_us,
                    cleanup_timestamp_us);
    model_unlock(model);
    return cancelled;
}

bool lab10_audio_fail_capture(lab10_audio_model_t *model,
                              lab10_audio_session_token_t session_token,
                              uint64_t terminal_timestamp_us,
                              uint64_t cleanup_timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool failed = false;
    if (!session_token_valid(model, session_token) ||
        model->status != LAB10_AUDIO_STATUS_ACTIVE) {
        goto done;
    }
    ++model->metrics.audio_error_count;
    failed = terminalize(model, session_token, LAB10_AUDIO_OUTCOME_FAILED,
                         LAB10_AUDIO_FAILURE_CAPTURE, terminal_timestamp_us,
                         cleanup_timestamp_us);

done:
    model_unlock(model);
    return failed;
}

bool lab10_audio_complete(lab10_audio_model_t *model,
                          lab10_audio_session_token_t session_token,
                          uint64_t terminal_timestamp_us,
                          uint64_t cleanup_timestamp_us)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    const bool completed =
        terminalize(model, session_token, LAB10_AUDIO_OUTCOME_COMPLETED,
                    LAB10_AUDIO_FAILURE_NONE, terminal_timestamp_us,
                    cleanup_timestamp_us);
    model_unlock(model);
    return completed;
}

bool lab10_audio_get_metrics(lab10_audio_model_t *model,
                             lab10_audio_session_token_t session_token,
                             lab10_audio_metrics_t *metrics)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool read = false;
    if (!session_token_valid(model, session_token) || metrics == NULL ||
        model->status == LAB10_AUDIO_STATUS_UNINITIALIZED) {
        goto done;
    }
    *metrics = model->metrics;
    read = true;

done:
    model_unlock(model);
    return read;
}

bool lab10_audio_get_result(lab10_audio_model_t *model,
                            lab10_audio_session_token_t session_token,
                            lab10_audio_result_t *result)
{
    if (model == NULL) {
        return false;
    }
    model_lock(model);
    bool read = false;
    if (!session_token_valid(model, session_token) || result == NULL ||
        model->status == LAB10_AUDIO_STATUS_UNINITIALIZED) {
        goto done;
    }
    const lab10_audio_scenario_t *scenario =
        lab10_audio_scenario(model->scenario_id);
    *result = (lab10_audio_result_t){
        .scenario_id = model->scenario_id,
        .session_token = model->session_token,
        .scenario_identity = lab10_audio_scenario_id_name(model->scenario_id),
        .expected_outcome = scenario == NULL
                                ? LAB10_AUDIO_EXPECT_ALLOCATION_FAILED_AND_CLEAN
                                : scenario->expected_outcome,
        .observed_outcome = model->outcome,
        .failure = model->failure,
        .provenance = model->metrics.provenance,
        .missing_reason = model->metrics.missing_reason,
        .expectation_met = expected_result(model),
        .resources_released = model->metrics.live_resources_after_terminal == 0 &&
                              model->pcm == NULL && model->drawing_depth == 0,
        .hardware_audio_health_measured = false,
        .cleanup_timestamps_recorded = model->cleanup_timestamps_recorded,
        .terminal_timestamp_us = model->terminal_timestamp_us,
        .cleanup_timestamp_us = model->cleanup_timestamp_us,
        .metrics = model->metrics,
    };
    read = true;

done:
    model_unlock(model);
    return read;
}

static void run_audio_tick(lab10_audio_model_t *model,
                           lab10_audio_session_token_t session_token,
                           uint64_t *timestamp_us)
{
    *timestamp_us += model->config.frame_duration_us;
    (void)lab10_audio_produce_frame(model, session_token, *timestamp_us);
    (void)lab10_audio_consume_frame(model, session_token, *timestamp_us);
}

bool lab10_audio_run_scenario(lab10_audio_model_t *model,
                              lab10_audio_scenario_id_t scenario_id,
                              lab10_audio_result_t *result)
{
    lab10_audio_session_token_t session_token =
        LAB10_AUDIO_INVALID_SESSION_TOKEN;
    if (model == NULL || result == NULL || !scenario_id_valid(scenario_id) ||
        !lab10_audio_start(model, scenario_id, &session_token)) {
        return false;
    }
    uint64_t timestamp_us = 0;
    switch (scenario_id) {
    case LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL:
        for (unsigned index = 0; index < 12u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
        }
        (void)lab10_audio_complete(model, session_token, timestamp_us + 1u,
                                    timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_SPARSE_DRAWING:
        for (unsigned index = 0; index < 12u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            if (index == 1u || index == 6u || index == 10u) {
                (void)lab10_audio_submit_drawing(model, session_token,
                                                 timestamp_us, index + 1u, 1u);
                (void)lab10_audio_process_drawing(model, session_token,
                                                  timestamp_us + 1u, 1u);
            }
        }
        (void)lab10_audio_complete(model, session_token, timestamp_us + 1u,
                                    timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_CONTINUOUS_DRAWING:
        for (unsigned index = 0; index < 24u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            (void)lab10_audio_submit_drawing(model, session_token, timestamp_us,
                                             1u, 1u);
            (void)lab10_audio_process_drawing(model, session_token,
                                              timestamp_us + 1u, 1u);
        }
        (void)lab10_audio_complete(model, session_token, timestamp_us + 1u,
                                    timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_DENSE_DRAWING:
        for (unsigned index = 0; index < 32u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            for (unsigned event = 0; event < 3u; ++event) {
                (void)lab10_audio_submit_drawing(model, session_token,
                                                 timestamp_us,
                                                 index + event + 1u,
                                                 (uint8_t)(event + 1u));
            }
            if (index % 4u == 0u) {
                (void)lab10_audio_process_drawing(model, session_token,
                                                  timestamp_us + 2u, 1u);
            }
        }
        while (lab10_audio_process_drawing(model, session_token,
                                           timestamp_us + 3u, 8u) != 0) {
        }
        (void)lab10_audio_complete(model, session_token, timestamp_us + 4u,
                                    timestamp_us + 5u);
        break;
    case LAB10_AUDIO_SCENARIO_RAPID_COLOR_SWITCHING:
        for (unsigned index = 0; index < 18u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            (void)lab10_audio_submit_drawing(
                model, session_token, timestamp_us, index + 1u,
                (uint8_t)((index % 5u) + 1u));
            (void)lab10_audio_process_drawing(model, session_token,
                                              timestamp_us + 1u, 1u);
        }
        (void)lab10_audio_complete(model, session_token, timestamp_us + 1u,
                                    timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO:
        for (unsigned index = 0; index < 4u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            (void)lab10_audio_submit_drawing(model, session_token, timestamp_us,
                                             index + 1u, 1u);
        }
        (void)lab10_audio_stop(model, session_token, timestamp_us + 1u,
                                timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_CANCEL_DURING_AUDIO:
        for (unsigned index = 0; index < 4u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
            (void)lab10_audio_submit_drawing(model, session_token, timestamp_us,
                                             index + 1u, 1u);
        }
        (void)lab10_audio_cancel(model, session_token, timestamp_us + 1u,
                                  timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_AUDIO_CAPTURE_FAILURE:
        for (unsigned index = 0; index < 3u; ++index) {
            run_audio_tick(model, session_token, &timestamp_us);
        }
        (void)lab10_audio_fail_capture(model, session_token, timestamp_us + 1u,
                                       timestamp_us + 2u);
        break;
    case LAB10_AUDIO_SCENARIO_ALLOCATION_FAILURE:
        /* start() terminalizes this scenario when its allocator rejects PCM. */
        break;
    case LAB10_AUDIO_SCENARIO_COUNT:
        return false;
    }
    return lab10_audio_get_result(model, session_token, result);
}
