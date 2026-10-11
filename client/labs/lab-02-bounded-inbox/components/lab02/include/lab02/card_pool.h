#pragma once

#include "lab02/card_view.h"
#include "lab02/page_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lab02_card_view_t views[LAB02_CARD_POOL_SIZE];
    int focus_index;
    uint32_t bind_generation;
} lab02_card_pool_t;

void lab02_card_pool_init(lab02_card_pool_t *pool, lv_obj_t *scroll_parent);
void lab02_card_pool_rebind(lab02_card_pool_t *pool, const lab02_page_provider_t *pp, int focus,
                            uint32_t generation);
void lab02_card_pool_layout_positions(lab02_card_pool_t *pool);

#ifdef __cplusplus
}
#endif
