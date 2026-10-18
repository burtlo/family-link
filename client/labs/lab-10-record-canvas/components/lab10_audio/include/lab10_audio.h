#pragma once

/*
 * LAB-10 L10-05 bounded audio-concurrency experiment.
 *
 * This component is a deterministic host model. It owns a bounded PCM frame
 * ring and a separate bounded drawing-event queue; it does not call the
 * L10-03 capture state machine or the L10-04 renderer. Firmware audio/DAC
 * integration is deliberately absent until an audio-device contract and
 * synchronized hardware evidence exist.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB10_AUDIO_MODEL_MAX_FRAME_BYTES 4096u
#define LAB10_AUDIO_MODEL_MAX_BUFFER_FRAMES 64u
#define LAB10_AUDIO_MODEL_MAX_DRAWING_QUEUE 64u
#define LAB10_AUDIO_MODEL_MAX_RESOURCE_BYTES (1024u * 1024u)

/*
 * A session token identifies one host-model restart. Zero is invalid and a
 * model consumes each token once; when the representable range is exhausted,
 * start fails closed instead of reusing a token held by an old worker.
 */
typedef uint64_t lab10_audio_session_token_t;

#define LAB10_AUDIO_INVALID_SESSION_TOKEN UINT64_C(0)
#define LAB10_AUDIO_SESSION_TOKEN_MAX UINT64_MAX

typedef struct lab10_audio_model lab10_audio_model_t;

typedef enum {
    LAB10_AUDIO_SCENARIO_NO_STROKE_CONTROL = 0,
    LAB10_AUDIO_SCENARIO_SPARSE_DRAWING,
    LAB10_AUDIO_SCENARIO_CONTINUOUS_DRAWING,
    LAB10_AUDIO_SCENARIO_DENSE_DRAWING,
    LAB10_AUDIO_SCENARIO_RAPID_COLOR_SWITCHING,
    LAB10_AUDIO_SCENARIO_STOP_DURING_AUDIO,
    LAB10_AUDIO_SCENARIO_CANCEL_DURING_AUDIO,
    LAB10_AUDIO_SCENARIO_AUDIO_CAPTURE_FAILURE,
    LAB10_AUDIO_SCENARIO_ALLOCATION_FAILURE,
    LAB10_AUDIO_SCENARIO_COUNT,
} lab10_audio_scenario_id_t;

typedef enum {
    LAB10_AUDIO_EXPECT_COMPLETED_AUDIO_CONTINUOUS = 0,
    LAB10_AUDIO_EXPECT_COMPLETED_WITH_DRAWING_PRESSURE,
    LAB10_AUDIO_EXPECT_STOPPED_AND_CLEAN,
    LAB10_AUDIO_EXPECT_CANCELLED_AND_CLEAN,
    LAB10_AUDIO_EXPECT_CAPTURE_FAILED_AND_CLEAN,
    LAB10_AUDIO_EXPECT_ALLOCATION_FAILED_AND_CLEAN,
} lab10_audio_expected_outcome_t;

typedef enum {
    LAB10_AUDIO_OUTCOME_NONE = 0,
    LAB10_AUDIO_OUTCOME_COMPLETED,
    LAB10_AUDIO_OUTCOME_STOPPED,
    LAB10_AUDIO_OUTCOME_CANCELLED,
    LAB10_AUDIO_OUTCOME_FAILED,
} lab10_audio_outcome_t;

typedef enum {
    LAB10_AUDIO_FAILURE_NONE = 0,
    LAB10_AUDIO_FAILURE_CAPTURE,
    LAB10_AUDIO_FAILURE_ALLOCATION,
    LAB10_AUDIO_FAILURE_INVALID_CONFIGURATION,
} lab10_audio_failure_t;

