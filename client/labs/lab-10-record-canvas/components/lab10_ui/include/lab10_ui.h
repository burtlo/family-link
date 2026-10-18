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

/* Physical-button ring wipe follows the LAB10 hardware entry affordance. */
#define LAB10_RING_ORIGIN_X 160
#define LAB10_RING_ORIGIN_Y 282
#define LAB10_RING_RADIUS_START 42
#define LAB10_RING_RADIUS_COVERAGE 365
#define LAB10_RING_BAND_WIDTH 18
#define LAB10_RING_DURATION_MS 600

#define LAB10_UI_SCRIPTED_MOVE_INTERVAL_US UINT64_C(44000)
#define LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS 3400u
#define LAB10_UI_SCRIPTED_MAX_COMPLETION_POINTS 4095u
#define LAB10_UI_SCRIPTED_STROKE_COUNT 18u
#define LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES \
    LAB10_UI_SCRIPTED_STROKE_COUNT
#define LAB10_UI_SCRIPTED_MAX_COMPLETION_COLOR_CHANGES \
    (LAB10_UI_SCRIPTED_STROKE_COUNT - 1u)
#define LAB10_UI_SCRIPTED_POINTS_PER_STROKE 200u
#define LAB10_UI_SCRIPTED_CADENCE_MIN_US UINT64_C(36000)
#define LAB10_UI_SCRIPTED_CADENCE_MAX_US UINT64_C(52000)
#define LAB10_UI_SCRIPTED_CADENCE_PROFILE "scripted-cadence-36-36-36-40-40-v2"
#define LAB10_UI_SCRIPTED_PEN_UP_GAP_US UINT64_C(400000)
#define LAB10_UI_SCRIPTED_PAUSE_US UINT64_C(780000)
#define LAB10_UI_SCRIPTED_MIN_COMPLETION_STROKES 6u
#define LAB10_UI_SCRIPTED_MIN_COMPLETION_COLOR_CHANGES 5u

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

#define LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK \
    ((uint8_t)((1u << LAB10_COLOR_WHITE) | (1u << LAB10_COLOR_GOLD) | \
               (1u << LAB10_COLOR_GREEN) | (1u << LAB10_COLOR_RED) | \
               (1u << LAB10_COLOR_BLUE)))

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

typedef enum {
    LAB10_UI_TERMINAL_COMPLETED = 0,
    LAB10_UI_TERMINAL_STOPPED,
    LAB10_UI_TERMINAL_CANCELLED,
    LAB10_UI_TERMINAL_DURATION_CAP,
    LAB10_UI_TERMINAL_POINT_CAP,
    LAB10_UI_TERMINAL_QUEUE_OVERFLOW,
    LAB10_UI_TERMINAL_FAILED,
} lab10_ui_terminal_kind_t;

typedef struct {
    bool scripted_session;
    bool decision_resolved;
    bool completion_accepted;
} lab10_ui_scripted_qualification_t;

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
    uint32_t accepted_point_count;
    uint32_t stroke_count;
    uint32_t color_change_count;
    uint8_t accepted_color_mask;
    bool trace_clean;
} lab10_ui_scripted_capture_summary_t;

typedef enum {
    LAB10_UI_SCRIPTED_ACTION_DOWN = 0,
    LAB10_UI_SCRIPTED_ACTION_MOVE,
    LAB10_UI_SCRIPTED_ACTION_UP,
    LAB10_UI_SCRIPTED_ACTION_PALETTE_CHANGE,
    LAB10_UI_SCRIPTED_ACTION_FINISHED,
} lab10_ui_scripted_action_t;

typedef struct {
    uint64_t capture_start_timestamp_us;
    uint64_t duration_cap_us;
    uint64_t next_event_timestamp_us;
    uint64_t last_poll_timestamp_us;
    uint64_t max_poll_gap_us;
    uint64_t max_event_deadline_lateness_us;
    uint32_t accepted_point_cap;
    uint32_t cadence_index;
    uint32_t poll_count;
    uint32_t event_count;
    uint32_t late_event_count;
    uint16_t stroke_point_count;
    uint8_t stroke_index;
    lab10_ui_scripted_action_t next_action;
    bool initialized;
    bool poll_timestamp_valid;
    bool down_emitted;
    bool terminal_reported;
} lab10_ui_scripted_schedule_t;

