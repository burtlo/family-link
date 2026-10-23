#include "fl_product_inbox_presentation.hpp"

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

} /* namespace fl_product_inbox */
