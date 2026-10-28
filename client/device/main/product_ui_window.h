#pragma once

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRODUCT_UI_SCENE_NONE = 0,
    PRODUCT_UI_SCENE_DESK,
    PRODUCT_UI_SCENE_RECORD,
} product_ui_scene_t;

/*
 * C bindings for boot and stage_router. Implementation is product_ui::UiWindow.
 *
 * Scene (this API): which top-level LVGL subtree is mounted — desk split-pane vs record.
 * Stage (product_stage_*): navigation bookmark inside desk (inbox, send, …).
 * Screen (product_stage_screen): which module product_stage_present shows.
 *
 * All calls require bsp_display_lock (LVGL task / app_main attach path).
 */
bool product_ui_window_create(lv_obj_t *screen);
void product_ui_window_destroy(void);
bool product_ui_window_created(void);
lv_obj_t *product_ui_window_content_root(void);
product_ui_scene_t product_ui_window_scene(void);

bool product_ui_window_show_desk(void);
bool product_ui_window_show_record(void);
bool product_ui_window_desk_roots(lv_obj_t **upper_out, lv_obj_t **lower_out);

#ifdef __cplusplus
}
#endif
