#include "lab10_workload.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool event_equal(const lab10_workload_event_t *left,
                        const lab10_workload_event_t *right)
{
    return left->event_index == right->event_index &&
           left->timestamp_us == right->timestamp_us &&
           left->ideal_timestamp_us == right->ideal_timestamp_us &&
           left->timestamp_jitter_us == right->timestamp_jitter_us &&
           left->raw_x == right->raw_x && left->raw_y == right->raw_y &&
           left->display_x == right->display_x &&
           left->display_y == right->display_y &&
           left->logical_x == right->logical_x &&
           left->logical_y == right->logical_y &&
           left->stroke_id == right->stroke_id &&
           left->color_rgb == right->color_rgb &&
           left->color_index == right->color_index &&
           left->touch_id == right->touch_id &&
           left->logical_valid == right->logical_valid &&
           left->accepted == right->accepted &&
           left->pause_before_us == right->pause_before_us &&
           left->pen_up_gap_before_us == right->pen_up_gap_before_us &&
           left->phase == right->phase && left->boundary == right->boundary;
}

static bool trace_equal(const lab10_workload_trace_t *left,
                        const lab10_workload_trace_t *right)
{
    if (strcmp(left->workload_identity, right->workload_identity) != 0 ||
        strcmp(left->workload_schema_version,
               right->workload_schema_version) != 0 ||
        strcmp(left->generator_version, right->generator_version) != 0 ||
        strcmp(left->evidence_provenance, right->evidence_provenance) != 0 ||
        left->seed != right->seed ||
        left->timestamp_jitter_bound_us != right->timestamp_jitter_bound_us ||
        left->actual_max_abs_jitter_us != right->actual_max_abs_jitter_us ||
        left->feature_flags != right->feature_flags ||
        left->event_count != right->event_count ||
        left->accepted_point_count != right->accepted_point_count ||
        left->stroke_count != right->stroke_count ||
        left->color_change_count != right->color_change_count ||
        left->duration_us != right->duration_us ||
        left->terminal_outcome != right->terminal_outcome) {
        return false;
    }
    for (uint32_t index = 0u; index < left->event_count; ++index) {
        if (!event_equal(&left->events[index], &right->events[index])) {
            return false;
        }
    }
    return true;
}

static void test_determinism(void)
{
    const lab10_workload_config_t config =
        lab10_workload_default_config(LAB10_WORKLOAD_DENSE_MULTICOLOR,
                                      UINT64_C(0x0123456789ABCDEF));
    lab10_workload_trace_t first;
    lab10_workload_trace_t second;
    assert(lab10_workload_generate(&config, &first) == LAB10_WORKLOAD_OK);
    assert(lab10_workload_generate(&config, &second) == LAB10_WORKLOAD_OK);
    assert(trace_equal(&first, &second));
    assert(first.seed == UINT64_C(0x0123456789ABCDEF));
    assert(strcmp(first.workload_schema_version,
                  LAB10_WORKLOAD_SCHEMA_VERSION) == 0);
    assert(strcmp(first.generator_version,
                  LAB10_WORKLOAD_GENERATOR_VERSION) == 0);
    assert(strcmp(first.evidence_provenance,
                  LAB10_WORKLOAD_EVIDENCE_PROVENANCE) == 0);
}

static void test_variation_across_workloads_and_seeds(void)
{
    const lab10_workload_config_t dense_config =
        lab10_workload_default_config(LAB10_WORKLOAD_DENSE_MULTICOLOR,
                                      UINT64_C(41));
    const lab10_workload_config_t sparse_config =
        lab10_workload_default_config(LAB10_WORKLOAD_SPARSE_MULTICOLOR,
                                      UINT64_C(41));
    const lab10_workload_config_t other_seed_config =
        lab10_workload_default_config(LAB10_WORKLOAD_DENSE_MULTICOLOR,
                                      UINT64_C(42));
    lab10_workload_trace_t dense;
    lab10_workload_trace_t sparse;
    lab10_workload_trace_t other_seed;
    assert(lab10_workload_generate(&dense_config, &dense) == LAB10_WORKLOAD_OK);
    assert(lab10_workload_generate(&sparse_config, &sparse) ==
           LAB10_WORKLOAD_OK);
    assert(lab10_workload_generate(&other_seed_config, &other_seed) ==
           LAB10_WORKLOAD_OK);
    assert(!trace_equal(&dense, &sparse));
    assert(!trace_equal(&dense, &other_seed));
    assert(dense.accepted_point_count > sparse.accepted_point_count);
}

