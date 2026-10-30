#include "fl_product_inbox_p1_wire.hpp"

#include <stdio.h>
#include <string.h>
#include <initializer_list>

namespace fl_product_inbox
{
namespace
{

constexpr int kInboxMaxBytes = 262144;

bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

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

int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Validate strings even when skipping. Decode JSON escapes for presentation;
// never truncate an identity or split a UTF-8 code point while shortening names.
bool metadata_string(const char *body, int len, int &pos, char *out = nullptr, int cap = 0,
                      bool truncate = false)
{
    if (pos >= len || body[pos++] != '"') return false;
    int written = 0;
    bool overflow = false;
    while (pos < len) {
        unsigned code = static_cast<unsigned char>(body[pos++]);
        if (code == '"') {
            if (out && cap > 0) out[written] = 0;
            return !overflow || truncate;
        }
        if (code < 32) return false;
        if (code == '\\') {
            if (pos >= len) return false;
            code = static_cast<unsigned char>(body[pos++]);
            switch (code) {
                case '"': case '\\': case '/': break;
                case 'b': code = 8; break;
                case 'f': code = 12; break;
                case 'n': code = 10; break;
                case 'r': code = 13; break;
                case 't': code = 9; break;
                case 'u': {
                    code = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (pos >= len || hex_value(body[pos]) < 0) return false;
                        code = code * 16 + hex_value(body[pos++]);
                    }
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (pos + 6 > len || body[pos++] != '\\' || body[pos++] != 'u') return false;
                        unsigned low = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (hex_value(body[pos]) < 0) return false;
                            low = low * 16 + hex_value(body[pos++]);
                        }
                        if (low < 0xdc00 || low > 0xdfff) return false;
                        code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                    } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                    if (!code) return false; // C string fields cannot represent embedded NUL.
                    break;
                }
                default: return false;
            }
            char encoded[4];
            int bytes = 1;
            if (code < 0x80) encoded[0] = static_cast<char>(code);
            else if (code < 0x800) {
                encoded[0] = static_cast<char>(0xc0 | (code >> 6));
                encoded[1] = static_cast<char>(0x80 | (code & 63)); bytes = 2;
            } else if (code < 0x10000) {
                encoded[0] = static_cast<char>(0xe0 | (code >> 12));
                encoded[1] = static_cast<char>(0x80 | ((code >> 6) & 63));
                encoded[2] = static_cast<char>(0x80 | (code & 63)); bytes = 3;
            } else {
                encoded[0] = static_cast<char>(0xf0 | (code >> 18));
                encoded[1] = static_cast<char>(0x80 | ((code >> 12) & 63));
                encoded[2] = static_cast<char>(0x80 | ((code >> 6) & 63));
                encoded[3] = static_cast<char>(0x80 | (code & 63)); bytes = 4;
            }
            if (out) {
                if (!overflow && written + bytes < cap) {
                    memcpy(out + written, encoded, bytes); written += bytes;
                } else overflow = true;
            }
        } else if (out) {
            if (!overflow && written + 1 < cap) out[written++] = static_cast<char>(code);
            else overflow = true;
        }
    }
    return false;
}

void metadata_ws(const char *body, int len, int &pos)
{
    while (pos < len && is_ws(body[pos])) ++pos;
}

