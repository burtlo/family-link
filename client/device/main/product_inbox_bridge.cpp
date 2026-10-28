#include "product_inbox_fw.h"
#include "product_inbox_http_worker.h"
#include "product_login_fw.h"
#include "product_shell_fw.h"
#include "product_volume_fw.h"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "fl_product_inbox_carousel.hpp"
#include "fl_product_inbox_presentation.hpp"
#include "fl_product_inbox_wire.hpp"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr const char *kTag = "product_inbox";
constexpr int kCardWidth = 132;
constexpr int kCardHeight = 134;
constexpr int kCardGap = 12;
constexpr int kCardStride = kCardWidth + kCardGap;
constexpr int kSlotWidth = 320;
constexpr int kCanvasBytes = fl_product_inbox::kSketchPixels * 2;
constexpr uint32_t kTextOnCard = 0x101418;
constexpr uint32_t kLowerFadeOutMs = 90;
constexpr uint32_t kLowerFadeInMs = 90;
constexpr int64_t kLowerFadeHoldMs = 200;

/* The carousel holds a full sketch frame. Internal DRAM BSS does not. */
EXT_RAM_BSS_ATTR fl_product_inbox::CarouselSession s_inbox;
fl_product_inbox::InboxWirePlanner s_wire;
lv_obj_t *s_upper_root = nullptr;
lv_obj_t *s_heading = nullptr;
lv_obj_t *s_play_button = nullptr;
lv_obj_t *s_play_label = nullptr;
lv_obj_t *s_lower_root = nullptr;
lv_obj_t *s_row = nullptr;
lv_obj_t *s_status = nullptr;
bool s_attached = false;
bool s_last_wifi_up = false;
bool s_http_allowed = false;
int s_ui_visible = 0;
int s_lower_kind = -1;
int s_lower_fade_out = 0;
int64_t s_fade_ms = -1000;
int s_dot_phase = 0;
int64_t s_dot_ms = 0;
int s_programmatic_scroll = 0;
int s_user_scrolling = 0;
int s_scroll_gesture = 0;
int s_press_scroll_x = 0;
char s_heading_shown[64];
char s_transport_shown[8];
char s_status_shown[32];
char s_card_order[fl_product_inbox::kMaxMessages * fl_product_inbox::kMaxMessageId];
EXT_RAM_BSS_ATTR uint16_t s_sketch_scratch[fl_product_inbox::kSketchPixels];

struct CardFill {
    const char *id;
    uint32_t fill;
};

struct CardWidget {
    char message_id[fl_product_inbox::kMaxMessageId];
    char sender_name[fl_product_inbox::kMaxName];
    char card_background_id[fl_product_inbox::kMaxCardBgId];
    uint32_t duration_ms;
    lv_obj_t *root;
    lv_obj_t *circle;
    lv_obj_t *initials;
    lv_obj_t *name;
    lv_obj_t *duration;
    lv_obj_t *canvas;
    uint8_t *canvas_buf;
    int painted_len;
    int painted_head;
    int painted_done;
    int has_ink;
    int alive;
};

const CardFill kCardFills[] = {
    {"card-bg-01", 0x5AA0E8}, {"card-bg-02", 0x67C587}, {"card-bg-03", 0xE8C040},
    {"card-bg-04", 0xD875A8}, {"card-bg-05", 0xE85B5B}, {"card-bg-06", 0x8B7CE8},
    {"card-bg-07", 0xE88840}, {"card-bg-08", 0x4EC4C8},
};

CardWidget s_cards[fl_product_inbox::kMaxMessages];

void wipe_bearer(char *bearer, int cap)
{
    if (bearer != nullptr && cap > 0) {
        memset(bearer, 0, (size_t)cap);
    }
}

bool submit_wire_job(const fl_product_inbox::InboxWireJob &wire, const char *bearer)
{
    product_inbox_http_job_t job;
    bool queued;
    memset(&job, 0, sizeof(job));
    job.kind = wire.kind;
    strncpy(job.method, wire.method, sizeof(job.method) - 1);
    strncpy(job.path, wire.path, sizeof(job.path) - 1);
    if (wire.body_len > 0 && wire.body_len < (int)sizeof(job.body)) {
        memcpy(job.body, wire.body, (size_t)wire.body_len);
        job.body_len = wire.body_len;
    }
    strncpy(job.range_header, wire.range_header, sizeof(job.range_header) - 1);
    strncpy(job.message_id, wire.message_id, sizeof(job.message_id) - 1);
    strncpy(job.sha256, wire.sha256, sizeof(job.sha256) - 1);
    if (bearer != nullptr) {
        strncpy(job.bearer, bearer, sizeof(job.bearer) - 1);
    }
    job.generation = wire.generation;
    job.operation_id = wire.operation_id;
    job.range_start = wire.range_start;
    job.asset_bytes = wire.asset_bytes;
    job.timeout_ms = wire.timeout_ms;
    queued = product_inbox_http_submit(&job);
    wipe_bearer(job.bearer, (int)sizeof(job.bearer));
    return queued;
}