typedef enum {
    LAB10_AUDIO_CONTINUITY_MODEL_NONE = 0,
    LAB10_AUDIO_CONTINUITY_MODEL_CONTINUOUS,
    LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN,
    LAB10_AUDIO_CONTINUITY_MODEL_UNDERRUN,
    LAB10_AUDIO_CONTINUITY_MODEL_OVERRUN_AND_UNDERRUN,
    LAB10_AUDIO_CONTINUITY_UNMEASURED,
} lab10_audio_continuity_t;

typedef enum {
    LAB10_AUDIO_PROVENANCE_HOST = 0,
    LAB10_AUDIO_PROVENANCE_HARDWARE,
    LAB10_AUDIO_PROVENANCE_DERIVED_STATIC,
    LAB10_AUDIO_PROVENANCE_UNMEASURED_BLOCKED,
} lab10_audio_provenance_t;

typedef enum {
    LAB10_AUDIO_STATUS_UNINITIALIZED = 0,
    LAB10_AUDIO_STATUS_ACTIVE,
    LAB10_AUDIO_STATUS_TERMINAL,
} lab10_audio_status_t;

typedef void *(*lab10_audio_alloc_fn)(size_t size, void *context);
typedef void (*lab10_audio_free_fn)(void *memory, void *context);

typedef struct {
    /* These are experiment-model inputs, not hardware claims. */
    size_t frame_bytes;
    uint32_t samples_per_frame;
    uint64_t frame_duration_us;
    uint16_t buffer_capacity_frames;
    uint16_t drawing_queue_capacity;
    size_t transient_pcm_bytes;
    size_t retained_sketch_bytes;
    size_t transient_sketch_bytes;
    size_t retained_framebuffer_bytes;
    size_t transient_framebuffer_bytes;
    lab10_audio_alloc_fn alloc;
    lab10_audio_free_fn free;
    void *allocator_context;
} lab10_audio_config_t;

typedef struct {
    const char *id;
    const char *description;
    lab10_audio_expected_outcome_t expected_outcome;
    lab10_audio_continuity_t expected_continuity;
    bool expected_drawing_pressure;
    bool expected_hardware_audio_evidence;
} lab10_audio_scenario_t;

typedef struct {
    lab10_audio_provenance_t provenance;
    const char *missing_reason;
    lab10_audio_continuity_t audio_continuity_status;
    uint32_t audio_buffer_occupancy_frames;
    size_t audio_buffer_occupancy_bytes;
    uint32_t audio_buffer_high_water_frames;
    size_t audio_buffer_high_water_bytes;
    uint64_t audio_overrun_count;
    uint64_t audio_underrun_count;
    uint64_t audio_underrun_duration_us;
    uint64_t audio_frame_count_produced;
    uint64_t audio_frame_count_consumed;
    uint64_t audio_sample_count_produced;
    uint64_t audio_sample_count_consumed;
    uint64_t audio_error_count;
    uint64_t drawing_event_count;
    uint32_t drawing_queue_depth;
    uint32_t drawing_queue_high_water;
    uint64_t drawing_queue_overflow_count;
    uint64_t drawing_color_change_count;
    uint64_t drawing_stroke_count;
    uint64_t drawing_latency_us_p50;
    uint64_t drawing_latency_us_p95;
    uint64_t drawing_latency_us_p99;
    uint64_t drawing_latency_us_max;
    size_t retained_pcm_bytes;
    size_t transient_pcm_bytes;
    size_t retained_sketch_bytes;
    size_t transient_sketch_bytes;
    size_t retained_framebuffer_bytes;
    size_t transient_framebuffer_bytes;
    size_t largest_allocation_bytes;
    uint32_t allocation_attempt_count;
    uint32_t allocation_failure_count;
    uint64_t cleanup_time_us;
    size_t live_resources_after_terminal;
    /* Rejected calls carrying a token from an older or unknown session. */
    uint32_t stale_worker_mutation_count;
} lab10_audio_metrics_t;

