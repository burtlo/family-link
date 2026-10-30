#include "product_inbox_view_controller.hpp"
#include "product_theme.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
void text(lv_obj_t *label, const char *value)
{
    if (std::strcmp(lv_label_get_text(label), value) != 0)
        lv_label_set_text(label, value);
}
void hidden(lv_obj_t *object, bool hide)
{
    if (lv_obj_is_hidden(object) != hide)
        lv_obj_set_hidden(object, hide);
}
void position(lv_obj_t *object, int x, int y)
{
    if (lv_obj_get_x(object) != x)
        lv_obj_set_x(object, x);
    if (lv_obj_get_y(object) != y)
        lv_obj_set_y(object, y);
}
lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font = &lv_font_montserrat_16)
{
    auto *object = lv_label_create(parent);
    if (object) {
        lv_obj_set_style_text_font(object, font, 0);
        lv_obj_set_style_text_color(object, lv_color_hex(product_theme::Dark::kTextPrimaryRgb24), 0);
        lv_obj_set_clickable(object, false);
    }
    return object;
}
uint32_t fill(const char *id)
{
    const uint32_t colors[] = {0x5AA0E8, 0x67C587, 0xE8C040, 0xD875A8, 0xE85B5B, 0x8B7CE8, 0xE88840, 0x4EC4C8};
    if (id && std::strlen(id) == 10 && std::strncmp(id, "card-bg-0", 9) == 0 && id[9] >= '1' && id[9] <= '8')
        return colors[id[9] - '1'];
    return colors[0];
}
} // namespace

