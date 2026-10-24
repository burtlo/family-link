#include "fl_product_inbox_p1_wire.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_inbox {
namespace {

constexpr int kInboxMaxBytes = 262144;

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

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
    while (pos < len && is_ws(body[pos])) {
        pos += 1;
    }
    if (pos >= len || body[pos] != ':') {
        return false;
    }
    pos += 1;
    while (pos < len && is_ws(body[pos])) {
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

bool read_uint_at(const char *body, int len, int pos, uint32_t *value)
{
    int i = pos;
    uint32_t number = 0;
    bool digits = false;
    while (i < len && is_ws(body[i])) {
        i += 1;
    }
    if (i >= len || body[i] != ':') {
        return false;
    }
    i += 1;
    while (i < len && is_ws(body[i])) {
        i += 1;
    }
    while (i < len && body[i] >= '0' && body[i] <= '9') {
        digits = true;
        number = number * 10u + (uint32_t)(body[i] - '0');
        i += 1;
    }
    if (!digits) {
        return false;
    }
    *value = number;
    return true;
}

bool read_bool_true(const char *body, int len, int pos)
{
    int i = pos;
    while (i < len && is_ws(body[i])) {
        i += 1;
    }
    if (i >= len || body[i] != ':') {
        return false;
    }
    i += 1;
    while (i < len && is_ws(body[i])) {
        i += 1;
    }
    return i + 3 < len && body[i] == 't' && body[i + 1] == 'r' && body[i + 2] == 'u' &&
           body[i + 3] == 'e';
}

bool read_media_descriptor(const char *body, int len, int media_pos, const char *kind,
                           MediaDescriptor *out)
{
    char key[32];
    int pos;
    snprintf(key, sizeof(key), "\"%s\"", kind);
    memset(out, 0, sizeof(*out));
    pos = find_key(body + media_pos, len - media_pos, key);
    if (pos < 0) {
        out->present = 0;
        return true;
    }
    media_pos += pos;
    pos = find_key(body + media_pos, len - media_pos, "\"format_id\"");
    if (pos < 0) {
        return false;
    }
    if (!read_string_at(body, len, media_pos + pos, out->format_id, (int)sizeof(out->format_id))) {
        return false;
    }
    pos = find_key(body + media_pos, len - media_pos, "\"byte_count\"");
    if (pos < 0) {
        return false;
    }
    if (!read_uint_at(body, len, media_pos + pos, &out->byte_count)) {
        return false;
    }
    pos = find_key(body + media_pos, len - media_pos, "\"sha256\"");
    if (pos < 0) {
        return false;
    }
    if (!read_string_at(body, len, media_pos + pos, out->sha256, (int)sizeof(out->sha256))) {
        return false;
    }
    out->present = 1;
    return true;
}

bool read_message_object(const char *body, int len, int obj_pos, InboxMessage *out)
{
    int pos;
    memset(out, 0, sizeof(*out));
    pos = find_key(body + obj_pos, len - obj_pos, "\"message_id\"");
    if (pos < 0 || !read_string_at(body, len, obj_pos + pos, out->message_id,
                                   (int)sizeof(out->message_id))) {
        return false;
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"sender_user_id\"");
    if (pos >= 0) {
        read_string_at(body, len, obj_pos + pos, out->sender_user_id,
                       (int)sizeof(out->sender_user_id));
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"sender_name\"");
    if (pos >= 0) {
        read_string_at(body, len, obj_pos + pos, out->sender_name, (int)sizeof(out->sender_name));
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"sender_avatar_id\"");
    if (pos >= 0) {
        read_string_at(body, len, obj_pos + pos, out->sender_avatar_id,
                       (int)sizeof(out->sender_avatar_id));
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"sender_card_background_id\"");
    if (pos >= 0) {
        read_string_at(body, len, obj_pos + pos, out->sender_card_background_id,
                       (int)sizeof(out->sender_card_background_id));
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"duration_ms\"");
    if (pos >= 0) {
        read_uint_at(body, len, obj_pos + pos, &out->duration_ms);
    }
    pos = find_key(body + obj_pos, len - obj_pos, "\"media\"");
    if (pos >= 0) {
        int media_pos = obj_pos + pos;
        if (!read_media_descriptor(body, len, media_pos, "audio", &out->audio)) {
            return false;
        }
        read_media_descriptor(body, len, media_pos, "sketch", &out->sketch);
    }
    return true;
}

} /* namespace */

int classify_inbox_http(int perform_ok, int http_status, const char *body, int body_len)
{
    if (perform_ok == 0) {
        return 0;
    }
    if (http_status == 401 || http_status == 403) {
        return 0;
    }
    if (http_status < 200 || http_status >= 300) {
        return 0;
    }
    if (body == 0 || body_len <= 0) {
        return 0;
    }
    return 1;
}

int parse_inbox_snapshot(const char *body, int body_len, int body_cap, InboxParseResult *out)
{
    int schema_pos;
    char schema[32];
    int items_pos;
    int obj_pos;
    int count;
    if (out == 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (body == 0 || body_len <= 0) {
        return 0;
    }
    if (body_cap > kInboxMaxBytes || body_len > kInboxMaxBytes) {
        out->oversize = 1;
        return 0;
    }
    schema_pos = find_key(body, body_len, "\"schema\"");
    if (schema_pos < 0 || !read_string_at(body, body_len, schema_pos, schema, (int)sizeof(schema))) {
        return 0;
    }
    if (strcmp(schema, "fl.inbox-page/2") != 0) {
        return 0;
    }
    out->schema_ok = 1;
    schema_pos = find_key(body, body_len, "\"inbox_revision\"");
    if (schema_pos < 0 || !read_uint_at(body, body_len, schema_pos, &out->inbox_revision)) {
        return 0;
    }
    schema_pos = find_key(body, body_len, "\"last_viewed_message_id\"");
    if (schema_pos >= 0) {
        int at = schema_pos;
        while (at < body_len && is_ws(body[at])) {
            at += 1;
        }
        if (at < body_len && body[at] == ':') {
            at += 1;
            while (at < body_len && is_ws(body[at])) {
                at += 1;
            }
            if (at < body_len && body[at] == '"') {
                read_string_at(body, body_len, schema_pos, out->last_viewed_message_id,
                               (int)sizeof(out->last_viewed_message_id));
            }
        }
    }
    schema_pos = find_key(body, body_len, "\"has_more\"");
    if (schema_pos >= 0 && read_bool_true(body, body_len, schema_pos)) {
        out->has_more = 1;
        return 0;
    }
    items_pos = find_key(body, body_len, "\"items\"");
    if (items_pos < 0) {
        return 0;
    }
    obj_pos = items_pos;
    while (obj_pos < body_len && body[obj_pos] != '[') {
        obj_pos += 1;
    }
    if (obj_pos >= body_len) {
        return 0;
    }
    obj_pos += 1;
    count = 0;
    while (obj_pos < body_len) {
        int mid = find_key(body + obj_pos, body_len - obj_pos, "\"message_id\"");
        if (mid < 0) {
            break;
        }
        if (count >= kMaxMessages) {
            out->overflow = 1;
            out->parse_ok = 0;
            out->message_count = 0;
            return 0;
        }
        int scan = obj_pos + mid;
        int obj_start = scan - 1;
        while (obj_start > 0 && body[obj_start] != '{') {
            obj_start -= 1;
        }
        if (body[obj_start] != '{') {
            return 0;
        }
        if (!read_message_object(body, body_len, obj_start, &out->messages[count])) {
            return 0;
        }
        count += 1;
        obj_pos = obj_start + 1;
        for (; obj_pos < body_len; ++obj_pos) {
            if (body[obj_pos] == '}') {
                obj_pos += 1;
                break;
            }
        }
    }
    out->message_count = count;
    out->parse_ok = 1;
    return 1;
}

} /* namespace fl_product_inbox */
