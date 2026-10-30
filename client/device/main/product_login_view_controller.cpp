#include "product_login_view_controller.hpp"

#include "product_theme.hpp"
#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
constexpr int kStride = 144;
uint32_t cardFill(const char *id)
{
    static constexpr uint32_t fills[] = {0x5AA0E8, 0x67C587, 0xE8C040, 0xD875A8,
                                         0xE85B5B, 0x8B7CE8, 0xE88840, 0x4EC4C8};
    if (id && std::strlen(id) == 10 && std::strncmp(id, "card-bg-0", 9) == 0 && id[9] >= '1' && id[9] <= '8') {
        return fills[id[9] - '1'];
    }
    return fills[0];
}
void initials(const char *name, char out[4])
{
    out[0] = name && name[0] ? name[0] : '?';
    out[1] = '\0';
    if (!name) return;
    const char *space = std::strchr(name, ' ');
    if (space && space[1]) { out[1] = space[1]; out[2] = '\0'; }
}
}

LoginViewController::LoginViewController(LoginService &service, LoginNavigation navigation)
    : service_(service), navigation_(navigation) {}

bool LoginViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent)) return false;
    heading_ = lv_label_create(upperSlot());
    status_ = lv_label_create(lowerSlot());
    if (!heading_ || !status_) return false;
    lv_obj_align(heading_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_text_font(heading_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(heading_, lv_color_hex(product_theme::Dark::kHeadingRgb24), 0);
    lv_obj_set_style_text_font(status_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_, lv_color_hex(product_theme::Dark::kSubheadingRgb24), 0);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(status_);
    navigation_requested_ = false;
    rendered_focus_[0] = '\0';
    return true;
}

void LoginViewController::viewWasRemoved()
{
    SplitPaneViewController::viewWasRemoved();
    heading_ = nullptr;
    status_ = nullptr;
    row_ = nullptr;
    rendered_focus_[0] = '\0';
    for (auto &card : cards_) card = {};
    gesture_ = false;
    programmatic_scroll_ = false;
    navigation_requested_ = false;
}

void LoginViewController::setLabel(lv_obj_t *label, const char *text)
{
    if (std::strcmp(lv_label_get_text(label), text) == 0) return;
    lv_label_set_text(label, text);
    ++widget_revision_;
}

void LoginViewController::setHidden(lv_obj_t *object, bool hidden)
{
    if (!object || lv_obj_is_hidden(object) == hidden) return;
    lv_obj_set_hidden(object, hidden);
    ++widget_revision_;
}

void LoginViewController::clearCards()
{
    if (!row_) return;
    lv_obj_delete(row_);
    row_ = nullptr;
    for (auto &card : cards_) card = {};
    rendered_focus_[0] = '\0';
    ++widget_revision_;
}

bool LoginViewController::createCard(Card &card)
{
    card.owner = this;
    card.root = lv_obj_create(row_);
    if (!card.root) return false;
    lv_obj_set_size(card.root, 132, 133);
    lv_obj_set_style_radius(card.root, 8, 0);
    lv_obj_set_style_border_width(card.root, 0, 0);
    lv_obj_set_style_pad_all(card.root, 0, 0);
    lv_obj_set_scrollable(card.root, false);
    lv_obj_set_clickable(card.root, true);
    lv_obj_add_event_cb(card.root, onPressed, LV_EVENT_PRESSED, &card);
    lv_obj_add_event_cb(card.root, onClicked, LV_EVENT_CLICKED, &card);
    auto *circle = lv_obj_create(card.root);
    if (!circle) return false;
    lv_obj_set_size(circle, 66, 66);
    lv_obj_align(circle, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(product_theme::Dark::kAvatarDefaultRgb24), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_scrollable(circle, false);
    lv_obj_set_clickable(circle, false);
    card.initials = lv_label_create(circle);
    card.name = lv_label_create(card.root);
    if (!card.initials || !card.name) return false;
    lv_obj_set_style_text_font(card.initials, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card.initials, lv_color_hex(0x101418), 0);
    lv_obj_center(card.initials);
    lv_obj_set_width(card.name, 116);
    lv_label_set_long_mode(card.name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(card.name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(card.name, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_align(card.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(card.name, LV_ALIGN_BOTTOM_MID, 0, -12);
    ++widget_revision_;
    return true;
}

void LoginViewController::syncCards(const LoginSnapshot &snapshot)
{
    if (!row_) {
        row_ = lv_obj_create(lowerSlot());
        if (!row_) return;
        lv_obj_set_size(row_, 320, 160);
        lv_obj_set_pos(row_, 0, 0);
        lv_obj_set_style_bg_opa(row_, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row_, 0, 0);
        lv_obj_set_style_pad_all(row_, 0, 0);
        lv_obj_set_style_pad_left(row_, (320 - 132) / 2, 0);
        lv_obj_set_style_pad_right(row_, (320 - 132) / 2, 0);
        lv_obj_set_style_pad_column(row_, 12, 0);
        lv_obj_set_flex_flow(row_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(row_, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(row_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_clickable(row_, false);
        lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL, this);
        lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL_END, this);
        ++widget_revision_;
    }
    for (auto &card : cards_) {
        if (!card.root) continue;
        bool retained = false;
        for (int i = 0; i < snapshot.count; ++i) {
            if (std::strcmp(card.user.id, snapshot.users[i].id) == 0) retained = true;
        }
        if (!retained) {
            lv_obj_delete(card.root);
            card = {};
            ++widget_revision_;
        }
    }
    for (int i = 0; i < snapshot.count; ++i) {
        Card *slot = nullptr;
        for (auto &card : cards_) {
            if (card.root && std::strcmp(card.user.id, snapshot.users[i].id) == 0) slot = &card;
        }
        if (!slot) {
            for (auto &card : cards_) if (!card.root) { slot = &card; break; }
            if (!slot) return;
            if (!createCard(*slot)) {
                if (slot->root) lv_obj_delete(slot->root);
                *slot = {};
                return;
            }
        }
        if (slot->index != i) {
            lv_obj_move_to_index(slot->root, i);
            slot->index = i;
            ++widget_revision_;
        }
        if (std::memcmp(&slot->user, &snapshot.users[i], sizeof(slot->user)) != 0) {
            slot->user = snapshot.users[i];
            char face[4];
            initials(slot->user.display_name, face);
            setLabel(slot->initials, face);
            setLabel(slot->name, slot->user.display_name);
            lv_obj_set_style_bg_color(slot->root, lv_color_hex(cardFill(slot->user.card_background_id)), 0);
            lv_obj_set_style_bg_opa(slot->root,
                snapshot.focus[0] && std::strcmp(snapshot.focus, slot->user.id) == 0 ? LV_OPA_COVER : LV_OPA_60, 0);
            ++widget_revision_;
        }
        const lv_opa_t opacity = snapshot.focus[0] && std::strcmp(snapshot.focus, slot->user.id) == 0
            ? LV_OPA_COVER : LV_OPA_60;
        if (lv_obj_get_style_bg_opa(slot->root, LV_PART_MAIN) != opacity) {
            lv_obj_set_style_bg_opa(slot->root, opacity, 0);
            ++widget_revision_;
        }
    }
    const auto direction = snapshot.updating ? LV_DIR_NONE : LV_DIR_HOR;
    if (lv_obj_get_scroll_dir(row_) != direction) {
        lv_obj_set_scroll_dir(row_, direction);
        ++widget_revision_;
    }
    if (std::strcmp(rendered_focus_, snapshot.focus) != 0) {
        for (int i = 0; i < snapshot.count; ++i) {
            if (std::strcmp(snapshot.users[i].id, snapshot.focus) != 0) continue;
            programmatic_scroll_ = true;
            lv_obj_update_layout(row_);
            lv_obj_scroll_to_x(row_, i * kStride, LV_ANIM_OFF);
            programmatic_scroll_ = false;
            break;
        }
        std::strncpy(rendered_focus_, snapshot.focus, sizeof(rendered_focus_) - 1);
        ++widget_revision_;
    }
}

void LoginViewController::render()
{
    if (!heading_ || !status_ || rendering_) return;
    struct RenderGuard {
        bool &flag;
        explicit RenderGuard(bool &value) : flag(value) { flag = true; }
        ~RenderGuard() { flag = false; }
    } guard(rendering_);
    const auto snapshot = service_.snapshot();
    setHidden(heading_, snapshot.muted);
    if (snapshot.muted) { setHidden(status_, true); clearCards(); return; }
    setLabel(heading_, snapshot.updating ? "Updating hangout ..." :
             snapshot.known ? "Sign in as ..." : "Loading hangout ...");
    if (snapshot.body == LoginSnapshot::Body::Roster) {
        setHidden(status_, true);
        syncCards(snapshot);
        return;
    }
    clearCards();
    setHidden(status_, false);
    if (snapshot.body == LoginSnapshot::Body::NoWifi) setLabel(status_, "no Wi-Fi");
    else if (snapshot.body == LoginSnapshot::Body::Empty) setLabel(status_, "No people in this hangout");
    else {
        char connecting[24];
        std::snprintf(connecting, sizeof(connecting), "connecting%.*s", snapshot.dotPhase + 1, "...");
        setLabel(status_, connecting);
    }
}

lv_obj_t *LoginViewController::cardAt(int index) const
{
    for (const auto &card : cards_) if (card.root && card.index == index) return card.root;
    return nullptr;
}

bool LoginViewController::selectUser(int index, const char *expectedId)
{
    if (!loaded() || !row_ || navigation_requested_ || !expectedId) return false;
    const auto snapshot = service_.snapshot();
    if (snapshot.muted || snapshot.updating || snapshot.body != LoginSnapshot::Body::Roster ||
        index < 0 || index >= snapshot.count || std::strcmp(snapshot.users[index].id, expectedId) != 0) return false;
    if (!navigation_.request) return false;
    if (!service_.focusUser(expectedId)) return false;
    render(); // The outgoing snapshot must show the selected identity focused.
    // Browsing changes the viewport without changing selected identity. A tap
    // must center even when the selected focus ID itself has not changed.
    programmatic_scroll_ = true;
    lv_obj_update_layout(row_);
    lv_obj_scroll_to_x(row_, index * kStride, LV_ANIM_OFF);
    programmatic_scroll_ = false;
    navigation_requested_ = true;
    // Navigation is an intent; the runtime applies it after the LVGL event.
    navigation_.request(navigation_.context, LoginDestination::PinEntry, snapshot.users[index].id);
    return true;
}

void LoginViewController::onPressed(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card || !card->owner || !card->owner->row_) return;
    card->owner->gesture_ = false;
    card->owner->press_scroll_ = lv_obj_get_scroll_x(card->owner->row_);
}

void LoginViewController::onClicked(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card || !card->owner) return;
    if (card->owner->gesture_) { card->owner->gesture_ = false; return; }
    card->owner->selectUser(card->index, card->user.id);
}

void LoginViewController::onScroll(lv_event_t *event)
{
    auto *self = static_cast<LoginViewController *>(lv_event_get_user_data(event));
    if (!self || !self->row_ || self->programmatic_scroll_ || self->rendering_) return;
    int scroll = lv_obj_get_scroll_x(self->row_);
    if (scroll < self->press_scroll_ - 4 || scroll > self->press_scroll_ + 4) {
        self->gesture_ = true;
        self->service_.noteActivity();
    }
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) return;
    const int count = self->service_.snapshot().count;
    if (count <= 0) return;
    int index = (scroll < 0 ? 0 : scroll + kStride / 2) / kStride;
    if (index >= count) index = count - 1;
    const int target = index * kStride;
    if (scroll == target) return;
    self->programmatic_scroll_ = true;
    lv_obj_scroll_to_x(self->row_, target, LV_ANIM_OFF);
    self->programmatic_scroll_ = false;
}

} // namespace product_ui
