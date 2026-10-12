#include "lvgl.h"

#include <time.h>

static lv_anim_t s_active;

int32_t lv_obj_get_scroll_x(const lv_obj_t *obj)
{
    return obj ? obj->scroll_x : 0;
}

void lv_obj_scroll_to_x(lv_obj_t *obj, int32_t x, int anim)
{
    (void)anim;
    if (obj) {
        obj->scroll_x = x;
    }
}

void *lv_anim_get_user_data(const lv_anim_t *a)
{
    return a ? a->user_data : NULL;
}

void lv_anim_start(lv_anim_t *a)
{
    if (!a) {
        return;
    }
    s_active = *a;
    s_active.active = true;
}

void lv_anim_delete(void *var, lv_anim_exec_xcb_t exec_cb)
{
    if (!s_active.active) {
        return;
    }
    if (s_active.var != var) {
        return;
    }
    if (exec_cb && s_active.exec_cb != exec_cb) {
        return;
    }
    memset(&s_active, 0, sizeof(s_active));
}

void lab02_lvgl_host_finish_active_anim(void)
{
    if (!s_active.active || !s_active.completed_cb) {
        return;
    }
    if (s_active.exec_cb && s_active.var) {
        s_active.exec_cb(s_active.var, s_active.end);
    }
    lv_anim_t done = s_active;
    memset(&s_active, 0, sizeof(s_active));
    done.completed_cb(&done);
}

int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
