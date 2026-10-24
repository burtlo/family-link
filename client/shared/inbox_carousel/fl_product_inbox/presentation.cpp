#include "fl_product_inbox_presentation.hpp"

#include "fl_product_inbox_carousel.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_inbox {
namespace {

bool catalog_avatar(const char *id)
{
    static const char *kKnown[] = {
        "avatar-01", "avatar-02", "avatar-03", "avatar-04",
        "avatar-05", "avatar-06", "avatar-07", "avatar-08",
    };
    if (id == 0 || id[0] == 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i) {
        if (strcmp(id, kKnown[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool catalog_card(const char *id)
{
    static const char *kKnown[] = {
        "card-bg-01", "card-bg-02", "card-bg-03", "card-bg-04",
        "card-bg-05", "card-bg-06", "card-bg-07", "card-bg-08",
    };
    if (id == 0 || id[0] == 0) {
        return false;
    }
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i) {
        if (strcmp(id, kKnown[i]) == 0) {
            return true;
        }
    }
    return false;
}

void copy_chrome_text(char *dst, int cap, const char *src)
{
    if (dst == 0 || cap <= 0) {
        return;
    }
    dst[0] = 0;
    if (src == 0) {
        return;
    }
    strncpy(dst, src, (size_t)cap - 1);
    dst[cap - 1] = 0;
}

} /* namespace */

SenderPresentation resolve_sender_presentation(const InboxMessage &message)
{
    SenderPresentation out;
    memset(&out, 0, sizeof(out));
    strncpy(out.display_name, message.sender_name, sizeof(out.display_name) - 1);
    strncpy(out.avatar_id, message.sender_avatar_id, sizeof(out.avatar_id) - 1);
    strncpy(out.card_background_id, message.sender_card_background_id,
            sizeof(out.card_background_id) - 1);
    out.use_initials_fallback = catalog_avatar(out.avatar_id) ? 0 : 1;
    out.use_card_fallback = catalog_card(out.card_background_id) ? 0 : 1;
    if (out.use_card_fallback) {
        strncpy(out.card_background_id, kFallbackCardBackground, sizeof(out.card_background_id) - 1);
    }
    return out;
}

void inbox_chrome(const CarouselSession &session, InboxChrome *out)
{
    const InboxMessage *selected;
    int has_selection;
    ConnectivitySlot slot;
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    slot = session.connectivity_slot();
    selected = session.message_at(session.selected_index());
    has_selection = selected != 0 && selected->message_id[0] != 0 ? 1 : 0;
    if (slot == kSlotNoWifi) {
        out->lower_kind = kLowerNoWifi;
        copy_chrome_text(out->lower_text, (int)sizeof(out->lower_text), "no Wi-Fi");
    } else if (slot == kSlotConnecting) {
        out->lower_kind = kLowerConnecting;
        copy_chrome_text(out->lower_text, (int)sizeof(out->lower_text), "connecting");
    } else if (session.has_valid_inbox() && session.message_count() == 0) {
        out->lower_kind = kLowerEmpty;
        copy_chrome_text(out->lower_text, (int)sizeof(out->lower_text), "No messages");
    } else if (session.has_valid_inbox() && session.message_count() > 0) {
        out->lower_kind = kLowerCarousel;
    } else {
        out->lower_kind = kLowerConnecting;
        copy_chrome_text(out->lower_text, (int)sizeof(out->lower_text), "connecting");
    }
    if (has_selection && session.playback_phase() == kPlaybackCantPlay && slot == kSlotNone) {
        copy_chrome_text(out->heading, (int)sizeof(out->heading), "can't play");
    } else if (has_selection) {
        snprintf(out->heading, sizeof(out->heading), "From %s", selected->sender_name);
    } else {
        copy_chrome_text(out->heading, (int)sizeof(out->heading), "Messages");
    }
    if (!has_selection || session.transport_state() == kTransportHidden) {
        out->transport_hidden = 1;
        return;
    }
    out->transport_hidden = 0;
    if (session.playback_phase() == kPlaybackPlaying && session.play_disabled() == 0) {
        copy_chrome_text(out->transport_label, (int)sizeof(out->transport_label), "Pause");
        out->transport_disabled = 0;
        return;
    }
    copy_chrome_text(out->transport_label, (int)sizeof(out->transport_label), "Play");
    out->transport_disabled = session.play_disabled() ? 1 : 0;
}

void format_duration_mss(uint32_t duration_ms, char *out, int cap)
{
    unsigned total;
    unsigned minutes;
    unsigned seconds;
    if (out == 0 || cap < 6) {
        return;
    }
    total = (unsigned)(duration_ms / 1000u);
    minutes = total / 60u;
    seconds = total % 60u;
    snprintf(out, (size_t)cap, "%u:%02u", minutes, seconds);
}

void format_connecting_status(int dot_count, char *out, int cap)
{
    int n;
    int i;
    if (out == 0 || cap < 16) {
        return;
    }
    if (dot_count < 1) {
        dot_count = 1;
    }
    if (dot_count > 3) {
        dot_count = 3;
    }
    n = snprintf(out, (size_t)cap, "%s", "connecting");
    if (n < 0 || n >= cap) {
        return;
    }
    for (i = 0; i < dot_count && n + 1 < cap; ++i) {
        out[n++] = '.';
    }
    out[n] = 0;
}

int card_face_portrait_hidden(int sketch_visible)
{
    return sketch_visible ? 1 : 0;
}

} /* namespace fl_product_inbox */