bool metadata_skip(const char *body, int len, int &pos, int depth = 0)
{
    metadata_ws(body, len, pos);
    if (pos >= len || depth > 16) return false;
    if (body[pos] == '"') return metadata_string(body, len, pos);
    if (body[pos] == '{' || body[pos] == '[') {
        bool object = body[pos++] == '{';
        const char end = object ? '}' : ']';
        metadata_ws(body, len, pos);
        if (pos < len && body[pos] == end) { ++pos; return true; }
        for (;;) {
            if (object) {
                if (!metadata_string(body, len, pos)) return false;
                metadata_ws(body, len, pos);
                if (pos >= len || body[pos++] != ':') return false;
            }
            if (!metadata_skip(body, len, pos, depth + 1)) return false;
            metadata_ws(body, len, pos);
            if (pos >= len) return false;
            if (body[pos] == end) { ++pos; return true; }
            if (body[pos++] != ',') return false;
            metadata_ws(body, len, pos);
        }
    }
    for (const char *literal : {"true", "false", "null"}) {
        int n = static_cast<int>(strlen(literal));
        if (pos + n <= len && strncmp(body + pos, literal, n) == 0) { pos += n; return true; }
    }
    if (body[pos] == '-') ++pos;
    if (pos >= len || body[pos] < '0' || body[pos] > '9') return false;
    if (body[pos] == '0') ++pos;
    else while (pos < len && body[pos] >= '0' && body[pos] <= '9') ++pos;
    if (pos < len && body[pos] == '.') {
        ++pos;
        int start = pos;
        while (pos < len && body[pos] >= '0' && body[pos] <= '9') ++pos;
        if (pos == start) return false;
    }
    if (pos < len && (body[pos] == 'e' || body[pos] == 'E')) {
        ++pos;
        if (pos < len && (body[pos] == '-' || body[pos] == '+')) ++pos;
        int start = pos;
        while (pos < len && body[pos] >= '0' && body[pos] <= '9') ++pos;
        if (pos == start) return false;
    }
    return true;
}

int metadata_field(const char *body, int len, const char *key)
{
    int pos = 0;
    metadata_ws(body, len, pos);
    if (pos >= len || body[pos++] != '{') return -1;
    metadata_ws(body, len, pos);
    while (pos < len && body[pos] != '}') {
        char name[64];
        if (!metadata_string(body, len, pos, name, sizeof(name))) return -1;
        metadata_ws(body, len, pos);
        if (pos >= len || body[pos++] != ':') return -1;
        metadata_ws(body, len, pos);
        if (strcmp(name, key) == 0) return pos;
        if (!metadata_skip(body, len, pos)) return -1;
        metadata_ws(body, len, pos);
        if (pos >= len || body[pos++] != ',') break;
        metadata_ws(body, len, pos);
    }
    return -1;
}

