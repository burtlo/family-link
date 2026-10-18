#pragma once

/*
 * LAB-10 L10-02 UI contract.
 *
 * This module is deliberately LVGL-free. It owns geometry, hit routing,
 * palette selection, transition state, and bounded invalidation accounting so
 * those rules can be tested on the host and reused by the LVGL presentation.
 * All calls are intended to run on the UI task. No worker retains an LVGL
 * handle or calls this module after handing an event to the UI task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAB10_LCD_WIDTH 320
#define LAB10_LCD_HEIGHT 240
#define LAB10_CANVAS_WIDTH 240
#define LAB10_CANVAS_HEIGHT 240
#define LAB10_RAIL_X 240
#define LAB10_RAIL_WIDTH 80
#define LAB10_LOGICAL_WIDTH 120
#define LAB10_LOGICAL_HEIGHT 120

/* The FAB lives below the LCD. Entry grows from this red-button origin. */
#define LAB10_FAB_ORIGIN_X (LAB10_LCD_WIDTH / 2)
#define LAB10_FAB_ORIGIN_Y (LAB10_LCD_HEIGHT + 32)
#define LAB10_FAB_RADIUS_START 22
#define LAB10_FAB_RADIUS_FULL 360

#define LAB10_UI_SCRIPTED_MOVE_INTERVAL_US UINT64_C(44000)
#define LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS 3500u

#define LAB10_SWATCH_COUNT 6
#define LAB10_SWATCH_DIAMETER 44
#define LAB10_SWATCH_RADIUS 22
#define LAB10_SWATCH_PITCH 36
#define LAB10_SWATCH_ZIG_ZAG 11
#define LAB10_SWATCH_BOTTOM_MARGIN 10

typedef enum {
    LAB10_COLOR_BACKGROUND = 0,
    LAB10_COLOR_WHITE = 1,
    LAB10_COLOR_GOLD = 2,
    LAB10_COLOR_GREEN = 3,
    LAB10_COLOR_RED = 4,
    LAB10_COLOR_BLUE = 5,
} lab10_color_index_t;

typedef enum {
    LAB10_ROUTE_OUTSIDE = 0,
    LAB10_ROUTE_CANVAS,
    LAB10_ROUTE_PALETTE,
    LAB10_ROUTE_RAIL_REJECTED,
} lab10_touch_route_t;

typedef enum {
    LAB10_PHASE_NONE = 0,
    LAB10_PHASE_DOWN,
    LAB10_PHASE_MOVE,
    LAB10_PHASE_UP,
    LAB10_PHASE_LEAVE,
    LAB10_PHASE_PALETTE_CHANGE,
} lab10_touch_phase_t;

typedef enum {
    LAB10_SCREEN_IDLE = 0,
    LAB10_SCREEN_ENTRY_FAB_BLOOM,
    LAB10_SCREEN_ENTRY_RING_WIPE,
    LAB10_SCREEN_RECORDING,
    LAB10_SCREEN_STOPPED,
    LAB10_SCREEN_CANCELLED,
    LAB10_SCREEN_FAILED,
    LAB10_SCREEN_COMPLETED,
    LAB10_SCREEN_EXIT_REVERSE,
} lab10_screen_state_t;

typedef struct {
    int16_t center_x;
    int16_t center_y;
    uint32_t color_rgb;
} lab10_swatch_geometry_t;

typedef struct {
    lab10_touch_route_t route;
    lab10_touch_phase_t phase;
    int16_t display_x;
    int16_t display_y;
    uint8_t touch_id;
    uint8_t color_index;
} lab10_ui_touch_event_t;

typedef struct {
    uint32_t palette_update_count;
    uint32_t palette_invalidation_count;
    uint32_t canvas_invalidation_count;
    uint32_t full_screen_invalidation_count;
    uint32_t transition_count;
} lab10_ui_invalidation_stats_t;

typedef struct {
    lab10_touch_phase_t phase;
    int16_t display_x;
    int16_t display_y;
    uint8_t touch_id;
} lab10_ui_scripted_event_t;

typedef struct {
    uint64_t capture_start_timestamp_us;
    uint64_t duration_cap_us;
    uint64_t move_interval_us;
    uint64_t next_move_timestamp_us;
    uint32_t accepted_point_cap;
    uint32_t next_path_point_index;
    bool initialized;
    bool down_emitted;
    bool terminal_reported;
} lab10_ui_scripted_schedule_t;

