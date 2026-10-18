#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "lab10_capture.h"
#include "lab10_render.h"
#include "lab10_ui.h"
#include "lvgl.h"

typedef struct {
    uint32_t accepted_points;
    uint32_t strokes;
    uint32_t palette_changes;
    uint8_t accepted_color_mask;
} lab10_ui_scripted_completion_t;

typedef struct {
    lab10_capture_terminal_outcome_t terminal_outcome;
    lab10_ui_scripted_diagnostic_snapshot_t snapshot;
    uint32_t strokes;
    uint32_t palette_changes;
    uint8_t accepted_color_mask;
    uint32_t queue_overflow_count;
    uint32_t trace_loss_count;
    uint32_t duration_cap_count;
    uint32_t terminal_event_count;
    bool completion_scheduler_accepted;
} lab10_ui_scripted_diagnostic_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *reveal_host;
    lv_obj_t *canvas;
    lv_obj_t *rail;
    lv_obj_t *swatches[LAB10_SWATCH_COUNT];
    lv_obj_t *transition_layer;
    lv_obj_t *bloom;
    lv_obj_t *ring;
    lv_obj_t *outcome_panel;
    lv_obj_t *outcome_label;
    lv_color_t *canvas_buffer;
    uint32_t canvas_buffer_bytes;
    lv_timer_t *poll_timer;
    lab10_capture_t *capture;
    lab10_render_t *renderer;
    lab10_capture_session_id_t capture_session_id;
    lab10_ui_model_t model;
    int16_t fab_origin_x;
    int16_t fab_origin_y;
    int16_t ring_origin_x;
    int16_t ring_origin_y;
    int16_t transition_origin_x;
    int16_t transition_origin_y;
    lab10_ui_scripted_schedule_t scripted_schedule;
    bool scripted_started;
    bool scripted_start_marker_pending;
    bool scripted_completion_marker_pending;
    lab10_ui_scripted_completion_t scripted_completion;
    lab10_ui_scripted_diagnostic_lifecycle_t scripted_diagnostic_lifecycle;
    lab10_ui_scripted_diagnostic_t scripted_diagnostic;
    uint8_t renderer_published_color_mask;
    uint8_t canvas_sampled_color_mask;
    uint8_t canvas_readback_color_mask;
    uint8_t canvas_color_match_mask;
    uint32_t canvas_color_format;
    bool canvas_readback_supported;
    bool physical_ring_available;
    bool entered_with_ring_wipe;
} lab10_ui_view_t;

bool lab10_ui_view_init(lab10_ui_view_t *view, lv_obj_t *parent);
void lab10_ui_view_deinit(lab10_ui_view_t *view);
void lab10_ui_view_set_physical_ring_available(lab10_ui_view_t *view,
                                               bool available);
void lab10_ui_view_enter_fab_bloom(lab10_ui_view_t *view, int16_t origin_x,
                                   int16_t origin_y);
void lab10_ui_view_enter_ring_wipe(lab10_ui_view_t *view, int16_t center_x,
                                   int16_t center_y);
void lab10_ui_view_present_state(lab10_ui_view_t *view,
                                 lab10_screen_state_t state);
void lab10_ui_view_stop_capture(lab10_ui_view_t *view);
void lab10_ui_view_cancel_capture(lab10_ui_view_t *view);
lab10_capture_t *lab10_ui_view_capture(lab10_ui_view_t *view);
void lab10_ui_view_exit_reverse(lab10_ui_view_t *view);
void lab10_ui_view_scripted_poll(lab10_ui_view_t *view);
void lab10_ui_view_poll(lab10_ui_view_t *view);
/* Nonblocking one-shot handoff consumed by app_main outside LVGL ownership. */
bool lab10_ui_view_take_pending_scripted_start(void);
bool lab10_ui_view_take_pending_scripted_completion(
    lab10_ui_scripted_completion_t *completion);
bool lab10_ui_view_take_pending_scripted_diagnostic(
    lab10_ui_scripted_diagnostic_t *diagnostic);
bool lab10_ui_view_take_pending_terminal_outcome(
    lab10_capture_terminal_outcome_t *outcome);