static void test_bounds_and_timestamp_contract(void)
{
    for (int workload_id = LAB10_WORKLOAD_SPARSE_MULTICOLOR;
         workload_id < LAB10_WORKLOAD_ID_COUNT; ++workload_id) {
        const lab10_workload_config_t config = lab10_workload_default_config(
            (lab10_workload_id_t)workload_id, UINT64_C(1000) + workload_id);
        lab10_workload_trace_t trace;
        assert(lab10_workload_generate(&config, &trace) == LAB10_WORKLOAD_OK);
        assert(trace.event_count > 0u);
        assert(trace.event_count <= config.event_capacity);
        assert(trace.accepted_point_count <= config.point_capacity);
        assert(trace.duration_us <= config.duration_capacity_us);
        assert(lab10_workload_trace_size_bytes(&trace) <=
               sizeof(lab10_workload_trace_t));
        assert(trace.actual_max_abs_jitter_us <=
               config.timestamp_jitter_bound_us);

        uint64_t previous_timestamp = 0u;
        uint64_t previous_ideal_timestamp = 0u;
        uint32_t terminal_count = 0u;
        for (uint32_t index = 0u; index < trace.event_count; ++index) {
            const lab10_workload_event_t *event = &trace.events[index];
            assert(event->event_index == index);
            assert(event->timestamp_us >= previous_timestamp);
            assert(event->ideal_timestamp_us >= previous_ideal_timestamp);
            assert(event->timestamp_jitter_us >=
                   -(int32_t)config.timestamp_jitter_bound_us);
            assert(event->timestamp_jitter_us <=
                   (int32_t)config.timestamp_jitter_bound_us);
            if (event->phase == LAB10_WORKLOAD_PHASE_TERMINAL) {
                ++terminal_count;
                assert(index + 1u == trace.event_count);
            }
            previous_timestamp = event->timestamp_us;
            previous_ideal_timestamp = event->ideal_timestamp_us;
        }
        assert(terminal_count == 1u);
        assert(trace.events[trace.event_count - 1u].phase ==
               LAB10_WORKLOAD_PHASE_TERMINAL);
        assert(trace.duration_us ==
               trace.events[trace.event_count - 1u].timestamp_us);
    }
}

