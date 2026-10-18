#pragma once

/*
 * LAB-10 L10-06 deterministic, human-like workload suite.
 *
 * This component is a bounded host/workload representation.  It does not
 * synthesize hardware touch latency, display scanout, audio continuity, or
 * task scheduling.  Raw and display coordinates are both retained even
 * though the deterministic source has no hardware coordinate transform.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB10_WORKLOAD_SCHEMA_VERSION "lab10-workload/v1"
#define LAB10_WORKLOAD_GENERATOR_VERSION "synthetic-human-like-v1"
#define LAB10_WORKLOAD_EVIDENCE_PROVENANCE "host"
#define LAB10_WORKLOAD_TIMESTAMP_JITTER_BOUND_US UINT32_C(2000)

#define LAB10_WORKLOAD_MAX_STROKES 8u
#define LAB10_WORKLOAD_MAX_EVENTS 512u
#define LAB10_WORKLOAD_MAX_POINTS 320u
#define LAB10_WORKLOAD_ID_MAX 48u
#define LAB10_WORKLOAD_VERSION_MAX 32u
#define LAB10_WORKLOAD_PROVENANCE_MAX 24u

typedef enum {
    LAB10_WORKLOAD_SPARSE_MULTICOLOR = 0,
    LAB10_WORKLOAD_CONTINUOUS_MULTICOLOR,
    LAB10_WORKLOAD_DENSE_MULTICOLOR,
    LAB10_WORKLOAD_EDGE_CORNER,
    LAB10_WORKLOAD_RAPID_COLOR_SWITCH,
    LAB10_WORKLOAD_BACKGROUND_ERASE,
    LAB10_WORKLOAD_STOP_DURING_STROKE,
    LAB10_WORKLOAD_CANCEL_DURING_AUDIO,
    LAB10_WORKLOAD_ID_COUNT,
} lab10_workload_id_t;

typedef enum {
    LAB10_WORKLOAD_PHASE_PALETTE_CHANGE = 0,
    LAB10_WORKLOAD_PHASE_DOWN,
    LAB10_WORKLOAD_PHASE_MOVE,
    LAB10_WORKLOAD_PHASE_UP,
    LAB10_WORKLOAD_PHASE_LEAVE,
    LAB10_WORKLOAD_PHASE_TERMINAL,
} lab10_workload_phase_t;

typedef enum {
    LAB10_WORKLOAD_BOUNDARY_NONE = 0,
    LAB10_WORKLOAD_BOUNDARY_STROKE_START,
    LAB10_WORKLOAD_BOUNDARY_STROKE_CONTINUE,
    LAB10_WORKLOAD_BOUNDARY_STROKE_END,
    LAB10_WORKLOAD_BOUNDARY_PEN_UP_GAP,
    LAB10_WORKLOAD_BOUNDARY_COLOR_CHANGE,
    LAB10_WORKLOAD_BOUNDARY_TERMINAL,
} lab10_workload_boundary_t;

typedef enum {
    LAB10_WORKLOAD_TERMINAL_COMPLETED = 0,
    LAB10_WORKLOAD_TERMINAL_STOPPED,
    LAB10_WORKLOAD_TERMINAL_CANCELLED,
} lab10_workload_terminal_t;

typedef enum {
    LAB10_WORKLOAD_PROVENANCE_HOST = 0,
    LAB10_WORKLOAD_PROVENANCE_HARDWARE,
    LAB10_WORKLOAD_PROVENANCE_DERIVED_STATIC,
    LAB10_WORKLOAD_PROVENANCE_UNMEASURED_BLOCKED,
} lab10_workload_provenance_t;

enum {
    LAB10_WORKLOAD_FEATURE_VARIABLE_SPACING = 1u << 0,
    LAB10_WORKLOAD_FEATURE_VARIABLE_VELOCITY = 1u << 1,
    LAB10_WORKLOAD_FEATURE_CORNERS = 1u << 2,
    LAB10_WORKLOAD_FEATURE_PAUSES = 1u << 3,
    LAB10_WORKLOAD_FEATURE_PEN_UP_GAPS = 1u << 4,
    LAB10_WORKLOAD_FEATURE_MULTIPLE_STROKES = 1u << 5,
    LAB10_WORKLOAD_FEATURE_COLOR_CHANGES = 1u << 6,
    LAB10_WORKLOAD_FEATURE_EDGE_POINTS = 1u << 7,
    LAB10_WORKLOAD_FEATURE_LEAVE_REENTER = 1u << 8,
    LAB10_WORKLOAD_FEATURE_BACKGROUND_ERASE = 1u << 9,
    LAB10_WORKLOAD_FEATURE_STOP = 1u << 10,
    LAB10_WORKLOAD_FEATURE_CANCEL = 1u << 11,
};

#define LAB10_WORKLOAD_REQUIRED_HUMAN_FEATURES                         \
    (LAB10_WORKLOAD_FEATURE_VARIABLE_SPACING |                         \
     LAB10_WORKLOAD_FEATURE_VARIABLE_VELOCITY |                        \
     LAB10_WORKLOAD_FEATURE_CORNERS |                                  \
     LAB10_WORKLOAD_FEATURE_PAUSES |                                   \
     LAB10_WORKLOAD_FEATURE_PEN_UP_GAPS |                              \
     LAB10_WORKLOAD_FEATURE_MULTIPLE_STROKES |                         \
     LAB10_WORKLOAD_FEATURE_COLOR_CHANGES |                             \
     LAB10_WORKLOAD_FEATURE_EDGE_POINTS |                              \
     LAB10_WORKLOAD_FEATURE_LEAVE_REENTER |                            \
     LAB10_WORKLOAD_FEATURE_BACKGROUND_ERASE)

typedef enum {
    LAB10_WORKLOAD_OK = 0,
    LAB10_WORKLOAD_INVALID_ARGUMENT,
    LAB10_WORKLOAD_INVALID_ID,
    LAB10_WORKLOAD_INVALID_SEED,
    LAB10_WORKLOAD_INVALID_EVENT_CAPACITY,
    LAB10_WORKLOAD_INVALID_POINT_CAPACITY,
    LAB10_WORKLOAD_INVALID_DURATION_CAPACITY,
    LAB10_WORKLOAD_INVALID_JITTER_BOUND,
    LAB10_WORKLOAD_OUTPUT_CAPACITY,
} lab10_workload_result_t;

typedef struct {
    lab10_workload_id_t workload_id;
    uint64_t seed;
    uint32_t event_capacity;
    uint32_t point_capacity;
    uint64_t duration_capacity_us;
    uint32_t timestamp_jitter_bound_us;
} lab10_workload_config_t;

typedef struct {
    uint32_t event_index;
    uint64_t timestamp_us;
    uint64_t ideal_timestamp_us;
    int32_t timestamp_jitter_us;
    int16_t raw_x;
    int16_t raw_y;
    int16_t display_x;
    int16_t display_y;
    int16_t logical_x;
    int16_t logical_y;
    uint32_t stroke_id;
    uint32_t color_rgb;
    uint8_t color_index;
    uint8_t touch_id;
    bool logical_valid;
    bool accepted;
    uint32_t pause_before_us;
    uint32_t pen_up_gap_before_us;
    lab10_workload_phase_t phase;
    lab10_workload_boundary_t boundary;
} lab10_workload_event_t;

typedef struct {
    char workload_identity[LAB10_WORKLOAD_ID_MAX];
    char workload_schema_version[LAB10_WORKLOAD_VERSION_MAX];
    char generator_version[LAB10_WORKLOAD_VERSION_MAX];
    char evidence_provenance[LAB10_WORKLOAD_PROVENANCE_MAX];
    uint64_t seed;
    uint32_t timestamp_jitter_bound_us;
    uint32_t actual_max_abs_jitter_us;
    uint32_t feature_flags;
    uint32_t event_count;
    uint32_t accepted_point_count;
    uint32_t stroke_count;
    uint32_t color_change_count;
    uint64_t duration_us;
    lab10_workload_terminal_t terminal_outcome;
    lab10_workload_event_t events[LAB10_WORKLOAD_MAX_EVENTS];
} lab10_workload_trace_t;

lab10_workload_config_t lab10_workload_default_config(
    lab10_workload_id_t workload_id, uint64_t seed);

const char *lab10_workload_id_name(lab10_workload_id_t workload_id);
const char *lab10_workload_phase_name(lab10_workload_phase_t phase);
const char *lab10_workload_boundary_name(lab10_workload_boundary_t boundary);
const char *lab10_workload_terminal_name(lab10_workload_terminal_t terminal);
const char *lab10_workload_result_name(lab10_workload_result_t result);

/* Generate a complete bounded trace without allocating or reading a clock. */
lab10_workload_result_t lab10_workload_generate(
    const lab10_workload_config_t *config, lab10_workload_trace_t *trace);

/* The encoded trace size is bounded by the header plus populated events. */
size_t lab10_workload_trace_size_bytes(const lab10_workload_trace_t *trace);

