#include "lab10_ui.h"

#include <assert.h>
#include <stdio.h>

static void test_geometry(void)
{
    static const int expected_x[] = {269, 291, 269, 291, 269, 291};
    static const int expected_y[] = {208, 172, 136, 100, 64, 28};
    for (unsigned index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry(index);
        assert(swatch != NULL);
        assert(swatch->center_x == expected_x[index]);
        assert(swatch->center_y == expected_y[index]);
    }
    assert(lab10_display_contains(0, 0));
    assert(lab10_display_contains(319, 239));
    assert(!lab10_display_contains(320, 239));
    assert(!lab10_display_contains(319, 240));
    assert(lab10_canvas_contains(239, 239));
    assert(!lab10_canvas_contains(240, 239));
    assert(lab10_rail_contains(240, 0));
    assert(lab10_rail_contains(319, 239));
    assert(!lab10_rail_contains(320, 239));
}

static void test_palette_and_routing(void)
{
    lab10_ui_model_t model;
    lab10_ui_model_init(&model);
    assert(lab10_ui_selected_color(&model) == LAB10_COLOR_WHITE);
    for (int index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry((unsigned)index);
        assert(lab10_palette_hit_test(swatch->center_x, swatch->center_y) == index);
        assert(lab10_palette_hit_test(swatch->center_x + 22, swatch->center_y) == index);
        assert(lab10_palette_hit_test(swatch->center_x + 23, swatch->center_y) == -1);
        assert(lab10_ui_route_touch(&model, swatch->center_x, swatch->center_y) ==
               LAB10_ROUTE_PALETTE);
    }
    /* The LVGL swatch object is rectangular, but its model hit region is a circle. */
    assert(lab10_palette_hit_test(247, 186) == -1);
    assert(lab10_ui_route_touch(&model, 247, 186) == LAB10_ROUTE_RAIL_REJECTED);
    assert(lab10_ui_handle_touch(&model, LAB10_PHASE_PALETTE_CHANGE, 247, 186, 0) ==
           LAB10_ROUTE_RAIL_REJECTED);
    assert(lab10_ui_selected_color(&model) == LAB10_COLOR_WHITE);
    assert(lab10_ui_route_touch(&model, 239, 100) == LAB10_ROUTE_CANVAS);
    assert(lab10_ui_route_touch(&model, 240, 100) == LAB10_ROUTE_RAIL_REJECTED);
    assert(lab10_ui_route_touch(&model, 320, 100) == LAB10_ROUTE_OUTSIDE);
    assert(lab10_ui_route_touch(&model, 100, 240) == LAB10_ROUTE_OUTSIDE);

    const lab10_ui_invalidation_stats_t *stats = lab10_ui_invalidation(&model);
    assert(stats->full_screen_invalidation_count == 0);
    assert(lab10_ui_handle_touch(&model, LAB10_PHASE_DOWN, 269, 64, 0) ==
           LAB10_ROUTE_PALETTE);
    assert(lab10_ui_selected_color(&model) == LAB10_COLOR_RED);
    assert(stats->palette_update_count == 1);
    assert(stats->palette_invalidation_count == 2);
    assert(stats->full_screen_invalidation_count == 0);
    assert(lab10_ui_handle_touch(&model, LAB10_PHASE_DOWN, 240, 64, 0) ==
           LAB10_ROUTE_RAIL_REJECTED);
    assert(stats->full_screen_invalidation_count == 0);
}

static void test_mapping_and_transitions(void)
{
    lab10_ui_model_t model;
    lab10_ui_model_init(&model);
    uint8_t x = 0;
    uint8_t y = 0;
    lab10_map_to_logical(0, 0, &x, &y);
    assert(x == 0 && y == 0);
    lab10_map_to_logical(239, 239, &x, &y);
    assert(x == 119 && y == 119);
    lab10_map_to_logical(-2, -4, &x, &y);
    assert(x == 0 && y == 0);

    assert(lab10_ui_enter_fab_bloom(&model) == 1);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_ENTRY_FAB_BLOOM);
    assert(lab10_ui_enter_ring_wipe(&model) == 1);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_ENTRY_FAB_BLOOM);
    assert(lab10_ui_finish_entry(&model) == 2);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_RECORDING);
    const lab10_screen_state_t outcomes[] = {
        LAB10_SCREEN_STOPPED,
        LAB10_SCREEN_CANCELLED,
        LAB10_SCREEN_FAILED,
        LAB10_SCREEN_COMPLETED,
    };
    for (unsigned index = 0; index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        assert(lab10_ui_present_outcome(&model, outcomes[index]) == 3 + index);
        assert(lab10_ui_state(&model) == outcomes[index]);
    }
    assert(lab10_ui_exit_reverse(&model) == 7);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_EXIT_REVERSE);
    assert(lab10_ui_finish_exit(&model) == 8);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_IDLE);

    assert(lab10_ui_enter_ring_wipe(&model) == 9);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_ENTRY_RING_WIPE);
    assert(lab10_ui_finish_entry(&model) == 10);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_RECORDING);
    assert(lab10_ui_exit_reverse(&model) == 11);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_EXIT_REVERSE);
    /* A button press during reverse exit cannot strand a second entry animation. */
    assert(lab10_ui_enter_ring_wipe(&model) == 11);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_EXIT_REVERSE);
    assert(lab10_ui_finish_exit(&model) == 12);
    assert(lab10_ui_state(&model) == LAB10_SCREEN_IDLE);
}

