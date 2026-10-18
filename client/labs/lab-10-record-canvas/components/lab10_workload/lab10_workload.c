#include "lab10_workload.h"

#include <string.h>

typedef struct {
    uint16_t point_count;
    uint8_t stroke_count;
    uint32_t interval_base_us;
    uint32_t interval_variation_us;
    uint32_t pause_us;
    lab10_workload_terminal_t terminal;
} workload_profile_t;

typedef struct {
    int16_t x;
    int16_t y;
} coordinate_t;

static const char *const WORKLOAD_NAMES[LAB10_WORKLOAD_ID_COUNT] = {
    "synthetic-sparse-multicolor-v1",
    "synthetic-continuous-multicolor-v1",
    "synthetic-dense-multicolor-v1",
    "synthetic-edge-corner-v1",
    "synthetic-rapid-color-switch-v1",
    "synthetic-background-erase-v1",
    "synthetic-stop-during-stroke-v1",
    "synthetic-cancel-during-audio-v1",
};

static const workload_profile_t WORKLOAD_PROFILES[LAB10_WORKLOAD_ID_COUNT] = {
    [LAB10_WORKLOAD_SPARSE_MULTICOLOR] = {7u, 3u, 9000u, 7000u, 42000u,
                                          LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_CONTINUOUS_MULTICOLOR] =
        {20u, 4u, 5000u, 6500u, 30000u,
         LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_DENSE_MULTICOLOR] =
        {38u, 5u, 6000u, 5000u, 24000u,
         LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_EDGE_CORNER] = {11u, 4u, 6000u, 7500u, 36000u,
                                    LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_RAPID_COLOR_SWITCH] =
        {12u, 6u, 4200u, 5000u, 18000u,
         LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_BACKGROUND_ERASE] =
        {15u, 5u, 5500u, 7000u, 28000u,
         LAB10_WORKLOAD_TERMINAL_COMPLETED},
    [LAB10_WORKLOAD_STOP_DURING_STROKE] =
        {18u, 3u, 5000u, 6000u, 30000u,
         LAB10_WORKLOAD_TERMINAL_STOPPED},
    [LAB10_WORKLOAD_CANCEL_DURING_AUDIO] =
        {18u, 4u, 5000u, 6000u, 30000u,
         LAB10_WORKLOAD_TERMINAL_CANCELLED},
};

/* Four-segment paths deliberately turn at each interior waypoint. */
static const coordinate_t PATHS[LAB10_WORKLOAD_MAX_STROKES][4] = {
    {{0, 0}, {46, 28}, {24, 78}, {96, 112}},
    {{221, 12}, {172, 56}, {210, 124}, {132, 176}},
    {{10, 229}, {76, 178}, {136, 226}, {224, 188}},
    {{239, 239}, {188, 200}, {230, 148}, {154, 116}},
    {{1, 132}, {50, 88}, {92, 148}, {142, 86}},
    {{228, 232}, {178, 182}, {102, 208}, {52, 154}},
    {{18, 198}, {64, 224}, {116, 166}, {198, 214}},
    {{198, 20}, {224, 74}, {158, 42}, {208, 116}},
};

static const uint8_t STROKE_COLORS[LAB10_WORKLOAD_MAX_STROKES] = {
    1u, 4u, 0u, 5u, 2u, 3u, 0u, 4u,
};

static const uint32_t COLOR_RGB[6] = {
    UINT32_C(0x000000), UINT32_C(0xF8F8F8), UINT32_C(0xE8C040),
    UINT32_C(0x4CAF50), UINT32_C(0xE53935), UINT32_C(0x5AA0E8),
};

static uint64_t next_random(uint64_t *state)
{
    uint64_t value = *state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    *state = value;
    return value * UINT64_C(2685821657736338717);
}

static uint32_t random_u32(uint64_t *state, uint32_t upper_exclusive)
{
    if (upper_exclusive == 0u) {
        return 0u;
    }
    return (uint32_t)(next_random(state) % upper_exclusive);
}

