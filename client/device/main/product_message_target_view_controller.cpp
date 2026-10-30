#include "product_message_target_view_controller.hpp"

#include "product_theme.hpp"
#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
constexpr int kStride = 144;
void label(lv_obj_t *object, const char *text)
{
    if (std::strcmp(lv_label_get_text(object), text) != 0)
        lv_label_set_text(object, text);
}
uint32_t fill(const char *id)
{
    static constexpr uint32_t colors[] = {0x5AA0E8, 0x67C587, 0xE8C040, 0xD875A8,
                                          0xE85B5B, 0x8B7CE8, 0xE88840, 0x4EC4C8};
    return id && std::strlen(id) == 10 && std::strncmp(id, "card-bg-0", 9) == 0 &&
                   id[9] >= '1' && id[9] <= '8'
               ? colors[id[9] - '1']
               : colors[0];
}
} // namespace
bool MessageTargetViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent))
        return false;
    heading_ = lv_label_create(upperSlot());
    status_ = lv_label_create(lowerSlot());
    if (!heading_ || !status_)
        return false;
    lv_obj_set_width(heading_, 296);
    lv_label_set_long_mode(heading_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(heading_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(heading_, lv_color_hex(product_theme::Dark::kHeadingRgb24), 0);
    lv_obj_align(heading_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_text_font(status_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_, lv_color_hex(product_theme::Dark::kSubheadingRgb24), 0);
    lv_label_set_text(status_, "No people to message");
    lv_obj_center(status_);
    navigation_requested_ = false;
    rendered_focus_[0] = 0;
    render();
    return service_.snapshot().count == 0 || row_;
}
bool MessageTargetViewController::createRow()
{
    row_ = lv_obj_create(lowerSlot());
    if (!row_)
        return false;
    lv_obj_set_size(row_, 320, 160);
    lv_obj_set_pos(row_, 0, 0);
    lv_obj_set_style_bg_opa(row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_, 0, 0);
    lv_obj_set_style_pad_all(row_, 0, 0);
    lv_obj_set_style_pad_left(row_, 94, 0);
    lv_obj_set_style_pad_right(row_, 94, 0);
    lv_obj_set_style_pad_column(row_, 12, 0);
    lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(row_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_clickable(row_, false);
    lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL, this);
    lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL_END, this);
    return true;
}
bool MessageTargetViewController::createCard(Card &card)
{
    card.owner = this;
    card.root = lv_obj_create(row_);
    if (!card.root)
        return false;
    lv_obj_set_size(card.root, 132, 133);
    lv_obj_set_style_radius(card.root, 8, 0);
    lv_obj_set_style_border_width(card.root, 0, 0);
    lv_obj_set_style_pad_all(card.root, 0, 0);
    lv_obj_set_style_outline_color(card.root, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_outline_pad(card.root, 0, 0);
    lv_obj_set_scrollable(card.root, false);
    lv_obj_set_clickable(card.root, true);
    lv_obj_add_event_cb(card.root, onPressed, LV_EVENT_PRESSED, &card);
    lv_obj_add_event_cb(card.root, onClicked, LV_EVENT_CLICKED, &card);
    auto *avatar = lv_obj_create(card.root);
    if (!avatar)
        return false;
    lv_obj_set_size(avatar, 66, 66);
    lv_obj_align(avatar, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(product_theme::Dark::kAvatarDefaultRgb24), 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_set_scrollable(avatar, false);
    lv_obj_set_clickable(avatar, false);
    card.initials = lv_label_create(avatar);
    card.name = lv_label_create(card.root);
    if (!card.initials || !card.name)
        return false;
    lv_obj_set_style_text_font(card.initials, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card.initials, lv_color_hex(0x101418), 0);
    lv_obj_center(card.initials);
    lv_obj_set_width(card.name, 116);
    lv_label_set_long_mode(card.name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(card.name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card.name, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_align(card.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(card.name, LV_ALIGN_BOTTOM_MID, 0, -12);
    return true;
}
void MessageTargetViewController::render()
{
    if (!heading_ || rendering_)
        return;
    rendering_ = true;
    const auto &snapshot = service_.snapshot();
    char title[128];
    service_.heading(title, sizeof(title));
    label(heading_, title);
    if (snapshot.notice[0]) {
        label(status_, snapshot.notice);
        lv_obj_set_hidden(status_, false);
        lv_obj_center(status_);
        if (row_)
            lv_obj_set_hidden(row_, true);
        rendering_ = false;
        return;
    }
    if (row_)
        lv_obj_set_hidden(row_, false);
    lv_obj_set_hidden(status_, snapshot.count != 0);
    if (!snapshot.count) {
        if (row_)
            lv_obj_delete(row_);
        row_ = nullptr;
        for (auto &card : cards_)
            card = {};
        rendered_focus_[0] = 0;
        rendered_focus_index_ = -1;
        rendering_ = false;
        return;
    }
    if (!row_ && !createRow()) {
        rendering_ = false;
        return;
    }
    for (int i = 0; i < fl_product_login::kMaxUsers; ++i) {
        auto &card = cards_[i];
        if (i >= snapshot.count) {
            if (card.root)
                lv_obj_delete(card.root);
            card = {};
            continue;
        }
        if (!card.root && !createCard(card)) {
            if (card.root)
                lv_obj_delete(card.root);
            card = {};
            continue;
        }
        if (card.index != i) {
            lv_obj_move_to_index(card.root, i);
            card.index = i;
        }
        if (std::memcmp(&card.user, &snapshot.users[i], sizeof(card.user)) != 0) {
            card.user = snapshot.users[i];
            char initial[5]{};
            // Keep the complete first UTF-8 code point for the placeholder face.
            unsigned bytes = 1;
            const auto first = static_cast<unsigned char>(card.user.display_name[0]);
            if (first >= 0xf0)
                bytes = 4;
            else if (first >= 0xe0)
                bytes = 3;
            else if (first >= 0xc0)
                bytes = 2;
            if (!first)
                initial[0] = '?';
            else
                std::memcpy(initial, card.user.display_name, bytes);
            label(card.initials, initial);
            label(card.name, card.user.display_name);
            lv_obj_set_style_bg_color(card.root, lv_color_hex(fill(card.user.card_background_id)), 0);
            lv_obj_set_style_bg_opa(card.root, LV_OPA_COVER, 0);
        }
        const int width = service_.isSelected(card.user.id) ? 2 : 0;
        if (lv_obj_get_style_outline_width(card.root, LV_PART_MAIN) != width)
            lv_obj_set_style_outline_width(card.root, width, 0);
    }
    int focused = -1;
    for (int i = 0; i < snapshot.count; ++i)
        if (std::strcmp(snapshot.users[i].id, snapshot.focus) == 0)
            focused = i;
    if (std::strcmp(rendered_focus_, snapshot.focus) != 0 || rendered_focus_index_ != focused) {
        if (focused >= 0) {
            programmatic_ = true;
            lv_obj_update_layout(row_);
            lv_obj_scroll_to_x(row_, focused * kStride, LV_ANIM_OFF);
            programmatic_ = false;
        }
        std::snprintf(rendered_focus_, sizeof(rendered_focus_), "%s", snapshot.focus);
        rendered_focus_index_ = focused;
    }
    rendering_ = false;
}
void MessageTargetViewController::viewWasRemoved()
{
    SplitPaneViewController::viewWasRemoved();
    heading_ = status_ = row_ = nullptr;
    for (auto &card : cards_)
        card = {};
    rendered_focus_[0] = 0;
    rendered_focus_index_ = -1;
    rendering_ = programmatic_ = gesture_ = navigation_requested_ = false;
}
lv_obj_t *MessageTargetViewController::cardAt(int index) const
{
    return index >= 0 && index < fl_product_login::kMaxUsers ? cards_[index].root : nullptr;
}
bool MessageTargetViewController::selectRecipient(int index, const char *expectedId)
{
    if (!loaded() || !appeared() || navigation_requested_ || !service_.toggle(index, expectedId))
        return false;
    render();
    return true;
}
void MessageTargetViewController::request(MessageTargetRequest intent)
{
    if (!loaded() || !appeared() || navigation_requested_ || !navigation_.request)
        return;
    navigation_requested_ = true;
    navigation_.request(navigation_.context, intent);
}
void MessageTargetViewController::onBoot()
{
    request({MessageTargetDestination::Inbox, {}});
}
void MessageTargetViewController::onRed()
{
    MessageTargetRequest intent;
    intent.destination = MessageTargetDestination::Recording;
    if (service_.freeze(intent.recipients))
        request(intent);
}
void MessageTargetViewController::onPressed(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card || !card->owner || !card->owner->row_)
        return;
    card->pressed_index = card->index;
    std::snprintf(card->pressed_id, sizeof(card->pressed_id), "%s", card->user.id);
    card->owner->gesture_ = false;
    card->owner->press_scroll_ = lv_obj_get_scroll_x(card->owner->row_);
}
void MessageTargetViewController::onClicked(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card || !card->owner)
        return;
    if (!card->owner->gesture_)
        card->owner->selectRecipient(card->pressed_index, card->pressed_id);
    card->pressed_index = -1;
    card->pressed_id[0] = 0;
    card->owner->gesture_ = false;
}
void MessageTargetViewController::onScroll(lv_event_t *event)
{
    auto *self = static_cast<MessageTargetViewController *>(lv_event_get_user_data(event));
    if (!self || !self->row_ || !self->appeared() || self->rendering_ || self->programmatic_ || self->navigation_requested_)
        return;
    int scroll = lv_obj_get_scroll_x(self->row_);
    if (scroll < self->press_scroll_ - 4 || scroll > self->press_scroll_ + 4)
        self->gesture_ = true;
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END)
        return;
    const auto &snapshot = self->service_.snapshot();
    if (!snapshot.count)
        return;
    int index = (scroll < 0 ? 0 : scroll + kStride / 2) / kStride;
    if (index >= snapshot.count)
        index = snapshot.count - 1;
    self->service_.focus(index, snapshot.users[index].id);
    self->programmatic_ = true;
    lv_obj_scroll_to_x(self->row_, index * kStride, LV_ANIM_OFF);
    self->programmatic_ = false;
    self->render();
}
} // namespace product_ui
