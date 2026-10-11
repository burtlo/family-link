#include "lab02/eager_carousel.h"

#include "lab02/card_view.h"
#include "lab02/layout.h"
#include "lab02/page_provider.h"
#include "lab02/snap.h"
#include "lab02/trace_emit.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *scroll;
    lab02_page_provider_t provider;
    lab02_card_view_t cards[16];
    lab02_snap_state_t snap;
    int focus;
    int count;
} eager_state_t;

static eager_state_t s_eager;

static void eager_scroll_end(lv_event_t *e)
{
    eager_state_t *st = lv_event_get_user_data(e);
    if (!st || st->snap.scroll_lock) {
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) {
        return;
    }
    int idx = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(st->scroll), st->count);
    st->focus = idx;
    int32_t target = lab02_snap_target_x_for_index(idx, st->count);
    lab02_snap_animate_to(st->scroll, &st->snap, target, NULL, NULL);
}

bool lab02_eager_carousel_start(void)
{
    memset(&s_eager, 0, sizeof(s_eager));
    if (!lab02_page_provider_local_init(&s_eager.provider)) {
        return false;
    }
    s_eager.count = (int)s_eager.provider.model->count;
    if (s_eager.count > 16) {
        s_eager.count = 16;
    }
    if (s_eager.count <= 0) {
        return false;
    }

    s_eager.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_eager.screen, lv_color_hex(LAB02_UI_BG), 0);
    lv_screen_load(s_eager.screen);

    lv_obj_t *title = lv_label_create(s_eager.screen);
    lv_label_set_text(title, "LAB-02 eager baseline (16 max)");
    lv_obj_set_style_text_color(title, lv_color_hex(LAB02_UI_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    s_eager.scroll = lv_obj_create(s_eager.screen);
    lv_obj_set_pos(s_eager.scroll, 0, LAB02_CARD_Y);
    lv_obj_set_size(s_eager.scroll, LAB02_LCD_W, LAB02_CARD_H);
    lv_obj_set_scroll_dir(s_eager.scroll, LV_DIR_HOR);
    lv_obj_set_style_bg_opa(s_eager.scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_eager.scroll, 0, 0);
    lv_obj_add_event_cb(s_eager.scroll, eager_scroll_end, LV_EVENT_SCROLL_END, &s_eager);

    int x = LAB02_CARD_GAP;
    for (int i = 0; i < s_eager.count; i++) {
        lab02_card_view_create(&s_eager.cards[i], s_eager.scroll);
        const lab02_inbox_item_t *item = lab02_page_provider_item(&s_eager.provider, i);
        lab02_card_view_bind(&s_eager.cards[i], i, 1, item);
        lv_obj_set_pos(s_eager.cards[i].root, x, 0);
        x += LAB02_CARD_W + LAB02_CARD_GAP;
    }

    lv_obj_t *spacer = lv_obj_create(s_eager.scroll);
    lv_obj_set_size(spacer, x, LAB02_CARD_H);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    int32_t target = lab02_snap_target_x_for_index(0, s_eager.count);
    lv_obj_scroll_to_x(s_eager.scroll, target, LV_ANIM_OFF);
    lab02_trace_emit("eager_ready", 1, 0, NULL, NULL);
    return true;
}
