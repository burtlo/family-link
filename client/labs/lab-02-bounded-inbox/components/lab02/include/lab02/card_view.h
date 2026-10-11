#pragma once

#include "lvgl.h"

#include "lab02/inbox_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *root;
    lv_obj_t *sender_lab;
    lv_obj_t *preview_lab;
    lv_obj_t *dur_lab;
    lv_obj_t *unread_dot;
    int bound_index;
    uint32_t bind_generation;
    char bound_message_id[24];
    lv_timer_t *bind_timer;
    lab02_inbox_item_t pending_item;
    bool pending_bind;
} lab02_card_view_t;

void lab02_card_view_style_root(lv_obj_t *card, bool focused);
void lab02_card_view_format_duration(char *buf, size_t cap, uint32_t duration_ms);

void lab02_card_view_create(lab02_card_view_t *view, lv_obj_t *parent);
void lab02_card_view_bind(lab02_card_view_t *view, int index, uint32_t generation,
                          const lab02_inbox_item_t *item);
void lab02_card_view_unbind(lab02_card_view_t *view, uint32_t generation);
void lab02_card_view_set_pos_for_index(lab02_card_view_t *view, int index);
bool lab02_card_view_is_stale(const lab02_card_view_t *view, uint32_t generation);

#ifdef __cplusplus
}
#endif
