#include "product_send_fw.h"

#include "product_inbox_fw.h"
#include "product_login_fw.h"
#include "product_mem_probe.h"
#include "product_record_fw.h"
#include "product_send_http_worker.h"
#include "product_split_pane.hpp"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fl_product_record_lab10.h"
#include "fl_product_send_classify.hpp"
#include "fl_product_send_journey.hpp"
#include "fl_product_send_stage.hpp"

namespace {

static_assert(static_cast<int>(fl_product_send::kSendIntentRed) == PRODUCT_SEND_INTENT_RED,
              "red intent");
static_assert(static_cast<int>(fl_product_send::kSendIntentBoot) == PRODUCT_SEND_INTENT_BOOT,
              "boot intent");
static_assert(static_cast<int>(fl_product_send::kSendIntentMute) == PRODUCT_SEND_INTENT_MUTE,
              "mute intent");
static_assert(static_cast<int>(fl_product_send::kSendIntentWipeComplete) == PRODUCT_SEND_INTENT_WIPE_COMPLETE,
              "wipe intent");
static_assert(static_cast<int>(fl_product_send::kSendIntentCaptureFailed) == PRODUCT_SEND_INTENT_CAPTURE_FAILED,
              "capture failed intent");
static_assert(static_cast<int>(fl_product_send::kSendIntentTakeFinished) == PRODUCT_SEND_INTENT_TAKE_FINISHED,
              "take finished intent");
static_assert(fl_product_send::kSendRingReverseMs == LAB10_RING_DURATION_MS, "ring duration");

constexpr const char *kTag = "product_send";
constexpr int kCardWidth = 132;
constexpr int kCardHeight = 133;
constexpr int kCardGap = 12;
constexpr int kCardStride = kCardWidth + kCardGap;
constexpr uint32_t kAccent = 0xE8C040;

/* Same catalog fills as the sign-in roster in product_login_bridge.cpp. */
struct CardFill {
    const char *id;
    uint32_t fill;
};

const CardFill kCardFills[] = {
    {"card-bg-01", 0x5AA0E8}, {"card-bg-02", 0x67C587}, {"card-bg-03", 0xE8C040},
    {"card-bg-04", 0xD875A8}, {"card-bg-05", 0xE85B5B}, {"card-bg-06", 0x8B7CE8},
    {"card-bg-07", 0xE88840}, {"card-bg-08", 0x4EC4C8},
};

fl_product_send::SendJourney s_journey;
fl_product_send::SendStageReducer s_stage;
lv_obj_t *s_upper_label = nullptr;
lv_obj_t *s_subtitle = nullptr;
lv_obj_t *s_lower_label = nullptr;
lv_obj_t *s_lower_slot = nullptr;
lv_obj_t *s_picker_header = nullptr;
lv_obj_t *s_empty_label = nullptr;
lv_obj_t *s_roster_row = nullptr;
lv_obj_t *s_cards[fl_product_send::kMaxRecipients];
char s_card_sig[1024];
int s_visible = 0;
int s_picker_open = 0;
int s_programmatic_scroll = 0;
int s_scroll_gesture = 0;
int s_press_scroll_x = 0;
bool s_attached = false;
int s_wifi_up = 0;
bool s_ids_seeded = false;
int s_dot_phase = 0;
int64_t s_dot_ms = 0;
int s_http_started = 0;
char s_media_id[fl_product_send::kMaxClientMessageId];
char s_released_id[fl_product_send::kMaxClientMessageId];

struct PendingSend {
    int valid;
    fl_product_send::FrozenPayload payload;
    char names[fl_product_send::kMaxRecipients][fl_product_send::kMaxName];
    int name_count;
};

PendingSend s_pending;
char s_finished_sender[fl_product_send::kMaxUserId];

int shell_for_place(int place)
{
    switch (place) {
    case fl_product_send::kSendPlacePicker:
    case fl_product_send::kSendPlaceCouldntRecord:
        return (int)FL_SHELL_STAGE_PICKER;
    case fl_product_send::kSendPlaceWipe:
    case fl_product_send::kSendPlaceRecording:
    case fl_product_send::kSendPlaceReverse:
    case fl_product_send::kSendPlaceFinishReverse:
        return (int)FL_SHELL_STAGE_RECORD;
    case fl_product_send::kSendPlaceReceipt:
        return (int)FL_SHELL_STAGE_RECEIPT;
    default:
        return (int)FL_SHELL_STAGE_INBOX;
    }
}

void remember_finished_sender(void)
{
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user = session != nullptr ? session->signed_user() : nullptr;
    if (user == nullptr || user[0] == '\0') {
        return;
    }
    strncpy(s_finished_sender, user, sizeof(s_finished_sender) - 1);
    s_finished_sender[sizeof(s_finished_sender) - 1] = '\0';
}

int resume_place_ok(void)
{
    int place = s_stage.place();
    if (place == fl_product_send::kSendPlaceWipe || place == fl_product_send::kSendPlaceRecording ||
        place == fl_product_send::kSendPlaceReverse || place == fl_product_send::kSendPlaceFinishReverse ||
        place == fl_product_send::kSendPlaceCouldntRecord) {
        return 0;
    }
    return 1;
}

void hide_obj(lv_obj_t *obj)
{
    if (obj != nullptr) {
        lv_obj_set_hidden(obj, true);
    }
}

void show_obj(lv_obj_t *obj)
{
    if (obj != nullptr) {
        lv_obj_set_hidden(obj, false);
    }
}

uint32_t card_fill(const char *id)
{
    int i;
    for (i = 0; i < (int)(sizeof(kCardFills) / sizeof(kCardFills[0])); ++i) {
        if (id != nullptr && strcmp(id, kCardFills[i].id) == 0) {
            return kCardFills[i].fill;
        }
    }
    return kCardFills[0].fill;
}

void copy_field(char *dst, int cap, const char *src)
{
    if (dst == nullptr || cap <= 0) {
        return;
    }
    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }
    strncpy(dst, src, (size_t)cap - 1);
    dst[cap - 1] = '\0';
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

void format_body(char *out, int cap)
{
    const char *label = s_journey.body_label();
    fl_product_send::SendUiPhase phase = s_journey.ui_phase();
    int n;
    int dots;
    int i;
    if (out == nullptr || cap <= 0) {
        return;
    }
    if (label == nullptr) {
        label = "";
    }
    n = snprintf(out, (size_t)cap, "%s", label);
    if (n < 0 || n >= cap) {
        return;
    }
    if (phase != fl_product_send::kSendUiPreparing && phase != fl_product_send::kSendUiSending) {
        return;
    }
    dots = (s_dot_phase % 3) + 1;
    for (i = 0; i < dots && n + 1 < cap; ++i) {
        out[n++] = '.';
    }
    out[n] = '\0';
}

void render_send()
{
    char title[32];
    char subtitle[96];
    char body[64];
    if (stage_shell::splitPane().stage() == FL_SHELL_STAGE_PICKER) {
        return;
    }
    s_journey.receipt_title(title, (int)sizeof(title));
    s_journey.receipt_subtitle(subtitle, (int)sizeof(subtitle));
    format_body(body, (int)sizeof(body));
    if (s_upper_label != nullptr) {
        lv_label_set_text(s_upper_label, title);
    }
    if (s_subtitle != nullptr) {
        lv_label_set_text(s_subtitle, subtitle);
    }
    if (s_lower_label != nullptr) {
        lv_label_set_text(s_lower_label, body);
    }
}

void paint_header()
{
    char text[96];
    if (s_picker_header == nullptr) {
        return;
    }
    s_journey.picker().header_text(text, (int)sizeof(text));
    lv_label_set_text(s_picker_header, text);
}

void paint_card_chrome()
{
    int focus = s_journey.picker().focused_index();
    int n = s_journey.picker().member_count();
    int i;
    for (i = 0; i < n && i < fl_product_send::kMaxRecipients; ++i) {
        lv_obj_t *card = s_cards[i];
        if (card == nullptr) {
            continue;
        }
        lv_obj_set_style_bg_opa(card, i == focus ? LV_OPA_COVER : LV_OPA_60, 0);
        if (s_journey.picker().is_selected(i)) {
            lv_obj_set_style_border_width(card, 2, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(kAccent), 0);
            lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_border_width(card, 0, 0);
        }
    }
}

void append_sig(char *out, int cap, int *used, const char *src)
{
    int k;
    if (out == nullptr || used == nullptr || cap <= 0 || src == nullptr) {
        return;
    }
    for (k = 0; src[k] != '\0' && *used + 1 < cap; ++k) {
        out[*used] = src[k];
        *used += 1;
    }
    out[*used] = '\0';
}

void roster_signature(char *out, int cap)
{
    int n = s_journey.picker().member_count();
    int used = 0;
    int i;
    if (out == nullptr || cap <= 0) {
        return;
    }
    out[0] = '\0';
    for (i = 0; i < n && used + 1 < cap; ++i) {
        fl_product_send::RecipientCard card = s_journey.picker().card_for_index(i);
        char flag[4];
        if (i > 0) {
            append_sig(out, cap, &used, ";");
        }
        append_sig(out, cap, &used, s_journey.picker().user_id_at(i));
        append_sig(out, cap, &used, "|");
        append_sig(out, cap, &used, card.display_name);
        append_sig(out, cap, &used, "|");
        append_sig(out, cap, &used, card.avatar_id);
        append_sig(out, cap, &used, "|");
        append_sig(out, cap, &used, card.card_background_id);
        append_sig(out, cap, &used, "|");
        flag[0] = card.use_initials_fallback ? 'I' : 'A';
        flag[1] = card.use_card_fallback ? 'C' : 'K';
        flag[2] = '\0';
        append_sig(out, cap, &used, flag);
    }
}

int index_at_scroll(int scroll, int count)
{
    int index;
    if (count <= 0) {
        return 0;
    }
    if (scroll < 0) {
        scroll = 0;
    }
    index = (scroll + (kCardStride / 2)) / kCardStride;
    if (index < 0) {
        index = 0;
    }
    if (index >= count) {
        index = count - 1;
    }
    return index;
}

void clear_cards()
{
    int i;
    if (s_roster_row != nullptr) {
        lv_obj_delete(s_roster_row);
        s_roster_row = nullptr;
    }
    for (i = 0; i < fl_product_send::kMaxRecipients; ++i) {
        s_cards[i] = nullptr;
    }
}

void on_picker_card_pressed(lv_event_t *event)
{
    (void)event;
    s_scroll_gesture = 0;
    s_press_scroll_x = s_roster_row != nullptr ? lv_obj_get_scroll_x(s_roster_row) : 0;
}

void on_picker_card_clicked(lv_event_t *event)
{
    int index;
    if (event == nullptr) {
        return;
    }
    if (s_scroll_gesture != 0) {
        s_scroll_gesture = 0;
        return;
    }
    index = (int)(intptr_t)lv_event_get_user_data(event);
    s_journey.picker().toggle_selection(index);
    paint_header();
    paint_card_chrome();
}

void on_picker_scroll(lv_event_t *event)
{
    lv_event_code_t code;
    int count;
    int index;
    int scroll;
    if (event == nullptr || s_programmatic_scroll != 0 || s_roster_row == nullptr) {
        return;
    }
    code = lv_event_get_code(event);
    count = s_journey.picker().member_count();
    if (count <= 0) {
        return;
    }
    scroll = lv_obj_get_scroll_x(s_roster_row);
    if (scroll < s_press_scroll_x - 4 || scroll > s_press_scroll_x + 4) {
        s_scroll_gesture = 1;
    }
    index = index_at_scroll(scroll, count);
    s_journey.picker().set_focus(index);
    if (code == LV_EVENT_SCROLL_END) {
        s_programmatic_scroll = 1;
        lv_obj_scroll_to_x(s_roster_row, index * kCardStride, LV_ANIM_OFF);
        s_programmatic_scroll = 0;
    }
    paint_card_chrome();
}

void build_cards()
{
    int count;
    int i;
    int focus;
    if (s_lower_slot == nullptr) {
        return;
    }
    clear_cards();
    count = s_journey.picker().member_count();
    if (count <= 0) {
        return;
    }
    if (count > fl_product_send::kMaxRecipients) {
        count = fl_product_send::kMaxRecipients;
    }
    s_roster_row = lv_obj_create(s_lower_slot);
    if (s_roster_row == nullptr) {
        return;
    }
    lv_obj_set_size(s_roster_row, 320, 160);
    lv_obj_set_pos(s_roster_row, 0, 0);
    lv_obj_set_style_bg_opa(s_roster_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_roster_row, 0, 0);
    lv_obj_set_style_pad_all(s_roster_row, 0, 0);
    lv_obj_set_style_pad_left(s_roster_row, (320 - kCardWidth) / 2, 0);
    lv_obj_set_style_pad_right(s_roster_row, (320 - kCardWidth) / 2, 0);
    lv_obj_set_style_pad_column(s_roster_row, kCardGap, 0);
    lv_obj_set_flex_flow(s_roster_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_roster_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_roster_row, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_roster_row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_roster_row, on_picker_scroll, LV_EVENT_SCROLL, nullptr);
    lv_obj_add_event_cb(s_roster_row, on_picker_scroll, LV_EVENT_SCROLL_END, nullptr);
    for (i = 0; i < count; ++i) {
        fl_product_send::RecipientCard card = s_journey.picker().card_for_index(i);
        const char *name = card.display_name;
        char initials[4];
        lv_obj_t *obj;
        lv_obj_t *circle;
        lv_obj_t *mark;
        lv_obj_t *label;
        if (name[0] == '\0') {
            name = s_journey.picker().user_id_at(i);
        }
        /* Portrait art is not on device yet. Paint initials for a known catalog
           avatar id the same way as a missing or unrecognized id. */
        initials_from_name(name, initials, (int)sizeof(initials));
        obj = lv_obj_create(s_roster_row);
        if (obj == nullptr) {
            break;
        }
        s_cards[i] = obj;
        lv_obj_set_size(obj, kCardWidth, kCardHeight);
        lv_obj_set_style_radius(obj, 8, 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(card_fill(card.card_background_id)), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(obj, 0, 0);
        lv_obj_set_style_pad_all(obj, 0, 0);
        lv_obj_set_scrollable(obj, false);
        lv_obj_set_clickable(obj, true);
        lv_obj_add_event_cb(obj, on_picker_card_pressed, LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(obj, on_picker_card_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        circle = lv_obj_create(obj);
        if (circle != nullptr) {
            lv_obj_set_size(circle, 66, 66);
            lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 16);
            lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(circle, lv_color_hex(0xF4F4F4), 0);
            lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(circle, 0, 0);
            lv_obj_set_clickable(circle, false);
            lv_obj_set_scrollable(circle, false);
            mark = lv_label_create(circle);
            if (mark != nullptr) {
                lv_label_set_text(mark, initials);
                lv_obj_set_style_text_color(mark, lv_color_hex(0x101418), 0);
                lv_obj_set_style_text_font(mark, &lv_font_montserrat_16, 0);
                lv_obj_center(mark);
                lv_obj_set_clickable(mark, false);
            }
        }
        label = lv_label_create(obj);
        if (label != nullptr) {
            lv_label_set_text(label, name != nullptr ? name : "");
            lv_obj_set_width(label, 116);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x101418), 0);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -12);
            lv_obj_set_clickable(label, false);
        }
    }
    focus = s_journey.picker().focused_index();
    if (focus < 0) {
        focus = 0;
    }
    s_programmatic_scroll = 1;
    lv_obj_scroll_to_x(s_roster_row, focus * kCardStride, LV_ANIM_OFF);
    s_programmatic_scroll = 0;
    paint_card_chrome();
}

