#include "fl_product_login_p1_wire.hpp"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace fl_product_login {
namespace {

bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool json_balanced(const char *body, int body_len)
{
    int i = 0;
    int brace = 0;
    int bracket = 0;
    bool in_string = false;
    bool escape = false;
    if (body == 0 || body_len <= 0) {
        return false;
    }
    while (i < body_len && is_ws(body[i])) {
        i += 1;
    }
    if (i >= body_len || body[i] != '{') {
        return false;
    }
    for (; i < body_len; ++i) {
        char c = body[i];
        if (in_string) {
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '{') {
            brace += 1;
        } else if (c == '}') {
            brace -= 1;
            if (brace == 0 && bracket == 0) {
                i += 1;
                break;
            }
        } else if (c == '[') {
            bracket += 1;
        } else if (c == ']') {
            bracket -= 1;
        }
    }
    if (in_string || brace != 0 || bracket != 0) {
        return false;
    }
    while (i < body_len && is_ws(body[i])) {
        i += 1;
    }
    return i == body_len;
}

int find_key(const char *body, int len, const char *key)
{
    int key_len = (int)strlen(key);
    int i;
    if (key_len <= 0 || len < key_len) {
        return -1;
    }
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

bool read_string(const char *body, int len, int pos, char *dst, int cap)
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
    if (pos >= len || body[pos] != '"') {
        return false;
    }
    dst[n] = 0;
    return n > 0 || cap > 0;
}

/* Hangout revision is the first 32 bits of a content hash, so it does not fit in a signed int. */
bool read_u32(const char *body, int len, int pos, uint32_t *value)
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
        uint32_t digit = (uint32_t)(body[i] - '0');
        if (number > (4294967295u - digit) / 10u) {
            return false;
        }
        digits = true;
        number = number * 10u + digit;
        i += 1;
    }
    if (!digits) {
        return false;
    }
    *value = number;
    return true;
}

bool schema_is(const char *body, int len, const char *literal)
{
    char found[48];
    int at = find_key(body, len, "\"schema\"");
    if (at < 0 || !read_string(body, len, at, found, (int)sizeof(found))) {
        return false;
    }
    return strcmp(found, literal) == 0;
}

