#include "lab02/card_pool.h"

#include "lab02/instrumentation.h"

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

static int slot_offset(int slot)
{
    return slot - 1;
}

void lab02_card_pool_layout_positions(lab02_card_pool_t *pool)
{
    if (!pool) {
        return;
    }
    for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++) {
        int idx = pool->focus_index + slot_offset(s);
        if (pool->views[s].bound_index >= 0) {
            lab02_card_view_set_pos_for_index(&pool->views[s], pool->views[s].bound_index);
        }
    }
}

void lab02_card_pool_rebind(lab02_card_pool_t *pool, const lab02_page_provider_t *pp, int focus,
                            uint32_t generation)
{
    if (!pool || !pp || !pp->model) {
        return;
    }
    int count = (int)pp->model->count;
    pool->focus_index = focus;
    pool->bind_generation = generation;
    for (int s = 0; s < LAB02_CARD_POOL_SIZE; s++) {
        int idx = focus + slot_offset(s);
        lab02_card_view_t *view = &pool->views[s];
        if (idx < 0 || idx >= count) {
            lab02_card_view_unbind(view, generation);
            continue;
        }
        const lab02_inbox_item_t *item = lab02_page_provider_item(pp, idx);
        lab02_card_view_bind(view, idx, generation, item);
        lab02_card_view_set_pos_for_index(view, idx);
        if (!view->pending_bind) {
            lv_obj_remove_flag(view->root, LV_OBJ_FLAG_HIDDEN);
        }
        lab02_card_view_style_root(view->root, idx == focus);
    }
}
