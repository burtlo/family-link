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
    LAB10_SCRIPTED_PATH_COLUMNS = 64,
    LAB10_SCRIPTED_PATH_ORIGIN = 16,
    LAB10_SCRIPTED_PATH_STEP = 3,
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

bool lab10_ui_scripted_schedule_init(
    lab10_ui_scripted_schedule_t *schedule, uint64_t capture_start_timestamp_us,
    uint64_t duration_cap_us, uint32_t accepted_point_cap,
    uint64_t move_interval_us)
{
    if (schedule == NULL || duration_cap_us == 0u ||
        accepted_point_cap < LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS ||
        move_interval_us == 0u || move_interval_us > duration_cap_us ||
        capture_start_timestamp_us > UINT64_MAX - duration_cap_us) {
        return false;
    }
    *schedule = (lab10_ui_scripted_schedule_t){
        .capture_start_timestamp_us = capture_start_timestamp_us,
        .duration_cap_us = duration_cap_us,
        .move_interval_us = move_interval_us,
        .accepted_point_cap = accepted_point_cap,
        .next_path_point_index = 0u,
        .initialized = true,
    };
    return true;
}

static void scripted_path_coordinates(uint32_t point_index, int16_t *x,
                                      int16_t *y)
{
    const uint32_t row = point_index / LAB10_SCRIPTED_PATH_COLUMNS;
    const uint32_t column = point_index % LAB10_SCRIPTED_PATH_COLUMNS;
    const uint32_t path_column =
        (row & 1u) == 0u ? column
                         : LAB10_SCRIPTED_PATH_COLUMNS - 1u - column;
    *x = (int16_t)(LAB10_SCRIPTED_PATH_ORIGIN +
                   (int32_t)path_column * LAB10_SCRIPTED_PATH_STEP);
    *y = (int16_t)(LAB10_SCRIPTED_PATH_ORIGIN +
                   (int32_t)row * LAB10_SCRIPTED_PATH_STEP);
}

lab10_ui_scripted_poll_result_t lab10_ui_scripted_schedule_poll(
    lab10_ui_scripted_schedule_t *schedule, uint64_t now_us,
    lab10_ui_scripted_capture_state_t capture_state,
    uint32_t accepted_point_count, lab10_ui_scripted_event_t *event)
{
    if (schedule == NULL || !schedule->initialized || event == NULL ||
        schedule->terminal_reported) {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    const uint64_t elapsed_us =
        now_us >= schedule->capture_start_timestamp_us
            ? now_us - schedule->capture_start_timestamp_us
            : 0u;
    if (capture_state != LAB10_UI_SCRIPTED_CAPTURE_ACTIVE) {
        schedule->terminal_reported = true;
        if (capture_state == LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP &&
            elapsed_us >= schedule->duration_cap_us && schedule->down_emitted &&
            accepted_point_count >=
                LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS &&
            accepted_point_count < schedule->accepted_point_cap) {
            return LAB10_UI_SCRIPTED_POLL_COMPLETE;
        }
        return LAB10_UI_SCRIPTED_POLL_INTERRUPTED;
    }

    if (accepted_point_count >= schedule->accepted_point_cap) {
        schedule->terminal_reported = true;
        return LAB10_UI_SCRIPTED_POLL_INTERRUPTED;
    }
    if (elapsed_us >= schedule->duration_cap_us) {
        /* Capture polling owns the terminal duration-cap transition. */
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    if (!schedule->down_emitted) {
        int16_t x = 0;
        int16_t y = 0;
        scripted_path_coordinates(0u, &x, &y);
        *event = (lab10_ui_scripted_event_t){
            .phase = LAB10_PHASE_DOWN,
            .display_x = x,
            .display_y = y,
            .touch_id = 0u,
        };
        schedule->down_emitted = true;
        schedule->next_path_point_index = 1u;
        schedule->next_move_timestamp_us =
            schedule->capture_start_timestamp_us + schedule->move_interval_us;
        return LAB10_UI_SCRIPTED_POLL_EVENT;
    }

    if (now_us < schedule->next_move_timestamp_us) {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    /* Skip missed time slots instead of emitting a catch-up burst after a
     * delayed UI tick. Every emitted move remains tied to one absolute
     * 44 ms slot, so scheduler latency cannot consume the point budget early.
     */
    const uint64_t due_move_index = elapsed_us / schedule->move_interval_us;
    if (due_move_index < schedule->next_path_point_index) {
        return LAB10_UI_SCRIPTED_POLL_WAIT;
    }

    int16_t x = 0;
    int16_t y = 0;
    scripted_path_coordinates((uint32_t)due_move_index, &x, &y);
    *event = (lab10_ui_scripted_event_t){
        .phase = LAB10_PHASE_MOVE,
        .display_x = x,
        .display_y = y,
        .touch_id = 0u,
    };
    schedule->next_path_point_index = (uint32_t)due_move_index + 1u;
    schedule->next_move_timestamp_us =
        schedule->capture_start_timestamp_us +
        (due_move_index + 1u) * schedule->move_interval_us;
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