static void test_required_gesture_features_and_trace_fields(void)
{
    const lab10_workload_config_t config =
        lab10_workload_default_config(LAB10_WORKLOAD_DENSE_MULTICOLOR,
                                      UINT64_C(0xD00D5E));
    lab10_workload_trace_t trace;
    assert(lab10_workload_generate(&config, &trace) == LAB10_WORKLOAD_OK);
    assert((trace.feature_flags & LAB10_WORKLOAD_REQUIRED_HUMAN_FEATURES) ==
           LAB10_WORKLOAD_REQUIRED_HUMAN_FEATURES);
    assert(trace.stroke_count >= 3u);
    assert(trace.color_change_count >= 2u);

    bool saw_palette = false;
    bool saw_leave = false;
    bool saw_up = false;
    bool saw_background = false;
    bool saw_edge = false;
    bool saw_non_edge = false;
    bool saw_variable_spacing = false;
    bool saw_variable_velocity = false;
    bool saw_pause = false;
    bool saw_pen_up_gap = false;
    bool saw_stroke_boundary = false;
    uint32_t accepted_count = 0u;
    uint32_t previous_distance = 0u;
    bool have_distance = false;
    uint64_t previous_timestamp = 0u;
    uint64_t previous_interval = 0u;
    bool have_interval = false;
    for (uint32_t index = 0u; index < trace.event_count; ++index) {
        const lab10_workload_event_t *event = &trace.events[index];
        if (index > 0u) {
            const uint64_t interval = event->timestamp_us - previous_timestamp;
            if (have_interval && interval != previous_interval) {
                saw_variable_velocity = true;
            }
            previous_interval = interval;
            have_interval = true;
        }
        previous_timestamp = event->timestamp_us;
        if (event->phase == LAB10_WORKLOAD_PHASE_PALETTE_CHANGE) {
            saw_palette = true;
        }
        if (event->phase == LAB10_WORKLOAD_PHASE_LEAVE) {
            saw_leave = true;
        }
        if (event->phase == LAB10_WORKLOAD_PHASE_UP) {
            saw_up = true;
        }
        if (event->accepted) {
            ++accepted_count;
            assert(event->raw_x == event->display_x);
            assert(event->raw_y == event->display_y);
            assert(event->display_x >= 0 && event->display_x < 240);
            assert(event->display_y >= 0 && event->display_y < 240);
            assert(event->logical_valid);
            assert(event->logical_x == event->display_x / 2);
            assert(event->logical_y == event->display_y / 2);
            assert(event->color_rgb != UINT32_C(0xFFFFFF));
            if (event->color_index == 0u) {
                saw_background = true;
            }
            if (event->display_x <= 1 || event->display_y <= 1 ||
                event->display_x >= 238 || event->display_y >= 238) {
                saw_edge = true;
            } else {
                saw_non_edge = true;
            }
            if (have_distance) {
                const lab10_workload_event_t *previous =
                    &trace.events[index - 1u];
                const int32_t dx = event->display_x - previous->display_x;
                const int32_t dy = event->display_y - previous->display_y;
                const uint32_t distance = (uint32_t)(dx * dx + dy * dy);
                if (distance != previous_distance) {
                    saw_variable_spacing = true;
                }
                previous_distance = distance;
            } else {
                previous_distance = 0u;
                have_distance = true;
            }
        }
        if (event->pause_before_us > 0u) {
            saw_pause = true;
        }
        if (event->pen_up_gap_before_us > 0u) {
            saw_pen_up_gap = true;
        }
        if (event->boundary == LAB10_WORKLOAD_BOUNDARY_STROKE_START ||
            event->boundary == LAB10_WORKLOAD_BOUNDARY_STROKE_END) {
            saw_stroke_boundary = true;
        }
    }
    assert(accepted_count == trace.accepted_point_count);
    assert(saw_palette);
    assert(saw_leave);
    assert(saw_up);
    assert(saw_background);
    assert(saw_edge && saw_non_edge);
    assert(saw_variable_spacing);
    assert(saw_variable_velocity);
    assert(saw_pause);
    assert(saw_pen_up_gap);
    assert(saw_stroke_boundary);
    assert((trace.feature_flags & LAB10_WORKLOAD_FEATURE_LEAVE_REENTER) != 0u);
    assert(strcmp(lab10_workload_id_name(LAB10_WORKLOAD_DENSE_MULTICOLOR),
                  "synthetic-dense-multicolor-v1") == 0);
}

static void test_jitter_bound_across_seed_range(void)
{
    for (int workload_id = LAB10_WORKLOAD_SPARSE_MULTICOLOR;
         workload_id < LAB10_WORKLOAD_ID_COUNT; ++workload_id) {
        for (uint64_t seed = 1u; seed <= 32u; ++seed) {
            const lab10_workload_config_t config =
                lab10_workload_default_config(
                    (lab10_workload_id_t)workload_id, seed);
            lab10_workload_trace_t trace;
            assert(lab10_workload_generate(&config, &trace) ==
                   LAB10_WORKLOAD_OK);
            uint64_t previous_timestamp = 0u;
            for (uint32_t index = 0u; index < trace.event_count; ++index) {
                const lab10_workload_event_t *event = &trace.events[index];
                assert(event->timestamp_us >= previous_timestamp);
                assert(event->timestamp_jitter_us >= -2000);
                assert(event->timestamp_jitter_us <= 2000);
                previous_timestamp = event->timestamp_us;
            }
        }
    }
}