void sync_cards()
{
    char sig[1024];
    int count = s_journey.picker().member_count();
    roster_signature(sig, (int)sizeof(sig));
    if (strcmp(sig, s_card_sig) != 0 || (count > 0 && s_roster_row == nullptr)) {
        build_cards();
        strncpy(s_card_sig, sig, sizeof(s_card_sig) - 1);
        s_card_sig[sizeof(s_card_sig) - 1] = '\0';
    } else if (count > 0) {
        paint_card_chrome();
    }
}

void apply_roster(int fresh)
{
    fl_product_send::HangoutMember members[fl_product_login::kMaxUsers];
    fl_product_login::SessionWriter *session = product_login_session();
    const char *self_id = "";
    int count = 0;
    int i;
    int n = 0;
    if (session != nullptr) {
        self_id = session->signed_user();
        if (self_id == nullptr) {
            self_id = "";
        }
        count = session->user_count();
        if (count > fl_product_login::kMaxUsers) {
            count = fl_product_login::kMaxUsers;
        }
        for (i = 0; i < count; ++i) {
            const char *avatar;
            memset(&members[n], 0, sizeof(members[n]));
            copy_field(members[n].user_id, fl_product_send::kMaxUserId, session->user_at(i));
            copy_field(members[n].display_name, fl_product_send::kMaxName, session->user_display_name_at(i));
            copy_field(members[n].card_background_id, fl_product_send::kMaxCardBgId,
                       session->user_card_background_at(i));
            avatar = session->user_avatar_id_at(i);
            if (session->user_uses_initials_fallback_at(i) != 0) {
                members[n].avatar_id[0] = '\0';
            } else {
                copy_field(members[n].avatar_id, fl_product_send::kMaxAvatarId, avatar);
            }
            n += 1;
        }
    }
    if (fresh) {
        s_journey.picker().load_members(n > 0 ? members : 0, n, self_id);
    } else {
        s_journey.picker().refresh_members(n > 0 ? members : 0, n, self_id);
    }
}

