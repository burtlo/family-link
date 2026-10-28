#include "lab10_ui.h"

#include <stddef.h>

static const lab10_swatch_geometry_t k_swatches[LAB10_SWATCH_COUNT] = {
    {269, 208, 0x000000},
    {291, 172, 0xF8F8F8},
    {269, 136, 0xE8C040},
    {291, 100, 0x4CAF50},
    {269, 64, 0xE53935},
    {291, 28, 0x5AA0E8},
};

enum {
    LAB10_SCRIPTED_PATH_WAYPOINTS = 4,
    LAB10_SCRIPTED_CADENCE_PATTERN_LENGTH = 5,
};

static const int16_t SCRIPTED_PATHS[4][LAB10_SCRIPTED_PATH_WAYPOINTS][2] = {
    {{1, 1}, {238, 32}, {12, 220}, {238, 238}},
    {{1, 238}, {224, 1}, {1, 12}, {238, 228}},
    {{1, 120}, {238, 8}, {10, 232}, {238, 120}},
    {{120, 1}, {238, 238}, {1, 220}, {220, 1}},
};

static const uint8_t SCRIPTED_STROKE_COLORS[LAB10_UI_SCRIPTED_STROKE_COUNT] = {
    LAB10_COLOR_WHITE,
    LAB10_COLOR_GOLD,
    LAB10_COLOR_GREEN,
    LAB10_COLOR_RED,
    LAB10_COLOR_BLUE,
    LAB10_COLOR_WHITE,
    LAB10_COLOR_GOLD,
    LAB10_COLOR_RED,
    LAB10_COLOR_GREEN,
    LAB10_COLOR_BLUE,
    LAB10_COLOR_WHITE,
    LAB10_COLOR_RED,
    LAB10_COLOR_GOLD,
    LAB10_COLOR_BLUE,
    LAB10_COLOR_GREEN,
    LAB10_COLOR_WHITE,
    LAB10_COLOR_BLUE,
    LAB10_COLOR_RED,
};

static const uint64_t SCRIPTED_CADENCE_US[LAB10_SCRIPTED_CADENCE_PATTERN_LENGTH] = {
    UINT64_C(36000),
    UINT64_C(36000),
    UINT64_C(36000),
    UINT64_C(40000),
    UINT64_C(40000),
};

static bool is_outcome(lab10_screen_state_t state)
{
    return state == LAB10_SCREEN_STOPPED || state == LAB10_SCREEN_CANCELLED ||
           state == LAB10_SCREEN_FAILED || state == LAB10_SCREEN_COMPLETED;
}

static int clamp_logical(int value, int limit)
{
    if (value < 0) {
        return 0;
    }
    if (value >= limit) {
        return limit - 1;
    }
    return value;
}

const lab10_swatch_geometry_t *lab10_swatch_geometry(uint8_t index)
{
    return index < LAB10_SWATCH_COUNT ? &k_swatches[index] : NULL;
}

uint32_t lab10_color_rgb(uint8_t index)
{
    const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry(index);
    return swatch != NULL ? swatch->color_rgb : k_swatches[LAB10_COLOR_WHITE].color_rgb;
}

bool lab10_display_contains(int16_t x, int16_t y)
{
    return x >= 0 && x < LAB10_LCD_WIDTH && y >= 0 && y < LAB10_LCD_HEIGHT;
}

bool lab10_canvas_contains(int16_t x, int16_t y)
{
    return x >= 0 && x < LAB10_CANVAS_WIDTH && y >= 0 && y < LAB10_CANVAS_HEIGHT;
}

bool lab10_rail_contains(int16_t x, int16_t y)
{
    return x >= LAB10_RAIL_X && x < LAB10_LCD_WIDTH && y >= 0 && y < LAB10_LCD_HEIGHT;
}

int lab10_palette_hit_test(int16_t x, int16_t y)
{
    if (!lab10_rail_contains(x, y)) {
        return -1;
    }
    for (int index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *swatch = &k_swatches[index];
        const int dx = (int)x - swatch->center_x;
        const int dy = (int)y - swatch->center_y;
        if (dx * dx + dy * dy <= LAB10_SWATCH_RADIUS * LAB10_SWATCH_RADIUS) {
            return index;
        }
    }
    return -1;
}

