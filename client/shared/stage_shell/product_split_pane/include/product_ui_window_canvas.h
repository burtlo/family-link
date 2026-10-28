#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Desk layout from product_ui::UiWindow (device main). Split-pane and other
 * chrome query these instead of hardcoding canvas or split geometry.
 */
bool product_ui_window_canvas_size(lv_coord_t *width_out, lv_coord_t *height_out);
/* Upper slot height (catalog split_pane.split_y). Lower slot fills the remainder. */
bool product_ui_window_split_pane_upper_height(lv_coord_t *height_out);
/* Active theme slot fills — THEME-DARK fl-color-surface-upper / -lower. */
bool product_ui_window_split_pane_slot_colors(uint32_t *upper_rgb24_out, uint32_t *lower_rgb24_out);

#ifdef __cplusplus
}
#endif