void copy_field(char *dst, int cap, const char *src)
{
    int i = 0;
    if (cap <= 0) {
        return;
    }
    if (src == 0) {
        src = "";
    }
    for (; i < cap - 1 && src[i] != 0; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

} /* namespace */

RosterResult classify_roster_http(uint64_t operation_id, int perform_ok, int http_status,
                                  const char *body, int body_len)
{
    RosterResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    if (!perform_ok || http_status != 200 || body == 0 || body_len <= 0) {
        result.http_status = perform_ok ? http_status : 0;
        return result;
    }
    result.http_status = http_status;
    if (!json_balanced(body, body_len) || !schema_is(body, body_len, "fl.hangout-roster/2")) {
        result.parse_ok = 0;
        result.valid = 0;
        return result;
    }
    result.parse_ok = 1;
    int hangout_at = find_key(body, body_len, "\"hangout_id\"");
    char hangout_id[48];
    uint32_t revision = 0;
    if (hangout_at < 0 || !read_string(body, body_len, hangout_at, hangout_id, (int)sizeof(hangout_id))) {
        result.valid = 0;
        return result;
    }
    int rev_at = find_key(body, body_len, "\"revision\"");
    if (rev_at < 0 || !read_u32(body, body_len, rev_at, &revision)) {
        result.valid = 0;
        return result;
    }
    (void)revision;
    int users_at = find_key(body, body_len, "\"users\"");
    if (users_at < 0) {
        result.valid = 0;
        return result;
    }
    int cursor = users_at;
    while (cursor < body_len) {
        int obj = find_key(body + cursor, body_len - cursor, "{");
        if (obj < 0) {
            break;
        }
        cursor += obj;
        if (result.user_count >= kMaxUsers) {
            result.valid = 0;
            result.user_count = 0;
            return result;
        }
        char user_id[kMaxId];
        char display_name[32];
        char avatar_id[24];
        char card_bg[24];
        user_id[0] = display_name[0] = avatar_id[0] = card_bg[0] = 0;
        int uid_at = find_key(body + cursor, body_len - cursor, "\"user_id\"");
        int name_at = find_key(body + cursor, body_len - cursor, "\"display_name\"");
        if (uid_at < 0 || name_at < 0 ||
            !read_string(body + cursor, body_len - cursor, uid_at, user_id, kMaxId) ||
            !read_string(body + cursor, body_len - cursor, name_at, display_name, (int)sizeof(display_name))) {
            result.valid = 0;
            result.user_count = 0;
            return result;
        }
        int av_at = find_key(body + cursor, body_len - cursor, "\"avatar_id\"");
        if (av_at >= 0) {
            read_string(body + cursor, body_len - cursor, av_at, avatar_id, (int)sizeof(avatar_id));
        }
        int bg_at = find_key(body + cursor, body_len - cursor, "\"card_background_id\"");
        if (bg_at >= 0) {
            read_string(body + cursor, body_len - cursor, bg_at, card_bg, (int)sizeof(card_bg));
        }
        for (int i = 0; i < result.user_count; ++i) {
            if (strcmp(result.users[i].id, user_id) == 0) {
                result.valid = 0;
                result.user_count = 0;
                return result;
            }
        }
        copy_field(result.users[result.user_count].id, kMaxId, user_id);
        copy_field(result.users[result.user_count].display_name, 32, display_name);
        copy_field(result.users[result.user_count].avatar_id, 24, avatar_id);
        copy_field(result.users[result.user_count].card_background_id, 24, card_bg);
        result.user_count += 1;
        int close = find_key(body + cursor, body_len - cursor, "}");
        if (close < 0) {
            break;
        }
        cursor += close;
    }
    if (find_key(body, body_len, "\"user_id\"") >= 0 && result.user_count == 0) {
        /* Parsed users array header but found no objects — still valid empty hangout when users is [] */
        int empty_users = find_key(body, body_len, "\"users\": []");
        if (empty_users < 0 && find_key(body, body_len, "\"users\":[]") < 0) {
            /* non-empty array expected objects; if we saw user_id keys we already counted */
        }
    }
    result.valid = 1;
    return result;
}

RosterResult deliver_classified_roster(SessionWriter *session, uint64_t operation_id, int perform_ok,
                                       int http_status, const char *body, int body_len, int64_t now_ms)
{
    RosterResult result = classify_roster_http(operation_id, perform_ok, http_status, body, body_len);
    if (session != 0) {
        session->deliver_roster_completion(result, now_ms);
    }
    return result;
}

PinResult classify_pin_http(uint64_t operation_id, uint32_t generation, const char *session_id,
                            const char *user_id, int perform_ok, int http_status, const char *body,
                            int body_len)
{
    PinResult result;
    char outcome[24];
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    result.generation = generation;
    if (session_id != 0) {
        strncpy(result.session_id, session_id, sizeof(result.session_id) - 1);
    }
    if (user_id != 0) {
        strncpy(result.user_id, user_id, sizeof(result.user_id) - 1);
    }
    result.outcome = kPinOutcomeTransport;
    if (!perform_ok || body == 0 || body_len <= 0) {
        result.http_status = perform_ok ? http_status : 0;
        return result;
    }
    result.http_status = http_status;
    if (!json_balanced(body, body_len) || !schema_is(body, body_len, "fl.session-login-result/2")) {
        result.parse_ok = 0;
        return result;
    }
    int outcome_at = find_key(body, body_len, "\"outcome\"");
    if (outcome_at < 0 || !read_string(body, body_len, outcome_at, outcome, (int)sizeof(outcome))) {
        result.parse_ok = 0;
        return result;
    }
    result.parse_ok = 1;
    if (http_status != 200) {
        return result;
    }
    if (strcmp(outcome, "authenticated") == 0) {
        char session_token[kSessionTokenCap];
        int session_at = find_key(body, body_len, "\"session\"");
        session_token[0] = 0;
        if (session_at >= 0) {
            read_string(body, body_len, session_at, session_token, kSessionTokenCap);
        }
        if (session_token[0] != 0) {
            copy_field(result.session_token, kSessionTokenCap, session_token);
            result.session_token_valid = 1;
        }
        result.outcome = kPinOutcomeSuccess;
        return result;
    }
    if (strcmp(outcome, "wrong_pin") == 0) {
        result.outcome = kPinOutcomeWrong;
        return result;
    }
    if (strcmp(outcome, "unknown_user") == 0) {
        result.outcome = kPinOutcomeUnknownUser;
        return result;
    }
    result.outcome = kPinOutcomeTransport;
    return result;
}

void format_p1_operation_id(uint64_t operation_id, char *out, int out_cap)
{
    if (out == 0 || out_cap < 37) {
        if (out != 0 && out_cap > 0) {
            out[0] = 0;
        }
        return;
    }
    snprintf(out, (size_t)out_cap, "00000000-0000-4000-8000-%012llx",
             (unsigned long long)(operation_id & 0xffffffffffffULL));
}

PinResult deliver_classified_pin(SessionWriter *session, uint64_t operation_id, uint32_t generation,
                                 const char *session_id, const char *user_id, int perform_ok,
                                 int http_status, const char *body, int body_len, int64_t now_ms)
{
    PinResult result = classify_pin_http(operation_id, generation, session_id, user_id, perform_ok, http_status,
                                       body, body_len);
    if (session != 0) {
        session->deliver_pin_completion(result, now_ms);
    }
    return result;
}

int classify_refresh_http(int perform_ok, int http_status, const char *body, int body_len)
{
    if (!perform_ok || http_status != 200) {
        return 0;
    }
    if (body == 0) {
        return body_len == 0 ? 1 : 0;
    }
    if (body_len == 0) {
        return 1;
    }
    return json_balanced(body, body_len) && body_len == 2 && body[0] == '{' && body[1] == '}';
}

bool json_bool_true(const char *body, int len, int key_pos)
{
    int i = key_pos;
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
    return i + 4 <= len && body[i] == 't' && body[i + 1] == 'r' && body[i + 2] == 'u' && body[i + 3] == 'e';
}

int classify_invalidate_http(int perform_ok, int http_status, const char *body, int body_len)
{
    if (!perform_ok || http_status != 200 || body == 0 || body_len <= 0) {
        return 0;
    }
    if (!json_balanced(body, body_len) ||
        !schema_is(body, body_len, "fl.session-invalidate-result/2")) {
        return 0;
    }
    int revoked_at = find_key(body, body_len, "\"revoked\"");
    if (revoked_at < 0 || !json_bool_true(body, body_len, revoked_at)) {
        return 0;
    }
    return 1;
}

int classify_health_ack_http(int perform_ok, int http_status, const char *body, int body_len)
{
    if (!perform_ok || http_status != 200 || body == 0 || body_len <= 0) {
        return 0;
    }
    if (!json_balanced(body, body_len) || !schema_is(body, body_len, "fl.endpoint-health-ack/2")) {
        return 0;
    }
    int accepted_at = find_key(body, body_len, "\"accepted\"");
    if (accepted_at < 0 || !json_bool_true(body, body_len, accepted_at)) {
        return 0;
    }
    return 1;
}

int parse_health_server_time(const char *body, int body_len, char *out, int out_cap)
{
    int at;
    if (body == 0 || body_len <= 0 || out == 0 || out_cap <= 0) {
        return 0;
    }
    out[0] = 0;
    at = find_key(body, body_len, "\"server_time\"");
    if (at < 0 || !read_string(body, body_len, at, out, out_cap)) {
        return 0;
    }
    return out[0] != 0 ? 1 : 0;
}

void format_invalidate_body(uint64_t pin_operation_id, const char *session_token, char *out, int out_cap)
{
    char operation_text[40];
    if (out == 0 || out_cap < 32) {
        return;
    }
    format_p1_operation_id(pin_operation_id, operation_text, (int)sizeof(operation_text));
    if (session_token != 0 && session_token[0] != 0) {
        snprintf(out, (size_t)out_cap,
                 "{\"schema\":\"fl.session-invalidate/2\",\"operation_id\":\"%s\",\"session\":\"%s\"}",
                 operation_text, session_token);
    } else {
        snprintf(out, (size_t)out_cap,
                 "{\"schema\":\"fl.session-invalidate/2\",\"operation_id\":\"%s\"}", operation_text);
    }
}

} /* namespace fl_product_login */
