#include "lab02/card_pool.h"

#include "lab02/instrumentation.h"
#include "lab02/snap_math.h"

#include <string.h>

void lab02_card_pool_init(lab02_card_pool_t *pool, lv_obj_t *scroll_parent)
{
    memset(pool, 0, sizeof(*pool));
    pool->focus_index = 0;
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) {
        lab02_card_view_create(&pool->views[i], scroll_parent);
        lv_obj_add_flag(pool->views[i].root, LV_OBJ_FLAG_HIDDEN);
    }
}

void lab02_card_pool_layout_positions(lab02_card_pool_t *pool)
{
    if (!pool) {
        return;
    }
    for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++) {
        if (pool->views[s].bound_index >= 0) {
            lab02_card_view_set_pos_for_index(&pool->views[s], pool->views[s].bound_index);
        }
    }
}

void lab02_card_pool_rebind(lab02_card_pool_t *pool, const lab02_page_provider_t *pp, int focus, int32_t scroll_x,
                            uint32_t generation)
{
    if (!pool || !pp || !pp->model) {
        return;
    }
    int count = (int)pp->model->count;
    pool->focus_index = focus;
    pool->bind_generation = generation;
    int first = lab02_window_first_index(scroll_x, count);
    int last = lab02_window_last_index(scroll_x, count);
    /* Retain surviving bindings (including delayed timers) in their root.
     * Only vacated roots recycle, so overlap is property-only work. */
    for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++) {
        lab02_card_view_t *view = &pool->views[s];
        view->card_slot = s;
        if (view->bound_index < first || view->bound_index > last ||
            view->bound_index >= count) {
            if (view->bound_index >= 0) lab02_card_view_unbind(view, generation);
        }
    }
    for (int idx = first; idx <= last && idx < first + LAB02_CARD_POOL_SIZE; idx++) {
        lab02_card_view_t *view = NULL;
        for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++)
            if (pool->views[s].bound_index == idx) view = &pool->views[s];
        if (!view) {
            for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++)
                if (pool->views[s].bound_index < 0) { view = &pool->views[s]; break; }
        }
        if (!view) continue;
        lab02_slot_snapshot_t slot = lab02_session_slot_snapshot(pp, idx);
        bool same = view->bound_index == idx &&
            view->session_generation == slot.session_generation &&
            view->inbox_revision == slot.inbox_revision &&
            ((slot.state == LAB02_SLOT_BOUND && slot.item &&
              strcmp(view->bound_message_id, slot.item->message_id) == 0 &&
              (view->state == LAB02_CARD_BOUND || view->pending_bind)) ||
             (slot.state == LAB02_SLOT_LOADING && view->state == LAB02_CARD_LOADING && !view->pending_bind) ||
             (slot.state == LAB02_SLOT_ERROR && view->state == LAB02_CARD_ERROR));
        if (!same) lab02_card_view_present(view, generation, &slot);
        lab02_card_view_set_pos_for_index(view, idx);
        lab02_card_view_style_root(view->root, idx == focus);
    }
}