typedef enum {
    LAB10_UI_SCRIPTED_CAPTURE_ACTIVE = 0,
    LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
    LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL,
} lab10_ui_scripted_capture_state_t;

typedef enum {
    LAB10_UI_SCRIPTED_POLL_WAIT = 0,
    LAB10_UI_SCRIPTED_POLL_EVENT,
    LAB10_UI_SCRIPTED_POLL_COMPLETE,
    LAB10_UI_SCRIPTED_POLL_INTERRUPTED,
} lab10_ui_scripted_poll_result_t;

typedef struct {
    int16_t origin_x;
    int16_t origin_y;
    int32_t radius_start;
    int32_t radius_end;
} lab10_ui_circle_transition_t;

typedef enum {
    LAB10_CANVAS_MEMORY_PSRAM = 0,
    LAB10_CANVAS_MEMORY_INTERNAL,
} lab10_canvas_memory_region_t;

typedef struct {
    void (*on_transition)(lab10_screen_state_t from, lab10_screen_state_t to,
                          uint32_t generation, void *context);
    void (*on_palette_changed)(uint8_t previous_index, uint8_t next_index,
                               void *context);
    void (*on_touch_routed)(const lab10_ui_touch_event_t *event, void *context);
    void *context;
} lab10_ui_hooks_t;

typedef struct {
    lab10_screen_state_t state;
    uint8_t selected_color;
    uint32_t transition_generation;
    lab10_ui_hooks_t hooks;
    lab10_ui_invalidation_stats_t invalidation;
} lab10_ui_model_t;

const lab10_swatch_geometry_t *lab10_swatch_geometry(uint8_t index);
uint32_t lab10_color_rgb(uint8_t index);
bool lab10_display_contains(int16_t x, int16_t y);
bool lab10_canvas_contains(int16_t x, int16_t y);
bool lab10_rail_contains(int16_t x, int16_t y);
int lab10_palette_hit_test(int16_t x, int16_t y);
void lab10_map_to_logical(int16_t display_x, int16_t display_y,
                          uint8_t *logical_x, uint8_t *logical_y);

void lab10_ui_model_init(lab10_ui_model_t *model);
void lab10_ui_set_hooks(lab10_ui_model_t *model, const lab10_ui_hooks_t *hooks);
lab10_screen_state_t lab10_ui_state(const lab10_ui_model_t *model);
uint8_t lab10_ui_selected_color(const lab10_ui_model_t *model);
const lab10_ui_invalidation_stats_t *lab10_ui_invalidation(
    const lab10_ui_model_t *model);

/* UI-task-only deterministic path; it does not claim hardware touch input. */
bool lab10_ui_scripted_schedule_init(
    lab10_ui_scripted_schedule_t *schedule, uint64_t capture_start_timestamp_us,
    uint64_t duration_cap_us, uint32_t accepted_point_cap,
    uint64_t move_interval_us);
lab10_ui_scripted_poll_result_t lab10_ui_scripted_schedule_poll(
    lab10_ui_scripted_schedule_t *schedule, uint64_t now_us,
    lab10_ui_scripted_capture_state_t capture_state,
    uint32_t accepted_point_count, lab10_ui_scripted_event_t *event);

size_t lab10_canvas_memory_attempt_count(void);
lab10_canvas_memory_region_t lab10_canvas_memory_attempt(size_t index);

lab10_ui_circle_transition_t lab10_ui_fab_entry_transition(
    int16_t origin_x, int16_t origin_y);
lab10_ui_circle_transition_t lab10_ui_fab_exit_transition(
    int16_t origin_x, int16_t origin_y);

lab10_touch_route_t lab10_ui_route_touch(const lab10_ui_model_t *model,
                                         int16_t x, int16_t y);
lab10_touch_route_t lab10_ui_handle_touch(lab10_ui_model_t *model,
                                           lab10_touch_phase_t phase,
                                           int16_t x, int16_t y,
                                           uint8_t touch_id);

uint32_t lab10_ui_enter_fab_bloom(lab10_ui_model_t *model);
uint32_t lab10_ui_enter_ring_wipe(lab10_ui_model_t *model);
uint32_t lab10_ui_finish_entry(lab10_ui_model_t *model);
uint32_t lab10_ui_present_outcome(lab10_ui_model_t *model,
                                  lab10_screen_state_t outcome);
uint32_t lab10_ui_exit_reverse(lab10_ui_model_t *model);
uint32_t lab10_ui_finish_exit(lab10_ui_model_t *model);