void hide_picker()
{
    hide_obj(s_picker_header);
    hide_obj(s_roster_row);
    hide_obj(s_empty_label);
}

void show_picker()
{
    int count = s_journey.picker().member_count();
    show_obj(s_picker_header);
    if (count <= 0) {
        hide_obj(s_roster_row);
        if (s_empty_label != nullptr) {
            lv_label_set_text(s_empty_label, "No people to message");
        }
        show_obj(s_empty_label);
        return;
    }
    hide_obj(s_empty_label);
    show_obj(s_roster_row);
}

void hide_receipt()
{
    hide_obj(s_upper_label);
    hide_obj(s_subtitle);
    hide_obj(s_lower_label);
}

void show_receipt()
{
    show_obj(s_upper_label);
    show_obj(s_subtitle);
    show_obj(s_lower_label);
    render_send();
}

void show_couldnt_record()
{
    hide_obj(s_upper_label);
    hide_obj(s_subtitle);
    hide_obj(s_roster_row);
    hide_obj(s_empty_label);
    show_obj(s_picker_header);
    if (s_lower_label != nullptr) {
        lv_label_set_text(s_lower_label, s_stage.lower_status());
        lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
        show_obj(s_lower_label);
    }
}

void paint_for_stage()
{
    int picker = stage_shell::splitPane().stage() == FL_SHELL_STAGE_PICKER;
    int receipt = stage_shell::splitPane().stage() == FL_SHELL_STAGE_RECEIPT;
    if (!s_attached) {
        return;
    }
    if (!s_visible) {
        picker = 0;
        receipt = 0;
    } else if (s_stage.place() == fl_product_send::kSendPlaceCouldntRecord) {
        show_couldnt_record();
        return;
    }
    if (!picker) {
        s_picker_open = 0;
        s_card_sig[0] = '\0';
        hide_picker();
    }
    if (!receipt) {
        hide_receipt();
    }
    if (picker) {
        apply_roster(s_picker_open == 0 ? 1 : 0);
        s_picker_open = 1;
        sync_cards();
        paint_header();
        show_picker();
        return;
    }
    if (receipt) {
        show_receipt();
    }
}

