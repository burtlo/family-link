#include "lab02/carousel_controller.h"

#include "lab02/instrumentation.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/trace_emit.h"

#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static lab02_carousel_t *s_carousel;

static void update_status_label(lab02_carousel_t *c);


static const char *focused_message_id(lab02_carousel_t *c, int index)
{
    const lab02_inbox_item_t *item = lab02_page_provider_item(&c->provider, index);
    return item ? item->message_id : NULL;
}

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
    if (snap.http_active && snap.load_stage != LAB02_LOAD_READY &&
        snap.load_stage != LAB02_LOAD_ERROR) {
        snprintf(buf, sizeof(buf), "LAB-02");
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
    if (snap.transport_unreachable) {
        snprintf(buf, sizeof(buf), "LAB-02  reconnecting…");
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    if (snap.focused_page_failed) {
        snprintf(buf, sizeof(buf), "LAB-02  %d/%d  tap card to retry", c->focus_index + 1,
                 c->item_count);
        lv_label_set_text(c->count_lab, buf);
        return;
    }
    const lab02_inbox_item_t *item = lab02_page_provider_item(&c->provider, c->focus_index);
    if (!item) {
        snprintf(buf, sizeof(buf), "LAB-02  %d/%d  loading page...", c->focus_index + 1,
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

static void on_status_clicked(lv_event_t *e)
{
    lab02_carousel_t *c = lv_event_get_user_data(e);
    if (!c) {
        return;
    }
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    if (!snap.http_active || snap.load_stage != LAB02_LOAD_ERROR) {
        return;
    }
    if (!lab02_session_intent_retry_initial_load()) {
        return;
    }
    lab02_session_pump();
    update_status_label(c);
}

static void rebind_pool(lab02_carousel_t *c)
{
    uint32_t gen = lab02_instrumentation_next_generation();
    lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, lv_obj_get_scroll_x(c->scroll), gen);
    update_status_label(c);
}

static void on_snap_done(void *user, uint32_t id, lab02_snap_outcome_t outcome)
{
    lab02_carousel_t *c = (lab02_carousel_t *)user;
    if (!c || !c->scroll || c->snap.operation_id != id || outcome == LAB02_SNAP_STALE) return;
    lab02_session_intent_scroll_lock(false);
    sync_from_session(c, true);
    rebind_pool(c);
}

/* Capture replacement focus before unlocking: unlock may apply a pending
 * mutation and relocate that identity. Never calculate a target from old count. */
static void supersede_snap(lab02_carousel_t *c, int index, const char *reason)
{
    if (!c->snap.scroll_lock) return;
    lab02_snap_cancel(&c->snap, reason);
    lab02_session_intent_focus(index);
    lab02_session_intent_scroll_lock(false);
    lab02_session_intent_scroll_lock(true);
    sync_from_session(c, true);
}

static void scroll_deleted(lv_event_t *e)
{
    lab02_carousel_t *c = lv_event_get_user_data(e);
    c->scroll = NULL; /* Invalidate before cancellation callbacks. */
    lab02_snap_cancel(&c->snap, "scroller_deleted");
    lab02_session_intent_scroll_lock(false);
    if (s_carousel == c) s_carousel = NULL;
}

static void scroll_event_cb(lv_event_t *e)
{
    lab02_carousel_t *c = (lab02_carousel_t *)lv_event_get_user_data(e);
    if (!c || c->programmatic_scroll) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL) {
        /* Render the actual viewport even while a custom snap owns the target.
         * One latest intent, at most 50 admissions/s; terminal flush is immediate. */
        if (!c->snap.scroll_lock) {
            c->pending_focus = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(c->scroll), c->item_count);
            c->focus_pending = true;
            if (esp_timer_get_time() >= c->next_focus_us) {
                lab02_session_intent_focus(c->pending_focus);
                sync_from_session(c, false);
                c->focus_pending = false;
                c->next_focus_us = esp_timer_get_time() + 20000;
            }
        }
        rebind_pool(c);
        return;
    }
    if (c->snap.driving_scroll) return;
    if (code == LV_EVENT_SCROLL_BEGIN) {
        int idx = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(c->scroll), c->item_count);
        supersede_snap(c, idx, "reversed");
        lab02_session_intent_scroll_lock(true);
        return;
    }
    if (code == LV_EVENT_SCROLL_END) {
        int idx = lab02_snap_index_from_scroll_x(lv_obj_get_scroll_x(c->scroll), c->item_count);
        c->focus_pending = false;
        if (c->item_count > 0) {
            lab02_session_intent_focus(idx);
            sync_from_session(c, false);

            int32_t target = lab02_snap_target_x_for_index(idx, c->item_count);
            lab02_session_intent_scroll_lock(true);
            lab02_snap_animate_to(c->scroll, &c->snap, target, idx,
                                 focused_message_id(c, idx), on_snap_done, c);
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
    lv_obj_add_flag(c->count_lab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c->count_lab, on_status_clicked, LV_EVENT_CLICKED, c);
    update_status_label(c);

    c->scroll = lv_obj_create(c->screen);
    lv_obj_set_pos(c->scroll, 0, LAB02_CARD_Y);
    lv_obj_set_size(c->scroll, LAB02_LCD_W, LAB02_CARD_H);
    lv_obj_set_scroll_dir(c->scroll, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(c->scroll, LV_SCROLL_SNAP_NONE);
    lv_obj_set_style_bg_opa(c->scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c->scroll, 0, 0);
    lv_obj_set_style_pad_all(c->scroll, 0, 0);
    lv_obj_add_event_cb(c->scroll, scroll_deleted, LV_EVENT_DELETE, c);
    lv_obj_add_event_cb(c->scroll, scroll_event_cb, LV_EVENT_SCROLL, c);
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
    if (!c || !c->scroll) {
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
    bool replacing = c->snap.scroll_lock;
    supersede_snap(c, index, "superseded");
    lab02_session_intent_focus(replacing ? c->focus_index : index);
    sync_from_session(c, false);
    index = c->focus_index;
    c->focus_pending = false;
    int32_t target = lab02_snap_target_x_for_index(index, c->item_count);
    if (animate) {
        lab02_session_intent_scroll_lock(true);
        lab02_snap_animate_to(c->scroll, &c->snap, target, index,
                             focused_message_id(c, index), on_snap_done, c);
    } else {
        lab02_session_intent_scroll_lock(false);
        sync_from_session(c, false);
        target = lab02_snap_target_x_for_index(c->focus_index, c->item_count);
        c->programmatic_scroll = true;
        lv_obj_scroll_to_x(c->scroll, target, LV_ANIM_OFF);
        c->programmatic_scroll = false;
        rebind_pool(c);
    }
    update_status_label(c);
}

void lab02_carousel_pump(lab02_carousel_t *c)
{
    if (!c || !c->scroll) {
        return;
    }
    if (c->focus_pending && !c->snap.scroll_lock && esp_timer_get_time() >= c->next_focus_us) {
        lab02_session_intent_focus(c->pending_focus);
        c->focus_pending = false;
        c->next_focus_us = esp_timer_get_time() + 20000;
    }
    if (sync_from_session(c, true)) {
        rebind_pool(c);
    }
}

lab02_carousel_t *lab02_carousel_instance(void)
{
    return s_carousel;
}
