#include "fl_product_login_session.hpp"

#include <inttypes.h>
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

bool text_eq(const char *left, const char *right)
{
    if (left == 0 || right == 0) {
        return left == right;
    }
    return strcmp(left, right) == 0;
}

bool user_ok(const char *user)
{
    int n = 0;
    if (user == 0 || user[0] == 0) {
        return false;
    }
    for (; user[n] != 0 && n < kMaxId; ++n) {
        char c = user[n];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return n > 0 && n < kMaxId && user[n] == 0;
}

void assign_session_id(char *dst, uint32_t generation)
{
    snprintf(dst, 16, "s%" PRIu32, generation);
}

} /* namespace */

bool scoped_kind_name(const char *kind)
{
    return text_eq(kind, "inbox") || text_eq(kind, "media") || text_eq(kind, "playback") ||
           text_eq(kind, "profile");
}

void SessionWriter::reset_scoped()
{
    signed_in_ = 0;
    signed_user_[0] = 0;
    scoped_inflight_ = false;
    scoped_operation_id_ = 0;
    scoped_frozen_generation_ = 0;
    scoped_kind_[0] = 0;
    scoped_user_[0] = 0;
    scoped_session_[0] = 0;
    scoped_scenario_[0] = 0;
    scoped_reason_[0] = 0;
    scoped_applied_ = 0;
    scoped_trace_applied_ = 0;
    scoped_trace_image_bytes_ = 0;
    scoped_trace_cards_ = 0;
    scoped_trace_dupes_ = 0;
    scoped_trace_http_ = 0;
    scoped_trace_b_side_ = 0;
    scoped_trace_generation_ = 0;
    scoped_trace_user_[0] = 0;
    scoped_queue_count_ = 0;
    scoped_overflow_terminals_ = 0;
    scoped_rejected_ = 0;
    memset(scoped_queue_, 0, sizeof(scoped_queue_));
}

void SessionWriter::clear_scoped_inflight()
{
    scoped_inflight_ = false;
    scoped_operation_id_ = 0;
    scoped_frozen_generation_ = 0;
    scoped_kind_[0] = 0;
    scoped_user_[0] = 0;
    scoped_session_[0] = 0;
}

void SessionWriter::stamp_scoped(TerminalRecord *record, const char *kind)
{
    (void)kind;
    record->generation = scoped_trace_generation_;
    record->applied = scoped_trace_applied_;
    record->image_bytes = scoped_trace_applied_ ? scoped_trace_image_bytes_ : 0;
    record->card_count = scoped_trace_applied_ ? scoped_trace_cards_ : 0;
    record->duplicate_cards = scoped_trace_applied_ ? scoped_trace_dupes_ : 0;
    record->http_status = scoped_trace_http_;
    record->b_side_effects = scoped_trace_b_side_;
    record->stage_restored = 0;
    if (scoped_scenario_[0] != 0) {
        copy_text(record->scenario, 32, scoped_scenario_);
    }
    if (scoped_trace_user_[0] != 0) {
        copy_text(record->user_id, kMaxId, scoped_trace_user_);
    }
    if (scoped_reason_[0] != 0) {
        copy_text(record->reason, 16, scoped_reason_);
    }
}

bool SessionWriter::take_scoped_for_mute(uint64_t *operation_id, uint32_t *generation, char *kind,
                                         int kind_cap)
{
    if (!scoped_inflight_ || scoped_operation_id_ == 0 ||
        terminal_count_for(scoped_operation_id_) > 0) {
        clear_scoped_inflight();
        return false;
    }
    *operation_id = scoped_operation_id_;
    *generation = scoped_frozen_generation_;
    copy_text(kind, kind_cap, scoped_kind_);
    copy_text(scoped_trace_user_, kMaxId, scoped_user_);
    scoped_trace_generation_ = scoped_frozen_generation_;
    clear_scoped_inflight();
    return true;
}

