#include "fl_product_send_p1_wire.hpp"

#include <cinttypes>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace fl_product_send
{
namespace
{

int find_key(const char *body, int len, const char *key)
{
    int key_len = (int)strlen(key);
    int i;
    for (i = 0; i + key_len <= len; ++i) {
        int j;
        for (j = 0; j < key_len; ++j) {
            if (body[i + j] != key[j]) {
                break;
            }
        }
        if (j == key_len) {
            return i + key_len;
        }
    }
    return -1;
}

bool read_string_at(const char *body, int len, int pos, char *dst, int cap)
{
    int n = 0;
    while (pos < len && (body[pos] == ' ' || body[pos] == '\t')) {
        pos += 1;
    }
    if (pos >= len || body[pos] != ':') {
        return false;
    }
    pos += 1;
    while (pos < len && (body[pos] == ' ' || body[pos] == '\t')) {
        pos += 1;
    }
    if (pos >= len || body[pos] != '"') {
        return false;
    }
    pos += 1;
    while (pos < len && body[pos] != '"') {
        if (n < cap - 1) {
            dst[n] = body[pos];
            n += 1;
        }
        pos += 1;
    }
    dst[n] = 0;
    return n > 0;
}

bool read_uint_nested(const char *body, int len, const char *section, const char *field, uint32_t *value)
{
    int section_pos = find_key(body, len, section);
    int field_pos;
    if (section_pos < 0) {
        return false;
    }
    field_pos = find_key(body + section_pos, len - section_pos, field);
    if (field_pos < 0) {
        return false;
    }
    const char *cursor = body + section_pos + field_pos;
    while (cursor < body + len && *cursor != ':' && *cursor != '\0') {
        cursor += 1;
    }
    if (cursor >= body + len || *cursor != ':') {
        return false;
    }
    cursor += 1;
    *value = (uint32_t)strtoul(cursor, 0, 10);
    return true;
}

} /* namespace */

int part_count_for_bytes(uint32_t byte_count)
{
    if (byte_count == 0) {
        return 0;
    }
    return (int)((byte_count + kPartSizeBytes - 1u) / kPartSizeBytes);
}

int build_message_create(const FrozenPayload &payload, char *out, int out_cap)
{
    int written = 0;
    int i;
    if (out == 0 || out_cap < 128) {
        return 0;
    }
    written = snprintf(out, (size_t)out_cap,
                       "{\"schema\":\"fl.message-create/2\",\"client_message_id\":\"%s\","
                       "\"recipient_user_ids\":[",
                       payload.client_message_id);
    for (i = 0; i < payload.recipient_count; ++i) {
        written += snprintf(out + written, (size_t)(out_cap - written), "%s\"%s\"",
                            i == 0 ? "" : ",", payload.recipient_user_ids[i]);
    }
    written += snprintf(out + written, (size_t)(out_cap - written),
                        "],\"audio\":{\"codec\":\"pcm_s16le\",\"sample_rate_hz\":16000,"
                        "\"channels\":1,\"duration_ms\":%" PRIu32 "}",
                        payload.duration_ms);
    if (payload.sketch_present) {
        written += snprintf(out + written, (size_t)(out_cap - written),
                            ",\"sketch\":{\"format_id\":\"flsk2\",\"present\":true}");
    }
    written += snprintf(out + written, (size_t)(out_cap - written), "}");
    return written > 0 && written < out_cap ? written : 0;
}

int build_message_complete(const FrozenPayload &payload, const char *server_message_id, uint32_t audio_parts,
                           uint32_t sketch_parts, const char *audio_sha256_hex,
                           const char *sketch_sha256_hex, char *out, int out_cap)
{
    int written;
    if (out == 0 || server_message_id == 0 || audio_sha256_hex == 0) {
        return 0;
    }
    written = snprintf(out, (size_t)out_cap,
                       "{\"schema\":\"fl.message-complete/2\",\"client_message_id\":\"%s\","
                       "\"audio\":{\"parts\":%" PRIu32 ",\"byte_count\":%" PRIu32
                       ",\"sha256\":\"%s\",\"duration_ms\":%" PRIu32 "}",
                       payload.client_message_id, audio_parts, payload.audio_byte_count, audio_sha256_hex,
                       payload.duration_ms);
    if (payload.sketch_present && sketch_sha256_hex != 0) {
        written += snprintf(out + written, (size_t)(out_cap - written),
                            ",\"sketch\":{\"parts\":%" PRIu32 ",\"byte_count\":%" PRIu32
                            ",\"sha256\":\"%s\"}",
                            sketch_parts,
                            payload.sketch_byte_count, sketch_sha256_hex);
    }
    written += snprintf(out + written, (size_t)(out_cap - written), "}");
    return written > 0 && written < out_cap ? written : 0;
}

int parse_delivery_response(const char *body, int body_len, WireParseResult *out)
{
    int pos;
    if (body == 0 || body_len <= 0 || out == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    pos = find_key(body, body_len, "\"schema\"");
    if (pos < 0 || !read_string_at(body, body_len, pos, out->schema, (int)sizeof(out->schema))) {
        return 0;
    }
    pos = find_key(body, body_len, "\"state\"");
    if (pos >= 0) {
        read_string_at(body, body_len, pos, out->state, (int)sizeof(out->state));
    }
    pos = find_key(body, body_len, "\"client_message_id\"");
    if (pos >= 0) {
        read_string_at(body, body_len, pos, out->client_message_id, (int)sizeof(out->client_message_id));
    }
    pos = find_key(body, body_len, "\"message_id\"");
    if (pos >= 0) {
        read_string_at(body, body_len, pos, out->message_id, (int)sizeof(out->message_id));
    }
    if (read_uint_nested(body, body_len, "\"audio\"", "\"parts_acknowledged\"", &out->audio_parts_ack)) {
        out->ok = 1;
    }
    if (read_uint_nested(body, body_len, "\"sketch\"", "\"parts_acknowledged\"", &out->sketch_parts_ack)) {
        out->sketch_present = 1;
        out->ok = 1;
    }
    if (strcmp(out->schema, "fl.send-receipt/2") == 0 && strcmp(out->state, "accepted") == 0) {
        out->ok = 1;
    }
    if (strcmp(out->schema, "fl.send-open/2") == 0) {
        out->ok = 1;
    }
    return out->ok;
}

} /* namespace fl_product_send */
