#include "pin_http.hpp"

#include <string.h>

namespace lab08 {
namespace {

bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int find_key(const char *body, int len, const char *key)
{
    int key_len = (int)strlen(key);
    int i;
    if (body == 0 || key_len <= 0 || len < key_len) {
        return -1;
    }
    for (i = 0; i <= len - key_len; ++i) {
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
    int i = pos;
    int n = 0;
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
    if (i >= len || body[i] != '"') {
        return false;
    }
    i += 1;
    while (i < len && body[i] != '"') {
        if (n < cap - 1) {
            dst[n] = body[i];
            n += 1;
        }
        i += 1;
    }
    if (i >= len || body[i] != '"') {
        return false;
    }
    dst[n] = 0;
    return true;
}

bool read_int(const char *body, int len, int pos, int *value)
{
    int i = pos;
    int sign = 1;
    int number = 0;
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
    if (i < len && body[i] == '-') {
        sign = -1;
        i += 1;
    }
    while (i < len && body[i] >= '0' && body[i] <= '9') {
        digits = true;
        number = number * 10 + (body[i] - '0');
        i += 1;
    }
    if (!digits) {
        return false;
    }
    *value = number * sign;
    return true;
}

} /* namespace */

lab07::PinResult classify_pin_http(uint64_t operation_id, uint32_t generation, const char *session_id,
                                   const char *user_id, int perform_ok, int http_status, const char *body,
                                   int body_len)
{
    lab07::PinResult result;
    char outcome[16];
    int side = 0;
    int outcome_at;
    int side_at;
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    result.generation = generation;
    if (session_id != 0) {
        strncpy(result.session_id, session_id, sizeof(result.session_id) - 1);
    }
    if (user_id != 0) {
        strncpy(result.user_id, user_id, sizeof(result.user_id) - 1);
    }
    result.outcome = lab07::kPinOutcomeTransport;
    if (!perform_ok || body == 0 || body_len <= 0) {
        result.http_status = perform_ok ? http_status : 0;
        result.parse_ok = 0;
        return result;
    }
    result.http_status = http_status;
    outcome_at = find_key(body, body_len, "\"outcome\"");
    if (outcome_at < 0 || !read_string(body, body_len, outcome_at, outcome, (int)sizeof(outcome))) {
        result.parse_ok = 0;
        return result;
    }
    result.parse_ok = 1;
    side_at = find_key(body, body_len, "\"side_effects\"");
    if (side_at >= 0 && read_int(body, body_len, side_at, &side)) {
        result.side_effects = side;
        result.side_effects_valid = 1;
    }
    if (http_status != 200) {
        result.outcome = lab07::kPinOutcomeTransport;
        return result;
    }
    if (strcmp(outcome, "ok") == 0) {
        result.outcome = lab07::kPinOutcomeSuccess;
        return result;
    }
    if (strcmp(outcome, "wrong") == 0) {
        result.outcome = lab07::kPinOutcomeWrong;
        return result;
    }
    if (strcmp(outcome, "unknown") == 0) {
        result.outcome = lab07::kPinOutcomeUnknownUser;
        return result;
    }
    result.outcome = lab07::kPinOutcomeTransport;
    return result;
}

lab07::PinResult deliver_classified_pin(lab07::SessionWriter *session, uint64_t operation_id,
                                        uint32_t generation, const char *session_id, const char *user_id,
                                        int perform_ok, int http_status, const char *body, int body_len,
                                        int64_t now_ms)
{
    lab07::PinResult result = classify_pin_http(operation_id, generation, session_id, user_id, perform_ok,
                                                http_status, body, body_len);
    session->deliver_pin_completion(result, now_ms);
    return result;
}

} /* namespace lab08 */
