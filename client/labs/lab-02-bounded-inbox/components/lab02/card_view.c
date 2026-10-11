#include "lab02/card_view.h"
#include "lab02/layout.h"
#include "lab02/instrumentation.h"
#include "lab02/trace_emit.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static int32_t card_x_for_index(int index)
{
    return LAB02_CARD_GAP + index * (LAB02_CARD_W + LAB02_CARD_GAP);
}

void lab02_card_view_create(lab02_card_view_t *view, lv_obj_t *parent)
{
    memset(view, 0, sizeof(*view));
    view->root = lv_obj_create(parent);
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(view->root, LAB02_CARD_W, LAB02_CARD_H);
    lab02_card_view_style_root(view->root, false);

    view->sender_lab = lv_label_create(view->root);
    lv_obj_set_style_text_color(view->sender_lab, lv_color_hex(LAB02_UI_TEXT), 0);
    lv_label_set_text(view->sender_lab, "—");

    view->preview_lab = lv_label_create(view->root);
    lv_obj_set_width(view->preview_lab, LAB02_CARD_W - 16);
    lv_label_set_long_mode(view->preview_lab, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(view->preview_lab, lv_color_hex(LAB02_UI_TEXT_MUT), 0);
    lv_label_set_text(view->preview_lab, "");

    view->dur_lab = lv_label_create(view->root);
    lv_obj_set_style_text_color(view->dur_lab, lv_color_hex(LAB02_UI_TEXT_MUT), 0);
    lv_label_set_text(view->dur_lab, "0:00");

    view->unread_dot = lv_obj_create(view->root);
    lv_obj_set_size(view->unread_dot, 8, 8);
    lv_obj_set_style_radius(view->unread_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(view->unread_dot, lv_color_hex(LAB02_UI_ACCENT), 0);
    lv_obj_add_flag(view->unread_dot, LV_OBJ_FLAG_HIDDEN);

    lv_obj_align(view->sender_lab, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_align(view->preview_lab, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_align(view->dur_lab, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_align(view->unread_dot, LV_ALIGN_TOP_RIGHT, -4, 4);
}

void lab02_card_view_set_pos_for_index(lab02_card_view_t *view, int index)
{
    if (!view || !view->root) {
        return;
    }
    lv_obj_set_pos(view->root, card_x_for_index(index), 0);
}

void lab02_card_view_bind(lab02_card_view_t *view, int index, uint32_t generation,
                          const lab02_inbox_item_t *item)
{
    if (!view) {
        return;
    }
    if (!item) {
        lab02_card_view_unbind(view, generation);
        return;
    }
    view->bound_index = index;
    view->bind_generation = generation;
    snprintf(view->bound_message_id, sizeof(view->bound_message_id), "%s", item->message_id);
#if CONFIG_LAB02_SLOW_BIND_MS > 0
    vTaskDelay(pdMS_TO_TICKS(CONFIG_LAB02_SLOW_BIND_MS));
#endif
    lv_label_set_text(view->sender_lab, item->sender_name);
    lv_label_set_text(view->preview_lab, item->preview);
    char dur[16];
    lab02_card_view_format_duration(dur, sizeof(dur), item->duration_ms);
    lv_label_set_text(view->dur_lab, dur);
    if (item->unread) {
        lv_obj_remove_flag(view->unread_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(view->unread_dot, LV_OBJ_FLAG_HIDDEN);
    }
    lab02_card_view_style_root(view->root, false);
    lab02_instrumentation_get()->bind_count++;
    lab02_trace_emit("bind", generation, index, item->message_id, NULL);
}

void lab02_card_view_unbind(lab02_card_view_t *view, uint32_t generation)
{
    if (!view) {
        return;
    }
    if (view->bound_message_id[0]) {
        lab02_trace_emit("unbind", generation, view->bound_index, view->bound_message_id, NULL);
        lab02_instrumentation_get()->unbind_count++;
    }
    view->bound_index = -1;
    view->bound_message_id[0] = 0;
    lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
}

bool lab02_card_view_is_stale(const lab02_card_view_t *view, uint32_t generation)
{
    return view && view->bind_generation != 0 && view->bind_generation != generation;
}