bool metadata_uint(const char *body, int len, const char *key, uint32_t &value)
{
    int pos = metadata_field(body, len, key);
    if (pos < 0 || body[pos] < '0' || body[pos] > '9') return false;
    value = 0;
    do {
        const unsigned digit = body[pos++] - '0';
        if (value > (UINT32_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    } while (pos < len && body[pos] >= '0' && body[pos] <= '9');
    return pos == len || is_ws(body[pos]) || body[pos] == ',' || body[pos] == '}';
}

bool metadata_text(const char *body, int len, const char *key, char *text, int cap,
                   bool optional = false, bool truncate = false)
{
    const int at = metadata_field(body, len, key);
    if (at < 0) { text[0] = 0; return optional; }
    int pos = at;
    if (optional && pos + 4 <= len && strncmp(body + pos, "null", 4) == 0) { text[0] = 0; return true; }
    return metadata_string(body, len, pos, text, cap, truncate);
}

bool metadata_media(const char *body, int len, const char *key, MediaDescriptor &out)
{
    const int pos = metadata_field(body, len, key);
    if (pos < 0) return false;
    body += pos; len -= pos;
    if (!metadata_text(body, len, "format_id", out.format_id, sizeof(out.format_id)) ||
        !metadata_uint(body, len, "byte_count", out.byte_count) ||
        !metadata_text(body, len, "sha256", out.sha256, sizeof(out.sha256))) return false;
    if (strlen(out.sha256) != 64) return false;
    for (int i = 0; i < 64; ++i)
        if (!((out.sha256[i] >= '0' && out.sha256[i] <= '9') ||
              (out.sha256[i] >= 'a' && out.sha256[i] <= 'f'))) return false;
    out.present = 1;
    return true;
}

bool metadata_message(const char *body, int len, InboxMessage &out)
{
    memset(&out, 0, sizeof(out));
    char kind[24];
    if (!metadata_text(body, len, "message_id", out.message_id, sizeof(out.message_id)) || !*out.message_id ||
        !metadata_text(body, len, "sender_user_id", out.sender_user_id, sizeof(out.sender_user_id)) ||
        !metadata_text(body, len, "sender_name", out.sender_name, sizeof(out.sender_name), false, true) ||
        !metadata_uint(body, len, "duration_ms", out.duration_ms) ||
        !metadata_text(body, len, "kind", kind, sizeof(kind)) ||
        (strcmp(kind, "audio") && strcmp(kind, "audio_sketch"))) return false;
    if (!metadata_text(body, len, "sender_avatar_id", out.sender_avatar_id, sizeof(out.sender_avatar_id), true) ||
        !metadata_text(body, len, "sender_card_background_id", out.sender_card_background_id,
                       sizeof(out.sender_card_background_id), true)) return false;
    const int pos = metadata_field(body, len, "media");
    if (pos < 0 || !metadata_media(body + pos, len - pos, "audio", out.audio)) return false;
    if (metadata_field(body + pos, len - pos, "sketch") >= 0 &&
        !metadata_media(body + pos, len - pos, "sketch", out.sketch)) return false;
    return strcmp(kind, "audio_sketch") != 0 || out.sketch.present;
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

int parse_inbox_metadata(const char *body, int body_len, int body_cap,
                         InboxMetadataHeader *out, InboxMetadataVisitor visitor, void *context)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!body || body_len <= 0 || body_len > body_cap || body_cap > kInboxMaxBytes) return 0;
    int pos = 0;
    if (!metadata_skip(body, body_len, pos)) return 0;
    metadata_ws(body, body_len, pos);
    if (pos != body_len) return 0;
    char schema[32];
    if (!metadata_text(body, body_len, "schema", schema, sizeof(schema)) ||
        strcmp(schema, "fl.inbox-page/2") != 0 ||
        !metadata_uint(body, body_len, "inbox_revision", out->inbox_revision)) return 0;
    uint32_t total = 0;
    if (!metadata_uint(body, body_len, "total_count", total)) return 0;
    pos = metadata_field(body, body_len, "has_more");
    if (pos < 0 || strncmp(body + pos, "false", 5)) return 0;
    pos = metadata_field(body, body_len, "next_cursor");
    if (pos < 0 || strncmp(body + pos, "null", 4)) return 0;
    pos = metadata_field(body, body_len, "last_viewed_message_id");
    if (pos < 0) return 0;
    if (strncmp(body + pos, "null", 4) &&
        !metadata_string(body, body_len, pos, out->last_viewed_message_id,
                          sizeof(out->last_viewed_message_id))) return 0;
    pos = metadata_field(body, body_len, "items");
    if (pos < 0 || body[pos++] != '[') return 0;
    for (;;) {
        while (pos < body_len && is_ws(body[pos])) ++pos;
        if (pos >= body_len) return 0;
        if (body[pos] == ']') return total == static_cast<uint32_t>(out->message_count);
        if (body[pos] != '{') return 0;
        const int start = pos;
        int depth = 0;
        bool string = false;
        bool escaped = false;
        do {
            char c = body[pos++];
            if (string) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') string = false;
            } else if (c == '"') string = true;
            else if (c == '{') ++depth;
            else if (c == '}') --depth;
        } while (pos < body_len && (depth > 0 || string));
        if (depth || string) return 0;
        InboxMessage message;
        if (!metadata_message(body + start, pos - start, message) ||
            !message.audio.present || (visitor && !visitor(context, message))) return 0;
        ++out->message_count;
        while (pos < body_len && is_ws(body[pos])) ++pos;
        if (pos >= body_len) return 0;
        if (body[pos] == ']') return total == static_cast<uint32_t>(out->message_count);
        if (body[pos++] != ',') return 0;
        int next = pos;
        while (next < body_len && is_ws(body[next])) ++next;
        if (next >= body_len || body[next] != '{') return 0;
    }
}

} /* namespace fl_product_inbox */
