#include "probe.hpp"

#include <stdio.h>
#include <string.h>

namespace lab09
{
namespace
{

void copy_text(char *dst, int cap, const char *src)
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

bool text_eq(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return false;
    }
    return strcmp(left, right) == 0;
}

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

bool json_flag(const char *body, int len, const char *key)
{
    int at = find_key(body, len, key);
    int i;
    if (at < 0) {
        return false;
    }
    i = at;
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

bool looks_like_object(const char *body, int len)
{
    int i = 0;
    if (body == 0 || len <= 0) {
        return false;
    }
    while (i < len && is_ws(body[i])) {
        i += 1;
    }
    return i < len && body[i] == '{';
}

} /* namespace */

Probe::Probe() : image_bytes_(0), image_operation_(0), card_count_(0), duplicate_cards_(0)
{
    memset(image_, 0, sizeof(image_));
    memset(cards_, 0, sizeof(cards_));
    playback_[0] = 0;
    profile_[0] = 0;
}

lab07::SessionWriter &Probe::session()
{
    return session_;
}
const lab07::SessionWriter &Probe::session() const
{
    return session_;
}

void Probe::release_image()
{
    memset(image_, 0, sizeof(image_));
    image_bytes_ = 0;
    image_operation_ = 0;
}

void Probe::clear_cards()
{
    memset(cards_, 0, sizeof(cards_));
    card_count_ = 0;
    duplicate_cards_ = 0;
}

void Probe::sign_in(const char *user_id, int64_t now_ms)
{
    char previous[lab07::kMaxId];
    int was = session_.signed_in();
    copy_text(previous, (int)sizeof(previous), session_.signed_user());
    session_.sign_in(user_id, now_ms);
    if (!was || !text_eq(previous, session_.signed_user())) {
        release_image();
        clear_cards();
        playback_[0] = 0;
        profile_[0] = 0;
    }
}

void Probe::sign_out(int64_t now_ms)
{
    session_.sign_out(now_ms);
    release_image();
    clear_cards();
    playback_[0] = 0;
    profile_[0] = 0;
}

void Probe::mute(int64_t now_ms)
{
    session_.mute(now_ms);
    release_image();
    clear_cards();
    playback_[0] = 0;
    profile_[0] = 0;
}

void Probe::unmute(int64_t now_ms)
{
    session_.unmute(now_ms);
}

void Probe::arm(const char *scenario)
{
    session_.arm_scoped_scenario(scenario);
}

lab07::ScopedTicket Probe::begin(const char *kind, int64_t now_ms)
{
    return session_.begin_scoped_operation(kind, now_ms);
}

int collect_card_ids(const char *body, int body_len, char ids[][kCardIdCap], int capacity, int *raw_count)
{
    int at;
    int stored = 0;
    int raw = 0;
    int i;
    if (raw_count != 0) {
        *raw_count = 0;
    }
    if (!looks_like_object(body, body_len) || capacity <= 0) {
        return 0;
    }
    at = find_key(body, body_len, "\"cards\"");
    if (at < 0) {
        return 0;
    }
    i = at;
    while (i < body_len && body[i] != '[') {
        i += 1;
    }
    if (i >= body_len) {
        return 0;
    }
    i += 1;
    while (i < body_len && body[i] != ']') {
        while (i < body_len && is_ws(body[i])) {
            i += 1;
        }
        if (i < body_len && body[i] == '"') {
            int n = 0;
            char id[kCardIdCap];
            i += 1;
            id[0] = 0;
            while (i < body_len && body[i] != '"') {
                if (n < kCardIdCap - 1) {
                    id[n] = body[i];
                    n += 1;
                }
                i += 1;
            }
            id[n] = 0;
            if (i < body_len && body[i] == '"') {
                i += 1;
            }
            raw += 1;
            if (n > 0 && stored < capacity) {
                copy_text(ids[stored], kCardIdCap, id);
                stored += 1;
            }
        } else if (i < body_len) {
            i += 1;
        }
    }
    if (raw_count != 0) {
        *raw_count = raw;
    }
    return stored;
}

lab07::ScopedResult classify_scoped_http(const lab07::ScopedTicket &ticket, int perform_ok, int http_status,
                                         const char *body, int body_len)
{
    lab07::ScopedResult result;
    char session_state[16];
    memset(&result, 0, sizeof(result));
    result.operation_id = ticket.operation_id;
    result.generation = ticket.generation;
    copy_text(result.user_id, lab07::kMaxId, ticket.user_id);
    copy_text(result.session_id, 16, ticket.session_id);
    copy_text(result.kind, 16, ticket.kind);
    if (!perform_ok) {
        return result;
    }
    result.http_status = http_status;
    if (text_eq(ticket.kind, "media") && body != 0 && body_len == kImageBytes && !looks_like_object(body, body_len)) {
        result.parse_ok = 1;
        result.body_ok = http_status == 200 ? 1 : 0;
        return result;
    }
    if (!looks_like_object(body, body_len)) {
        return result;
    }
    result.parse_ok = 1;
    session_state[0] = 0;
    if (find_key(body, body_len, "\"session\"") >= 0) {
        read_string(body, body_len, find_key(body, body_len, "\"session\""), session_state, (int)sizeof(session_state));
    }
    if (text_eq(session_state, "expired")) {
        result.expired = 1;
        result.body_ok = 0;
        return result;
    }
    result.body_ok = json_flag(body, body_len, "\"ok\"") ? 1 : 0;
    return result;
}

bool sign_in_response_ok(int perform_ok, int http_status, const char *body, int body_len, const char *user_id)
{
    char actor[lab07::kMaxId];
    int at;
    if (!perform_ok || http_status != 200 || !looks_like_object(body, body_len) || user_id == 0) {
        return false;
    }
    if (!json_flag(body, body_len, "\"ok\"")) {
        return false;
    }
    at = find_key(body, body_len, "\"actor\"");
    if (!read_string(body, body_len, at, actor, (int)sizeof(actor))) {
        return false;
    }
    return text_eq(actor, user_id);
}

int Probe::complete(const lab07::ScopedTicket &ticket, int perform_ok, int http_status, const char *body,
                    int body_len, int64_t now_ms)
{
    lab07::ScopedResult result = classify_scoped_http(ticket, perform_ok, http_status, body, body_len);
    char raw_ids[32][kCardIdCap];
    char unique_ids[kPageCap][kCardIdCap];
    int raw_count = 0;
    int stored = 0;
    int unique = 0;
    int i;
    int applied;
    int image_note = 0;
    int terminals_before;
    bool same_session;
    bool historical;
    memset(raw_ids, 0, sizeof(raw_ids));
    memset(unique_ids, 0, sizeof(unique_ids));
    if (text_eq(ticket.kind, "inbox")) {
        stored = collect_card_ids(body, body_len, raw_ids, 32, &raw_count);
        for (i = 0; i < stored; ++i) {
            int j;
            int seen = 0;
            for (j = 0; j < unique; ++j) {
                if (text_eq(unique_ids[j], raw_ids[i])) {
                    seen = 1;
                    break;
                }
            }
            if (!seen && unique < kPageCap) {
                copy_text(unique_ids[unique], kCardIdCap, raw_ids[i]);
                unique += 1;
            }
        }
    }
    if (text_eq(ticket.kind, "media") && result.body_ok == 1 && result.expired == 0) {
        image_note = kImageBytes;
    }
    terminals_before = session_.terminal_count_for(ticket.operation_id);
    session_.note_scoped_payload(image_note, unique, 0, result.http_status);
    applied = session_.deliver_scoped_completion(result, now_ms);
    same_session = session_.signed_in() == 1 && text_eq(session_.signed_user(), ticket.user_id) &&
                   session_.generation() == ticket.generation && session_.stage() == lab07::kStageSignedIn;
    historical = applied != 1 && terminals_before > 0 &&
                 session_.terminal_count_for(ticket.operation_id) == terminals_before &&
                 strcmp(session_.terminal_for(ticket.operation_id), "success") == 0 && same_session;
    if (applied != 1 || !same_session) {
        if (terminals_before == 0 && (image_operation_ == 0 || image_operation_ == ticket.operation_id)) {
            release_image();
        }
        return historical ? 1 : 0;
    }
    if (text_eq(ticket.kind, "media") && body != 0 && body_len >= kImageBytes) {
        memcpy(image_, body, kImageBytes);
        image_bytes_ = kImageBytes;
        image_operation_ = ticket.operation_id;
    }
    if (text_eq(ticket.kind, "inbox")) {
        clear_cards();
        for (i = 0; i < unique; ++i) {
            copy_text(cards_[i], kCardIdCap, unique_ids[i]);
        }
        card_count_ = unique;
        duplicate_cards_ = 0;
    }
    if (text_eq(ticket.kind, "playback")) {
        copy_text(playback_, 16, "playback");
    }
    if (text_eq(ticket.kind, "profile")) {
        copy_text(profile_, 16, "profile");
    }
    return 1;
}

void Probe::cleanup()
{
    release_image();
}

int Probe::image_bytes() const
{
    return image_bytes_;
}
int Probe::card_count() const
{
    return card_count_;
}
const char *Probe::card_at(int index) const
{
    if (index < 0 || index >= card_count_) {
        return "";
    }
    return cards_[index];
}
int Probe::duplicate_cards() const
{
    return duplicate_cards_;
}
int Probe::applied_visible() const
{
    return session_.scoped_applied();
}
const char *Probe::active_user() const
{
    return session_.signed_in() ? session_.signed_user() : "";
}
const char *Probe::playback_label() const
{
    return playback_;
}
const char *Probe::profile_label() const
{
    return profile_;
}

int emit_scoped_traces(const lab07::SessionWriter &session, char *buffer, int capacity)
{
    int i;
    int used = 0;
    if (buffer == 0 || capacity <= 0) {
        return 0;
    }
    buffer[0] = 0;
    for (i = 0; i < session.event_count(); ++i) {
        const lab07::TerminalRecord &record = session.event_at(i);
        int wrote;
        if (!lab07::scoped_kind_name(record.kind)) {
            continue;
        }
        if (used > 0 && used < capacity - 1) {
            buffer[used] = '\n';
            used += 1;
            buffer[used] = 0;
        }
        wrote = lab07::format_scoped_trace_line(record, buffer + used, capacity - used);
        if (wrote < 0 || used + wrote >= capacity) {
            return -1;
        }
        used += wrote;
    }
    return used;
}

} /* namespace lab09 */