void present_stage_change(const fl_product_send::SendStageResult &advanced)
{
    if (advanced.changed == 0) {
        return;
    }
    stage_shell::splitPane().setStage((fl_shell_stage_t)shell_for_place(advanced.place));
    product_mem_probe("stage_service", advanced.place);
}

int read_stored_part(void *ctx, int kind, uint32_t offset, uint8_t *dest, uint32_t len, uint32_t *out_read)
{
    const char *id = static_cast<const char *>(ctx);
    return product_record_read_finished(id, kind, offset, dest, len, out_read);
}

int start_payload(const fl_product_send::FrozenPayload &payload, const char names[][fl_product_send::kMaxName],
                  int name_count, int64_t now_ms)
{
    fl_product_send::MediaPartReader reader;
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user = session != nullptr ? session->signed_user() : "";
    if (user == nullptr || user[0] == '\0' || strcmp(user, payload.sender_user_id) != 0) {
        return 0;
    }
    if (s_journey.outbox().active()) {
        return 0;
    }
    strncpy(s_media_id, payload.client_message_id, sizeof(s_media_id) - 1);
    s_media_id[sizeof(s_media_id) - 1] = '\0';
    memset(&reader, 0, sizeof(reader));
    reader.ctx = s_media_id;
    reader.read_at = read_stored_part;
    s_journey.set_signed_in_user(user);
    s_journey.install_receipt_names(names, name_count);
    if (!s_journey.bind_stored_payload(payload, reader)) {
        return 0;
    }
    if (!product_record_mark_uploading(payload.client_message_id)) {
        return 0;
    }
    s_journey.outbox().begin_submit(now_ms);
    product_mem_probe("upload_begin", (int32_t)payload.audio_byte_count);
    s_released_id[0] = '\0';
    return 1;
}