void SessionWriter::close_scoped(const char *terminal, const char *reason)
{
    uint64_t operation_id = scoped_operation_id_;
    char kind[16];
    if (!scoped_inflight_ || operation_id == 0 || terminal_count_for(operation_id) > 0) {
        clear_scoped_inflight();
        return;
    }
    copy_text(kind, 16, scoped_kind_);
    scoped_trace_generation_ = scoped_frozen_generation_;
    copy_text(scoped_trace_user_, kMaxId, scoped_user_);
    scoped_trace_applied_ = 0;
    scoped_trace_image_bytes_ = 0;
    scoped_trace_cards_ = 0;
    scoped_trace_dupes_ = 0;
    scoped_applied_ = 0;
    copy_text(scoped_reason_, 16, reason);
    clear_scoped_inflight();
    last_update_level_ = 0;
    property_seq_ += 1;
    record_terminal(operation_id, 1, kind, terminal);
}

void SessionWriter::sign_in(const char *user_id, int64_t now_ms)
{
    bool changed;
    uint64_t previous_id = 0;
    uint32_t previous_gen = 0;
    char previous_kind[16];
    char previous_user[kMaxId];
    bool had = false;
    now_ms_ = now_ms;
    previous_kind[0] = 0;
    previous_user[0] = 0;
    if (!user_ok(user_id) || stage_ == kStageMuted) {
        return;
    }
    if (pin_submitted_ && pin_operation_id_ != 0 && terminal_count_for(pin_operation_id_) == 0 &&
        pin_frozen_user_[0] != 0 && !text_eq(pin_frozen_user_, user_id)) {
        cancel_pin(now_ms);
    }
    changed = signed_in_ == 0 || !text_eq(signed_user_, user_id);
    if (changed && scoped_inflight_) {
        had = take_scoped_for_mute(&previous_id, &previous_gen, previous_kind, (int)sizeof(previous_kind));
        copy_text(previous_user, kMaxId, scoped_trace_user_);
    }
    if (!changed) {
        if (stage_ != kStageSignedIn && stage_ != kStageMuted) {
            apply_stage(kStageSignedIn, "sign_in");
            assign_session_id(session_id_, generation_);
        }
        return;
    }
    copy_text(signed_user_, kMaxId, user_id);
    signed_in_ = 1;
    scoped_applied_ = 0;
    if (stage_ != kStageSignedIn) {
        apply_stage(kStageSignedIn, "sign_in");
    } else {
        generation_ += 1;
        last_update_level_ = 0;
        property_seq_ += 1;
    }
    assign_session_id(session_id_, generation_);
    if (had) {
        scoped_trace_generation_ = previous_gen;
        copy_text(scoped_trace_user_, kMaxId, previous_user);
        scoped_trace_applied_ = 0;
        scoped_trace_image_bytes_ = 0;
        scoped_trace_cards_ = 0;
        scoped_trace_dupes_ = 0;
        copy_text(scoped_reason_, 16, "switch");
        record_terminal(previous_id, 1, previous_kind, "stale");
    }
}

void SessionWriter::sign_out(int64_t now_ms)
{
    uint64_t operation_id = 0;
    uint32_t generation = 0;
    char kind[16];
    bool had;
    kind[0] = 0;
    now_ms_ = now_ms;
    had = take_scoped_for_mute(&operation_id, &generation, kind, (int)sizeof(kind));
    if (stage_ != kStageSignedOut) {
        apply_stage(kStageSignedOut, "sign_out");
    }
    signed_in_ = 0;
    signed_user_[0] = 0;
    scoped_applied_ = 0;
    assign_session_id(session_id_, generation_);
    if (!had) {
        return;
    }
    scoped_trace_generation_ = generation;
    scoped_trace_applied_ = 0;
    scoped_trace_image_bytes_ = 0;
    scoped_trace_cards_ = 0;
    scoped_trace_dupes_ = 0;
    copy_text(scoped_reason_, 16, "sign_out");
    record_terminal(operation_id, 1, kind, "cancelled");
}

ScopedTicket SessionWriter::begin_scoped_operation(const char *kind, int64_t now_ms)
{
    ScopedTicket ticket;
    memset(&ticket, 0, sizeof(ticket));
    now_ms_ = now_ms;
    if (signed_in_ == 0 || stage_ != kStageSignedIn || !scoped_kind_name(kind)) {
        return ticket;
    }
    if (scoped_inflight_) {
        cancel_scoped(now_ms);
    }
    scoped_operation_id_ = next_operation_id_;
    next_operation_id_ += 1;
    scoped_frozen_generation_ = generation_;
    scoped_inflight_ = true;
    scoped_applied_ = 0;
    copy_text(scoped_kind_, 16, kind);
    copy_text(scoped_user_, kMaxId, signed_user_);
    copy_text(scoped_session_, 16, session_id_);
    last_update_level_ = 0;
    property_seq_ += 1;
    ticket.operation_id = scoped_operation_id_;
    ticket.generation = scoped_frozen_generation_;
    copy_text(ticket.user_id, kMaxId, scoped_user_);
    copy_text(ticket.session_id, 16, scoped_session_);
    copy_text(ticket.kind, 16, scoped_kind_);
    return ticket;
}

