#include "product_pin_entry_view_controller.hpp"
#include "product_theme.hpp"

#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
void labelText(lv_obj_t *label, const char *text)
{
    if (label && std::strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}
void hidden(lv_obj_t *object, bool hide)
{
    if (object && lv_obj_is_hidden(object) != hide) lv_obj_set_hidden(object, hide);
}
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font)
{
    auto *object = lv_label_create(parent);
    if (!object) return nullptr;
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(product_theme::Dark::kTextPrimaryRgb24), 0);
    lv_obj_set_clickable(object, false);
    return object;
}
}

bool PinEntryViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent)) return false;
    lv_obj_set_style_bg_color(lowerSlot(), lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    prefix_ = label(upperSlot(), &lv_font_montserrat_16);
    identity_ = label(upperSlot(), &lv_font_montserrat_24);
    avatar_ = lv_obj_create(upperSlot());
    if (!prefix_ || !identity_ || !avatar_) return false;
    labelText(prefix_, "Sign in as");
    lv_obj_set_pos(prefix_, 12, 30);
    lv_obj_set_size(avatar_, 28, 28);
    lv_obj_set_pos(avatar_, 92, 26);
    lv_obj_set_style_radius(avatar_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar_, lv_color_hex(0x5AA0E8), 0);
    lv_obj_set_style_bg_opa(avatar_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(avatar_, 0, 0);
    lv_obj_set_style_pad_all(avatar_, 0, 0);
    lv_obj_set_scrollable(avatar_, false);
    lv_obj_set_clickable(avatar_, false);
    initials_ = label(avatar_, &lv_font_montserrat_16);
    if (!initials_) return false;
    lv_obj_set_style_text_color(initials_, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_center(initials_);
    lv_obj_set_pos(identity_, 124, 26);
    lv_obj_set_width(identity_, 64);
    lv_label_set_long_mode(identity_, LV_LABEL_LONG_DOT);
    checking_ = label(upperSlot(), &lv_font_montserrat_16);
    if (!checking_) return false;
    lv_obj_set_pos(checking_, 198, 30);
    for (int i = 0; i < 4; ++i) {
        marks_[i] = label(upperSlot(), &lv_font_montserrat_32);
        if (!marks_[i]) return false;
        lv_obj_set_pos(marks_[i], 204 + i * 26, 20);
    }
    pad_ = lv_obj_create(lowerSlot());
    status_ = label(lowerSlot(), &lv_font_montserrat_16);
    if (!pad_ || !status_) return false;
    lv_obj_remove_style_all(pad_);
    lv_obj_set_size(pad_, 320, 160);
    lv_obj_set_scrollable(pad_, false);
    lv_obj_set_clickable(pad_, false);
    lv_obj_center(status_);
    for (int i = 0; i < 9; ++i) {
        auto *key = lv_button_create(pad_);
        keys_[i] = key;
        if (!key) return false;
        lv_obj_set_size(key, 101, 48);
        lv_obj_set_pos(key, 4 + (i % 3) * 105, 4 + (i / 3) * 52);
        lv_obj_set_style_radius(key, 8, 0);
        lv_obj_set_style_bg_color(key, lv_color_hex(product_theme::Dark::kPinKeysRgb24), 0);
        lv_obj_set_style_border_width(key, 0, 0);
        lv_obj_set_style_pad_all(key, 0, 0);
        lv_obj_set_scrollable(key, false);
        auto *digit = label(key, &lv_font_montserrat_32);
        if (!digit) return false;
        char text[2] = {static_cast<char>('1' + i), 0};
        labelText(digit, text);
        lv_obj_set_style_text_color(digit, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
        lv_obj_center(digit);
        key_contexts_[i] = {this, text[0]};
        lv_obj_add_event_cb(key, onKey, LV_EVENT_PRESSED, &key_contexts_[i]);
    }
    render();
    return true;
}

void PinEntryViewController::displayBody(bool status)
{
    desired_status_ = status;
    if (!body_initialized_) {
        body_initialized_ = true;
        status_body_ = status;
    } else if (!fading_ && status_body_ != desired_status_) {
        fading_ = true;
        fade_started_ = lv_tick_get();
    }
    const uint32_t elapsed = fading_ ? lv_tick_elaps(fade_started_) : 180;
    if (fading_ && elapsed >= 90 && status_body_ != desired_status_) status_body_ = desired_status_;
    hidden(pad_, status_body_);
    hidden(status_, !status_body_);
    lv_opa_t opacity = LV_OPA_COVER;
    if (fading_ && elapsed < 90) opacity = static_cast<lv_opa_t>(255 - elapsed * 255 / 90);
    else if (fading_ && elapsed < 180) opacity = static_cast<lv_opa_t>((elapsed - 90) * 255 / 90);
    else fading_ = false;
    auto *shown = status_body_ ? status_ : pad_;
    if (lv_obj_get_style_opa(shown, LV_PART_MAIN) != opacity) lv_obj_set_style_opa(shown, opacity, 0);
}

void PinEntryViewController::render()
{
    if (!identity_) return;
    const auto snapshot = service_.pinSnapshot();
    hidden(prefix_, snapshot.wrong);
    hidden(avatar_, snapshot.wrong);
    labelText(identity_, snapshot.wrong ? "wrong pin" : (snapshot.name[0] ? snapshot.name : snapshot.user));
    const int x = snapshot.wrong ? 12 : 124;
    const int width = snapshot.wrong ? 176 : 64;
    if (lv_obj_get_x(identity_) != x) lv_obj_set_x(identity_, x);
    if (lv_obj_get_width(identity_) != width) lv_obj_set_width(identity_, width);
    char initials[3] = {snapshot.name[0] ? snapshot.name[0] : '?', 0, 0};
    const char *space = std::strchr(snapshot.name, ' ');
    if (space && space[1]) initials[1] = space[1];
    labelText(initials_, initials);
    labelText(checking_, snapshot.checking ? "checking..." : "");
    hidden(checking_, !snapshot.checking);
    for (int i = 0; i < 4; ++i) {
        hidden(marks_[i], snapshot.checking);
        labelText(marks_[i], i < snapshot.digits ? "*" : "_");
        const auto color = lv_color_hex(i < snapshot.digits ? product_theme::Dark::kAccentRgb24 :
                                                                  product_theme::Dark::kTextSecondaryRgb24);
        if (!lv_color_eq(lv_obj_get_style_text_color(marks_[i], LV_PART_MAIN), color))
            lv_obj_set_style_text_color(marks_[i], color, 0);
    }
    const char *dots[] = {"connecting.", "connecting..", "connecting..."};
    labelText(status_, snapshot.wifi ? dots[snapshot.dotPhase] : "no Wi-Fi");
    for (auto *key : keys_) {
        if (lv_obj_is_clickable(key) != snapshot.keypadEnabled) lv_obj_set_clickable(key, snapshot.keypadEnabled);
        const lv_opa_t opacity = snapshot.keypadEnabled ? LV_OPA_COVER : LV_OPA_40;
        if (lv_obj_get_style_opa(key, LV_PART_MAIN) != opacity) lv_obj_set_style_opa(key, opacity, 0);
    }
    displayBody(snapshot.connectionStatus);
}

void PinEntryViewController::onKey(lv_event_t *event)
{
    auto *context = static_cast<Key *>(lv_event_get_user_data(event));
    if (context && context->owner) context->owner->enterDigit(context->digit);
}

bool PinEntryViewController::enterDigit(char digit)
{
    if (!loaded() || !appeared() || !service_.pinSnapshot().keypadEnabled) return false;
    const bool accepted = service_.enterPinDigit(digit);
    if (accepted) render();
    return accepted;
}

void PinEntryViewController::onBoot()
{
    service_.pinBoot();
}

lv_obj_t *PinEntryViewController::keyAt(int digit) const
{
    return digit >= 1 && digit <= 9 ? keys_[digit - 1] : nullptr;
}

void PinEntryViewController::viewWasRemoved()
{
    identity_ = prefix_ = avatar_ = initials_ = checking_ = pad_ = status_ = nullptr;
    for (auto &mark : marks_) mark = nullptr;
    for (auto &key : keys_) key = nullptr;
    body_initialized_ = fading_ = false;
    SplitPaneViewController::viewWasRemoved();
}

bool PinCooldownViewController::createView(lv_obj_t *parent)
{
    if (!ViewController::createView(parent)) return false;
    title_ = label(root_, &lv_font_montserrat_24);
    countdown_ = label(root_, &lv_font_montserrat_24);
    if (!title_ || !countdown_) return false;
    labelText(title_, "Too many attempts.");
    lv_obj_align(title_, LV_ALIGN_CENTER, 0, -18);
    lv_obj_align(countdown_, LV_ALIGN_CENTER, 0, 18);
    render();
    return true;
}

void PinCooldownViewController::render()
{
    if (!countdown_) return;
    char text[48];
    std::snprintf(text, sizeof(text), "Try again in %d.", service_.pinSnapshot().seconds);
    labelText(countdown_, text);
}

void PinCooldownViewController::viewWasRemoved()
{
    title_ = countdown_ = nullptr;
}

} // namespace product_ui
