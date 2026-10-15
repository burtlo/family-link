#include "lab02/card_view.h"
#include "lab02/layout.h"
#include "lab02/instrumentation.h"
#include "lab02/trace_emit.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"

#include "sdkconfig.h"

#include <string.h>

/* The existing default font lacks U+2026. A small static fallback preserves
 * approved copy without missing-glyph boxes or another widget. */
static bool ellipsis_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out,
                         uint32_t letter, uint32_t next)
{
    (void)font; (void)next;
    if (letter != 0x2026) return false;
    out->adv_w = 10; out->box_w = 9; out->box_h = 2;
    out->ofs_x = 0; out->ofs_y = 0; out->stride = 9;
    out->format = LV_FONT_GLYPH_FORMAT_A8;
    return true;
}
static const void *ellipsis_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buffer)
{
    (void)glyph; (void)buffer;
    static const uint8_t dots[] = {255,255,0,0,255,255,0,255,255,
                                  255,255,0,0,255,255,0,255,255};
    return dots;
}
static const lv_font_t ellipsis_font = {.get_glyph_dsc = ellipsis_dsc,
    .get_glyph_bitmap = ellipsis_bitmap, .line_height = 16, .base_line = 3,
    .static_bitmap = 1};
static lv_font_t card_font;

static void cancel_pending(lab02_card_view_t *view, const char *reason)
{
    if (view->pending_bind) {
        view->state = LAB02_CARD_STALE;
        lab02_instrumentation_get()->stale_bind_drops++;
        const lab02_trace_payload_t payload = {
            .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_REVISION |
                      LAB02_TRACE_FIELD_BIND_GENERATION | LAB02_TRACE_FIELD_CARD_SLOT |
                      LAB02_TRACE_FIELD_OUTCOME | LAB02_TRACE_FIELD_REASON |
                      LAB02_TRACE_FIELD_STATE,
            .session_id = view->pending_session,
            .revision = view->pending_revision,
            .bind_generation = view->pending_generation,
            .card_slot = view->card_slot >= 0 ? (uint32_t)view->card_slot : 0u,
            .outcome = "stale",
            .reason = reason,
            .state = "stale",
        };
        lab02_trace_emit_payload("bind_stale", view->pending_generation,
                                 view->bound_index, view->pending_item.message_id,
                                 &payload);
    }
    view->pending_bind = false;
    memset(&view->pending_item, 0, sizeof(view->pending_item));
#if CONFIG_LAB02_SLOW_BIND_MS > 0
    if (view->bind_timer) lv_timer_pause(view->bind_timer);
#endif
}

static void clear_content(lab02_card_view_t *view)
{
    lv_label_set_text(view->sender_lab, "");
    lv_label_set_text(view->preview_lab, "");
    lv_label_set_text(view->dur_lab, "");
    lv_obj_add_flag(view->unread_dot, LV_OBJ_FLAG_HIDDEN);
}

static int32_t card_x_for_index(int index)
{
    return LAB02_CARD_GAP + index * (LAB02_CARD_W + LAB02_CARD_GAP);
}

static void on_card_clicked(lv_event_t *e)
{
    lab02_card_view_t *view = lv_event_get_user_data(e);
    if (!view || view->state != LAB02_CARD_ERROR) {
        return;
    }
    (void)lab02_session_intent_retry_page_for_index(view->bound_index);
}

static void apply_bind_labels(lab02_card_view_t *view, const lab02_inbox_item_t *item)
{
    view->state = LAB02_CARD_BOUND;
    lv_label_set_long_mode(view->sender_lab, LV_LABEL_LONG_DOT);
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
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    lab02_instrumentation_get()->bind_count++;
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_REVISION |
                  LAB02_TRACE_FIELD_BIND_GENERATION | LAB02_TRACE_FIELD_CARD_SLOT |
                  LAB02_TRACE_FIELD_STATE,
        .session_id = view->session_generation,
        .revision = view->inbox_revision,
        .bind_generation = view->bind_generation,
        .card_slot = view->card_slot >= 0 ? (uint32_t)view->card_slot : 0u,
        .state = "bound",
    };
    lab02_trace_emit_payload("bind", view->bind_generation, view->bound_index,
                             item->message_id, &payload);
}

#if CONFIG_LAB02_SLOW_BIND_MS > 0
static void delayed_bind_cb(lv_timer_t *timer)
{
    lab02_card_view_t *view = (lab02_card_view_t *)lv_timer_get_user_data(timer);
    lv_timer_pause(timer);
    if (!view || !view->pending_bind) {
        return;
    }
    if (view->bind_generation != view->pending_generation ||
        view->session_generation != view->pending_session ||
        view->inbox_revision != view->pending_revision ||
        lab02_session_generation() != view->pending_session ||
        lab02_session_snapshot().inbox_revision != view->pending_revision ||
        strcmp(view->bound_message_id, view->pending_item.message_id) != 0) {
        cancel_pending(view, "snapshot_changed");
        clear_content(view);
        view->state = LAB02_CARD_STALE;
        lv_label_set_text(view->sender_lab, "Loading message…");
        return;
    }
    view->pending_bind = false;
    apply_bind_labels(view, &view->pending_item);
}
#endif