void lab10_map_to_logical(int16_t display_x, int16_t display_y,
                          uint8_t *logical_x, uint8_t *logical_y)
{
    if (logical_x != NULL) {
        *logical_x = (uint8_t)clamp_logical(display_x / 2, LAB10_LOGICAL_WIDTH);
    }
    if (logical_y != NULL) {
        *logical_y = (uint8_t)clamp_logical(display_y / 2, LAB10_LOGICAL_HEIGHT);
    }
}

void lab10_ui_model_init(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return;
    }
    *model = (lab10_ui_model_t){
        .state = LAB10_SCREEN_IDLE,
        .selected_color = LAB10_COLOR_WHITE,
    };
}

void lab10_ui_set_hooks(lab10_ui_model_t *model, const lab10_ui_hooks_t *hooks)
{
    if (model == NULL) {
        return;
    }
    model->hooks = hooks != NULL ? *hooks : (lab10_ui_hooks_t){0};
}

lab10_screen_state_t lab10_ui_state(const lab10_ui_model_t *model)
{
    return model != NULL ? model->state : LAB10_SCREEN_IDLE;
}

uint8_t lab10_ui_selected_color(const lab10_ui_model_t *model)
{
    return model != NULL ? model->selected_color : LAB10_COLOR_WHITE;
}

const lab10_ui_invalidation_stats_t *lab10_ui_invalidation(
    const lab10_ui_model_t *model)
{
    return model != NULL ? &model->invalidation : NULL;
}

void lab10_ui_scripted_qualification_begin(
    lab10_ui_scripted_qualification_t *qualification, bool scripted_session)
{
    if (qualification == NULL) {
        return;
    }
    *qualification = (lab10_ui_scripted_qualification_t){
        .scripted_session = scripted_session,
    };
}

void lab10_ui_scripted_qualification_resolve(
    lab10_ui_scripted_qualification_t *qualification, bool accepted)
{
    if (qualification == NULL || !qualification->scripted_session) {
        return;
    }
    qualification->decision_resolved = true;
    qualification->completion_accepted = accepted;
}

bool lab10_ui_terminal_screen_state(
    lab10_ui_terminal_kind_t terminal,
    const lab10_ui_scripted_qualification_t *qualification,
    lab10_screen_state_t *state)
{
    if (state == NULL) {
        return false;
    }

    switch (terminal) {
        case LAB10_UI_TERMINAL_COMPLETED:
            if (qualification != NULL && qualification->scripted_session &&
                (!qualification->decision_resolved ||
                 !qualification->completion_accepted)) {
                *state = LAB10_SCREEN_FAILED;
            } else {
                *state = LAB10_SCREEN_COMPLETED;
            }
            return true;
        case LAB10_UI_TERMINAL_STOPPED:
            *state = LAB10_SCREEN_STOPPED;
            return true;
        case LAB10_UI_TERMINAL_CANCELLED:
            *state = LAB10_SCREEN_CANCELLED;
            return true;
        case LAB10_UI_TERMINAL_DURATION_CAP:
            if (qualification != NULL && qualification->scripted_session) {
                if (!qualification->decision_resolved) {
                    return false;
                }
                *state = qualification->completion_accepted
                             ? LAB10_SCREEN_COMPLETED
                             : LAB10_SCREEN_FAILED;
                return true;
            }
            *state = LAB10_SCREEN_COMPLETED;
            return true;
        case LAB10_UI_TERMINAL_POINT_CAP:
        case LAB10_UI_TERMINAL_QUEUE_OVERFLOW:
        case LAB10_UI_TERMINAL_FAILED:
        default:
            *state = LAB10_SCREEN_FAILED;
            return true;
    }
}

bool lab10_ui_scripted_diagnostic_begin(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    lab10_ui_scripted_diagnostic_snapshot_t *snapshot)
{
    if (lifecycle == NULL || snapshot == NULL || lifecycle->pending ||
        lifecycle->reported) {
        return false;
    }
    lifecycle->pending = true;
    lifecycle->reported = true;
    lifecycle->snapshot_frozen = false;
    snapshot->canvas_final_color_mask = 0u;
    snapshot->renderer_drain_complete = false;
    return true;
}

