#ifndef FL_PRODUCT_INBOX_P1_WIRE_HPP
#define FL_PRODUCT_INBOX_P1_WIRE_HPP

#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox
{

struct InboxParseResult {
    int parse_ok;
    int schema_ok;
    int has_more;
    int oversize;
    int overflow;
    uint32_t inbox_revision;
    char last_viewed_message_id[kMaxMessageId];
    int message_count;
    InboxMessage messages[kMaxMessages];
};

int classify_inbox_http(int perform_ok, int http_status, const char *body, int body_len);
int parse_inbox_snapshot(const char *body, int body_len, int body_cap, InboxParseResult *out);

// Metadata-only streaming parser. No per-message cap: the accepted wire body
// remains bounded to 262144 bytes. Visitors run on the HTTP worker, not the UI.
struct InboxMetadataHeader {
    uint32_t inbox_revision;
    char last_viewed_message_id[kMaxMessageId];
    int message_count;
};
using InboxMetadataVisitor = bool (*)(void *, const InboxMessage &);
int parse_inbox_metadata(const char *body, int body_len, int body_cap,
                         InboxMetadataHeader *out, InboxMetadataVisitor visitor = nullptr,
                         void *context = nullptr);
// A worker result is one free()-owned block: header followed by message_count
// InboxMessage records. Its allocation is in PSRAM on the device.
inline const InboxMessage *metadata_messages(const InboxMetadataHeader *header)
{
    return reinterpret_cast<const InboxMessage *>(header + 1);
}

} /* namespace fl_product_inbox */

#endif