static void test_canvas_memory_policy_contract(void)
{
    assert(lab10_canvas_memory_attempt_count() == 2);
    assert(lab10_canvas_memory_attempt(0) == LAB10_CANVAS_MEMORY_PSRAM);
    assert(lab10_canvas_memory_attempt(1) == LAB10_CANVAS_MEMORY_INTERNAL);
}

static void test_fab_transition_geometry_contract(void)
{
    const lab10_ui_circle_transition_t entry = lab10_ui_fab_entry_transition(
        LAB10_FAB_ORIGIN_X, LAB10_FAB_ORIGIN_Y);
    const lab10_ui_circle_transition_t exit = lab10_ui_fab_exit_transition(
        LAB10_FAB_ORIGIN_X, LAB10_FAB_ORIGIN_Y);
    assert(entry.origin_x == exit.origin_x);
    assert(entry.origin_y == exit.origin_y);
    assert(entry.origin_y > LAB10_LCD_HEIGHT);
    assert(entry.radius_start < entry.radius_end);
    assert(exit.radius_start == entry.radius_end);
    assert(exit.radius_end == entry.radius_start);
}

static void test_ring_transition_geometry_contract(void)
{
    const lab10_ui_circle_transition_t entry = lab10_ui_ring_entry_transition(
        LAB10_RING_ORIGIN_X, LAB10_RING_ORIGIN_Y);
    const lab10_ui_circle_transition_t exit = lab10_ui_ring_exit_transition(
        LAB10_RING_ORIGIN_X, LAB10_RING_ORIGIN_Y);
    assert(entry.origin_x == 160);
    assert(entry.origin_y == 282);
    assert(entry.origin_x == exit.origin_x);
    assert(entry.origin_y == exit.origin_y);
    assert(entry.origin_y > LAB10_LCD_HEIGHT);
    assert(entry.radius_start == 42);
    assert(entry.radius_end == 365);
    assert(exit.radius_start == entry.radius_end);
    assert(exit.radius_end == entry.radius_start);
    assert(LAB10_RING_DURATION_MS == 600);
}

static void assert_ring_reveal_clip(int32_t outer_radius, int32_t expected_y,
                                    int32_t expected_height, bool hidden)
{
    const lab10_ui_ring_reveal_clip_t clip = lab10_ui_ring_reveal_clip(
        LAB10_RING_ORIGIN_Y, outer_radius);
    assert(clip.x == 0);
    assert(clip.width == LAB10_LCD_WIDTH);
    assert(clip.y == expected_y);
    assert(clip.height == expected_height);
    assert(clip.content_offset_y == -expected_y);
    assert(clip.hidden == hidden);
    if (!hidden) {
        assert(clip.y + clip.height == LAB10_LCD_HEIGHT);
        assert(clip.y + clip.content_offset_y == 0);
    }
}

static void test_ring_reveal_entry_and_reverse_geometry(void)
{
    const lab10_ui_circle_transition_t entry = lab10_ui_ring_entry_transition(
        LAB10_RING_ORIGIN_X, LAB10_RING_ORIGIN_Y);
    const lab10_ui_circle_transition_t exit = lab10_ui_ring_exit_transition(
        LAB10_RING_ORIGIN_X, LAB10_RING_ORIGIN_Y);

    /* Entry: hidden below the LCD, partial viewport, then full viewport. */
    assert_ring_reveal_clip(entry.radius_start, 258, 0, true);
    assert_ring_reveal_clip(203, 97, 143, false);
    assert_ring_reveal_clip(entry.radius_end, 0, LAB10_LCD_HEIGHT, false);

    /* Reverse traverses the same clip geometry from full back to hidden. */
    assert_ring_reveal_clip(exit.radius_start, 0, LAB10_LCD_HEIGHT, false);
    assert_ring_reveal_clip(203, 97, 143, false);
    assert_ring_reveal_clip(exit.radius_end, 258, 0, true);
}

int main(void)
{
    test_geometry();
    test_palette_and_routing();
    test_mapping_and_transitions();
    test_canvas_memory_policy_contract();
    test_fab_transition_geometry_contract();
    test_ring_transition_geometry_contract();
    test_ring_reveal_entry_and_reverse_geometry();
    puts("LAB10_UI_NATIVE_TEST_PASS");
    return 0;
}