bool InboxViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent))
        return false;
    heading_ = label(upperSlot(), &lv_font_montserrat_24);
    transport_ = lv_button_create(upperSlot());
    status_ = label(lowerSlot());
    row_ = lv_obj_create(lowerSlot());
    if (!heading_ || !transport_ || !status_ || !row_)
        return false;
    lv_obj_set_pos(heading_, 12, 26);
    lv_obj_set_width(heading_, 200);
    lv_obj_set_size(transport_, 88, 40);
    lv_obj_align(transport_, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(transport_, lv_color_hex(product_theme::Dark::kAccentRgb24), 0);
    transport_label_ = label(transport_);
    if (!transport_label_)
        return false;
    lv_obj_set_style_text_color(transport_label_, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_center(transport_label_);
    lv_obj_add_event_cb(transport_, onTransport, LV_EVENT_CLICKED, this);
    lv_label_set_long_mode(heading_, LV_LABEL_LONG_DOT);
    lv_obj_center(status_);
    lv_obj_remove_style_all(row_);
    lv_obj_set_size(row_, 320, 160);
    lv_obj_set_scroll_dir(row_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(row_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_clickable(row_, false);
    lv_obj_set_scroll_elastic(row_, false);
    lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL, this);
    lv_obj_add_event_cb(row_, onScroll, LV_EVENT_SCROLL_END, this);
    track_ = lv_obj_create(row_);
    if (!track_)
        return false;
    lv_obj_remove_style_all(track_);
    lv_obj_set_size(track_, 320, 160);
    lv_obj_set_scrollable(track_, false);
    lv_obj_set_clickable(track_, false);
    for (auto &card : cards_) {
        card.owner = this;
        if (!createCard(card))
            return false;
    }
    render();
    return true;
}

bool InboxViewController::createCard(Card &card)
{
    card.root = lv_obj_create(track_);
    if (!card.root)
        return false;
    lv_obj_set_size(card.root, 132, 134);
    lv_obj_set_style_radius(card.root, 8, 0);
    lv_obj_set_style_pad_all(card.root, 0, 0);
    lv_obj_set_style_border_width(card.root, 0, 0);
    lv_obj_set_scrollable(card.root, false);
    lv_obj_add_event_cb(card.root, onPressed, LV_EVENT_PRESSED, &card);
    lv_obj_add_event_cb(card.root, onClicked, LV_EVENT_CLICKED, &card);
    card.avatar = lv_obj_create(card.root);
    if (!card.avatar)
        return false;
    lv_obj_set_size(card.avatar, 66, 66);
    lv_obj_set_pos(card.avatar, 33, 12);
    lv_obj_set_style_radius(card.avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(card.avatar, lv_color_hex(product_theme::Dark::kAvatarDefaultRgb24), 0);
    lv_obj_set_style_border_width(card.avatar, 0, 0);
    lv_obj_set_style_pad_all(card.avatar, 0, 0);
    lv_obj_set_scrollable(card.avatar, false);
    lv_obj_set_clickable(card.avatar, false);
    card.initials = label(card.avatar);
    card.name = label(card.root);
    card.duration = label(card.root);
    if (!card.initials || !card.name || !card.duration)
        return false;
    lv_obj_set_style_text_color(card.initials, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_center(card.initials);
    lv_obj_set_pos(card.name, 8, 87);
    lv_obj_set_width(card.name, 116);
    lv_obj_set_style_text_align(card.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(card.name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(card.duration, 8, 111);
    lv_obj_set_width(card.duration, 116);
    lv_obj_set_style_text_align(card.duration, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(card.name, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_set_style_text_color(card.duration, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    card.canvas = lv_canvas_create(card.root);
    card.ink = lv_obj_create(card.root);
    if (!card.canvas || !card.ink)
        return false;
    for (lv_obj_t *layer : {card.canvas, card.ink}) {
        lv_obj_set_pos(layer, 6, 7);
        lv_obj_set_size(layer, 120, 120);
        lv_obj_set_clickable(layer, false);
        lv_obj_set_scrollable(layer, false);
        hidden(layer, true);
    }
    lv_obj_set_style_bg_color(card.ink, lv_color_hex(product_theme::Dark::kSketchCanvasRgb24), 0);
    lv_obj_set_style_bg_opa(card.ink, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card.ink, 0, 0);
    lv_obj_set_style_pad_all(card.ink, 0, 0);
    lv_obj_add_event_cb(card.ink, onInk, LV_EVENT_DRAW_MAIN, &card);
    return true;
}

void InboxViewController::syncCards(int center)
{
    const auto snapshot = service_.snapshot();
    center = std::max(0, std::min(center, snapshot.count - 1));
    const int width = std::max(320, 320 + (snapshot.count - 1) * 144);
    if (lv_obj_get_width(track_) != width)
        lv_obj_set_width(track_, width);
    for (int i = 0; i < kLiveCards; ++i) {
        auto &card = cards_[i];
        const int index = center + i - 1;
        const auto *message = service_.messageAt(index);
        hidden(card.root, !message);
        if (!message) {
            card.index = -1;
            card.id[0] = 0;
            continue;
        }
        card.index = index;
        std::snprintf(card.id, sizeof(card.id), "%s", message->message_id);
        position(card.root, 94 + index * 144, 13);
        const char *name = message->sender_name[0] ? message->sender_name : message->sender_user_id;
        text(card.name, name);
        char initials[3] = {name[0] ? name[0] : '?', 0, 0};
        const char *space = std::strchr(name, ' ');
        if (space && space[1])
            initials[1] = space[1];
        text(card.initials, initials);
        char duration[24];
        const unsigned seconds = message->duration_ms / 1000;
        std::snprintf(duration, sizeof(duration), "%u:%02u", seconds / 60, seconds % 60);
        text(card.duration, duration);
        const auto color = lv_color_hex(fill(message->sender_card_background_id));
        if (!lv_color_eq(lv_obj_get_style_bg_color(card.root, LV_PART_MAIN), color))
            lv_obj_set_style_bg_color(card.root, color, 0);
        const auto opacity = index == snapshot.selectedIndex ? LV_OPA_COVER : LV_OPA_60;
        if (lv_obj_get_style_bg_opa(card.root, LV_PART_MAIN) != opacity)
            lv_obj_set_style_bg_opa(card.root, opacity, 0);
    }
}

void InboxViewController::centerSelected()
{
    const int selected = service_.snapshot().selectedIndex;
    if (selected < 0)
        return;
    syncCards(selected);
    programmatic_ = true;
    lv_obj_update_layout(row_);
    if (lv_obj_get_scroll_x(row_) != selected * 144)
        lv_obj_scroll_to_x(row_, selected * 144, LV_ANIM_OFF);
    programmatic_ = false;
}

void InboxViewController::displayBody(bool cards)
{
    desired_cards_ = cards;
    if (!body_initialized_) {
        showing_cards_ = cards;
        body_initialized_ = true;
    } else if (!fading_ && showing_cards_ != cards) {
        fading_ = true;
        fade_started_ = lv_tick_get();
    }
    const auto elapsed = fading_ ? lv_tick_elaps(fade_started_) : 180;
    if (fading_ && elapsed >= 90)
        showing_cards_ = desired_cards_;
    hidden(row_, !showing_cards_);
    hidden(status_, showing_cards_);
    lv_opa_t opacity = LV_OPA_COVER;
    if (fading_ && elapsed < 90)
        opacity = static_cast<lv_opa_t>(255 - elapsed * 255 / 90);
    else if (fading_ && elapsed < 180)
        opacity = static_cast<lv_opa_t>((elapsed - 90) * 255 / 90);
    else
        fading_ = false;
    auto *shown = showing_cards_ ? row_ : status_;
    if (lv_obj_get_style_opa(shown, LV_PART_MAIN) != opacity)
        lv_obj_set_style_opa(shown, opacity, 0);
}

void InboxViewController::presentTransport(const InboxSnapshot &snapshot)
{
    const auto playback = playback_.snapshot();
    const bool hide = playback.transport == PlaybackSnapshot::Transport::Hidden ||
                      snapshot.body == InboxSnapshot::Body::Empty;
    hidden(transport_, hide);
    if (hide)
        return;
    text(transport_label_, playback.transport == PlaybackSnapshot::Transport::Pause ? "Pause" : "Play");
    const bool disabled = !playback.enabled;
    if (lv_obj_has_state(transport_, LV_STATE_DISABLED) != disabled) {
        if (disabled)
            lv_obj_add_state(transport_, LV_STATE_DISABLED);
        else
            lv_obj_remove_state(transport_, LV_STATE_DISABLED);
    }
}

void InboxViewController::detachLease()
{
    for (auto &card : cards_) {
        if (card.canvas)
            lv_image_set_src(card.canvas, nullptr);
    }
    if (lease_.generation == 0)
        return;
    if (lease_.consumers > 0)
        (void)playback_.surfaces().removeConsumer(&lease_);
    (void)playback_.surfaces().release(&lease_);
}

void InboxViewController::viewWillUnload()
{
    detachLease();
    for (auto &card : cards_) {
        card.canvas = nullptr;
        card.ink = nullptr;
    }
}

void InboxViewController::presentInk()
{
    const char *visible[InboxViewController::kLiveCards] = {};
    int count = 0;
    for (const auto &card : cards_) {
        if (card.index >= 0 && card.id[0])
            visible[count++] = card.id;
    }
#ifdef ESP_PLATFORM
    constexpr SurfacePlace place = SurfacePlace::Psram;
#else
    constexpr SurfacePlace place = SurfacePlace::Host;
#endif
    if (playback_.needsFrame()) {
        if (lease_.generation == 0 && !playback_.surfaces().acquire("inbox", place, &lease_))
            return;
        uint16_t *pixels = playback_.surfaces().pixels(lease_);
        if (!pixels)
            return;
        playback_.paint(pixels, fl_product_inbox::kSketchPixels);
        if (lease_.consumers == 0)
            (void)playback_.surfaces().addConsumer(&lease_);
    } else {
        detachLease();
    }
    playback_.noteVisible(visible, count);
    for (auto &card : cards_) {
        if (card.index < 0 || !card.root)
            continue;
        const bool ink = playback_.inkFor(card.id);
        const bool selected = std::strcmp(card.id, service_.selectedMessageId()) == 0;
        hidden(card.avatar, ink);
        hidden(card.initials, ink);
        hidden(card.name, ink);
        hidden(card.duration, ink);
        hidden(card.ink, !(ink && !selected));
        hidden(card.canvas, !(ink && selected));
        if (ink && selected && lease_.generation != 0) {
            uint16_t *pixels = playback_.surfaces().pixels(lease_);
            if (pixels) {
                lv_canvas_set_buffer(card.canvas, pixels, 120, 120, LV_COLOR_FORMAT_RGB565);
                lv_obj_invalidate(card.canvas);
            }
        } else if (card.canvas) {
            lv_image_set_src(card.canvas, nullptr);
        }
    }
}

void InboxViewController::render()
{
    if (!heading_)
        return;
    const auto snapshot = service_.snapshot();
    const auto *selected = service_.messageAt(snapshot.selectedIndex);
    PlaybackMedia media{};
    if (selected) {
        std::snprintf(media.message_id, sizeof(media.message_id), "%s", selected->message_id);
        std::snprintf(media.audio_format, sizeof(media.audio_format), "%s", selected->audio.format_id);
        std::snprintf(media.audio_sha, sizeof(media.audio_sha), "%s", selected->audio.sha256);
        std::snprintf(media.sketch_sha, sizeof(media.sketch_sha), "%s", selected->sketch.sha256);
        media.duration_ms = selected->duration_ms;
        media.audio_bytes = selected->audio.byte_count;
        media.sketch_bytes = selected->sketch.byte_count;
        media.sketch = selected->sketch.present != 0;
    }
    playback_.bind(selected ? &media : nullptr, snapshot.body == InboxSnapshot::Body::Cards);
    const auto playback = playback_.snapshot();
    char heading[80];
    if (playback.cantPlay)
        std::snprintf(heading, sizeof(heading), "can't play");
    else
        std::snprintf(heading, sizeof(heading), selected ? "From %s" : "Messages", selected ? selected->sender_name : "");
    text(heading_, heading);
    presentTransport(snapshot);
    const char *dots[] = {"connecting.", "connecting..", "connecting..."};
    text(status_, snapshot.body == InboxSnapshot::Body::NoWifi ? "no Wi-Fi" : snapshot.body == InboxSnapshot::Body::Empty ? "No messages"
                                                                                                                          : dots[snapshot.dotPhase]);
    if (snapshot.selectedIndex != rendered_selected_ ||
        std::strcmp(rendered_id_, service_.selectedMessageId()) != 0) {
        centerSelected();
        rendered_selected_ = snapshot.selectedIndex;
        std::snprintf(rendered_id_, sizeof(rendered_id_), "%s", service_.selectedMessageId());
    } else {
        const int center = std::max(0, static_cast<int>((lv_obj_get_scroll_x(row_) + 72) / 144));
        syncCards(center);
    }
    displayBody(snapshot.body == InboxSnapshot::Body::Cards);
    presentInk();
}

bool InboxViewController::selectMessage(int index, const char *expectedId)
{
    if (!loaded() || !appeared())
        return false;
    if (expectedId && std::strcmp(service_.selectedMessageId(), expectedId) != 0)
        playback_.pausePreserving();
    if (!service_.selectIndex(index, expectedId))
        return false;
    centerSelected();
    render();
    return true;
}
void InboxViewController::onPressed(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    card->pressed_index = card->index;
    std::snprintf(card->pressed_id, sizeof(card->pressed_id), "%s", card->id);
    card->owner->gesture_ = false;
    card->owner->press_scroll_ = lv_obj_get_scroll_x(card->owner->row_);
}
void InboxViewController::onClicked(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    if (!card->owner->gesture_)
        card->owner->selectMessage(card->pressed_index, card->pressed_id);
}
void InboxViewController::onScroll(lv_event_t *event)
{
    auto *owner = static_cast<InboxViewController *>(lv_event_get_user_data(event));
    if (owner->programmatic_ || !owner->row_ || !owner->appeared())
        return;
    const int scroll = lv_obj_get_scroll_x(owner->row_);
    if (std::abs(scroll - owner->press_scroll_) > 4)
        owner->gesture_ = true;
    const int index = std::max(0, std::min((scroll + 72) / 144, owner->service_.snapshot().count - 1));
    if (lv_event_get_code(event) == LV_EVENT_SCROLL_END) {
        const auto *message = owner->service_.messageAt(index);
        if (message)
            owner->selectMessage(index, message->message_id);
    } else
        owner->syncCards(index);
}
lv_obj_t *InboxViewController::cardForIndex(int index) const
{
    for (const auto &card : cards_)
        if (card.index == index)
            return card.root;
    return nullptr;
}
void InboxViewController::request(InboxDestination destination)
{
    if (!loaded() || !appeared() || navigation_requested_ || !navigation_.request)
        return;
    navigation_requested_ = true;
    navigation_.request(navigation_.context, destination);
}
void InboxViewController::onTransport(lv_event_t *event)
{
    auto *owner = static_cast<InboxViewController *>(lv_event_get_user_data(event));
    if (!owner || !owner->loaded() || !owner->appeared() || !owner->transport_)
        return;
    if (lv_obj_has_state(owner->transport_, LV_STATE_DISABLED))
        return;
    owner->playback_.toggle();
    owner->render();
}

void InboxViewController::onInk(lv_event_t *event)
{
    auto *card = static_cast<Card *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    if (!card || !card->owner || !layer)
        return;
    struct Draw {
        lv_layer_t *layer;
        lv_obj_t *object;
    } draw{layer, card->ink};
    card->owner->playback_.visitInk(
        card->id,
        [](void *context, int x, int y, uint16_t color) {
            auto *target = static_cast<Draw *>(context);
            lv_draw_rect_dsc_t descriptor;
            lv_draw_rect_dsc_init(&descriptor);
            descriptor.bg_opa = LV_OPA_COVER;
            const int red = ((color >> 11) & 31) * 255 / 31;
            const int green = ((color >> 5) & 63) * 255 / 63;
            const int blue = (color & 31) * 255 / 31;
            descriptor.bg_color = lv_color_make(red, green, blue);
            lv_area_t bounds;
            lv_obj_get_coords(target->object, &bounds);
            lv_area_t pixel{bounds.x1 + x, bounds.y1 + y, bounds.x1 + x, bounds.y1 + y};
            lv_draw_rect(target->layer, &descriptor, &pixel);
        },
        &draw);
}

void InboxViewController::onBoot()
{
    playback_.pausePreserving();
    request(InboxDestination::Settings);
}
void InboxViewController::onRed()
{
    playback_.pausePreserving();
    request(InboxDestination::MessageTarget);
}
void InboxViewController::viewWasRemoved()
{
    if (lease_.generation != 0) {
        if (lease_.consumers > 0)
            (void)playback_.surfaces().removeConsumer(&lease_);
        (void)playback_.surfaces().release(&lease_);
    }
    heading_ = transport_ = transport_label_ = status_ = row_ = track_ = nullptr;
    for (auto &card : cards_)
        card = {};
    rendered_selected_ = -2;
    rendered_id_[0] = 0;
    body_initialized_ = fading_ = navigation_requested_ = gesture_ = false;
    SplitPaneViewController::viewWasRemoved();
}
} // namespace product_ui