void SessionWriter::accept_scoped_result(const ScopedResult &result)
{
    bool active;
    bool identity;
    const char *terminal = "failure";
    const char *reason = "rejected";
    if (result.operation_id == 0) {
        scoped_rejected_ += 1;
        return;
    }
    /* One terminal stays final. A later delivery does not apply again. */
    if (terminal_count_for(result.operation_id) > 0) {
        return;
    }
    scoped_trace_generation_ = result.generation;
    scoped_trace_http_ = result.http_status;
    scoped_trace_applied_ = 0;
    copy_text(scoped_trace_user_, kMaxId, result.user_id);
    active = scoped_inflight_ && result.operation_id == scoped_operation_id_;
    if (!active) {
        copy_text(scoped_reason_, 16, "stale");
        record_terminal(result.operation_id, 1, result.kind, "stale");
        return;
    }
    identity = result.generation != 0 && result.generation == scoped_frozen_generation_ &&
               result.generation == generation_ && text_eq(result.user_id, scoped_user_) &&
               text_eq(result.session_id, scoped_session_) && text_eq(result.kind, scoped_kind_);
    if (!identity || stage_ == kStageMuted || stage_ == kStageSignedOut) {
        clear_scoped_inflight();
        scoped_applied_ = 0;
        copy_text(scoped_reason_, 16, "stale");
        record_terminal(result.operation_id, 1, result.kind, "stale");
        return;
    }
    if (result.expired) {
        reason = "expired";
    } else if (result.parse_ok == 1 && result.body_ok == 1 && result.http_status >= 200 &&
               result.http_status < 300) {
        scoped_applied_ = 1;
        scoped_trace_applied_ = 1;
        clear_scoped_inflight();
        last_update_level_ = text_eq(result.kind, "inbox") ? 1 : 0;
        property_seq_ += 1;
        copy_text(scoped_reason_, 16, "ok");
        record_terminal(result.operation_id, 1, result.kind, "success");
        return;
    }
    clear_scoped_inflight();
    scoped_applied_ = 0;
    copy_text(scoped_reason_, 16, reason);
    record_terminal(result.operation_id, 1, result.kind, terminal);
}

void SessionWriter::drain_scoped()
{
    int i;
    int count = scoped_queue_count_;
    ScopedResult pending[kScopedResultQueueCapacity];
    for (i = 0; i < count; ++i) {
        pending[i] = scoped_queue_[i];
    }
    scoped_queue_count_ = 0;
    for (i = 0; i < count; ++i) {
        accept_scoped_result(pending[i]);
    }
}

bool SessionWriter::enqueue_scoped(const ScopedResult &result)
{
    const char *kind = result.kind;
    if (scoped_queue_count_ >= kScopedResultQueueCapacity) {
        scoped_rejected_ += 1;
        if (result.operation_id != 0 && terminal_count_for(result.operation_id) == 0) {
            scoped_trace_generation_ = result.generation;
            scoped_trace_http_ = result.http_status;
            scoped_trace_applied_ = 0;
            scoped_trace_image_bytes_ = 0;
            scoped_trace_cards_ = 0;
            scoped_trace_dupes_ = 0;
            copy_text(scoped_trace_user_, kMaxId, result.user_id);
            copy_text(scoped_reason_, 16, "overflow");
            if (scoped_inflight_ && result.operation_id == scoped_operation_id_) {
                scoped_applied_ = 0;
                clear_scoped_inflight();
            }
            if (!scoped_kind_name(kind)) {
                kind = "inbox";
            }
            if (record_terminal(result.operation_id, 1, kind, "failure")) {
                scoped_overflow_terminals_ += 1;
            }
        }
        return false;
    }
    scoped_queue_[scoped_queue_count_] = result;
    scoped_queue_count_ += 1;
    return true;
}

