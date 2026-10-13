#pragma once

#include "lvgl.h"

#include "lab02/card_pool.h"
#include "lab02/page_provider.h"
#include "lab02/snap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *scroll;
    lv_obj_t *content_spacer;
    lv_obj_t *count_lab;
    lab02_page_provider_t provider;
    lab02_card_pool_t pool;
    lab02_snap_state_t snap;
    int focus_index;
    int item_count;
    bool programmatic_scroll;
    bool focus_pending;
    int pending_focus;
    int64_t next_focus_us;
} lab02_carousel_t;

bool lab02_carousel_init(lab02_carousel_t *c);
void lab02_carousel_set_focus(lab02_carousel_t *c, int index, bool animate);
/** Apply session I/O results to carousel layout (call outside snap lock). */
void lab02_carousel_pump(lab02_carousel_t *c);
lab02_carousel_t *lab02_carousel_instance(void);

#ifdef __cplusplus
}
#endif