typedef struct {
    uint64_t capture_session_id;
    uint64_t max_poll_gap_us;
    uint64_t max_event_deadline_lateness_us;
    uint32_t accepted_points;
    uint32_t scheduler_poll_count;
    uint32_t scripted_event_count;
    uint32_t late_event_count;
    uint8_t renderer_published_color_mask;
    uint8_t canvas_sampled_color_mask;
    uint8_t canvas_readback_color_mask;
    uint8_t canvas_color_match_mask;
    uint8_t canvas_final_color_mask;
    uint32_t canvas_color_format;
    bool canvas_readback_supported;
    bool renderer_drain_complete;
} lab10_ui_scripted_diagnostic_snapshot_t;

typedef struct {
    bool pending;
    bool reported;
    bool snapshot_frozen;
} lab10_ui_scripted_diagnostic_lifecycle_t;

typedef struct {
    uint32_t source_point_count;
    uint32_t source_cursor;
    uint32_t pending_point_count;
    bool frame_idle;
} lab10_ui_scripted_renderer_drain_t;

typedef struct {
    uint8_t renderer_published_color_mask;
    uint8_t canvas_sampled_color_mask;
    uint8_t canvas_readback_color_mask;
    uint8_t canvas_color_match_mask;
    uint8_t canvas_final_color_mask;
    uint32_t canvas_color_format;
    bool canvas_readback_supported;
} lab10_ui_scripted_canvas_observation_t;

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

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t content_offset_y;
    bool hidden;
} lab10_ui_ring_reveal_clip_t;

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
void lab10_ui_scripted_qualification_begin(
    lab10_ui_scripted_qualification_t *qualification, bool scripted_session);
void lab10_ui_scripted_qualification_resolve(
    lab10_ui_scripted_qualification_t *qualification, bool accepted);
bool lab10_ui_terminal_screen_state(
    lab10_ui_terminal_kind_t terminal,
    const lab10_ui_scripted_qualification_t *qualification,
    lab10_screen_state_t *state);

/* UI-task-only deterministic path; it does not claim hardware touch input. */
bool lab10_ui_scripted_schedule_init(
    lab10_ui_scripted_schedule_t *schedule, uint64_t capture_start_timestamp_us,
    uint64_t duration_cap_us, uint32_t accepted_point_cap,
    uint64_t move_interval_us);
lab10_ui_scripted_poll_result_t lab10_ui_scripted_schedule_poll(
    lab10_ui_scripted_schedule_t *schedule, uint64_t now_us,
    lab10_ui_scripted_capture_state_t capture_state,
    const lab10_ui_scripted_capture_summary_t *capture_summary,
    lab10_ui_scripted_event_t *event);

/* UI-task-only snapshot lifecycle; mailbox delivery stays with the bridge. */
bool lab10_ui_scripted_diagnostic_begin(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    lab10_ui_scripted_diagnostic_snapshot_t *snapshot);
bool lab10_ui_scripted_diagnostic_prepare_delivery(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    lab10_ui_scripted_diagnostic_snapshot_t *snapshot,
    bool active_session_valid, uint64_t active_session_id,
    bool renderer_metrics_available,
    const lab10_ui_scripted_renderer_drain_t *renderer_drain,
    const lab10_ui_scripted_canvas_observation_t *canvas_observation);
void lab10_ui_scripted_diagnostic_finish_delivery(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle,
    const lab10_ui_scripted_diagnostic_snapshot_t *snapshot,
    bool mailbox_accepted);
void lab10_ui_scripted_diagnostic_allow_next_session(
    lab10_ui_scripted_diagnostic_lifecycle_t *lifecycle);

size_t lab10_canvas_memory_attempt_count(void);
lab10_canvas_memory_region_t lab10_canvas_memory_attempt(size_t index);

lab10_ui_circle_transition_t lab10_ui_fab_entry_transition(
    int16_t origin_x, int16_t origin_y);
lab10_ui_circle_transition_t lab10_ui_fab_exit_transition(
    int16_t origin_x, int16_t origin_y);
lab10_ui_circle_transition_t lab10_ui_ring_entry_transition(
    int16_t origin_x, int16_t origin_y);
lab10_ui_circle_transition_t lab10_ui_ring_exit_transition(
    int16_t origin_x, int16_t origin_y);
lab10_ui_ring_reveal_clip_t lab10_ui_ring_reveal_clip(int16_t origin_y,
                                                       int32_t outer_radius);

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