void apply_http_result(const product_inbox_http_result_t *done, int64_t now_ms)
{
    if (done == nullptr) {
        return;
    }
    if (done->kind == fl_product_inbox::kInboxWireInbox) {
        if (done->parsed != nullptr) {
            s_wire.apply_inbox_parsed(s_inbox, done->generation,
                                      static_cast<const fl_product_inbox::InboxParseResult *>(done->parsed), now_ms);
            free(done->parsed);
        } else {
            s_wire.apply_inbox(s_inbox, done->generation, done->perform_ok, done->http_status, nullptr, 0, now_ms);
        }
    } else if (done->kind == fl_product_inbox::kInboxWireLastViewed) {
        if (done->http_status == 409 || done->http_status == 410) {
            ESP_LOGW(kTag, "last-viewed rejected http=%d", done->http_status);
        }
        s_wire.apply_last_viewed(s_inbox, done->generation, reinterpret_cast<const char *>(done->data), done->data_len,
                                 done->perform_ok, done->http_status, now_ms);
    } else if (done->kind == fl_product_inbox::kInboxWireAudio) {
        s_wire.apply_audio(s_inbox, done->generation, done->operation_id, done->message_id, done->perform_ok,
                           done->http_status, done->data, done->data_len, done->full_asset, done->hash_matches,
                           done->terminal, now_ms);
    } else if (done->kind == fl_product_inbox::kInboxWireSketch) {
        s_wire.apply_sketch(s_inbox, done->generation, done->operation_id, done->message_id, done->perform_ok,
                            done->http_status, done->data, done->data_len, done->full_asset, done->hash_matches,
                            done->terminal, now_ms);
    }
    if (done->data != nullptr) {
        free(done->data);
    }
}

void queue_due_jobs(int64_t now_ms)
{
    static fl_product_inbox::InboxWireSlots slots;
    fl_product_login::SessionWriter *login = product_login_session();
    const char *bearer = "";
    int ordinary_due;
    int recovery_due;
    const product_inbox_http_result_t *done;
    if (s_http_allowed) {
        (void)product_inbox_http_worker_start();
    }
    while ((done = product_inbox_http_poll()) != nullptr) {
        apply_http_result(done, now_ms);
    }
    ordinary_due = s_inbox.inbox_fetch_due(now_ms);
    recovery_due = s_inbox.recovery_fetch_due(now_ms);
    s_inbox.advance_to(now_ms);
    s_wire.collect(s_inbox, &slots, now_ms, ordinary_due, recovery_due);
    if (login != nullptr && login->credential_token() != nullptr) {
        bearer = login->credential_token();
    }
    if (slots.inbox) {
        if (s_http_allowed && submit_wire_job(slots.inbox_job, bearer)) {
            s_wire.accept_queued(s_inbox, fl_product_inbox::kInboxWireInbox, now_ms);
        } else {
            s_wire.refuse_queued(s_inbox, fl_product_inbox::kInboxWireInbox, now_ms);
        }
    }
    if (slots.last_viewed) {
        if (s_http_allowed && submit_wire_job(slots.last_viewed_job, bearer)) {
            s_wire.accept_queued(s_inbox, fl_product_inbox::kInboxWireLastViewed, now_ms);
        } else {
            s_wire.refuse_queued(s_inbox, fl_product_inbox::kInboxWireLastViewed, now_ms);
        }
    }
    if (slots.audio) {
        if (s_http_allowed && submit_wire_job(slots.audio_job, bearer)) {
            s_wire.accept_queued(s_inbox, fl_product_inbox::kInboxWireAudio, now_ms);
        } else {
            s_wire.refuse_queued(s_inbox, fl_product_inbox::kInboxWireAudio, now_ms);
        }
    }
    if (slots.sketch) {
        if (s_http_allowed && submit_wire_job(slots.sketch_job, bearer)) {
            s_wire.accept_queued(s_inbox, fl_product_inbox::kInboxWireSketch, now_ms);
        } else {
            s_wire.refuse_queued(s_inbox, fl_product_inbox::kInboxWireSketch, now_ms);
        }
    }
}

uint32_t card_fill(const char *id)
{
    size_t i;
    for (i = 0; i < sizeof(kCardFills) / sizeof(kCardFills[0]); ++i) {
        if (id != nullptr && strcmp(id, kCardFills[i].id) == 0) {
            return kCardFills[i].fill;
        }
    }
    return kCardFills[0].fill;
}

void initials_from_name(const char *name, char *out, int cap)
{
    int n = 0;
    const char *space;
    if (out == nullptr || cap < 2) {
        return;
    }
    if (name == nullptr || name[0] == '\0') {
        out[0] = '?';
        out[1] = '\0';
        return;
    }
    out[n++] = name[0];
    space = strchr(name, ' ');
    if (space != nullptr && space[1] != '\0' && n + 1 < cap) {
        out[n++] = space[1];
    }
    out[n] = '\0';
}

