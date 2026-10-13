#pragma once

#include "lvgl.h"

#include "lab02/layout.h"
#include "lab02/snap_math.h"
#include "lab02/snap_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int item_count;
    int32_t snap_target_x;
    bool scroll_lock;
} lab02_snap_state_t;

void lab02_snap_animate_to(lv_obj_t *scroller, lab02_snap_state_t *st, int32_t target_x,
                           int focus_index, const char *message_id,
                           void (*on_done)(void *), void *user_data);
#ifdef __cplusplus
}
#endif
