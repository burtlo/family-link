#ifndef FL_PRODUCT_INBOX_P1_WIRE_HPP
#define FL_PRODUCT_INBOX_P1_WIRE_HPP

#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox {

struct InboxParseResult {
    int parse_ok;
    int schema_ok;
    int has_more;
    int oversize;
    uint32_t inbox_revision;
    char last_viewed_message_id[kMaxMessageId];
    int message_count;
    InboxMessage messages[kMaxMessages];
};

int classify_inbox_http(int perform_ok, int http_status, const char *body, int body_len);
int parse_inbox_snapshot(const char *body, int body_len, int body_cap, InboxParseResult *out);

} /* namespace fl_product_inbox */

#endif