int SessionWriter::deliver_scoped_completion(const ScopedResult &result, int64_t now_ms)
{
    int before = 0;
    int i;
    now_ms_ = now_ms;
    if (result.operation_id != 0) {
        before = terminal_count_for(result.operation_id);
    }
    enqueue_scoped(result);
    if (scoped_queue_count_ > 0) {
        drain_scoped();
    }
    if (result.operation_id == 0 || terminal_count_for(result.operation_id) <= before) {
        return 0;
    }
    for (i = event_count_ - 1; i >= 0; --i) {
        const TerminalRecord &event = events_[i];
        if (event.operation_id != result.operation_id || event.applied != 1) {
            continue;
        }
        if (strcmp(event.terminal, "success") != 0) {
            continue;
        }
        if (event.generation != result.generation || event.session_generation != result.generation) {
            continue;
        }
        if (!text_eq(event.user_id, result.user_id)) {
            continue;
        }
        return 1;
    }
    return 0;
}

void SessionWriter::cancel_scoped(int64_t now_ms)
{
    now_ms_ = now_ms;
    close_scoped("cancelled", "cancelled");
}

void SessionWriter::arm_scoped_scenario(const char *scenario)
{
    copy_text(scoped_scenario_, 32, scenario);
}

void SessionWriter::note_scoped_payload(int image_bytes, int card_count, int duplicate_cards, int http_status)
{
    scoped_trace_image_bytes_ = image_bytes < 0 ? 0 : image_bytes;
    scoped_trace_cards_ = card_count < 0 ? 0 : card_count;
    scoped_trace_dupes_ = duplicate_cards < 0 ? 0 : duplicate_cards;
    scoped_trace_http_ = http_status;
}

void SessionWriter::note_scoped_observation(int b_side_effects)
{
    scoped_trace_b_side_ = b_side_effects;
}

int SessionWriter::signed_in() const
{
    return signed_in_;
}
const char *SessionWriter::signed_user() const
{
    return signed_user_;
}
int SessionWriter::scoped_applied() const
{
    return scoped_applied_;
}
int SessionWriter::scoped_inflight() const
{
    return scoped_inflight_ ? 1 : 0;
}
uint64_t SessionWriter::scoped_operation_id() const
{
    return scoped_operation_id_;
}
uint32_t SessionWriter::scoped_generation() const
{
    return scoped_frozen_generation_;
}
int SessionWriter::scoped_queue_count() const
{
    return scoped_queue_count_;
}
int SessionWriter::scoped_overflow_terminals() const
{
    return scoped_overflow_terminals_;
}

int format_scoped_trace_line(const TerminalRecord &record, char *buffer, int capacity)
{
    return snprintf(
        buffer, (size_t)capacity,
        "LAB09_TRACE schema=lab09-trace/v1 event=scenario_terminal seq=%" PRIu32 " monotonic_us=%lld "
        "session_id=%s user_id=%s operation_id=%llu generation=%" PRIu32 " session_generation=%" PRIu32 " "
        "scenario=%s class=%s kind=%s terminal=%s terminal_count=%d stage=%s applied=%d image_bytes=%d "
        "card_count=%d duplicate_cards=%d display_power=%d http_status=%d b_side_effects=%d "
        "stage_restored=%d reason=%s update_level=%d",
        record.generation, (long long)record.monotonic_us, record.session_id, record.user_id,
        (unsigned long long)record.operation_id, record.generation, record.session_generation,
        record.scenario, record.cls, record.kind, record.terminal, record.terminal_count, record.stage,
        record.applied, record.image_bytes, record.card_count, record.duplicate_cards, record.display_power,
        record.http_status, record.b_side_effects, record.stage_restored,
        record.reason[0] ? record.reason : "none", record.update_level);
}

int format_scoped_observation(const char *scenario, const char *mode, int http_status, int b_side_effects,
                              int revoked, char *buffer, int capacity)
{
    const char *safe_mode = mode != 0 && mode[0] != 0 ? mode : "none";
    const char *safe_scenario = scenario != 0 ? scenario : "none";
    return snprintf(
        buffer, (size_t)capacity,
        "LAB09_TRACE schema=lab09-trace/v1 event=observation scenario=%s class=exploratory "
        "terminal=recorded mode=%s http_status=%d b_side_effects=%d revoked=%d",
        safe_scenario, safe_mode, http_status, b_side_effects, revoked);
}

} /* namespace fl_product_login */