bool lab10_ui_scripted_diagnostic_prepare_delivery(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    lab10_ui_scripted_diagnostic_snapshot_t *snapshot,
    bool active_session_valid, uint64_t active_session_id,
    bool renderer_metrics_available,
    const lab10_ui_scripted_renderer_drain_t *renderer_drain,
    const lab10_ui_scripted_canvas_observation_t *canvas_observation)
{
    if (lifecycle == NULL || snapshot == NULL || !lifecycle->pending) {
        return false;
    }
    if (lifecycle->snapshot_frozen) {
        return true;
    }

    if (!active_session_valid ||
        active_session_id != snapshot->capture_session_id) {
        snapshot->canvas_final_color_mask = 0u;
        snapshot->renderer_drain_complete = false;
        lifecycle->snapshot_frozen = true;
        return true;
    }
    if (!renderer_metrics_available || renderer_drain == NULL ||
        canvas_observation == NULL) {
        return false;
    }

    const bool renderer_drained =
        renderer_drain->source_point_count >= snapshot->accepted_points &&
        renderer_drain->source_cursor >= snapshot->accepted_points &&
        renderer_drain->pending_point_count == 0u &&
        renderer_drain->frame_idle;
    if (!renderer_drained) {
        return false;
    }

    snapshot->renderer_published_color_mask =
        canvas_observation->renderer_published_color_mask;
    snapshot->canvas_sampled_color_mask =
        canvas_observation->canvas_sampled_color_mask;
    snapshot->canvas_readback_color_mask =
        canvas_observation->canvas_readback_color_mask;
    snapshot->canvas_color_match_mask =
        canvas_observation->canvas_color_match_mask;
    snapshot->canvas_final_color_mask =
        canvas_observation->canvas_final_color_mask &
        LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK;
    snapshot->canvas_color_format = canvas_observation->canvas_color_format;
    snapshot->canvas_readback_supported =
        canvas_observation->canvas_readback_supported;
    snapshot->renderer_drain_complete = true;
    lifecycle->snapshot_frozen = true;
    return true;
}

void lab10_ui_scripted_diagnostic_finish_delivery(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    const lab10_ui_scripted_diagnostic_snapshot_t *snapshot,
    bool mailbox_accepted)
{
    if (lifecycle == NULL || snapshot == NULL || !lifecycle->pending ||
        !mailbox_accepted) {
        return;
    }
    lifecycle->pending = false;
    lifecycle->snapshot_frozen = false;
    if (!snapshot->renderer_drain_complete) {
        lifecycle->reported = false;
    }
}

void lab10_ui_scripted_diagnostic_allow_next_session(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL && !lifecycle->pending) {
        lifecycle->reported = false;
    }
}

bool lab10_ui_scripted_schedule_init(
    lab10_ui_scripted_schedule_t *schedule, uint64_t capture_start_timestamp_us,
    uint64_t duration_cap_us, uint32_t accepted_point_cap,
    uint64_t move_interval_us)
{
    if (schedule == NULL || duration_cap_us == 0u ||
        accepted_point_cap < LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS ||
        move_interval_us != LAB10_UI_SCRIPTED_MOVE_INTERVAL_US ||
        duration_cap_us < LAB10_UI_SCRIPTED_PEN_UP_GAP_US ||
        capture_start_timestamp_us > UINT64_MAX - duration_cap_us) {
        return false;
    }
    *schedule = (lab10_ui_scripted_schedule_t){
        .capture_start_timestamp_us = capture_start_timestamp_us,
        .duration_cap_us = duration_cap_us,
        .next_event_timestamp_us = capture_start_timestamp_us,
        .accepted_point_cap = accepted_point_cap,
        .next_action = LAB10_UI_SCRIPTED_ACTION_DOWN,
        .initialized = true,
    };
    return true;
}

