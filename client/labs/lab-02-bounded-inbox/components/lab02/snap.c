#include "lab02/snap.h"

#include "lab02/instrumentation.h"
#include "lab02/snap_math.h"
#include "lab02/trace_emit.h"

#include "esp_timer.h"
#include <stdlib.h>

static int32_t s_anim_target;
static lab02_snap_state_t *s_anim_st;
static void (*s_anim_done)(void *);
static void *s_anim_user;

static void scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
}

static void snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    if (scroller) {
        lv_obj_scroll_to_x(scroller, s_anim_target, LV_ANIM_OFF);
    }
    if (s_anim_st) {
        s_anim_st->scroll_lock = false;
        s_anim_st->snap_target_x = s_anim_target;
    }
    lab02_trace_emit("snap_end", 0, -1, NULL, NULL);
    lab02_instrumentation_get()->snap_count++;
    if (s_anim_done) {
        s_anim_done(s_anim_user);
    }
    s_anim_done = NULL;
    s_anim_user = NULL;
    s_anim_st = NULL;
}

void lab02_snap_animate_to(lv_obj_t *scroller, lab02_snap_state_t *st, int32_t target_x,
                           void (*on_done)(void *), void *user_data)
{
    if (!scroller || !st) {
        return;
    }
    int32_t start = lv_obj_get_scroll_x(scroller);
    if (start == target_x) {
        st->scroll_lock = false;
        st->snap_target_x = target_x;
        if (on_done) {
            on_done(user_data);
        }
        return;
    }
    lv_anim_delete(scroller, scroll_x_exec);
    s_anim_target = target_x;
    s_anim_st = st;
    s_anim_done = on_done;
    s_anim_user = user_data;
    st->snap_target_x = target_x;
    st->scroll_lock = true;
    lab02_trace_emit("snap_start", 0, -1, NULL, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroller);
    lv_anim_set_user_data(&a, scroller);
    lv_anim_set_values(&a, start, target_x);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_exec_cb(&a, scroll_x_exec);
    lv_anim_set_completed_cb(&a, snap_anim_done);
    lv_anim_start(&a);
}