void lab02_card_view_create(lab02_card_view_t *view, lv_obj_t *parent)
{
    memset(view, 0, sizeof(*view));
    view->card_slot = -1;
    view->bound_index = -1;
    card_font = *LV_FONT_DEFAULT;
    card_font.fallback = &ellipsis_font;
    view->root = lv_obj_create(parent);
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(view->root, LAB02_CARD_W, LAB02_CARD_H);
    lv_obj_add_flag(view->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(view->root, on_card_clicked, LV_EVENT_CLICKED, view);
    lab02_card_view_style_root(view->root, false);

    view->sender_lab = lv_label_create(view->root);
    lv_obj_set_style_text_font(view->sender_lab, &card_font, 0);
    lv_obj_set_width(view->sender_lab, LAB02_CARD_W - 16);
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
#if CONFIG_LAB02_SLOW_BIND_MS > 0
    view->bind_timer = lv_timer_create(delayed_bind_cb, CONFIG_LAB02_SLOW_BIND_MS, view);
    lv_timer_pause(view->bind_timer);
#endif
}

void lab02_card_view_set_pos_for_index(lab02_card_view_t *view, int index)
{
    if (!view || !view->root) {
        return;
    }
    lv_obj_set_pos(view->root, card_x_for_index(index), 0);
}

void lab02_card_view_present(lab02_card_view_t *view, uint32_t generation,
                              const lab02_slot_snapshot_t *slot)
{
    if (!view || !slot) return;
    /* All property changes occur in one LVGL owner callback; no render can
     * interleave old content with the new immutable identity. */
    lab02_card_view_unbind(view, generation);
    view->bind_generation = generation;
    view->session_generation = slot->session_generation;
    view->inbox_revision = slot->inbox_revision;
    if (slot->state == LAB02_SLOT_OUT_OF_RANGE) return;
    view->bound_index = slot->index;
    lv_label_set_long_mode(view->sender_lab, LV_LABEL_LONG_WRAP);
    view->state = slot->state == LAB02_SLOT_ERROR ? LAB02_CARD_ERROR : LAB02_CARD_LOADING;
    lv_label_set_text(view->sender_lab, view->state == LAB02_CARD_ERROR ?
                      "Message unavailable" : "Loading message…");
    lv_obj_remove_flag(view->root, LV_OBJ_FLAG_HIDDEN);
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_REVISION |
                  LAB02_TRACE_FIELD_BIND_GENERATION | LAB02_TRACE_FIELD_CARD_SLOT |
                  LAB02_TRACE_FIELD_STATE,
        .session_id = slot->session_generation,
        .revision = slot->inbox_revision,
        .bind_generation = generation,
        .card_slot = view->card_slot >= 0 ? (uint32_t)view->card_slot : 0u,
        .state = view->state == LAB02_CARD_ERROR ? "error" : "loading",
    };
    lab02_trace_emit_payload("card_state", generation, slot->index,
                             slot->item ? slot->item->message_id : NULL, &payload);
    if (slot->state != LAB02_SLOT_BOUND || !slot->item) return;
    strncpy(view->bound_message_id, slot->item->message_id,
            sizeof(view->bound_message_id) - 1u);
    view->bound_message_id[sizeof(view->bound_message_id) - 1u] = '\0';
#if CONFIG_LAB02_SLOW_BIND_MS > 0
    if (view->bind_timer) {
        view->pending_item = *slot->item;
        view->pending_generation = generation;
        view->pending_session = slot->session_generation;
        view->pending_revision = slot->inbox_revision;
        view->pending_bind = true;
        lv_timer_reset(view->bind_timer);
        lv_timer_resume(view->bind_timer);
    } else apply_bind_labels(view, slot->item);
#else
    apply_bind_labels(view, slot->item);
#endif
}

void lab02_card_view_bind(lab02_card_view_t *view, int index, uint32_t generation,
                          const lab02_inbox_item_t *item)
{
    if (view && view->card_slot < 0) {
        view->card_slot = index;
    }
    lab02_session_snapshot_t session = lab02_session_snapshot();
    lab02_slot_snapshot_t slot = {.index = index, .item = item,
        .session_generation = lab02_session_generation(), .inbox_revision = session.inbox_revision,
        .state = item ? LAB02_SLOT_BOUND : LAB02_SLOT_LOADING};
    lab02_card_view_present(view, generation, &slot);
}

void lab02_card_view_unbind(lab02_card_view_t *view, uint32_t generation)
{
    if (!view) return;
    cancel_pending(view, "superseded");
    if (view->bound_message_id[0]) {
        const lab02_trace_payload_t payload = {
            .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_REVISION |
                      LAB02_TRACE_FIELD_BIND_GENERATION | LAB02_TRACE_FIELD_CARD_SLOT |
                      LAB02_TRACE_FIELD_STATE,
            .session_id = view->session_generation,
            .revision = view->inbox_revision,
            .bind_generation = generation,
            .card_slot = view->card_slot >= 0 ? (uint32_t)view->card_slot : 0u,
            .state = "unbound",
        };
        lab02_trace_emit_payload("unbind", generation, view->bound_index,
                                 view->bound_message_id, &payload);
        lab02_instrumentation_get()->unbind_count++;
    }
    clear_content(view);
    view->bound_index = -1;
    view->bind_generation = generation;
    view->bound_message_id[0] = 0;
    view->state = LAB02_CARD_UNBOUND;
    lv_obj_add_flag(view->root, LV_OBJ_FLAG_HIDDEN);
}

bool lab02_card_view_is_stale(const lab02_card_view_t *view, uint32_t generation)
{
    return view && view->bind_generation != 0 && view->bind_generation != generation;
}