int build_finished_payload(fl_product_send::FrozenPayload *payload)
{
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user;
    const char *id;
    uint32_t audio = 0;
    uint32_t sketch = 0;
    int n;
    int i;
    if (payload == nullptr || session == nullptr) {
        return 0;
    }
    user = session->signed_user();
    if ((user == nullptr || user[0] == '\0') && s_finished_sender[0] == '\0') {
        return 0;
    }
    id = s_journey.capture_client_message_id();
    if (id == nullptr || id[0] == '\0') {
        return 0;
    }
    if (!product_record_finished_info(id, &audio, &sketch) || audio == 0) {
        return 0;
    }
    n = s_journey.frozen_recipient_count();
    if (n <= 0) {
        return 0;
    }
    if (n > fl_product_send::kMaxRecipients) {
        n = fl_product_send::kMaxRecipients;
    }
    memset(payload, 0, sizeof(*payload));
    strncpy(payload->client_message_id, id, sizeof(payload->client_message_id) - 1);
    strncpy(payload->sender_user_id, s_finished_sender[0] != '\0' ? s_finished_sender : user,
            sizeof(payload->sender_user_id) - 1);
    for (i = 0; i < n; ++i) {
        strncpy(payload->recipient_user_ids[i], s_journey.frozen_recipient_id(i), fl_product_send::kMaxUserId - 1);
    }
    payload->recipient_count = n;
    payload->audio_byte_count = audio;
    payload->sketch_byte_count = sketch;
    payload->sketch_present = sketch > 0 ? 1 : 0;
    payload->duration_ms = audio / 32u;
    return 1;
}

void try_start_current(int64_t now_ms)
{
    fl_product_send::FrozenPayload payload;
    char names[fl_product_send::kMaxRecipients][fl_product_send::kMaxName];
    int count;
    if (s_journey.outbox().active()) {
        if (s_pending.valid) {
            return;
        }
        if (!build_finished_payload(&s_pending.payload)) {
            return;
        }
        if (strcmp(s_pending.payload.client_message_id, s_journey.outbox().client_message_id()) == 0) {
            memset(&s_pending, 0, sizeof(s_pending));
            return;
        }
        s_pending.name_count = s_journey.copy_frozen_names(s_pending.names, fl_product_send::kMaxRecipients);
        s_pending.valid = 1;
        return;
    }
    if (!build_finished_payload(&payload)) {
        return;
    }
    count = s_journey.copy_frozen_names(names, fl_product_send::kMaxRecipients);
    (void)start_payload(payload, names, count, now_ms);
}

