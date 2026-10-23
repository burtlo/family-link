#ifndef FL_PRODUCT_INBOX_PRESENTATION_HPP
#define FL_PRODUCT_INBOX_PRESENTATION_HPP

#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox {

constexpr char kFallbackCardBackground[] = "card-bg-01";

SenderPresentation resolve_sender_presentation(const InboxMessage &message);

} /* namespace fl_product_inbox */

#endif