static int32_t random_signed(uint64_t *state, int32_t magnitude)
{
    const uint32_t span = (uint32_t)(magnitude * 2 + 1);
    return (int32_t)random_u32(state, span) - magnitude;
}

static void copy_text(char *destination, size_t capacity, const char *source)
{
    size_t index = 0u;
    if (capacity == 0u) {
        return;
    }
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static bool valid_workload_id(lab10_workload_id_t workload_id)
{
    return workload_id >= LAB10_WORKLOAD_SPARSE_MULTICOLOR &&
           workload_id < LAB10_WORKLOAD_ID_COUNT;
}

static uint8_t clamp_display_coordinate(int32_t coordinate)
{
    if (coordinate < 0) {
        return 0u;
    }
    if (coordinate >= 240) {
        return 239u;
    }
    return (uint8_t)coordinate;
}

static void map_logical(uint8_t display_x, uint8_t display_y, int16_t *logical_x,
                        int16_t *logical_y)
{
    *logical_x = (int16_t)(display_x / 2u);
    *logical_y = (int16_t)(display_y / 2u);
}

static uint32_t abs_jitter(int32_t jitter)
{
    if (jitter < 0) {
        return (uint32_t)(-(jitter + 1)) + 1u;
    }
    return (uint32_t)jitter;
}

static bool append_event(lab10_workload_trace_t *trace,
                         const lab10_workload_config_t *config,
                         uint64_t *random_state, uint64_t ideal_timestamp_us,
                         lab10_workload_phase_t phase,
                         lab10_workload_boundary_t boundary, int16_t raw_x,
                         int16_t raw_y, int16_t display_x, int16_t display_y,
                         bool logical_valid, bool accepted, uint32_t stroke_id,
                         uint8_t color_index, uint32_t pause_before_us,
                         uint32_t pen_up_gap_before_us)
{
    if (trace->event_count >= config->event_capacity ||
        trace->event_count >= LAB10_WORKLOAD_MAX_EVENTS) {
        return false;
    }

    lab10_workload_event_t *event = &trace->events[trace->event_count];
    const int32_t jitter = random_signed(
        random_state, (int32_t)config->timestamp_jitter_bound_us);
    uint64_t timestamp_us = ideal_timestamp_us;
    if (jitter < 0) {
        timestamp_us -= (uint64_t)(-(jitter + 1)) + 1u;
    } else {
        timestamp_us += (uint64_t)jitter;
    }
    if (trace->event_count > 0u &&
        timestamp_us <= trace->events[trace->event_count - 1u].timestamp_us) {
        timestamp_us = trace->events[trace->event_count - 1u].timestamp_us + 1u;
    }

    const int64_t timestamp_delta = (int64_t)timestamp_us -
                                    (int64_t)ideal_timestamp_us;
    *event = (lab10_workload_event_t){
        .event_index = trace->event_count,
        .timestamp_us = timestamp_us,
        .ideal_timestamp_us = ideal_timestamp_us,
        .timestamp_jitter_us = (int32_t)timestamp_delta,
        .raw_x = raw_x,
        .raw_y = raw_y,
        .display_x = display_x,
        .display_y = display_y,
        .logical_x = 0,
        .logical_y = 0,
        .stroke_id = stroke_id,
        .color_rgb = color_index < 6u ? COLOR_RGB[color_index] : 0u,
        .color_index = color_index,
        .touch_id = 0u,
        .logical_valid = logical_valid,
        .accepted = accepted,
        .pause_before_us = pause_before_us,
        .pen_up_gap_before_us = pen_up_gap_before_us,
        .phase = phase,
        .boundary = boundary,
    };
    if (logical_valid) {
        map_logical((uint8_t)display_x, (uint8_t)display_y,
                    &event->logical_x, &event->logical_y);
    }
    const uint32_t jitter_abs = abs_jitter(event->timestamp_jitter_us);
    if (jitter_abs > trace->actual_max_abs_jitter_us) {
        trace->actual_max_abs_jitter_us = jitter_abs;
    }
    ++trace->event_count;
    return true;
}

static coordinate_t path_point(uint32_t stroke_index, uint32_t point_index,
                               uint32_t point_count, uint64_t *random_state)
{
    const coordinate_t *path = PATHS[stroke_index % LAB10_WORKLOAD_MAX_STROKES];
    const uint32_t path_steps = 3u;
    const uint32_t scaled = point_count > 1u
                                ? point_index * path_steps * 100u /
                                      (point_count - 1u)
                                : 0u;
    uint32_t segment = scaled / 100u;
    if (segment >= path_steps) {
        segment = path_steps - 1u;
    }
    const uint32_t local = scaled - segment * 100u;
    const coordinate_t start = path[segment];
    const coordinate_t end = path[segment + 1u];
    int32_t x = start.x + ((int32_t)(end.x - start.x) * (int32_t)local) / 100;
    int32_t y = start.y + ((int32_t)(end.y - start.y) * (int32_t)local) / 100;
    const bool at_waypoint = local == 0u || point_index == point_count - 1u;
    if (!at_waypoint) {
        x += random_signed(random_state, 3);
        y += random_signed(random_state, 3);
    }
    return (coordinate_t){(int16_t)clamp_display_coordinate(x),
                          (int16_t)clamp_display_coordinate(y)};
}

static bool append_palette_change(lab10_workload_trace_t *trace,
                                  const lab10_workload_config_t *config,
                                  uint64_t *random_state,
                                  uint64_t *ideal_timestamp_us,
                                  uint8_t color_index, uint32_t gap_us)
{
    const uint16_t center_x[6] = {269u, 291u, 269u, 291u, 269u, 291u};
    const uint16_t center_y[6] = {208u, 172u, 136u, 100u, 64u, 28u};
    *ideal_timestamp_us += (uint64_t)gap_us;
    return append_event(
        trace, config, random_state, *ideal_timestamp_us,
        LAB10_WORKLOAD_PHASE_PALETTE_CHANGE,
        LAB10_WORKLOAD_BOUNDARY_COLOR_CHANGE, (int16_t)center_x[color_index],
        (int16_t)center_y[color_index], (int16_t)center_x[color_index],
        (int16_t)center_y[color_index], false, false, 0u, color_index, 0u,
        gap_us);
}

static bool append_terminal(lab10_workload_trace_t *trace,
                            const lab10_workload_config_t *config,
                            uint64_t *random_state, uint64_t *ideal_timestamp_us,
                            lab10_workload_terminal_t terminal)
{
    *ideal_timestamp_us += 6000u;
    if (!append_event(trace, config, random_state, *ideal_timestamp_us,
                      LAB10_WORKLOAD_PHASE_TERMINAL,
                      LAB10_WORKLOAD_BOUNDARY_TERMINAL, 0, 0, 0, 0, false,
                      false, 0u, 0u, 0u, 0u)) {
        return false;
    }
    trace->terminal_outcome = terminal;
    trace->duration_us = trace->events[trace->event_count - 1u].timestamp_us;
    return true;
}

lab10_workload_config_t lab10_workload_default_config(
    lab10_workload_id_t workload_id, uint64_t seed)
{
    return (lab10_workload_config_t){
        .workload_id = workload_id,
        .seed = seed,
        .event_capacity = LAB10_WORKLOAD_MAX_EVENTS,
        .point_capacity = LAB10_WORKLOAD_MAX_POINTS,
        .duration_capacity_us = UINT64_C(180000000),
        .timestamp_jitter_bound_us =
            LAB10_WORKLOAD_TIMESTAMP_JITTER_BOUND_US,
    };
}

const char *lab10_workload_id_name(lab10_workload_id_t workload_id)
{
    return valid_workload_id(workload_id) ? WORKLOAD_NAMES[workload_id] : NULL;
}

const char *lab10_workload_phase_name(lab10_workload_phase_t phase)
{
    static const char *const names[] = {"PALETTE_CHANGE", "DOWN", "MOVE",
                                        "UP", "LEAVE", "TERMINAL"};
    return phase <= LAB10_WORKLOAD_PHASE_TERMINAL ? names[phase] : NULL;
}

const char *lab10_workload_boundary_name(lab10_workload_boundary_t boundary)
{
    static const char *const names[] = {
        "NONE", "STROKE_START", "STROKE_CONTINUE", "STROKE_END",
        "PEN_UP_GAP", "COLOR_CHANGE", "TERMINAL",
    };
    return boundary <= LAB10_WORKLOAD_BOUNDARY_TERMINAL ? names[boundary]
                                                         : NULL;
}

const char *lab10_workload_terminal_name(lab10_workload_terminal_t terminal)
{
    static const char *const names[] = {"completed", "stopped", "cancelled"};
    return terminal <= LAB10_WORKLOAD_TERMINAL_CANCELLED ? names[terminal]
                                                         : NULL;
}

const char *lab10_workload_result_name(lab10_workload_result_t result)
{
    static const char *const names[] = {
        "ok", "invalid-argument", "invalid-workload-id", "invalid-seed",
        "invalid-event-capacity", "invalid-point-capacity",
        "invalid-duration-capacity", "invalid-jitter-bound",
        "output-capacity",
    };
    return result <= LAB10_WORKLOAD_OUTPUT_CAPACITY ? names[result] : NULL;
}

lab10_workload_result_t lab10_workload_generate(
    const lab10_workload_config_t *config, lab10_workload_trace_t *trace)
{
    if (config == NULL || trace == NULL) {
        return LAB10_WORKLOAD_INVALID_ARGUMENT;
    }
    if (!valid_workload_id(config->workload_id)) {
        return LAB10_WORKLOAD_INVALID_ID;
    }
    if (config->seed == 0u) {
        return LAB10_WORKLOAD_INVALID_SEED;
    }
    if (config->event_capacity == 0u ||
        config->event_capacity > LAB10_WORKLOAD_MAX_EVENTS) {
        return LAB10_WORKLOAD_INVALID_EVENT_CAPACITY;
    }
    if (config->point_capacity == 0u ||
        config->point_capacity > LAB10_WORKLOAD_MAX_POINTS) {
        return LAB10_WORKLOAD_INVALID_POINT_CAPACITY;
    }
    if (config->duration_capacity_us == 0u ||
        config->duration_capacity_us > UINT64_C(180000000)) {
        return LAB10_WORKLOAD_INVALID_DURATION_CAPACITY;
    }
    if (config->timestamp_jitter_bound_us !=
        LAB10_WORKLOAD_TIMESTAMP_JITTER_BOUND_US) {
        return LAB10_WORKLOAD_INVALID_JITTER_BOUND;
    }

    lab10_workload_trace_t generated = {0};
    copy_text(generated.workload_identity, sizeof(generated.workload_identity),
              WORKLOAD_NAMES[config->workload_id]);
    copy_text(generated.workload_schema_version,
              sizeof(generated.workload_schema_version),
              LAB10_WORKLOAD_SCHEMA_VERSION);
    copy_text(generated.generator_version, sizeof(generated.generator_version),
              LAB10_WORKLOAD_GENERATOR_VERSION);
    copy_text(generated.evidence_provenance,
              sizeof(generated.evidence_provenance),
              LAB10_WORKLOAD_EVIDENCE_PROVENANCE);
    generated.seed = config->seed;
    generated.timestamp_jitter_bound_us =
        config->timestamp_jitter_bound_us;
    generated.terminal_outcome = WORKLOAD_PROFILES[config->workload_id].terminal;

    uint64_t random_state = config->seed ^
                            (UINT64_C(0x9E3779B97F4A7C15) +
                             (uint64_t)config->workload_id *
                                 UINT64_C(0xD1B54A32D192ED03));
    if (random_state == 0u) {
        random_state = UINT64_C(0xA5A5A5A5A5A5A5A5);
    }
    uint64_t ideal_timestamp_us = 10000u;
    uint8_t selected_color = 1u;
    bool had_variable_spacing = false;
    bool had_variable_velocity = false;
    uint32_t previous_distance = 0u;
    uint32_t previous_interval = 0u;
    bool have_previous_distance = false;
    bool have_previous_interval = false;

    const workload_profile_t profile = WORKLOAD_PROFILES[config->workload_id];
    for (uint32_t stroke_index = 0u; stroke_index < profile.stroke_count;
         ++stroke_index) {
        uint32_t point_count =
            (uint32_t)profile.point_count + random_u32(&random_state, 4u);
        if (point_count < 7u) {
            point_count = 7u;
        }
        const bool terminal_partial =
            profile.terminal != LAB10_WORKLOAD_TERMINAL_COMPLETED &&
            stroke_index == 1u;
        if (terminal_partial) {
            point_count = point_count / 2u + 2u;
        }

        const uint8_t color_index =
            STROKE_COLORS[stroke_index % LAB10_WORKLOAD_MAX_STROKES];
        const uint32_t gap_us = 26000u + random_u32(&random_state, 12000u);
        if (!append_palette_change(&generated, config, &random_state,
                                   &ideal_timestamp_us, color_index, gap_us)) {
            return LAB10_WORKLOAD_OUTPUT_CAPACITY;
        }
        if (selected_color != color_index) {
            ++generated.color_change_count;
        }
        selected_color = color_index;

        const uint32_t stroke_id = stroke_index + 1u;
        for (uint32_t point_index = 0u; point_index < point_count;
             ++point_index) {
            const coordinate_t point =
                path_point(stroke_index, point_index, point_count,
                           &random_state);
            const bool first_point = point_index == 0u;
            const lab10_workload_phase_t phase =
                first_point ? LAB10_WORKLOAD_PHASE_DOWN
                            : LAB10_WORKLOAD_PHASE_MOVE;
            const lab10_workload_boundary_t boundary =
                first_point ? LAB10_WORKLOAD_BOUNDARY_STROKE_START
                            : LAB10_WORKLOAD_BOUNDARY_STROKE_CONTINUE;
            const uint32_t pause_before_us =
                !first_point && point_index == point_count / 2u
                    ? profile.pause_us + random_u32(&random_state, 18000u)
                    : 0u;
            const uint32_t interval_us =
                profile.interval_base_us +
                random_u32(&random_state, profile.interval_variation_us);
            ideal_timestamp_us += (uint64_t)interval_us + pause_before_us;

            if (!first_point) {
                const int32_t dx = (int32_t)point.x -
                                   generated.events[generated.event_count - 1u]
                                       .display_x;
                const int32_t dy = (int32_t)point.y -
                                   generated.events[generated.event_count - 1u]
                                       .display_y;
                const uint32_t distance = (uint32_t)(dx * dx + dy * dy);
                if (have_previous_distance && distance != previous_distance) {
                    had_variable_spacing = true;
                }
                previous_distance = distance;
                have_previous_distance = true;
            }
            if (have_previous_interval && interval_us != previous_interval) {
                had_variable_velocity = true;
            }
            previous_interval = interval_us;
            have_previous_interval = true;

            if (!append_event(&generated, config, &random_state,
                              ideal_timestamp_us, phase, boundary, point.x,
                              point.y, point.x, point.y, true, true, stroke_id,
                              color_index, pause_before_us, 0u)) {
                return LAB10_WORKLOAD_OUTPUT_CAPACITY;
            }
            ++generated.accepted_point_count;

            if (terminal_partial && point_index + 1u == point_count) {
                if (!append_terminal(&generated, config, &random_state,
                                     &ideal_timestamp_us, profile.terminal)) {
                    return LAB10_WORKLOAD_OUTPUT_CAPACITY;
                }
                ++generated.stroke_count;
                goto generation_finished;
            }
        }

        const bool leave_for_reenter = stroke_index == 0u;
        const coordinate_t end = path_point(
            stroke_index, point_count - 1u, point_count, &random_state);
        ideal_timestamp_us += 6000u;
        if (!append_event(&generated, config, &random_state, ideal_timestamp_us,
                          leave_for_reenter ? LAB10_WORKLOAD_PHASE_LEAVE
                                            : LAB10_WORKLOAD_PHASE_UP,
                          LAB10_WORKLOAD_BOUNDARY_STROKE_END, end.x, end.y,
                          end.x, end.y, false, false, stroke_id, color_index,
                          0u, 0u)) {
            return LAB10_WORKLOAD_OUTPUT_CAPACITY;
        }
        ++generated.stroke_count;
        if (leave_for_reenter) {
            generated.feature_flags |= LAB10_WORKLOAD_FEATURE_LEAVE_REENTER;
        }
    }

generation_finished:
    if (generated.terminal_outcome == LAB10_WORKLOAD_TERMINAL_COMPLETED) {
        if (!append_terminal(&generated, config, &random_state,
                             &ideal_timestamp_us, generated.terminal_outcome)) {
            return LAB10_WORKLOAD_OUTPUT_CAPACITY;
        }
    }

    if (generated.accepted_point_count > config->point_capacity ||
        generated.duration_us > config->duration_capacity_us) {
        return LAB10_WORKLOAD_OUTPUT_CAPACITY;
    }

    generated.feature_flags |= LAB10_WORKLOAD_FEATURE_CORNERS |
                               LAB10_WORKLOAD_FEATURE_MULTIPLE_STROKES |
                               LAB10_WORKLOAD_FEATURE_COLOR_CHANGES |
                               LAB10_WORKLOAD_FEATURE_PEN_UP_GAPS;
    if (had_variable_spacing) {
        generated.feature_flags |= LAB10_WORKLOAD_FEATURE_VARIABLE_SPACING;
    }
    if (had_variable_velocity) {
        generated.feature_flags |= LAB10_WORKLOAD_FEATURE_VARIABLE_VELOCITY;
    }
    for (uint32_t index = 0u; index < generated.event_count; ++index) {
        const lab10_workload_event_t *event = &generated.events[index];
        if (event->accepted &&
            (event->display_x <= 1 || event->display_y <= 1 ||
             event->display_x >= 238 || event->display_y >= 238)) {
            generated.feature_flags |= LAB10_WORKLOAD_FEATURE_EDGE_POINTS;
        }
        if (event->accepted && event->color_index == 0u) {
            generated.feature_flags |= LAB10_WORKLOAD_FEATURE_BACKGROUND_ERASE;
        }
        if (event->pause_before_us > 0u) {
            generated.feature_flags |= LAB10_WORKLOAD_FEATURE_PAUSES;
        }
        if (event->pen_up_gap_before_us > 0u) {
            generated.feature_flags |= LAB10_WORKLOAD_FEATURE_PEN_UP_GAPS;
        }
    }
    if (generated.terminal_outcome == LAB10_WORKLOAD_TERMINAL_STOPPED) {
        generated.feature_flags |= LAB10_WORKLOAD_FEATURE_STOP;
    } else if (generated.terminal_outcome == LAB10_WORKLOAD_TERMINAL_CANCELLED) {
        generated.feature_flags |= LAB10_WORKLOAD_FEATURE_CANCEL;
    }

    *trace = generated;
    return LAB10_WORKLOAD_OK;
}

size_t lab10_workload_trace_size_bytes(const lab10_workload_trace_t *trace)
{
    if (trace == NULL || trace->event_count > LAB10_WORKLOAD_MAX_EVENTS) {
        return 0u;
    }
    return sizeof(*trace) -
               sizeof(trace->events) +
           (size_t)trace->event_count * sizeof(trace->events[0]);
}