void try_pending(int64_t now_ms)
{
    if (!s_pending.valid || s_journey.outbox().active()) {
        return;
    }
    if (start_payload(s_pending.payload, s_pending.names, s_pending.name_count, now_ms)) {
        s_pending.valid = 0;
    }
}

void release_if_terminal()
{
    fl_product_send::SendTerminal term = s_journey.outbox().terminal();
    const char *id = s_journey.outbox().client_message_id();
    if (id == nullptr || id[0] == '\0') {
        return;
    }
    if (term != fl_product_send::kSendTerminalAccepted && term != fl_product_send::kSendTerminalDiscarded) {
        return;
    }
    if (strcmp(s_released_id, id) == 0) {
        return;
    }
    product_record_clear_upload(id);
    if (s_pending.valid == 0) {
        s_finished_sender[0] = '\0';
    }
    strncpy(s_released_id, id, sizeof(s_released_id) - 1);
    s_released_id[sizeof(s_released_id) - 1] = '\0';
}

void note_signed_in_user(const char *user)
{
    int same_finished = s_finished_sender[0] != '\0' && user != nullptr && strcmp(user, s_finished_sender) == 0;
    if (user == nullptr || user[0] == '\0') {
        s_journey.set_signed_in_user(nullptr);
        return;
    }
    if (s_journey.outbox().client_message_id()[0] != '\0') {
        s_journey.on_same_user_signed_in(user);
        return;
    }
    if (s_journey.mute_held_finished() != 0 && same_finished) {
        s_journey.on_mute_after_finish(0);
    }
    s_journey.set_signed_in_user(user);
}

void sync_send_session()
{
    fl_product_login::SessionWriter *session = product_login_session();
    const char *user = "";
    if (session != nullptr && session->signed_user() != nullptr) {
        user = session->signed_user();
    }
    note_signed_in_user(user);
    s_journey.set_wifi(s_wifi_up);
    /* esp_random() draws on RF noise only once Wi-Fi is running. */
    if (!s_ids_seeded && s_wifi_up) {
        s_journey.seed_capture_ids(esp_random());
        s_ids_seeded = true;
    }
    if (session != nullptr && session->signed_in() != 0 && session->credential_expired() != 0) {
        s_journey.set_refresh_failed(1);
    }
}

void service_send_http(int64_t now_ms)
{
    const product_send_http_result_t *done;
    fl_product_send::HttpAction action;
    fl_product_send::SendTransportResult transport;
    fl_product_login::SessionWriter *session = product_login_session();
    const char *bearer;
    uint32_t seq = 0;
    if (s_http_started == 0) {
        s_http_started = product_send_http_worker_start() ? 1 : 0;
    }
    sync_send_session();
    done = product_send_http_poll();
    if (done != nullptr) {
        product_mem_probe("send_http_result", done->perform_ok ? done->http_status : -1);
        memset(&transport, 0, sizeof(transport));
        transport.transport_ok = done->perform_ok;
        transport.http_status = done->http_status;
        transport.body = done->body;
        transport.body_len = done->body_len;
        memset(&action, 0, sizeof(action));
        (void)s_journey.outbox().apply_http_result(done->client_message_id, done->seq, action, transport, now_ms);
    }
    release_if_terminal();
    try_pending(now_ms);
    if (!s_journey.outbox().active() && resume_place_ok()) {
        try_start_current(now_ms);
    }
    if (s_journey.outbox().http_inflight() || s_http_started == 0) {
        return;
    }
    bearer = session != nullptr ? session->credential_token() : nullptr;
    if (bearer == nullptr || bearer[0] == '\0') {
        return;
    }
    if (!s_journey.outbox().prepare_http_action(&action, now_ms)) {
        return;
    }
    s_journey.outbox().note_http_sent(&seq);
    if (!product_send_http_submit(action.method, action.path, action.body, action.body_len, action.is_json,
                                  s_journey.outbox().client_message_id(), seq, bearer)) {
        s_journey.outbox().cancel_http_sent();
    }
}

void restore_inbox_selection(int64_t now_ms)
{
    fl_product_inbox::CarouselSession *inbox = product_inbox_session();
    const char *want = s_journey.remembered_inbox_selection();
    int i;
    if (inbox == nullptr) {
        return;
    }
    if (want != nullptr && want[0] != '\0' &&
        (inbox->selected_message_id() == nullptr || strcmp(inbox->selected_message_id(), want) != 0)) {
        int n = inbox->message_count();
        for (i = 0; i < n; ++i) {
            const fl_product_inbox::InboxMessage *msg = inbox->message_at(i);
            if (msg != nullptr && strcmp(msg->message_id, want) == 0) {
                inbox->select_index(i, now_ms);
                break;
            }
        }
    }
    if (inbox->playback_phase() == fl_product_inbox::kPlaybackPlaying) {
        inbox->pause_for_record(now_ms);
    }
}

} /* namespace */

