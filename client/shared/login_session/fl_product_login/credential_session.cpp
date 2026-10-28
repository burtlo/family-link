#include "fl_product_login_p1_wire.hpp"
#include "fl_product_login_session.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_login
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

bool token_chars_ok(const char *token)
{
    int n = 0;
    if (token == 0) {
        return false;
    }
    for (; token[n] != 0 && n < kSessionTokenCap; ++n) {
        char c = token[n];
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                  c == '_';
        if (!ok) {
            return false;
        }
    }
    return n == 43;
}

} /* namespace */

void SessionWriter::reset_credential()
{
    credential_present_ = false;
    credential_expired_ = false;
    credential_token_[0] = 0;
    credential_user_[0] = 0;
    credential_pin_operation_id_ = 0;
    credential_bound_ms_ = 0;
    refresh_timer_armed_ = false;
    next_refresh_due_ms_ = 0;
    refresh_inflight_ = false;
    refresh_operation_id_ = 0;
    invalidate_armed_ = false;
    invalidate_attempts_sent_ = 0;
    next_invalidate_due_ms_ = 0;
    invalidate_operation_text_[0] = 0;
    invalidate_session_token_[0] = 0;
    invalidate_pin_operation_id_ = 0;
    health_ack_count_ = 0;
    health_server_time_[0] = 0;
}

void SessionWriter::issue_credential(const char *user_id, const char *token, uint64_t pin_operation_id,
                                     int64_t now_ms)
{
    now_ms_ = now_ms;
    if (user_id == 0 || token == 0 || !token_chars_ok(token)) {
        return;
    }
    credential_present_ = true;
    credential_expired_ = false;
    copy_text(credential_token_, kSessionTokenCap, token);
    copy_text(credential_user_, kMaxId, user_id);
    credential_pin_operation_id_ = pin_operation_id;
    credential_bound_ms_ = now_ms + kSessionCredentialBoundMs;
    refresh_timer_armed_ = true;
    next_refresh_due_ms_ = now_ms + kSessionRefreshPeriodMs;
    refresh_inflight_ = false;
    refresh_operation_id_ = 0;
    last_update_level_ = 0;
    property_seq_ += 1;
}

void SessionWriter::clear_credential(int64_t now_ms)
{
    now_ms_ = now_ms;
    credential_present_ = false;
    credential_expired_ = false;
    credential_token_[0] = 0;
    credential_user_[0] = 0;
    credential_pin_operation_id_ = 0;
    credential_bound_ms_ = 0;
    refresh_timer_armed_ = false;
    next_refresh_due_ms_ = 0;
    refresh_inflight_ = false;
    refresh_operation_id_ = 0;
    signed_in_ = 0;
    signed_user_[0] = 0;
    scoped_applied_ = 0;
    last_update_level_ = 0;
    property_seq_ += 1;
}

void SessionWriter::arm_invalidate(uint64_t pin_operation_id, int64_t now_ms)
{
    char operation_text[40];
    if (pin_operation_id == 0) {
        return;
    }
    format_p1_operation_id(pin_operation_id, operation_text, (int)sizeof(operation_text));
    if (operation_text[0] == 0) {
        return;
    }
    invalidate_armed_ = true;
    invalidate_attempts_sent_ = 0;
    invalidate_pin_operation_id_ = pin_operation_id;
    next_invalidate_due_ms_ = now_ms + kInvalidateRetryDelay0Ms;
    copy_text(invalidate_operation_text_, (int)sizeof(invalidate_operation_text_), operation_text);
    if (credential_token_[0] != 0) {
        copy_text(invalidate_session_token_, kSessionTokenCap, credential_token_);
    } else {
        invalidate_session_token_[0] = 0;
    }
}

uint64_t SessionWriter::begin_refresh_request(int64_t now_ms)
{
    now_ms_ = now_ms;
    if (!credential_present_ || credential_expired_ || stage_ != kStageSignedIn || refresh_inflight_ ||
        signed_in_ == 0) {
        return 0;
    }
    refresh_operation_id_ = next_operation_id_;
    next_operation_id_ += 1;
    refresh_inflight_ = true;
    return refresh_operation_id_;
}

void SessionWriter::deliver_refresh_completion(const RefreshResult &result, int64_t now_ms)
{
    bool success;
    now_ms_ = now_ms;
    if (!refresh_inflight_ || result.operation_id != refresh_operation_id_) {
        return;
    }
    refresh_inflight_ = false;
    refresh_operation_id_ = 0;
    success = result.perform_ok == 1 && result.http_status == 200 && result.parse_ok == 1;
    if (success && credential_present_ && stage_ == kStageSignedIn) {
        credential_bound_ms_ = now_ms + kSessionCredentialBoundMs;
        credential_expired_ = false;
        next_refresh_due_ms_ = now_ms + kSessionRefreshPeriodMs;
        refresh_timer_armed_ = true;
        record_terminal(result.operation_id, 1, "refresh", "success");
        return;
    }
    if (result.operation_id != 0) {
        record_terminal(result.operation_id, 1, "refresh", "failure");
    }
}

