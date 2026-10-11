#include "lab02/carousel_controller.h"

#include "lab02/instrumentation.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
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

static bool sync_from_session(lab02_carousel_t *c, bool refresh_layout)
{
    lab02_session_pump();
    const lab02_page_provider_t *sp = lab02_session_provider();
    if (sp) {
        c->provider = *sp;
    }
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    bool count_changed = c->item_count != (int)snap.total_count;
    bool focus_changed = c->focus_index != snap.focus_index;
    bool dirty = lab02_session_take_presentation_dirty();
    bool changed = count_changed || focus_changed || dirty;
    c->focus_index = snap.focus_index;
    c->item_count = (int)snap.total_count;
    if (refresh_layout && count_changed && c->content_spacer) {
        int32_t w = content_w(c->item_count);
        lv_obj_set_size(c->content_spacer, w, LAB02_CARD_H);
    }
    if (refresh_layout && (focus_changed || count_changed) && c->scroll &&
        !snap.scroll_locked && c->item_count > 0) {
        c->programmatic_scroll = true;
        lv_obj_scroll_to_x(c->scroll,
                           lab02_snap_target_x_for_index(c->focus_index, c->item_count),
                           LV_ANIM_OFF);
        c->programmatic_scroll = false;
    }
    return changed;
}

static void update_status_label(lab02_carousel_t *c)
{
    if (!c->count_lab) {
        return;
    }
    char buf[96];
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    if (snap.load_stage == LAB02_LOAD_CONNECTING || snap.load_stage == LAB02_LOAD_FETCHING) {
        snprintf(buf, sizeof(buf), "LAB-02  loading inbox…");
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    if (snap.load_stage == LAB02_LOAD_ERROR) {
        snprintf(buf, sizeof(buf), "LAB-02  inbox unavailable");
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    if (c->item_count <= 0) {
        snprintf(buf, sizeof(buf), "LAB-02  empty");
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    if (snap.focused_page_failed) {
        snprintf(buf, sizeof(buf), "LAB-02  page unavailable; retrying");
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    const lab02_inbox_item_t *item = lab02_page_provider_item(&c->provider, c->focus_index);
    if (!item) {
        snprintf(buf, sizeof(buf), "LAB-02  %d/%d  loading page…", c->focus_index + 1,
                 c->item_count);
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    const char *who = item->sender_name[0] ? item->sender_name : "…";
    uint16_t loaded = lab02_session_materialized_count();
    const char *cached = snap.cached_due_to_refresh_failure ? " cached" : "";
    if (snap.total_count > loaded) {
        snprintf(buf, sizeof(buf), "LAB-02  %d/%d (%u loaded)%s  %s", c->focus_index + 1,
                 c->item_count, (unsigned)loaded, cached, who);
    } else {
        snprintf(buf, sizeof(buf), "LAB-02  %d/%d%s  %s", c->focus_index + 1,
                 c->item_count, cached, who);
    }
    lv_label_set_text(c->count_lab, buf);
}

static void rebind_pool(lab02_carousel_t *c)
{
    uint32_t gen = lab02_instrumentation_next_generation();
    lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, gen);
    update_status_label(c);
}

static void on_snap_done(void *user)
{
    lab02_carousel_t *c = (lab02_carousel_t *)user;
    if (!c) {
        return;
    }
    lab02_session_intent_scroll_lock(false);
    sync_from_session(c, true);
    rebind_pool(c);
}

static void scroll_event_cb(lv_event_t *e)
{
    lab02_carousel_t *c = (lab02_carousel_t *)lv_event_get_user_data(e);
    if (!c || c->snap.scroll_lock || c->programmatic_scroll) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL_BEGIN) {
        lab02_session_intent_scroll_lock(true);
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        int idx = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(c->scroll), c->item_count);
        if (idx != c->focus_index && c->item_count > 0) {
            lab02_session_intent_focus(idx);
            sync_from_session(c, false);
            c->focus_index = idx;
            int32_t target = lab02_snap_target_x_for_index(idx, c->item_count);
            lab02_session_intent_scroll_lock(true);
            lab02_snap_animate_to(c->scroll, &c->snap, target, on_snap_done, c);
        } else {
            lab02_session_intent_scroll_lock(false);
        }
    }
}

bool lab02_carousel_init(lab02_carousel_t *c)
{
    if (!c) {
        return false;
    }
    memset(c, 0, sizeof(*c));
    if (!lab02_page_provider_init(&c->provider)) {
        return false;
    }
    sync_from_session(c, false);
    if (c->item_count <= 0) {
        c->focus_index = 0;
    }
    s_carousel = c;

    c->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(c->screen, lv_color_hex(LAB02_UI_BG), 0);
    lv_screen_load(c->screen);

    c->count_lab = lv_label_create(c->screen);
    lv_obj_set_style_text_color(c->count_lab, lv_color_hex(LAB02_UI_TEXT), 0);
    lv_obj_align(c->count_lab, LV_ALIGN_TOP_MID, 0, 2);
    update_status_label(c);

    c->scroll = lv_obj_create(c->screen);
    lv_obj_set_pos(c->scroll, 0, LAB02_CARD_Y);
    lv_obj_set_size(c->scroll, LAB02_LCD_W, LAB02_CARD_H);
    lv_obj_set_scroll_dir(c->scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(c->scroll, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_bg_opa(c->scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->scroll, 0, 0);
    lv_obj_set_style_pad_all(c->scroll, 0, 0);
    lv_obj_add_event_cb(c->scroll, scroll_event_cb, LV_EVENT_SCROLL_BEGIN, c);
    lv_obj_add_event_cb(c->scroll, scroll_event_cb, LV_EVENT_SCROLL_END, c);

    c->content_spacer = lv_obj_create(c->scroll);
    lv_obj_set_size(c->content_spacer, content_w(c->item_count), LAB02_CARD_H);
    lv_obj_set_style_bg_opa(c->content_spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->content_spacer, 0, 0);
    lv_obj_remove_flag(c->content_spacer, LV_OBJ_FLAG_SCROLLABLE);

    lab02_card_pool_init(&c->pool, c->scroll);
    rebind_pool(c);
    if (c->item_count > 0) {
        lab02_carousel_set_focus(c, c->focus_index, false);
    }
    return true;
}

void lab02_carousel_set_focus(lab02_carousel_t *c, int index, bool animate)
{
    if (!c) {
        return;
    }
    sync_from_session(c, false);
    if (c->item_count <= 0) {
        update_status_label(c);
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= c->item_count) {
        index = c->item_count - 1;
    }
    lab02_session_intent_focus(index);
    sync_from_session(c, false);
    c->focus_index = index;
    int32_t target = lab02_snap_target_x_for_index(index, c->item_count);
    if (animate) {
        lab02_session_intent_scroll_lock(true);
        lab02_snap_animate_to(c->scroll, &c->snap, target, on_snap_done, c);
    } else {
        c->programmatic_scroll = true;
        lv_obj_scroll_to_x(c->scroll, target, LV_ANIM_OFF);
        c->programmatic_scroll = false;
        rebind_pool(c);
    }
    update_status_label(c);
}

void lab02_carousel_pump(lab02_carousel_t *c)
{
    if (!c) {
        return;
    }
    if (sync_from_session(c, true)) {
        rebind_pool(c);
    }
}

lab02_carousel_t *lab02_carousel_instance(void)
{
    return s_carousel;
}
