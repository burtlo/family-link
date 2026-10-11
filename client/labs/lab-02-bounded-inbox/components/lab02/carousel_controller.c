#include "lab02/carousel_controller.h"

#include "lab02/instrumentation.h"
#include "lab02/trace_emit.h"

#include <stdio.h>
#include <string.h>

static lab02_carousel_t *s_carousel;

static int32_t content_w(int item_count)
{
    if (item_count <= 0) {
        return LAB02_LCD_W;
    }
    return LAB02_CARD_GAP + item_count * (LAB02_CARD_W + LAB02_CARD_GAP);
}

static void update_count_label(lab02_carousel_t *c)
{
    if (!c->count_lab) {
        return;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "LAB-02  %d msgs  page_size=%u pages=%u", c->item_count,
             (unsigned)c->provider.meta.page_size, (unsigned)c->provider.meta.page_count);
    lv_label_set_text(c->count_lab, buf);
}

static void on_snap_done(void *user)
{
    lab02_carousel_t *c = (lab02_carousel_t *)user;
    if (!c) {
        return;
    }
    uint32_t gen = lab02_instrumentation_next_generation();
    lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, gen);
}

static void scroll_event_cb(lv_event_t *e)
{
    lab02_carousel_t *c = (lab02_carousel_t *)lv_event_get_user_data(e);
    if (!c || c->snap.scroll_lock) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL_END) {
        int idx = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(c->scroll), c->item_count);
        if (idx != c->focus_index) {
            c->focus_index = idx;
            int32_t target = lab02_snap_target_x_for_index(idx, c->item_count);
            lab02_snap_animate_to(c->scroll, &c->snap, target, on_snap_done, c);
        }
    }
}

bool lab02_carousel_init(lab02_carousel_t *c)
{
    if (!c) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    if (!lab02_page_provider_local_init(&c->provider)) {
        return false;
    }
    c->item_count = (int)c->provider.model->count;
    c->focus_index = 0;
    s_carousel = c;

    c->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(c->screen, lv_color_hex(LAB02_UI_BG), 0);
    lv_screen_load(c->screen);

    c->count_lab = lv_label_create(c->screen);
    lv_obj_set_style_text_color(c->count_lab, lv_color_hex(LAB02_UI_TEXT), 0);
    lv_obj_align(c->count_lab, LV_ALIGN_TOP_MID, 0, 2);
    update_count_label(c);

    c->scroll = lv_obj_create(c->screen);
    lv_obj_set_pos(c->scroll, 0, LAB02_CARD_Y);
    lv_obj_set_size(c->scroll, LAB02_LCD_W, LAB02_CARD_H);
    lv_obj_set_scroll_dir(c->scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(c->scroll, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_bg_opa(c->scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->scroll, 0, 0);
    lv_obj_set_style_pad_all(c->scroll, 0, 0);
    lv_obj_add_event_cb(c->scroll, scroll_event_cb, LV_EVENT_SCROLL_END, c);

    c->content_spacer = lv_obj_create(c->scroll);
    lv_obj_set_size(c->content_spacer, content_w(c->item_count), LAB02_CARD_H);
    lv_obj_set_style_bg_opa(c->content_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->content_spacer, 0, 0);
    lv_obj_remove_flag(c->content_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lab02_card_pool_init(&c->pool, c->scroll);
    uint32_t gen = lab02_instrumentation_next_generation();
    lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, gen);
    lab02_carousel_set_focus(c, c->focus_index, false);
    return true;
}

void lab02_carousel_set_focus(lab02_carousel_t *c, int index, bool animate)
{
    if (!c || c->item_count <= 0) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= c->item_count) {
        index = c->item_count - 1;
    }
    c->focus_index = index;
    int32_t target = lab02_snap_target_x_for_index(index, c->item_count);
    if (animate) {
        lab02_snap_animate_to(c->scroll, &c->snap, target, on_snap_done, c);
    } else {
        lv_obj_scroll_to_x(c->scroll, target, LV_ANIM_OFF);
        uint32_t gen = lab02_instrumentation_next_generation();
        lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, gen);
    }
}

lab02_carousel_t *lab02_carousel_instance(void)
{
    return s_carousel;
}