InvalidateTicket SessionWriter::take_invalidate_job(int64_t now_ms)
{
    InvalidateTicket ticket;
    memset(&ticket, 0, sizeof(ticket));
    now_ms_ = now_ms;
    if (!invalidate_armed_ || invalidate_attempts_sent_ >= 3 || now_ms < next_invalidate_due_ms_) {
        return ticket;
    }
    ticket.active = 1;
    ticket.attempt = invalidate_attempts_sent_ + 1;
    ticket.pin_operation_id = invalidate_pin_operation_id_;
    copy_text(ticket.operation_id, (int)sizeof(ticket.operation_id), invalidate_operation_text_);
    copy_text(ticket.session_token, kSessionTokenCap, invalidate_session_token_);
    return ticket;
}

void SessionWriter::deliver_invalidate_completion(uint64_t operation_id, int attempt, int http_status,
                                                  int perform_ok, int64_t now_ms)
{
    int delay_ms;
    now_ms_ = now_ms;
    if (!invalidate_armed_ || attempt != invalidate_attempts_sent_ + 1) {
        return;
    }
    invalidate_attempts_sent_ = attempt;
    if (perform_ok == 1 && http_status == 200) {
        invalidate_armed_ = false;
        if (operation_id != 0) {
            record_terminal(operation_id, attempt, "invalidate", "success");
        }
        return;
    }
    if (operation_id != 0) {
        record_terminal(operation_id, attempt, "invalidate", "failure");
    }
    if (invalidate_attempts_sent_ >= 3) {
        invalidate_armed_ = false;
        return;
    }
    if (invalidate_attempts_sent_ == 1) {
        delay_ms = kInvalidateRetryDelay1Ms;
    } else {
        delay_ms = kInvalidateRetryDelay2Ms;
    }
    next_invalidate_due_ms_ = now_ms + delay_ms;
}

void SessionWriter::deliver_health_completion(int perform_ok, int http_status, const char *body, int body_len,
                                              int64_t now_ms)
{
    now_ms_ = now_ms;
    if (classify_health_ack_http(perform_ok, http_status, body, body_len) != 1) {
        return;
    }
    health_ack_count_ += 1;
    if (parse_health_server_time(body, body_len, health_server_time_, (int)sizeof(health_server_time_)) != 1) {
        health_server_time_[0] = 0;
    }
    ack_health();
}

int SessionWriter::health_ack_count() const
{
    return health_ack_count_;
}

const char *SessionWriter::health_server_time() const
{
    return health_server_time_;
}

int64_t SessionWriter::credential_next_event_ms() const
{
    int64_t next = 0x7fffffffffffffffLL;
    if (refresh_timer_armed_ && !refresh_inflight_ && credential_present_ && !credential_expired_ &&
        stage_ == kStageSignedIn) {
        next = next_refresh_due_ms_;
    }
    if (invalidate_armed_ && invalidate_attempts_sent_ < 3 && next_invalidate_due_ms_ < next) {
        next = next_invalidate_due_ms_;
    }
    if (credential_present_ && !credential_expired_ && credential_bound_ms_ < next) {
        next = credential_bound_ms_;
    }
    return next;
}

void SessionWriter::on_credential_clock()
{
    if (credential_present_ && !credential_expired_ && now_ms_ >= credential_bound_ms_) {
        credential_expired_ = true;
        credential_token_[0] = 0;
        refresh_timer_armed_ = false;
        refresh_inflight_ = false;
        last_update_level_ = 0;
        property_seq_ += 1;
        return;
    }
    if (refresh_timer_armed_ && !refresh_inflight_ && credential_present_ && !credential_expired_ &&
        stage_ == kStageSignedIn && now_ms_ >= next_refresh_due_ms_) {
        begin_refresh_request(now_ms_);
    }
}

int SessionWriter::credential_active() const
{
    return credential_present_ && !credential_expired_ ? 1 : 0;
}
int SessionWriter::credential_expired() const
{
    return credential_expired_ ? 1 : 0;
}
const char *SessionWriter::credential_token() const
{
    return credential_token_;
}
int64_t SessionWriter::credential_bound_ms() const
{
    return credential_bound_ms_;
}
int64_t SessionWriter::next_refresh_due_ms() const
{
    return next_refresh_due_ms_;
}
int SessionWriter::refresh_inflight() const
{
    return refresh_inflight_ ? 1 : 0;
}
uint64_t SessionWriter::refresh_operation_id() const
{
    return refresh_operation_id_;
}
int SessionWriter::invalidate_attempts() const
{
    return invalidate_attempts_sent_;
}

} /* namespace fl_product_login */
