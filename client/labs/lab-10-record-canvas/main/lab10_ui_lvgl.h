#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "lab10_capture.h"
#include "lab10_render.h"
#include "lab10_ui.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *root;
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
    int16_t transition_origin_x;
    int16_t transition_origin_y;
    size_t scripted_event_index;
    bool physical_ring_available;
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
