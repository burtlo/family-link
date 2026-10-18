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

static const lab10_ui_scripted_event_t k_scripted_events[
    LAB10_UI_SCRIPTED_EVENT_COUNT] = {
        {LAB10_PHASE_PALETTE_CHANGE, 269, 64, 0},
        {LAB10_PHASE_DOWN, 18, 20, 0},
        {LAB10_PHASE_MOVE, 60, 44, 0},
        {LAB10_PHASE_MOVE, 96, 82, 0},
        {LAB10_PHASE_UP, 96, 82, 0},
        {LAB10_PHASE_PALETTE_CHANGE, 291, 28, 0},
        {LAB10_PHASE_DOWN, 204, 28, 0},
        {LAB10_PHASE_MOVE, 170, 62, 0},
        {LAB10_PHASE_MOVE, 142, 104, 0},
        {LAB10_PHASE_UP, 142, 104, 0},
        {LAB10_PHASE_PALETTE_CHANGE, 269, 136, 0},
        {LAB10_PHASE_DOWN, 32, 196, 0},
        {LAB10_PHASE_MOVE, 76, 164, 0},
        {LAB10_PHASE_MOVE, 128, 208, 0},
        {LAB10_PHASE_UP, 128, 208, 0},
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

size_t lab10_ui_scripted_event_count(void)
{
    return LAB10_UI_SCRIPTED_EVENT_COUNT;
}

const lab10_ui_scripted_event_t *lab10_ui_scripted_event(size_t index)
{
    return index < LAB10_UI_SCRIPTED_EVENT_COUNT ? &k_scripted_events[index]
                                                 : NULL;
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