extern "C" void product_send_service(int64_t now_ms)
{
    fl_product_send::SendStageResult advanced;
    if (!s_attached) {
        return;
    }
    if (now_ms - s_dot_ms >= 400) {
        s_dot_ms = now_ms;
        s_dot_phase = (s_dot_phase + 1) % 3;
    }
    advanced = s_stage.advance(now_ms, s_journey);
    if (advanced.upload_ready) {
        try_start_current(now_ms);
    }
    service_send_http(now_ms);
    if (advanced.returned_inbox) {
        restore_inbox_selection(now_ms);
    }
    present_stage_change(advanced);
}

extern "C" bool product_send_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_journey.reset();
    s_stage.reset();
    s_finished_sender[0] = '\0';
    memset(&s_pending, 0, sizeof(s_pending));
    if (!product_send_build_widgets(upper_slot, lower_slot)) {
        return false;
    }
    s_dot_phase = 0;
    s_dot_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    s_attached = true;
    ESP_LOGI(kTag, "V2_PRODUCT_SEND_READY");
    return true;
}

extern "C" void product_send_forget_widgets(void)
{
    int i;
    s_upper_label = nullptr;
    s_subtitle = nullptr;
    s_lower_label = nullptr;
    s_picker_header = nullptr;
    s_empty_label = nullptr;
    s_roster_row = nullptr;
    s_lower_slot = nullptr;
    for (i = 0; i < fl_product_send::kMaxRecipients; ++i) {
        s_cards[i] = nullptr;
    }
    s_card_sig[0] = '\0';
    s_picker_open = 0;
    s_visible = 0;
}