void set_hidden(lv_obj_t *obj, int hidden)
{
    if (obj == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_label_if_changed(lv_obj_t *label, char *cache, int cap, const char *text)
{
    if (label == nullptr || cache == nullptr || cap <= 0) {
        return;
    }
    if (text == nullptr) {
        text = "";
    }
    if (strcmp(cache, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
    strncpy(cache, text, (size_t)cap - 1);
    cache[cap - 1] = '\0';
}

void apply_root_visibility(int shown)
{
    set_hidden(s_upper_root, shown ? 0 : 1);
    set_hidden(s_lower_root, shown ? 0 : 1);
}

int find_card(const char *message_id)
{
    int i;
    if (message_id == nullptr || message_id[0] == '\0') {
        return -1;
    }
    for (i = 0; i < fl_product_inbox::kMaxMessages; ++i) {
        if (s_cards[i].alive && strcmp(s_cards[i].message_id, message_id) == 0) {
            return i;
        }
    }
    return -1;
}

void apply_face(CardWidget *card, int portrait_hidden)
{
    int hide_identity = portrait_hidden && card->canvas != nullptr ? 1 : 0;
    set_hidden(card->circle, hide_identity);
    set_hidden(card->initials, hide_identity);
    set_hidden(card->name, hide_identity);
    set_hidden(card->duration, hide_identity);
    set_hidden(card->canvas, hide_identity ? 0 : 1);
}

void destroy_card(int slot)
{
    CardWidget *card;
    if (slot < 0 || slot >= fl_product_inbox::kMaxMessages) {
        return;
    }
    card = &s_cards[slot];
    if (card->root != nullptr) {
        lv_obj_delete(card->root);
    }
    if (card->canvas_buf != nullptr) {
        free(card->canvas_buf);
    }
    memset(card, 0, sizeof(*card));
}

void destroy_all_cards(void)
{
    int i;
    for (i = 0; i < fl_product_inbox::kMaxMessages; ++i) {
        if (s_cards[i].alive) {
            destroy_card(i);
        }
    }
    s_card_order[0] = '\0';
}

int ensure_canvas(CardWidget *card)
{
    uint8_t *buf;
    if (card == nullptr || card->root == nullptr) {
        return 0;
    }
    if (card->canvas != nullptr && card->canvas_buf != nullptr) {
        return 1;
    }
    if (card->canvas_buf == nullptr) {
        buf = static_cast<uint8_t *>(heap_caps_malloc((size_t)kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buf == nullptr) {
            buf = static_cast<uint8_t *>(malloc((size_t)kCanvasBytes));
        }
        if (buf == nullptr) {
            return 0;
        }
        card->canvas_buf = buf;
    }
    card->canvas = lv_canvas_create(card->root);
    if (card->canvas == nullptr) {
        return 0;
    }
    /* flsk2 colors are native RGB565 uint16 values, same layout as this canvas. */
    lv_canvas_set_buffer(card->canvas, card->canvas_buf, fl_product_inbox::kSketchGrid,
                         fl_product_inbox::kSketchGrid, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(card->canvas, 6, 7);
    lv_obj_remove_flag(card->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(card->canvas, LV_OBJ_FLAG_SCROLLABLE);
    return 1;
}

int copy_frame(CardWidget *card, const uint16_t *pixels)
{
    if (card == nullptr || pixels == nullptr || ensure_canvas(card) == 0) {
        return 0;
    }
    memcpy(card->canvas_buf, pixels, (size_t)kCanvasBytes);
    lv_obj_invalidate(card->canvas);
    return 1;
}

void refresh_card_sketch(int slot, int index)
{
    CardWidget *card;
    int selected;
    int len;
    int head;
    int done;
    int ink;
    if (slot < 0 || slot >= fl_product_inbox::kMaxMessages) {
        return;
    }
    card = &s_cards[slot];
    if (card->alive == 0) {
        return;
    }
    selected = index == s_inbox.selected_index() ? 1 : 0;
    len = s_inbox.message_sketch_len(index);
    head = s_inbox.play_head_ms_at(index);
    done = s_inbox.message_completed_at(index);
    if (selected) {
        ink = s_inbox.sketch_visible();
        if (ink) {
            if (copy_frame(card, s_inbox.selected_sketch_pixels())) {
                card->has_ink = 1;
                card->painted_len = len;
                card->painted_head = head;
                card->painted_done = done;
            } else {
                card->has_ink = 0;
            }
        } else {
            card->has_ink = 0;
            card->painted_len = len;
            card->painted_head = head;
            card->painted_done = done;
        }
    } else if (len != card->painted_len || head != card->painted_head || done != card->painted_done) {
        ink = 0;
        if (len >= 8 &&
            s_inbox.paint_message_sketch(index, s_sketch_scratch, fl_product_inbox::kSketchPixels)) {
            if (copy_frame(card, s_sketch_scratch)) {
                ink = 1;
            }
        }
        if (ink || len < 8) {
            card->painted_len = len;
            card->painted_head = head;
            card->painted_done = done;
        }
        card->has_ink = ink;
    }
    apply_face(card, fl_product_inbox::card_face_portrait_hidden(card->has_ink));
}

void update_card_identity(int slot, int index)
{
    CardWidget *card;
    const fl_product_inbox::InboxMessage *message;
    fl_product_inbox::SenderPresentation view;
    char duration[16];
    char initials[4];
    if (slot < 0) {
        return;
    }
    card = &s_cards[slot];
    message = s_inbox.message_at(index);
    if (card->alive == 0 || message == nullptr || card->name == nullptr) {
        return;
    }
    view = fl_product_inbox::resolve_sender_presentation(*message);
    if (strcmp(card->sender_name, message->sender_name) != 0) {
        strncpy(card->sender_name, message->sender_name, sizeof(card->sender_name) - 1);
        card->sender_name[sizeof(card->sender_name) - 1] = '\0';
        initials_from_name(card->sender_name, initials, (int)sizeof(initials));
        lv_label_set_text(card->name, card->sender_name);
        if (card->initials != nullptr) {
            lv_label_set_text(card->initials, initials);
        }
    }
    if (strcmp(card->card_background_id, view.card_background_id) != 0) {
        strncpy(card->card_background_id, view.card_background_id, sizeof(card->card_background_id) - 1);
        card->card_background_id[sizeof(card->card_background_id) - 1] = '\0';
        lv_obj_set_style_bg_color(card->root, lv_color_hex(card_fill(card->card_background_id)), 0);
    }
    if (card->duration_ms != message->duration_ms) {
        card->duration_ms = message->duration_ms;
        fl_product_inbox::format_duration_mss(message->duration_ms, duration, (int)sizeof(duration));
        if (card->duration != nullptr) {
            lv_label_set_text(card->duration, duration);
        }
    }
    lv_obj_set_user_data(card->root, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
}

void render_inbox(void);

void on_card_pressed(lv_event_t *event)
{
    (void)event;
    s_scroll_gesture = 0;
    s_press_scroll_x = s_row != nullptr ? lv_obj_get_scroll_x(s_row) : 0;
}

void on_card_clicked(lv_event_t *event)
{
    lv_obj_t *obj;
    int index;
    if (event == nullptr || s_row == nullptr) {
        return;
    }
    if (s_scroll_gesture != 0) {
        s_scroll_gesture = 0;
        return;
    }
    obj = static_cast<lv_obj_t *>(lv_event_get_target(event));
    if (obj == nullptr) {
        return;
    }
    index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
    s_user_scrolling = 0;
    /* Tap selects and centers. It does not start, pause, or resume audio. */
    s_inbox.select_index(index, (int64_t)(esp_timer_get_time() / 1000LL));
    render_inbox();
}

void on_row_scroll(lv_event_t *event)
{
    lv_event_code_t code;
    int count;
    int index;
    int scroll;
    int target;
    if (event == nullptr || s_programmatic_scroll != 0 || s_row == nullptr) {
        return;
    }
    code = lv_event_get_code(event);
    count = s_inbox.message_count();
    if (count <= 0) {
        return;
    }
    scroll = lv_obj_get_scroll_x(s_row);
    if (scroll < s_press_scroll_x - 4 || scroll > s_press_scroll_x + 4) {
        s_scroll_gesture = 1;
    }
    if (code == LV_EVENT_SCROLL) {
        s_user_scrolling = 1;
        return;
    }
    if (code != LV_EVENT_SCROLL_END) {
        return;
    }
    index = scroll;
    if (index < 0) {
        index = 0;
    }
    index = (index + (kCardStride / 2)) / kCardStride;
    if (index >= count) {
        index = count - 1;
    }
    if (index < 0) {
        index = 0;
    }
    target = index * kCardStride;
    s_programmatic_scroll = 1;
    s_inbox.select_index(index, (int64_t)(esp_timer_get_time() / 1000LL));
    if (lv_obj_get_scroll_x(s_row) != target) {
        lv_obj_scroll_to_x(s_row, target, LV_ANIM_OFF);
    }
    s_programmatic_scroll = 0;
    s_user_scrolling = 0;
    render_inbox();
}

void on_play_clicked(lv_event_t *event)
{
    (void)event;
    /* Header transport only. Selection stays where it is. */
    s_inbox.toggle_transport((int64_t)(esp_timer_get_time() / 1000LL));
    render_inbox();
}

int create_card(const char *message_id)
{
    int slot;
    CardWidget *card;
    lv_obj_t *obj;
    for (slot = 0; slot < fl_product_inbox::kMaxMessages; ++slot) {
        if (s_cards[slot].alive == 0) {
            break;
        }
    }
    if (slot >= fl_product_inbox::kMaxMessages || s_row == nullptr || message_id == nullptr) {
        return -1;
    }
    obj = lv_obj_create(s_row);
    if (obj == nullptr) {
        return -1;
    }
    card = &s_cards[slot];
    memset(card, 0, sizeof(*card));
    card->alive = 1;
    card->duration_ms = 0xffffffffu;
    card->painted_len = -1;
    card->painted_head = -1;
    strncpy(card->message_id, message_id, sizeof(card->message_id) - 1);
    card->root = obj;
    lv_obj_set_size(obj, kCardWidth, kCardHeight);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(kCardFills[0].fill), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, on_card_pressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, on_card_clicked, LV_EVENT_CLICKED, nullptr);
    card->circle = lv_obj_create(obj);
    if (card->circle != nullptr) {
        lv_obj_set_size(card->circle, 66, 66);
        lv_obj_align(card->circle, LV_ALIGN_TOP_MID, 0, 14);
        lv_obj_set_style_radius(card->circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(card->circle, lv_color_hex(0xF4F4F4), 0);
        lv_obj_set_style_bg_opa(card->circle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card->circle, 0, 0);
        lv_obj_remove_flag(card->circle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(card->circle, LV_OBJ_FLAG_SCROLLABLE);
        card->initials = lv_label_create(card->circle);
        if (card->initials != nullptr) {
            lv_obj_set_style_text_color(card->initials, lv_color_hex(kTextOnCard), 0);
            lv_obj_set_style_text_font(card->initials, &lv_font_montserrat_16, 0);
            lv_obj_center(card->initials);
            lv_obj_remove_flag(card->initials, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    card->name = lv_label_create(obj);
    if (card->name != nullptr) {
        lv_obj_set_width(card->name, 116);
        lv_label_set_long_mode(card->name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(card->name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(card->name, lv_color_hex(kTextOnCard), 0);
        lv_obj_set_style_text_font(card->name, &lv_font_montserrat_16, 0);
        lv_obj_align(card->name, LV_ALIGN_TOP_MID, 0, 84);
        lv_obj_remove_flag(card->name, LV_OBJ_FLAG_CLICKABLE);
    }
    card->duration = lv_label_create(obj);
    if (card->duration != nullptr) {
        lv_obj_set_width(card->duration, 116);
        lv_obj_set_style_text_align(card->duration, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(card->duration, lv_color_hex(kTextOnCard), 0);
        lv_obj_set_style_text_font(card->duration, &lv_font_montserrat_16, 0);
        lv_obj_align(card->duration, LV_ALIGN_TOP_MID, 0, 104);
        lv_obj_remove_flag(card->duration, LV_OBJ_FLAG_CLICKABLE);
    }
    return slot;
}

void paint_card_focus(int center_index)
{
    int i;
    int count = s_inbox.message_count();
    for (i = 0; i < count; ++i) {
        const fl_product_inbox::InboxMessage *message = s_inbox.message_at(i);
        int slot;
        if (message == nullptr) {
            continue;
        }
        slot = find_card(message->message_id);
        if (slot < 0 || s_cards[slot].root == nullptr) {
            continue;
        }
        lv_obj_set_style_opa(s_cards[slot].root, i == center_index ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

void sync_cards(void)
{
    int count;
    int keep[fl_product_inbox::kMaxMessages];
    int order[fl_product_inbox::kMaxMessages];
    static char signature[sizeof(s_card_order)];
    int used;
    int i;
    if (s_row == nullptr) {
        return;
    }
    count = s_inbox.message_count();
    if (count > fl_product_inbox::kMaxMessages) {
        count = fl_product_inbox::kMaxMessages;
    }
    memset(keep, 0, sizeof(keep));
    for (i = 0; i < fl_product_inbox::kMaxMessages; ++i) {
        order[i] = -1;
    }
    used = 0;
    signature[0] = '\0';
    for (i = 0; i < count; ++i) {
        const fl_product_inbox::InboxMessage *message = s_inbox.message_at(i);
        int slot;
        int n;
        if (message == nullptr) {
            continue;
        }
        slot = find_card(message->message_id);
        if (slot < 0) {
            slot = create_card(message->message_id);
        }
        if (slot < 0) {
            continue;
        }
        order[i] = slot;
        keep[slot] = 1;
        update_card_identity(slot, i);
        refresh_card_sketch(slot, i);
        n = snprintf(signature + used, sizeof(signature) - (size_t)used, "%s;", message->message_id);
        if (n < 0 || used + n >= (int)sizeof(signature)) {
            used = (int)sizeof(signature) - 1;
            signature[used] = '\0';
            break;
        }
        used += n;
    }
    for (i = 0; i < fl_product_inbox::kMaxMessages; ++i) {
        if (s_cards[i].alive && keep[i] == 0) {
            destroy_card(i);
        }
    }
    if (strcmp(signature, s_card_order) != 0) {
        for (i = 0; i < count; ++i) {
            if (order[i] < 0 || s_cards[order[i]].root == nullptr) {
                continue;
            }
            lv_obj_move_to_index(s_cards[order[i]].root, i);
        }
        strncpy(s_card_order, signature, sizeof(s_card_order) - 1);
        s_card_order[sizeof(s_card_order) - 1] = '\0';
    }
}

void center_selection(void)
{
    int index;
    int target;
    if (s_row == nullptr || s_programmatic_scroll != 0 || s_user_scrolling != 0) {
        return;
    }
    index = s_inbox.selected_index();
    if (index < 0) {
        return;
    }
    target = index * kCardStride;
    if (lv_obj_get_scroll_x(s_row) == target) {
        return;
    }
    lv_obj_update_layout(s_row);
    if (lv_obj_get_scroll_x(s_row) == target) {
        return;
    }
    s_programmatic_scroll = 1;
    lv_obj_scroll_to_x(s_row, target, LV_ANIM_OFF);
    s_programmatic_scroll = 0;
}

void apply_header(const fl_product_inbox::InboxChrome &chrome)
{
    set_label_if_changed(s_heading, s_heading_shown, (int)sizeof(s_heading_shown), chrome.heading);
    if (s_play_button == nullptr) {
        return;
    }
    set_hidden(s_play_button, chrome.transport_hidden);
    if (chrome.transport_hidden) {
        return;
    }
    set_label_if_changed(s_play_label, s_transport_shown, (int)sizeof(s_transport_shown), chrome.transport_label);
    if (chrome.transport_disabled) {
        lv_obj_add_state(s_play_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(s_play_button, LV_STATE_DISABLED);
    }
}

void apply_status_text(const fl_product_inbox::InboxChrome &chrome, int64_t now_ms)
{
    char text[32];
    const char *shown;
    if (s_status == nullptr) {
        return;
    }
    if (chrome.lower_kind == fl_product_inbox::kLowerConnecting) {
        if (s_dot_ms == 0 || now_ms - s_dot_ms >= 400) {
            if (s_dot_ms != 0) {
                s_dot_phase = (s_dot_phase + 1) % 3;
            }
            s_dot_ms = now_ms;
        }
        fl_product_inbox::format_connecting_status((s_dot_phase % 3) + 1, text, (int)sizeof(text));
        shown = text;
    } else {
        shown = chrome.lower_text;
    }
    set_label_if_changed(s_status, s_status_shown, (int)sizeof(s_status_shown), shown);
}

void show_lower_body(const fl_product_inbox::InboxChrome &chrome, int64_t now_ms)
{
    int center;
    int first;
    first = s_lower_kind < 0 ? 1 : 0;
    if (chrome.lower_kind == fl_product_inbox::kLowerConnecting && s_lower_kind != fl_product_inbox::kLowerConnecting) {
        s_dot_phase = 0;
        s_dot_ms = now_ms;
    }
    if (chrome.lower_kind == fl_product_inbox::kLowerCarousel) {
        set_hidden(s_row, 0);
        set_hidden(s_status, 1);
        sync_cards();
        center_selection();
        center = s_inbox.selected_index();
        if (s_user_scrolling != 0 && s_row != nullptr) {
            int count = s_inbox.message_count();
            int scroll = lv_obj_get_scroll_x(s_row);
            if (count > 0) {
                center = scroll < 0 ? 0 : (scroll + (kCardStride / 2)) / kCardStride;
                if (center >= count) {
                    center = count - 1;
                }
            }
        }
        paint_card_focus(center);
    } else {
        /* Connecting, no Wi-Fi, and empty replace the carousel in this slot. */
        s_user_scrolling = 0;
        set_hidden(s_row, 1);
        set_hidden(s_status, 0);
        apply_status_text(chrome, now_ms);
        if (s_inbox.message_count() == 0) {
            destroy_all_cards();
        }
    }
    s_lower_kind = chrome.lower_kind;
    if (first && s_lower_root != nullptr) {
        lv_obj_set_style_opa(s_lower_root, LV_OPA_COVER, 0);
    }
}

void fade_inbox_lower_opa(void *obj, int32_t value)
{
    /* Opacity only. Returning the carousel must not start or pause audio. */
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(value), 0);
}

void start_lower_fade_in(void)
{
    lv_anim_t anim;
    if (s_lower_root == nullptr) {
        return;
    }
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_lower_root);
    lv_anim_set_values(&anim, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&anim, kLowerFadeInMs);
    lv_anim_set_exec_cb(&anim, fade_inbox_lower_opa);
    if (lv_anim_start(&anim) == nullptr) {
        lv_obj_set_style_opa(s_lower_root, LV_OPA_COVER, 0);
    }
}

void finish_lower_fade_out(lv_anim_t *anim)
{
    fl_product_inbox::InboxChrome chrome;
    int64_t now_ms;
    if (anim == nullptr || s_lower_root == nullptr || anim->var != static_cast<void *>(s_lower_root)) {
        s_lower_fade_out = 0;
        return;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    fl_product_inbox::inbox_chrome(s_inbox, &chrome);
    /* Slot is transparent. Swap to the current lower body, then fade it in.
     * Playback stays on the header Play control. */
    show_lower_body(chrome, now_ms);
    start_lower_fade_in();
    s_lower_fade_out = 0;
}

/* 1: fade-out is running, so the caller must keep the outgoing lower body.
 * 0: show that body now. A fade that began less than 200 ms ago is not restarted. */
int note_lower_kind(int kind)
{
    int64_t now_ms;
    lv_anim_t anim;
    if (s_lower_root == nullptr) {
        return 0;
    }
    if (s_lower_fade_out) {
        return 1;
    }
    if (s_lower_kind < 0 || kind == s_lower_kind) {
        return 0;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    if (now_ms - s_fade_ms < kLowerFadeHoldMs) {
        return 0;
    }
    s_fade_ms = now_ms;
    s_lower_fade_out = 1;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_lower_root);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, kLowerFadeOutMs);
    lv_anim_set_exec_cb(&anim, fade_inbox_lower_opa);
    lv_anim_set_completed_cb(&anim, finish_lower_fade_out);
    if (lv_anim_start(&anim) == nullptr) {
        s_lower_fade_out = 0;
        return 0;
    }
    return 1;
}

void render_inbox(void)
{
    fl_product_inbox::InboxChrome chrome;
    int64_t now_ms;
    int shown;
    if (s_upper_root == nullptr || s_lower_root == nullptr) {
        return;
    }
    shown = s_ui_visible;
    apply_root_visibility(shown);
    if (!shown) {
        return;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    fl_product_inbox::inbox_chrome(s_inbox, &chrome);
    apply_header(chrome);
    if (note_lower_kind(chrome.lower_kind)) {
        return;
    }
    show_lower_body(chrome, now_ms);
}

} /* namespace */

extern "C" bool product_inbox_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_inbox.boot(0);
    if (!product_inbox_build_widgets(upper_slot, lower_slot)) {
        return false;
    }
    s_attached = true;
    render_inbox();
    ESP_LOGI(kTag, "V2_PRODUCT_INBOX_READY build=%s", FIRMWARE_BUILD_ID);
    return true;
}

extern "C" void product_inbox_forget_widgets(void)
{
    int i;
    for (i = 0; i < fl_product_inbox::kMaxMessages; ++i) {
        if (s_cards[i].canvas_buf != nullptr) {
            free(s_cards[i].canvas_buf);
        }
    }
    memset(s_cards, 0, sizeof(s_cards));
    s_card_order[0] = '\0';
    s_upper_root = nullptr;
    s_heading = nullptr;
    s_play_button = nullptr;
    s_play_label = nullptr;
    s_lower_root = nullptr;
    s_row = nullptr;
    s_status = nullptr;
    s_heading_shown[0] = '\0';
    s_transport_shown[0] = '\0';
    s_status_shown[0] = '\0';
    s_ui_visible = 0;
    s_lower_kind = -1;
    s_lower_fade_out = 0;
    s_user_scrolling = 0;
}

extern "C" bool product_inbox_build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_upper_root = lv_obj_create(upper_slot);
    s_lower_root = lv_obj_create(lower_slot);
    if (s_upper_root == nullptr || s_lower_root == nullptr) {
        return false;
    }
    lv_obj_remove_style_all(s_upper_root);
    lv_obj_remove_style_all(s_lower_root);
    lv_obj_set_size(s_upper_root, 320, 80);
    lv_obj_set_size(s_lower_root, 320, 160);
    lv_obj_set_pos(s_upper_root, 0, 0);
    lv_obj_set_pos(s_lower_root, 0, 0);
    lv_obj_remove_flag(s_upper_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_lower_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_upper_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_lower_root, LV_OBJ_FLAG_CLICKABLE);
    s_heading = lv_label_create(s_upper_root);
    s_play_button = lv_button_create(s_upper_root);
    s_row = lv_obj_create(s_lower_root);
    s_status = lv_label_create(s_lower_root);
    if (s_heading == nullptr || s_play_button == nullptr || s_row == nullptr || s_status == nullptr) {
        return false;
    }
    s_play_label = lv_label_create(s_play_button);
    if (s_play_label == nullptr) {
        return false;
    }
    lv_obj_set_width(s_heading, 200);
    lv_label_set_long_mode(s_heading, LV_LABEL_LONG_DOT);
    lv_obj_align(s_heading, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_text_color(s_heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_heading, &lv_font_montserrat_16, 0);
    lv_obj_set_size(s_play_button, 88, 40);
    lv_obj_align(s_play_button, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_event_cb(s_play_button, on_play_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_text_color(s_play_label, lv_color_hex(kTextOnCard), 0);
    lv_obj_set_style_text_font(s_play_label, &lv_font_montserrat_16, 0);
    lv_obj_center(s_play_label);
    lv_obj_set_size(s_row, kSlotWidth, 160);
    lv_obj_set_pos(s_row, 0, 0);
    lv_obj_set_style_bg_opa(s_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_row, 0, 0);
    lv_obj_set_style_pad_all(s_row, 0, 0);
    lv_obj_set_style_pad_left(s_row, (kSlotWidth - kCardWidth) / 2, 0);
    lv_obj_set_style_pad_right(s_row, (kSlotWidth - kCardWidth) / 2, 0);
    lv_obj_set_style_pad_column(s_row, kCardGap, 0);
    lv_obj_set_flex_flow(s_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_row, on_card_pressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_row, on_row_scroll, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(s_row, on_row_scroll, LV_EVENT_SCROLL_END, nullptr);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, 0);
    lv_obj_center(s_status);
    s_heading_shown[0] = '\0';
    s_transport_shown[0] = '\0';
    s_status_shown[0] = '\0';
    s_ui_visible = 0;
    s_lower_kind = -1;
    s_lower_fade_out = 0;
    s_dot_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    apply_root_visibility(0);
    return true;
}

extern "C" void product_inbox_service(int64_t now_ms)
{
    if (!s_attached) {
        return;
    }
    /* Queue and apply only. HTTP runs on the inbox worker task. */
    queue_due_jobs(now_ms);
    render_inbox();
}

extern "C" void product_inbox_on_signed_in(void)
{
    fl_product_login::SessionWriter *login;
    uint32_t generation;
    int64_t now_ms;
    if (!s_attached) {
        return;
    }
    now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    s_wire.reset();
    s_inbox.boot(now_ms);
    login = product_login_session();
    generation = 1;
    if (login != nullptr && login->generation() != 0) {
        generation = login->generation();
    }
    s_inbox.set_session_generation(generation);
    s_inbox.set_wifi(s_last_wifi_up, now_ms);
    s_http_allowed = true;
    (void)product_inbox_http_worker_start();
    s_user_scrolling = 0;
    destroy_all_cards();
    render_inbox();
}

extern "C" void product_inbox_discard_results(void)
{
    const product_inbox_http_result_t *done;
    if (!s_attached) {
        return;
    }
    while ((done = product_inbox_http_poll()) != nullptr) {
        if (done->parsed != nullptr) {
            free(done->parsed);
        }
        if (done->data != nullptr) {
            free(done->data);
        }
    }
}

extern "C" void product_inbox_on_mute(void)
{
    if (!s_attached) {
        return;
    }
    /* The LVGL timer samples the mute latch. Deleting the inbox HTTP task
     * from this timer can stall it while that task holds the heap lock, and
     * a later unmute then never turns the backlight back on. Leave the task
     * running. Later ticks drain its results; on_mute retires the generation
     * so those results cannot restore this session. */
    s_http_allowed = false;
    s_wire.reset();
    s_inbox.on_mute((int64_t)(esp_timer_get_time() / 1000LL));
    s_user_scrolling = 0;
    s_lower_fade_out = 0;
    if (s_lower_root != nullptr) {
        lv_anim_delete(s_lower_root, nullptr);
        lv_obj_set_style_opa(s_lower_root, LV_OPA_COVER, 0);
    }
    /* Login rebuilds its roster from the same LVGL pool on unmute. */
    destroy_all_cards();
}

extern "C" void product_inbox_note_refresh(int64_t now_ms)
{
    if (!s_attached) {
        return;
    }
    s_inbox.note_refresh_success(now_ms);
}

extern "C" void product_inbox_note_refresh_failure(int64_t now_ms)
{
    if (!s_attached) {
        return;
    }
    s_inbox.note_refresh_failure(now_ms);
}

extern "C" void product_inbox_set_wifi(bool up)
{
    s_last_wifi_up = up;
    if (!s_attached) {
        return;
    }
    s_inbox.set_wifi(up, (int64_t)(esp_timer_get_time() / 1000LL));
    render_inbox();
}

extern "C" void product_inbox_set_ui_visible(int visible)
{
    const int shown = visible ? 1 : 0;
    if (!s_attached) {
        return;
    }
    /* The carousel model gates fetch and connecting retry on this flag, and
     * boot()/on_mute() may reset it, so the model is written every tick. */
    s_inbox.set_ui_visible(shown);
    if (shown == s_ui_visible) {
        return;
    }
    s_ui_visible = shown;
    if (shown == 0) {
        s_user_scrolling = 0;
    }
    render_inbox();
}

fl_product_inbox::CarouselSession *product_inbox_session(void) { return &s_inbox; }

extern "C" int product_inbox_on_boot_pressed(void)
{
    if (!s_attached) {
        return 0;
    }
    if (product_volume_active()) {
        return product_volume_on_boot_leave();
    }
    /* Picker, wipe, record, and receipt own Boot. Only the inbox opens volume. */
    if (product_shell_stage() != FL_SHELL_STAGE_INBOX) {
        return 0;
    }
    return product_volume_on_boot_from_inbox();
}