typedef struct {
    lab10_audio_scenario_id_t scenario_id;
    lab10_audio_session_token_t session_token;
    const char *scenario_identity;
    lab10_audio_expected_outcome_t expected_outcome;
    lab10_audio_outcome_t observed_outcome;
    lab10_audio_failure_t failure;
    lab10_audio_provenance_t provenance;
    const char *missing_reason;
    bool expectation_met;
    bool resources_released;
    bool hardware_audio_health_measured;
    bool cleanup_timestamps_recorded;
    uint64_t terminal_timestamp_us;
    uint64_t cleanup_timestamp_us;
    lab10_audio_metrics_t metrics;
} lab10_audio_result_t;

lab10_audio_config_t lab10_audio_default_config(void);
const lab10_audio_scenario_t *lab10_audio_scenario(
    lab10_audio_scenario_id_t scenario_id);
const char *lab10_audio_scenario_id_name(lab10_audio_scenario_id_t scenario_id);
const char *lab10_audio_expected_outcome_name(
    lab10_audio_expected_outcome_t expected_outcome);
const char *lab10_audio_outcome_name(lab10_audio_outcome_t outcome);
const char *lab10_audio_failure_name(lab10_audio_failure_t failure);
const char *lab10_audio_continuity_name(lab10_audio_continuity_t continuity);

lab10_audio_model_t *lab10_audio_create(const lab10_audio_config_t *config);
/*
 * Lifetime rule: destroy must not race with any other model operation. The
 * caller owns the model lifetime and must stop and join every worker before
 * calling destroy; destroy does not wait for or cancel callers.
 */
void lab10_audio_destroy(lab10_audio_model_t *model);

/* The returned token is required by every operation for this session. */
bool lab10_audio_start(lab10_audio_model_t *model,
                       lab10_audio_scenario_id_t scenario_id,
                       lab10_audio_session_token_t *session_token);
bool lab10_audio_produce_frame(lab10_audio_model_t *model,
                               lab10_audio_session_token_t session_token,
                               uint64_t timestamp_us);
bool lab10_audio_consume_frame(lab10_audio_model_t *model,
                               lab10_audio_session_token_t session_token,
                               uint64_t timestamp_us);
bool lab10_audio_submit_drawing(lab10_audio_model_t *model,
                                lab10_audio_session_token_t session_token,
                                uint64_t timestamp_us, uint32_t stroke_id,
                                uint8_t color_index);
size_t lab10_audio_process_drawing(lab10_audio_model_t *model,
                                   lab10_audio_session_token_t session_token,
                                   uint64_t timestamp_us, size_t max_events);
/* Terminal and cleanup timestamps come from one monotonic host schedule. */
bool lab10_audio_stop(lab10_audio_model_t *model,
                      lab10_audio_session_token_t session_token,
                      uint64_t terminal_timestamp_us,
                      uint64_t cleanup_timestamp_us);
bool lab10_audio_cancel(lab10_audio_model_t *model,
                        lab10_audio_session_token_t session_token,
                        uint64_t terminal_timestamp_us,
                        uint64_t cleanup_timestamp_us);
bool lab10_audio_fail_capture(lab10_audio_model_t *model,
                              lab10_audio_session_token_t session_token,
                              uint64_t terminal_timestamp_us,
                              uint64_t cleanup_timestamp_us);
bool lab10_audio_complete(lab10_audio_model_t *model,
                          lab10_audio_session_token_t session_token,
                          uint64_t terminal_timestamp_us,
                          uint64_t cleanup_timestamp_us);

bool lab10_audio_get_metrics(lab10_audio_model_t *model,
                             lab10_audio_session_token_t session_token,
                             lab10_audio_metrics_t *metrics);
bool lab10_audio_get_result(lab10_audio_model_t *model,
                            lab10_audio_session_token_t session_token,
                            lab10_audio_result_t *result);

/* Run one deterministic host schedule; no wall clock or hardware is used. */
bool lab10_audio_run_scenario(lab10_audio_model_t *model,
                              lab10_audio_scenario_id_t scenario_id,
                              lab10_audio_result_t *result);
