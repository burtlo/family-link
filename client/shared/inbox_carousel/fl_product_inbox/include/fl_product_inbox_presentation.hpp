#ifndef FL_PRODUCT_INBOX_PRESENTATION_HPP
#define FL_PRODUCT_INBOX_PRESENTATION_HPP

#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox
{

constexpr char kFallbackCardBackground[] = "card-bg-01";

enum LowerSlotKind {
    kLowerCarousel = 0,
    kLowerEmpty = 1,
    kLowerConnecting = 2,
    kLowerNoWifi = 3
};

struct InboxChrome {
    char heading[64];
    char transport_label[8];
    int transport_hidden;
    int transport_disabled;
    int lower_kind;
    char lower_text[32];
};

class CarouselSession;

SenderPresentation resolve_sender_presentation(const InboxMessage &message);

/* Heading, Play/Pause, and lower-slot kind. Strings live here so the device
 * bridge does not keep a second copy. */
void inbox_chrome(const CarouselSession &session, InboxChrome *out);

/* Whole seconds, `m:ss`. 8000 ms is `0:08`. 134000 ms is `2:14`. */
void format_duration_mss(uint32_t duration_ms, char *out, int cap);

/* `connecting` plus one, two, or three dots. dot_count is clamped to 1..3. */
void format_connecting_status(int dot_count, char *out, int cap);

/* Portrait, name, and duration hide while this card's sketch ink is visible. */
int card_face_portrait_hidden(int sketch_visible);

} /* namespace fl_product_inbox */

#endif