extern "C" bool product_send_build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot)
{
    int i;
    if (upper_slot == nullptr || lower_slot == nullptr) {
        return false;
    }
    s_lower_slot = lower_slot;
    s_card_sig[0] = '\0';
    s_picker_open = 0;
    for (i = 0; i < fl_product_send::kMaxRecipients; ++i) {
        s_cards[i] = nullptr;
    }
    const lv_font_t *title_font = &lv_font_montserrat_16;
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    title_font = &lv_font_montserrat_24;
#endif
    s_upper_label = lv_label_create(upper_slot);
    s_subtitle = lv_label_create(upper_slot);
    s_lower_label = lv_label_create(lower_slot);
    s_picker_header = lv_label_create(upper_slot);
    s_empty_label = lv_label_create(lower_slot);
    if (s_upper_label == nullptr || s_subtitle == nullptr || s_lower_label == nullptr ||
        s_picker_header == nullptr || s_empty_label == nullptr) {
        return false;
    }
    lv_obj_set_width(s_upper_label, 296);
    lv_label_set_long_mode(s_upper_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_upper_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_upper_label, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_style_text_font(s_upper_label, title_font, 0);
    lv_obj_align(s_upper_label, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_set_width(s_subtitle, 296);
    lv_label_set_long_mode(s_subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_subtitle, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_subtitle, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_font(s_subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align(s_subtitle, LV_ALIGN_TOP_LEFT, 12, 41);
    lv_obj_set_width(s_lower_label, 280);
    lv_obj_set_style_text_align(s_lower_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_lower_label, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_style_text_font(s_lower_label, &lv_font_montserrat_16, 0);
    lv_obj_center(s_lower_label);
    lv_obj_set_width(s_picker_header, 296);
    lv_label_set_long_mode(s_picker_header, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_picker_header, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(s_picker_header, lv_color_hex(0xE8F0E8), 0);
    lv_obj_set_style_text_font(s_picker_header, &lv_font_montserrat_16, 0);
    lv_obj_align(s_picker_header, LV_ALIGN_LEFT_MID, 12, 0);
    lv_label_set_text(s_empty_label, "No people to message");
    lv_obj_set_style_text_color(s_empty_label, lv_color_hex(0xB7CBD7), 0);
    lv_obj_set_style_text_font(s_empty_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(s_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_empty_label);
    hide_receipt();
    hide_picker();
    s_visible = 0;
    return true;
}

static void pause_for_record_hook(void *playback, int64_t now_ms)
{
    fl_product_inbox::CarouselSession *inbox = product_inbox_session();
    (void)playback;
    if (inbox != nullptr) {
        inbox->pause_for_record(now_ms);
    }
}

static const char *selected_message_hook(void *playback)
{
    fl_product_inbox::CarouselSession *inbox = product_inbox_session();
    (void)playback;
    if (inbox == nullptr || inbox->selected_message_id() == nullptr) {
        return "";
    }
    return inbox->selected_message_id();
}

static int can_start_second_hook(void *capture)
{
    (void)capture;
    return product_record_can_start_second();
}

static void discard_in_progress_hook(void *capture)
{
    (void)capture;
    product_record_discard_in_progress();
    if (s_journey.outbox().active() == 0) {
        s_finished_sender[0] = '\0';
    }
}

static void cancel_recording_hook(void *capture)
{
    (void)capture;
    product_record_cancel_recording();
}

static int finish_take_hook(void *capture)
{
    (void)capture;
    return product_record_finish_take();
}

static int capture_can_start_hook(void *capture)
{
    (void)capture;
    return product_record_capture_can_start();
}

static void begin_wipe_hook(void *capture, const char *client_message_id, int64_t now_ms)
{
    (void)capture;
    (void)product_record_begin_wipe(client_message_id, now_ms);
}

static void begin_reverse_hook(void *capture, int64_t now_ms)
{
    (void)capture;
    product_record_begin_reverse(now_ms);
}

static void begin_finish_collapse_hook(void *capture, int64_t now_ms)
{
    (void)capture;
    remember_finished_sender();
    product_record_begin_finish_collapse(now_ms);
}

extern "C" void product_send_note_inbox(void) { s_stage.note_inbox(); }

extern "C" void product_send_set_wifi(int up)
{
    s_wifi_up = up ? 1 : 0;
    s_journey.set_wifi(s_wifi_up);
}

extern "C" void product_send_set_refresh_failed(int failed)
{
    fl_shell_stage_t shell;
    int place;
    int restored;
    s_journey.set_refresh_failed(failed ? 1 : 0);
    if (failed == 0 || product_login_is_signed_in() == 0) {
        return;
    }
    place = s_stage.place();
    shell = stage_shell::splitPane().stage();
    if (shell != FL_SHELL_STAGE_ROSTER && shell != FL_SHELL_STAGE_PIN && shell != FL_SHELL_STAGE_HANGOUT) {
        return;
    }
    if (place != fl_product_send::kSendPlaceReceipt && place != fl_product_send::kSendPlaceInbox &&
        place != fl_product_send::kSendPlaceWipe && place != fl_product_send::kSendPlaceRecording &&
        place != fl_product_send::kSendPlaceFinishReverse && place != fl_product_send::kSendPlacePicker) {
        return;
    }
    restored = shell_for_place(place);
    stage_shell::splitPane().setStage(static_cast<fl_shell_stage_t>(restored));
}

extern "C" void product_send_on_signed_in(const char *user_id)
{
    note_signed_in_user(user_id);
    s_journey.set_refresh_failed(0);
}

extern "C" void product_send_present(int visible)
{
    s_visible = visible ? 1 : 0;
    paint_for_stage();
}

extern "C" product_send_stage_result_t product_send_apply_intent(int intent, int signed_in)
{
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageResult result;
    product_send_stage_result_t out;
    int64_t now_ms = (int64_t)(esp_timer_get_time() / 1000LL);
    memset(&hooks, 0, sizeof(hooks));
    hooks.pause_for_record = pause_for_record_hook;
    hooks.selected_message_id = selected_message_hook;
    hooks.can_start_second_recording = can_start_second_hook;
    hooks.discard_in_progress = discard_in_progress_hook;
    hooks.cancel_recording = cancel_recording_hook;
    hooks.finish_take = finish_take_hook;
    hooks.capture_can_start = capture_can_start_hook;
    hooks.begin_wipe = begin_wipe_hook;
    hooks.begin_reverse = begin_reverse_hook;
    hooks.begin_finish_collapse = begin_finish_collapse_hook;
    result = s_stage.apply(static_cast<fl_product_send::SendIntent>(intent), s_journey, &hooks, signed_in,
                           now_ms);
    if (intent == PRODUCT_SEND_INTENT_MUTE &&
        (s_journey.outbox().active() != 0 || s_finished_sender[0] != '\0')) {
        s_journey.on_mute_after_finish(1);
    }
    memset(&out, 0, sizeof(out));
    out.place = result.place;
    out.shell_stage = shell_for_place(result.place);
    out.changed = result.changed;
    out.ignored = result.ignored;
    out.opened_volume = result.opened_volume;
    out.show_record_canvas = result.show_record_canvas;
    out.show_split_pane = result.show_split_pane;
    out.left_wait = result.left_wait;
    return out;
}

extern "C" void product_send_release_widgets(void)
{
    clear_cards();
    s_card_sig[0] = '\0';
}

extern "C" void product_send_detach(void)
{
    int i;
    product_send_http_worker_stop();
    s_http_started = 0;
    s_attached = false;
    s_picker_open = 0;
    s_upper_label = nullptr;
    s_subtitle = nullptr;
    s_lower_label = nullptr;
    s_picker_header = nullptr;
    s_empty_label = nullptr;
    s_roster_row = nullptr;
    s_lower_slot = nullptr;
    for (i = 0; i < fl_product_send::kMaxRecipients; ++i) {
        s_cards[i] = nullptr;
    }
}