static void scripted_path_coordinates(uint8_t stroke_index,
                                      uint16_t point_index, int16_t *x,
                                      int16_t *y)
{
    const int16_t (*path)[2] = SCRIPTED_PATHS[stroke_index % 4u];
    const uint32_t path_segments = LAB10_SCRIPTED_PATH_WAYPOINTS - 1u;
    const uint32_t last_point = LAB10_UI_SCRIPTED_POINTS_PER_STROKE - 1u;
    const uint32_t scaled = (uint32_t)point_index * path_segments * 100u /
                            last_point;
    uint32_t segment = scaled / 100u;
    if (segment >= path_segments) {
        segment = path_segments - 1u;
    }
    const uint32_t local = scaled - segment * 100u;
    const int16_t *start = path[segment];
    const int16_t *end = path[segment + 1u];
    const int32_t offset_cycle = (int32_t)(stroke_index / 4u);
    const int32_t offset_x = offset_cycle * 2 - 4;
    const int32_t offset_y = 4 - offset_cycle * 2;
    int32_t point_x = start[0] +
                      ((int32_t)(end[0] - start[0]) * (int32_t)local) / 100 +
                      offset_x;
    int32_t point_y = start[1] +
                      ((int32_t)(end[1] - start[1]) * (int32_t)local) / 100 +
                      offset_y;
    if (point_x < 1) {
        point_x = 1;
    } else if (point_x > LAB10_CANVAS_WIDTH - 2) {
        point_x = LAB10_CANVAS_WIDTH - 2;
    }
    if (point_y < 1) {
        point_y = 1;
    } else if (point_y > LAB10_CANVAS_HEIGHT - 2) {
        point_y = LAB10_CANVAS_HEIGHT - 2;
    }
    *x = (int16_t)point_x;
    *y = (int16_t)point_y;
}

static void scripted_advance(lab10_ui_scripted_schedule_t *schedule,
                             uint64_t now_us,
                             lab10_ui_scripted_action_t completed_action)
{
    uint64_t delay = SCRIPTED_CADENCE_US[schedule->cadence_index % LAB10_SCRIPTED_CADENCE_PATTERN_LENGTH];
    ++schedule->cadence_index;
    if (completed_action == LAB10_UI_SCRIPTED_ACTION_UP) {
        delay += LAB10_UI_SCRIPTED_PEN_UP_GAP_US;
    } else if (completed_action ==
               LAB10_UI_SCRIPTED_ACTION_PALETTE_CHANGE) {
        delay += LAB10_UI_SCRIPTED_PAUSE_US;
    }
    schedule->next_event_timestamp_us += delay;

    /* A delayed UI tick never triggers a catch-up burst. Preserve boundary
     * actions and restart this one cadence interval from the observed tick.
     */
    if (schedule->next_event_timestamp_us <= now_us) {
        schedule->next_event_timestamp_us = now_us +
                                            SCRIPTED_CADENCE_US[schedule->cadence_index %
                                                                LAB10_SCRIPTED_CADENCE_PATTERN_LENGTH];
    }
}

