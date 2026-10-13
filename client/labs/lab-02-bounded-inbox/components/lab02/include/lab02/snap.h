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
    bool driving_scroll; /* Synchronous events from our animation tick only. */
    uint32_t operation_id;
} lab02_snap_state_t;

typedef enum {
    LAB02_SNAP_SUCCESS, LAB02_SNAP_STALE, LAB02_SNAP_CANCELLED
} lab02_snap_outcome_t;
typedef void (*lab02_snap_terminal_cb)(void *, uint32_t, lab02_snap_outcome_t);

/** Nonblocking; terminalizes before deleting LVGL's animation. */
void lab02_snap_cancel(lab02_snap_state_t *st, const char *reason);
bool lab02_snap_is_current(uint32_t id);

void lab02_snap_animate_to(lv_obj_t *scroller, lab02_snap_state_t *st, int32_t target_x,
                           int focus_index, const char *message_id,
                           lab02_snap_terminal_cb on_done, void *user_data);
#ifdef __cplusplus
}
#endif
