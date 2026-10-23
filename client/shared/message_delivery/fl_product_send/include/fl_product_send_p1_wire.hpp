#ifndef FL_PRODUCT_SEND_P1_WIRE_HPP
#define FL_PRODUCT_SEND_P1_WIRE_HPP

#include "fl_product_send_types.hpp"

namespace fl_product_send {

struct WireParseResult {
    int ok;
    char schema[32];
    char state[16];
    char client_message_id[kMaxClientMessageId];
    char message_id[kMaxMessageId];
    uint32_t audio_parts_ack;
    uint32_t sketch_parts_ack;
    int sketch_present;
};

int build_message_create(const FrozenPayload &payload, char *out, int out_cap);
int build_message_complete(const FrozenPayload &payload, const char *server_message_id,
                           uint32_t audio_parts, uint32_t sketch_parts, const char *audio_sha256_hex,
                           const char *sketch_sha256_hex, char *out, int out_cap);

int parse_delivery_response(const char *body, int body_len, WireParseResult *out);
int part_count_for_bytes(uint32_t byte_count);

} /* namespace fl_product_send */

#endif
