#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct lv_obj_t lv_obj_t;

typedef struct lv_anim_t lv_anim_t;

typedef void (*lv_anim_exec_xcb_t)(void *, int32_t);
typedef void (*lv_anim_completed_cb_t)(lv_anim_t *);

struct lv_anim_t {
    lv_obj_t *var;
    void *user_data;
    int32_t start;
    int32_t end;
    uint32_t duration_ms;
    lv_anim_exec_xcb_t exec_cb;
    lv_anim_completed_cb_t completed_cb;
    bool active;
};

#define LV_ANIM_OFF 0

struct lv_obj_t {
    int32_t scroll_x;
};

static inline void lv_anim_init(lv_anim_t *a)
{
    memset(a, 0, sizeof(*a));
}

static inline void lv_anim_set_var(lv_anim_t *a, void *var)
{
    a->var = (lv_obj_t *)var;
}

static inline void lv_anim_set_user_data(lv_anim_t *a, void *user)
{
    a->user_data = user;
}

static inline void lv_anim_set_values(lv_anim_t *a, int32_t start, int32_t end)
{
    a->start = start;
    a->end = end;
}

static inline void lv_anim_set_duration(lv_anim_t *a, uint32_t ms)
{
    a->duration_ms = ms;
}

static inline void lv_anim_set_exec_cb(lv_anim_t *a, lv_anim_exec_xcb_t cb)
{
    a->exec_cb = cb;
}

static inline void lv_anim_set_completed_cb(lv_anim_t *a, lv_anim_completed_cb_t cb)
{
    a->completed_cb = cb;
}

void lv_anim_start(lv_anim_t *a);
void lv_anim_delete(void *var, lv_anim_exec_xcb_t exec_cb);
void *lv_anim_get_user_data(const lv_anim_t *a);

int32_t lv_obj_get_scroll_x(const lv_obj_t *obj);
void lv_obj_scroll_to_x(lv_obj_t *obj, int32_t x, int anim);

/** Host test helper: run the active animation completed callback (if any). */
void lab02_lvgl_host_finish_active_anim(void);