lab10_ui_scripted_poll_result_t lab10_ui_scripted_schedule_poll(
    lab10_ui_scripted_schedule_t *schedule, uint64_t now_us,
    lab10_ui_scripted_capture_state_t capture_state,
    const lab10_ui_scripted_capture_summary_t *capture_summary,
    lab10_ui_scripted_event_t *event)
{
    if (schedule == NULL || !schedule->initialized || capture_summary == NULL ||
        event == NULL || schedule->terminal_reported) {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    ++schedule->poll_count;
    if (schedule->poll_timestamp_valid &&
        now_us >= schedule->last_poll_timestamp_us) {
        const uint64_t poll_gap_us = now_us - schedule->last_poll_timestamp_us;
        if (poll_gap_us > schedule->max_poll_gap_us) {
            schedule->max_poll_gap_us = poll_gap_us;
        }
    }
    schedule->last_poll_timestamp_us = now_us;
    schedule->poll_timestamp_valid = true;

    const uint64_t elapsed_us =
        now_us >= schedule->capture_start_timestamp_us
            ? now_us - schedule->capture_start_timestamp_us
            : 0u;
    if (capture_state != LAB10_UI_SCRIPTED_CAPTURE_ACTIVE) {
        schedule->terminal_reported = true;
        if (capture_state == LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP &&
            elapsed_us >= schedule->duration_cap_us && schedule->down_emitted &&
            capture_summary->accepted_point_count >=
                LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS &&
            capture_summary->accepted_point_count <=
                LAB10_UI_SCRIPTED_MAX_COMPLETION_POINTS &&
            capture_summary->accepted_point_count <
                schedule->accepted_point_cap &&
            capture_summary->stroke_count >=
                LAB10_UI_SCRIPTED_MIN_COMPLETION_STROKES &&
            capture_summary->stroke_count <=
                LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES &&
            capture_summary->color_change_count >=
                LAB10_UI_SCRIPTED_MIN_COMPLETION_COLOR_CHANGES &&
            capture_summary->color_change_count <=
                LAB10_UI_SCRIPTED_MAX_COMPLETION_COLOR_CHANGES &&
            capture_summary->stroke_count ==
                capture_summary->color_change_count + 1u &&
            (capture_summary->accepted_color_mask &
             LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK) ==
                LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK &&
            capture_summary->trace_clean) {
            return LAB10_UI_SCRIPTED_POLL_COMPLETE;
        }
        return LAB10_UI_SCRIPTED_POLL_INTERRUPTED;
    }

    if (capture_summary->accepted_point_count >= schedule->accepted_point_cap) {
        schedule->terminal_reported = true;
        return LAB10_UI_SCRIPTED_POLL_INTERRUPTED;
    }
    if (elapsed_us >= schedule->duration_cap_us ||
        schedule->next_action == LAB10_UI_SCRIPTED_ACTION_FINISHED) {
        /* Capture polling owns the terminal duration-cap transition. */
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }
    if (now_us < schedule->next_event_timestamp_us) {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    const uint64_t event_deadline_lateness_us =
        now_us - schedule->next_event_timestamp_us;
    if (event_deadline_lateness_us > 0u) {
        ++schedule->late_event_count;
        if (event_deadline_lateness_us >
            schedule->max_event_deadline_lateness_us) {
            schedule->max_event_deadline_lateness_us =
                event_deadline_lateness_us;
        }
    }

    const lab10_ui_scripted_action_t action = schedule->next_action;
    int16_t x = 0;
    int16_t y = 0;
    if (action == LAB10_UI_SCRIPTED_ACTION_DOWN) {
        scripted_path_coordinates(schedule->stroke_index, 0u, &x, &y);
        *event = (lab10_ui_scripted_event_t){
            .phase = LAB10_PHASE_DOWN,
            .display_x = x,
            .display_y = y,
            .touch_id = 0u,
        };
        schedule->down_emitted = true;
        schedule->stroke_point_count = 1u;
        schedule->next_action = LAB10_UI_SCRIPTED_ACTION_MOVE;
    } else if (action == LAB10_UI_SCRIPTED_ACTION_MOVE) {
        scripted_path_coordinates(schedule->stroke_index,
                                  schedule->stroke_point_count, &x, &y);
        *event = (lab10_ui_scripted_event_t){
            .phase = LAB10_PHASE_MOVE,
            .display_x = x,
            .display_y = y,
            .touch_id = 0u,
        };
        ++schedule->stroke_point_count;
        if (schedule->stroke_point_count ==
            LAB10_UI_SCRIPTED_POINTS_PER_STROKE) {
            schedule->next_action = schedule->stroke_index + 1u <
                                            LAB10_UI_SCRIPTED_STROKE_COUNT
                                        ? LAB10_UI_SCRIPTED_ACTION_UP
                                        : LAB10_UI_SCRIPTED_ACTION_FINISHED;
        }
    } else if (action == LAB10_UI_SCRIPTED_ACTION_UP) {
        scripted_path_coordinates(
            schedule->stroke_index,
            LAB10_UI_SCRIPTED_POINTS_PER_STROKE - 1u, &x, &y);
        *event = (lab10_ui_scripted_event_t){
            .phase = LAB10_PHASE_UP,
            .display_x = x,
            .display_y = y,
            .touch_id = 0u,
        };
        schedule->next_action = LAB10_UI_SCRIPTED_ACTION_PALETTE_CHANGE;
    } else if (action == LAB10_UI_SCRIPTED_ACTION_PALETTE_CHANGE) {
        ++schedule->stroke_index;
        const uint8_t color = SCRIPTED_STROKE_COLORS[schedule->stroke_index];
        const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry(color);
        *event = (lab10_ui_scripted_event_t){
            .phase = LAB10_PHASE_PALETTE_CHANGE,
            .display_x = swatch->center_x,
            .display_y = swatch->center_y,
            .touch_id = 0u,
        };
        schedule->stroke_point_count = 0u;
        schedule->next_action = LAB10_UI_SCRIPTED_ACTION_DOWN;
    } else {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    ++schedule->event_count;
    scripted_advance(schedule, now_us, action);
    return LAB10_UI_SCRIPTED_POLL_EVENT;
}

size_t lab10_canvas_memory_attempt_count(void)
{
    return 2u;
}

lab10_canvas_memory_region_t lab10_canvas_memory_attempt(size_t index)
{
    return index == 0u ? LAB10_CANVAS_MEMORY_PSRAM
                       : LAB10_CANVAS_MEMORY_INTERNAL;
}

lab10_ui_circle_transition_t lab10_ui_fab_entry_transition(
    int16_t origin_x, int16_t origin_y)
{
    return (lab10_ui_circle_transition_t){
        .origin_x = origin_x,
        .origin_y = origin_y,
        .radius_start = LAB10_FAB_RADIUS_START,
        .radius_end = LAB10_FAB_RADIUS_FULL,
    };
}

lab10_ui_circle_transition_t lab10_ui_fab_exit_transition(
    int16_t origin_x, int16_t origin_y)
{
    const lab10_ui_circle_transition_t entry =
        lab10_ui_fab_entry_transition(origin_x, origin_y);
    return (lab10_ui_circle_transition_t){
        .origin_x = entry.origin_x,
        .origin_y = entry.origin_y,
        .radius_start = entry.radius_end,
        .radius_end = entry.radius_start,
    };
}

lab10_ui_circle_transition_t lab10_ui_ring_entry_transition(
    int16_t origin_x, int16_t origin_y)
{
    return (lab10_ui_circle_transition_t){
        .origin_x = origin_x,
        .origin_y = origin_y,
        .radius_start = LAB10_RING_RADIUS_START,
        .radius_end = LAB10_RING_RADIUS_COVERAGE,
    };
}

lab10_ui_circle_transition_t lab10_ui_ring_exit_transition(
    int16_t origin_x, int16_t origin_y)
{
    const lab10_ui_circle_transition_t entry =
        lab10_ui_ring_entry_transition(origin_x, origin_y);
    return (lab10_ui_circle_transition_t){
        .origin_x = entry.origin_x,
        .origin_y = entry.origin_y,
        .radius_start = entry.radius_end,
        .radius_end = entry.radius_start,
    };
}

lab10_ui_ring_reveal_clip_t lab10_ui_ring_reveal_clip(int16_t origin_y,
                                                      int32_t outer_radius)
{
    const int32_t inner_radius = outer_radius - LAB10_RING_BAND_WIDTH;
    int32_t y_line = (int32_t)origin_y - inner_radius;
    if (y_line < 0) {
        y_line = 0;
    }

    const bool hidden = y_line >= LAB10_LCD_HEIGHT;
    return (lab10_ui_ring_reveal_clip_t){
        .x = 0,
        .y = y_line,
        .width = LAB10_LCD_WIDTH,
        .height = hidden ? 0 : LAB10_LCD_HEIGHT - y_line,
        .content_offset_y = -y_line,
        .hidden = hidden,
    };
}

static void transition(lab10_ui_model_t *model, lab10_screen_state_t next,
                       bool full_screen)
{
    if (model == NULL || model->state == next) {
        return;
    }
    const lab10_screen_state_t previous = model->state;
    model->state = next;
    ++model->transition_generation;
    ++model->invalidation.transition_count;
    if (full_screen) {
        ++model->invalidation.full_screen_invalidation_count;
    }
    if (model->hooks.on_transition != NULL) {
        model->hooks.on_transition(previous, next, model->transition_generation,
                                   model->hooks.context);
    }
}

lab10_touch_route_t lab10_ui_route_touch(const lab10_ui_model_t *model,
                                         int16_t x, int16_t y)
{
    (void)model;
    if (!lab10_display_contains(x, y)) {
        return LAB10_ROUTE_OUTSIDE;
    }
    if (lab10_canvas_contains(x, y)) {
        return LAB10_ROUTE_CANVAS;
    }
    return lab10_palette_hit_test(x, y) >= 0 ? LAB10_ROUTE_PALETTE
                                             : LAB10_ROUTE_RAIL_REJECTED;
}

lab10_touch_route_t lab10_ui_handle_touch(lab10_ui_model_t *model,
                                          lab10_touch_phase_t phase,
                                          int16_t x, int16_t y,
                                          uint8_t touch_id)
{
    if (model == NULL) {
        return LAB10_ROUTE_OUTSIDE;
    }
    const lab10_touch_route_t route = lab10_ui_route_touch(model, x, y);
    lab10_ui_touch_event_t event = {
        .route = route,
        .phase = phase,
        .display_x = x,
        .display_y = y,
        .touch_id = touch_id,
        .color_index = model->selected_color,
    };

    if (route == LAB10_ROUTE_PALETTE &&
        (phase == LAB10_PHASE_DOWN || phase == LAB10_PHASE_PALETTE_CHANGE)) {
        const int selected = lab10_palette_hit_test(x, y);
        if (selected >= 0 && selected != model->selected_color) {
            const uint8_t previous = model->selected_color;
            model->selected_color = (uint8_t)selected;
            ++model->invalidation.palette_update_count;
            model->invalidation.palette_invalidation_count += 2;
            event.color_index = model->selected_color;
            if (model->hooks.on_palette_changed != NULL) {
                model->hooks.on_palette_changed(previous, model->selected_color,
                                                model->hooks.context);
            }
        }
    } else if (route == LAB10_ROUTE_CANVAS && phase != LAB10_PHASE_NONE) {
        ++model->invalidation.canvas_invalidation_count;
    }

    if (model->hooks.on_touch_routed != NULL) {
        model->hooks.on_touch_routed(&event, model->hooks.context);
    }
    return route;
}

uint32_t lab10_ui_enter_fab_bloom(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return 0;
    }
    if (model->state != LAB10_SCREEN_IDLE) {
        return model->transition_generation;
    }
    transition(model, LAB10_SCREEN_ENTRY_FAB_BLOOM, true);
    return model->transition_generation;
}

uint32_t lab10_ui_enter_ring_wipe(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return 0;
    }
    if (model->state != LAB10_SCREEN_IDLE) {
        return model->transition_generation;
    }
    transition(model, LAB10_SCREEN_ENTRY_RING_WIPE, true);
    return model->transition_generation;
}

uint32_t lab10_ui_finish_entry(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return 0;
    }
    if (model->state == LAB10_SCREEN_ENTRY_FAB_BLOOM ||
        model->state == LAB10_SCREEN_ENTRY_RING_WIPE) {
        transition(model, LAB10_SCREEN_RECORDING, true);
    }
    return model->transition_generation;
}

uint32_t lab10_ui_present_outcome(lab10_ui_model_t *model,
                                  lab10_screen_state_t outcome)
{
    if (model == NULL || !is_outcome(outcome)) {
        return model != NULL ? model->transition_generation : 0;
    }
    transition(model, outcome, true);
    return model->transition_generation;
}

uint32_t lab10_ui_exit_reverse(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return 0;
    }
    if (model->state != LAB10_SCREEN_RECORDING && !is_outcome(model->state)) {
        return model->transition_generation;
    }
    transition(model, LAB10_SCREEN_EXIT_REVERSE, true);
    return model->transition_generation;
}

uint32_t lab10_ui_finish_exit(lab10_ui_model_t *model)
{
    if (model == NULL) {
        return 0;
    }
    if (model->state == LAB10_SCREEN_EXIT_REVERSE) {
        transition(model, LAB10_SCREEN_IDLE, true);
    }
    return model->transition_generation;
}