static void test_terminal_workload_identities(void)
{
    const lab10_workload_id_t terminal_ids[] = {
        LAB10_WORKLOAD_STOP_DURING_STROKE,
        LAB10_WORKLOAD_CANCEL_DURING_AUDIO,
    };
    const lab10_workload_terminal_t expected[] = {
        LAB10_WORKLOAD_TERMINAL_STOPPED,
        LAB10_WORKLOAD_TERMINAL_CANCELLED,
    };
    for (size_t index = 0u; index < sizeof(terminal_ids) / sizeof(terminal_ids[0]);
         ++index) {
        const lab10_workload_config_t config =
            lab10_workload_default_config(terminal_ids[index],
                                          UINT64_C(700) + index);
        lab10_workload_trace_t trace;
        assert(lab10_workload_generate(&config, &trace) == LAB10_WORKLOAD_OK);
        assert(trace.terminal_outcome == expected[index]);
        assert(trace.events[trace.event_count - 1u].phase ==
               LAB10_WORKLOAD_PHASE_TERMINAL);
        assert((trace.feature_flags &
                (expected[index] == LAB10_WORKLOAD_TERMINAL_STOPPED
                     ? LAB10_WORKLOAD_FEATURE_STOP
                     : LAB10_WORKLOAD_FEATURE_CANCEL)) != 0u);
    }
}

static void test_invalid_configuration_failures(void)
{
    lab10_workload_trace_t trace;
    lab10_workload_config_t config =
        lab10_workload_default_config(LAB10_WORKLOAD_SPARSE_MULTICOLOR, 1u);
    assert(lab10_workload_generate(NULL, &trace) ==
           LAB10_WORKLOAD_INVALID_ARGUMENT);
    assert(lab10_workload_generate(&config, NULL) ==
           LAB10_WORKLOAD_INVALID_ARGUMENT);

    config.workload_id = LAB10_WORKLOAD_ID_COUNT;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_ID);
    config = lab10_workload_default_config(LAB10_WORKLOAD_SPARSE_MULTICOLOR, 0u);
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_SEED);
    config.seed = 1u;
    config.event_capacity = 0u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_EVENT_CAPACITY);
    config.event_capacity = LAB10_WORKLOAD_MAX_EVENTS + 1u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_EVENT_CAPACITY);
    config.event_capacity = LAB10_WORKLOAD_MAX_EVENTS;
    config.point_capacity = 0u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_POINT_CAPACITY);
    config.point_capacity = LAB10_WORKLOAD_MAX_POINTS;
    config.duration_capacity_us = 0u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_DURATION_CAPACITY);
    config.duration_capacity_us = UINT64_C(180000001);
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_DURATION_CAPACITY);
    config.duration_capacity_us = UINT64_C(180000000);
    config.timestamp_jitter_bound_us = 1999u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_JITTER_BOUND);
    config.timestamp_jitter_bound_us = 2001u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_INVALID_JITTER_BOUND);

    config.timestamp_jitter_bound_us = 2000u;
    config.event_capacity = 1u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_OUTPUT_CAPACITY);
    config.event_capacity = LAB10_WORKLOAD_MAX_EVENTS;
    config.point_capacity = 1u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_OUTPUT_CAPACITY);
    config.point_capacity = LAB10_WORKLOAD_MAX_POINTS;
    config.duration_capacity_us = 1u;
    assert(lab10_workload_generate(&config, &trace) ==
           LAB10_WORKLOAD_OUTPUT_CAPACITY);
}

int main(void)
{
    test_determinism();
    test_variation_across_workloads_and_seeds();
    test_bounds_and_timestamp_contract();
    test_required_gesture_features_and_trace_fields();
    test_jitter_bound_across_seed_range();
    test_terminal_workload_identities();
    test_invalid_configuration_failures();
    puts("LAB10_WORKLOAD_NATIVE_TEST_PASS");
    return 0;
}
