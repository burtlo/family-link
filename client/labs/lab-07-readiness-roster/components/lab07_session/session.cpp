#include "catalog.hpp"
#include "roster_http.hpp"
#include "session.hpp"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace lab07
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

const char *scenario_class(const char *scenario)
{
    static const char *kDecided[] = {
        "R-wifi-absent", "R-invalid-200", "R-empty", "R-eight", "R-retry",
        "R-retry-fail", "R-idle", "R-stale-beat", "R-mute", "R-timeout-ui",
        "K-ok", "K-wrong", "K-five", "K-switch-user", "K-timeouts",
        "K-lost", "K-mute-local", "K-boot", "K-idle",
        "I-mute", "I-signout", "I-switch", "I-push", "I-expire-client"};
    int i;
    if (text_eq(scenario, "R-wire") || text_eq(scenario, "R-stale-clock") ||
        text_eq(scenario, "K-concurrent") || text_eq(scenario, "K-mismatch") ||
        text_eq(scenario, "K-mute-server") || text_eq(scenario, "I-confused") ||
        text_eq(scenario, "I-revoke-branches") || text_eq(scenario, "I-expire-http")) {
        return "exploratory";
    }
    for (i = 0; i < 24; ++i) {
        if (text_eq(scenario, kDecided[i])) {
            return "decided";
        }
    }
    return "exploratory";
}

RosterResult users_from(const char *const *ids, int count, int valid)
{
    RosterResult result;
    memset(&result, 0, sizeof(result));
    result.http_status = 200;
    result.valid = valid;
    result.parse_ok = 1;
    fill_users(&result, ids, count);
    return result;
}

bool is_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool fixture_json_ok(const char *body, int body_len)
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
            if (brace < 0) {
                return false;
            }
            if (brace == 0 && bracket == 0) {
                i += 1;
                break;
            }
        } else if (c == '[') {
            bracket += 1;
        } else if (c == ']') {
            bracket -= 1;
            if (bracket < 0) {
                return false;
            }
        }
    }
    if (in_string || escape || brace != 0 || bracket != 0) {
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
        bool match = true;
        for (j = 0; j < key_len; ++j) {
            if (body[i + j] != key[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return i + key_len;
        }
    }
    return -1;
}

bool read_json_int_after(const char *body, int len, int pos, int *value)
{
    int number = 0;
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
    if (pos >= len || body[pos] < '0' || body[pos] > '9') {
        return false;
    }
    while (pos < len && body[pos] >= '0' && body[pos] <= '9') {
        number = number * 10 + (body[pos] - '0');
        pos += 1;
    }
    *value = number;
    return true;
}

int read_json_string_after(const char *body, int len, int pos, char *dst, int cap)
{
    int n = 0;
    while (pos < len && is_ws(body[pos])) {
        pos += 1;
    }
    if (pos >= len || body[pos] != ':') {
        return -1;
    }
    pos += 1;
    while (pos < len && is_ws(body[pos])) {
        pos += 1;
    }
    if (pos >= len || body[pos] != '"') {
        return -1;
    }
    pos += 1;
    while (pos < len && body[pos] != '"') {
        if (body[pos] == '\\' || n >= cap - 1) {
            return -1;
        }
        dst[n] = body[pos];
        n += 1;
        pos += 1;
    }
    if (pos >= len || body[pos] != '"') {
        return -1;
    }
    dst[n] = 0;
    return pos + 1;
}

} /* namespace */

void set_user(UserId *user, const char *id)
{
    copy_text(user->id, kMaxId, id);
}

void fill_users(RosterResult *result, const char *const *ids, int count)
{
    int i;
    if (count > kMaxUsers) {
        count = kMaxUsers;
    }
    if (count < 0) {
        count = 0;
    }
    result->user_count = count;
    for (i = 0; i < kMaxUsers; ++i) {
        result->users[i].id[0] = 0;
    }
    for (i = 0; i < count; ++i) {
        set_user(&result->users[i], ids[i]);
    }
}

SessionWriter::SessionWriter()
{
    event_count_ = 0;
    next_operation_id_ = 1;
    overflow_operation_id_ = 0;
    overflow_terminals_ = 0;
    rejected_results_ = 0;
    copy_text(session_id_, 16, "s-boot");
    reset_runtime();
}

void SessionWriter::reset_runtime()
{
    stage_ = kStageConnecting;
    wifi_up_ = false;
    now_ms_ = 0;
    last_user_activity_ms_ = 0;
    last_roster_change_ms_ = 0;
    next_roster_due_ms_ = 0;
    next_heartbeat_due_ms_ = 0;
    roster_timer_armed_ = false;
    heartbeat_armed_ = false;
    roster_inflight_ = false;
    roster_periodic_ = false;
    roster_hold_ = false;
    roster_operation_id_ = 0;
    roster_attempt_ = 0;
    roster_deadline_ms_ = 0;
    stage_at_roster_start_ = kStageConnecting;
    roster_touch_ = 0;
    health_unacked_ = false;
    health_operation_id_ = 0;
    missed_beats_ = 0;
    stale_ = 0;
    user_count_ = 0;
    selectable_cards_ = 0;
    focus_id_[0] = 0;
    focus_stable_ = 0;
    valid_ = 0;
    generation_ = 0;
    queue_count_ = 0;
    armed_scenario_[0] = 0;
    trigger_count_ = 0;
    memset(users_, 0, sizeof(users_));
    memset(queue_, 0, sizeof(queue_));
    reset_pin();
    reset_scoped();
}

void SessionWriter::apply_stage(int stage, const char *trigger)
{
    if (trigger_count_ < 8 && trigger != 0) {
        copy_text(triggers_[trigger_count_], 24, trigger);
        trigger_count_ += 1;
    }
    if (stage_ == stage) {
        return;
    }
    stage_ = stage;
    generation_ += 1;
    last_update_level_ = 2;
}

void SessionWriter::fill_snapshot(TerminalRecord *record) const
{
    memset(record, 0, sizeof(*record));
    copy_text(record->stage, 24, stage_token(stage_));
    record->user_count = user_count_;
    record->selectable_cards = selectable_cards_;
    record->stale = stale_;
    record->missed_beats = missed_beats_;
    record->inflight_requests = roster_inflight_ ? 1 : 0;
    record->touch_during_wait = touch_during_wait();
    record->focus_stable = focus_stable_;
    record->valid = valid_;
    record->roster_period_ms = roster_period_ms();
    record->heartbeat_period_ms = heartbeat_period_ms();
    copy_text(record->focus_id, kMaxId, focus_id_[0] ? focus_id_ : "none");
    copy_text(record->user_id, kMaxId, focus_id_[0] ? focus_id_ : "none");
    copy_text(record->session_id, 16, session_id_);
    record->monotonic_us = now_ms_ * 1000;
    record->generation = generation_;
    record->session_generation = generation_;
    record->queue_count = queue_count_;
    record->grace_ms = 0;
    record->clear_stale = 0;
    copy_text(record->parsed_keys, 80, "none");
    record->digit_len = pin_digit_len_;
    record->display_power = stage_ == kStageMuted ? 0 : 1;
    record->wrong_count = wrong_count_;
    record->cooldown_remaining_ms = cooldown_remaining_ms();
    record->side_effects = pin_side_effects_;
    record->delay_ms_0 = delay_ms_[0];
    record->delay_ms_1 = delay_ms_[1];
    record->delay_ms_2 = delay_ms_[2];
    record->attempts_used = pin_attempt_;
    record->boot_zero = boot_zero_;
    record->boot_partial = boot_partial_;
    record->boot_full = boot_full_;
    record->update_level = last_update_level_;
    record->key_generation = pin_key_generation_;
    copy_text(record->pin_copy, 16, pin_copy_token_[0] ? pin_copy_token_ : "none");
    copy_text(record->reason, 16, pin_reason_[0] ? pin_reason_ : "none");
    copy_text(record->key_fingerprint, 68, pin_fingerprint_[0] ? pin_fingerprint_ : "none");
}

bool SessionWriter::record_terminal(uint64_t operation_id, uint32_t attempt, const char *kind,
                                    const char *terminal)
{
    int i;
    TerminalRecord record;
    if (operation_id == 0 || terminal == 0) {
        return false;
    }
    for (i = 0; i < event_count_; ++i) {
        if (events_[i].operation_id == operation_id && events_[i].terminal_count == 1) {
            return false;
        }
    }
    if (event_count_ >= kMaxEvents) {
        return false;
    }
    fill_snapshot(&record);
    record.operation_id = operation_id;
    record.attempt = attempt;
    record.terminal_count = 1;
    record.stage_unchanged = (stage_ == stage_at_roster_start_) ? 1 : 0;
    copy_text(record.kind, 16, kind);
    copy_text(record.terminal, 16, terminal);
    if (strcmp(kind, "pin") == 0 && pin_frozen_user_[0] != 0) {
        copy_text(record.user_id, kMaxId, pin_frozen_user_);
    }
    if (strcmp(kind, "pin") == 0 && pin_scenario_[0] != 0) {
        copy_text(record.scenario, 32, pin_scenario_);
        pin_scenario_[0] = 0;
    } else {
        copy_text(record.scenario, 32, armed_scenario_[0] ? armed_scenario_ : "lab-step");
        armed_scenario_[0] = 0;
    }
    if (scoped_kind_name(kind)) {
        stamp_scoped(&record, kind);
    }
    copy_text(record.cls, 16, scenario_class(record.scenario));
    events_[event_count_] = record;
    event_count_ += 1;
    return true;
}

void SessionWriter::begin_roster(int64_t now_ms, bool periodic, bool hold)
{
    if (roster_inflight_ || stage_ == kStageMuted || !wifi_up_) {
        return;
    }
    now_ms_ = now_ms;
    roster_inflight_ = true;
    roster_periodic_ = periodic;
    roster_hold_ = hold;
    roster_operation_id_ = next_operation_id_;
    next_operation_id_ += 1;
    roster_attempt_ = 1;
    roster_deadline_ms_ = now_ms + kRequestTimeoutMs;
    stage_at_roster_start_ = stage_;
    roster_touch_ = 0;
    roster_timer_armed_ = false;
}

void SessionWriter::begin_health(int64_t now_ms, const char *trigger)
{
    (void)trigger;
    now_ms_ = now_ms;
    if (health_unacked_) {
        missed_beats_ += 1;
        if (missed_beats_ >= kStaleAfterMissedBeats) {
            stale_ = 1;
        }
        record_terminal(health_operation_id_, 1, "health", "failure");
        health_unacked_ = false;
    }
    health_operation_id_ = next_operation_id_;
    next_operation_id_ += 1;
    health_unacked_ = true;
}

void SessionWriter::clear_roster_inflight()
{
    roster_inflight_ = false;
    roster_periodic_ = false;
    roster_hold_ = false;
    roster_attempt_ = 0;
}

void SessionWriter::boot(int64_t now_ms, bool wifi_up)
{
    reset_runtime();
    now_ms_ = now_ms;
    wifi_up_ = wifi_up;
    last_user_activity_ms_ = now_ms;
    last_roster_change_ms_ = now_ms;
    if (!wifi_up) {
        apply_stage(kStageWifiUnavailable, "boot_station_down");
        selectable_cards_ = 0;
    } else {
        apply_stage(kStageConnecting, "boot");
        selectable_cards_ = 0;
        begin_roster(now_ms, false, false);
    }
    begin_health(now_ms, "boot");
    next_heartbeat_due_ms_ = now_ms + kHeartbeatPeriodMs;
    heartbeat_armed_ = true;
}

void SessionWriter::pull_roster_period_forward(int64_t now_ms)
{
    if (!wifi_up_ || stage_ == kStageMuted) {
        return;
    }
    if (stage_ != kStageRoster && stage_ != kStageEmptyHangout) {
        return;
    }
    if (roster_inflight_) {
        return;
    }
    if (!roster_timer_armed_ || next_roster_due_ms_ - now_ms > kActiveRosterPeriodMs) {
        next_roster_due_ms_ = now_ms + kActiveRosterPeriodMs;
        roster_timer_armed_ = true;
    }
}

void SessionWriter::note_user_activity(int64_t now_ms)
{
    now_ms_ = now_ms;
    last_user_activity_ms_ = now_ms;
    pull_roster_period_forward(now_ms);
}

void SessionWriter::set_wifi(bool up, int64_t now_ms)
{
    bool changed = wifi_up_ != up;
    now_ms_ = now_ms;
    wifi_up_ = up;
    if (changed) {
        begin_health(now_ms, "connectivity");
        next_heartbeat_due_ms_ = now_ms + kHeartbeatPeriodMs;
        heartbeat_armed_ = true;
    }
    if (!up) {
        if (roster_inflight_ && stage_ != kStageMuted) {
            record_terminal(roster_operation_id_, roster_attempt_, "roster", "cancelled");
            clear_roster_inflight();
        }
        roster_timer_armed_ = false;
        if (stage_ != kStageMuted && !pin_holds_input()) {
            apply_stage(kStageWifiUnavailable, "station_down");
            selectable_cards_ = 0;
        }
        return;
    }
    if (stage_ != kStageMuted && !pin_holds_input()) {
        apply_stage(kStageConnecting, "station_up");
        selectable_cards_ = 0;
        begin_roster(now_ms, false, false);
    }
}

void SessionWriter::mute(int64_t now_ms)
{
    uint64_t operation_id = roster_operation_id_;
    uint32_t attempt = roster_attempt_;
    uint64_t pin_id = 0;
    uint32_t pin_attempt = 1;
    uint64_t scoped_id = 0;
    uint32_t scoped_gen = 0;
    char scoped_kind[16];
    bool had_roster = roster_inflight_;
    bool had_pin;
    bool had_scoped;
    scoped_kind[0] = 0;
    now_ms_ = now_ms;
    had_pin = take_pin_for_mute(&pin_id, &pin_attempt);
    had_scoped = take_scoped_for_mute(&scoped_id, &scoped_gen, scoped_kind, (int)sizeof(scoped_kind));
    if (had_roster) {
        clear_roster_inflight();
    }
    roster_timer_armed_ = false;
    apply_stage(kStageMuted, "mute");
    selectable_cards_ = 0;
    if (had_pin) {
        copy_text(pin_reason_, 16, "mute");
        record_terminal(pin_id, pin_attempt, "pin", "cancelled");
    }
    if (had_scoped) {
        scoped_applied_ = 0;
        scoped_trace_generation_ = scoped_gen;
        scoped_trace_applied_ = 0;
        scoped_trace_image_bytes_ = 0;
        scoped_trace_cards_ = 0;
        scoped_trace_dupes_ = 0;
        copy_text(scoped_reason_, 16, "mute");
        record_terminal(scoped_id, 1, scoped_kind, "stale");
    }
    if (had_roster) {
        record_terminal(operation_id, attempt, "roster", "stale");
    }
}

void SessionWriter::unmute(int64_t now_ms)
{
    now_ms_ = now_ms;
    if (stage_ != kStageMuted) {
        return;
    }
    if (!wifi_up_) {
        apply_stage(kStageWifiUnavailable, "unmute");
        selectable_cards_ = 0;
        return;
    }
    apply_stage(kStageConnecting, "unmute");
    selectable_cards_ = 0;
    begin_roster(now_ms, false, false);
}

void SessionWriter::begin_hold_probe(int64_t now_ms)
{
    if (roster_inflight_) {
        return;
    }
    begin_roster(now_ms, false, true);
}

void SessionWriter::request_roster(int64_t now_ms)
{
    now_ms_ = now_ms;
    begin_roster(now_ms, false, false);
}

uint64_t SessionWriter::begin_roster_request(int64_t now_ms)
{
    bool was_inflight = roster_inflight_;
    uint64_t previous = roster_operation_id_;
    request_roster(now_ms);
    if (!roster_inflight_ || was_inflight || roster_operation_id_ == previous) {
        return 0;
    }
    return roster_operation_id_;
}

void SessionWriter::note_touch_during_wait()
{
    if (roster_inflight_) {
        roster_touch_ = 1;
    }
    note_user_activity(now_ms_);
}

void SessionWriter::note_transport_failure()
{
    uint64_t operation_id;
    uint32_t attempt;
    if (!roster_inflight_) {
        return;
    }
    if (roster_periodic_ && roster_attempt_ == 1 && !roster_hold_) {
        roster_attempt_ = 2;
        roster_deadline_ms_ = now_ms_ + kRequestTimeoutMs;
        return;
    }
    operation_id = roster_operation_id_;
    attempt = roster_attempt_;
    clear_roster_inflight();
    if (stage_ != kStageMuted && stage_ != kStageWifiUnavailable && !pin_holds_input()) {
        apply_stage(kStageConnecting, "transport_failed");
        selectable_cards_ = 0;
    }
    record_terminal(operation_id, attempt, "roster", "failure");
}

void SessionWriter::ack_health()
{
    if (!health_unacked_) {
        return;
    }
    health_unacked_ = false;
    record_terminal(health_operation_id_, 1, "health", "success");
}

bool SessionWriter::enqueue_roster(const RosterResult &result)
{
    if (queue_count_ >= kRosterResultQueueCapacity) {
        rejected_results_ += 1;
        /* A full queue rejects this completion. Do not mint an id and do not
           apply the body onto the active request. An identified result still
           gets one failure terminal. */
        if (result.operation_id != 0) {
            if (roster_inflight_ && result.operation_id == roster_operation_id_) {
                clear_roster_inflight();
            }
            overflow_operation_id_ = result.operation_id;
            if (record_terminal(result.operation_id, 1, "roster", "failure")) {
                overflow_terminals_ += 1;
            }
        }
        return false;
    }
    /* The id on the result is the submission id. An unidentified result stays
       unidentified, including one queued while a request is in flight. */
    queue_[queue_count_] = result;
    queue_count_ += 1;
    return true;
}

void SessionWriter::remember_users(const RosterResult &result)
{
    int i;
    user_count_ = result.user_count;
    if (user_count_ > kMaxUsers) {
        user_count_ = kMaxUsers;
    }
    if (user_count_ < 0) {
        user_count_ = 0;
    }
    for (i = 0; i < kMaxUsers; ++i) {
        users_[i].id[0] = 0;
    }
    for (i = 0; i < user_count_; ++i) {
        copy_text(users_[i].id, kMaxId, result.users[i].id);
    }
}

bool SessionWriter::same_membership(const RosterResult &result) const
{
    int i;
    int count = result.user_count;
    if (count != user_count_) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (!text_eq(users_[i].id, result.users[i].id)) {
            return false;
        }
    }
    return true;
}

void SessionWriter::preserve_focus(const RosterResult &result)
{
    int i;
    int old_index = -1;
    char previous[kMaxId];
    copy_text(previous, kMaxId, focus_id_);
    if (previous[0] == 0) {
        if (result.user_count > 0) {
            copy_text(focus_id_, kMaxId, result.users[0].id);
        }
        focus_stable_ = 0;
        return;
    }
    for (i = 0; i < result.user_count; ++i) {
        if (text_eq(result.users[i].id, previous)) {
            focus_stable_ = 1;
            return;
        }
    }
    for (i = 0; i < user_count_; ++i) {
        if (text_eq(users_[i].id, previous)) {
            old_index = i;
        }
    }
    focus_stable_ = 0;
    focus_id_[0] = 0;
    if (old_index > 0) {
        for (i = old_index - 1; i >= 0; --i) {
            int j;
            for (j = 0; j < result.user_count; ++j) {
                if (text_eq(result.users[j].id, users_[i].id)) {
                    copy_text(focus_id_, kMaxId, users_[i].id);
                    return;
                }
            }
        }
    }
    if (result.user_count > 0) {
        copy_text(focus_id_, kMaxId, result.users[0].id);
    }
}

void SessionWriter::apply_roster_while_pin(const RosterResult &result)
{
    uint64_t operation_id = result.operation_id;
    uint32_t attempt = roster_attempt_;
    valid_ = result.valid;
    if (!result.parse_ok || result.http_status != 200 || result.valid == 0) {
        clear_roster_inflight();
        record_terminal(operation_id, attempt, "roster", "failure");
        return;
    }
    if (result.user_count > 0) {
        preserve_focus(result);
        remember_users(result);
        selectable_cards_ = user_count_;
    }
    clear_roster_inflight();
    if (stage_ == kStageRoster || stage_ == kStageEmptyHangout) {
        next_roster_due_ms_ = now_ms_ + roster_period_ms();
        roster_timer_armed_ = true;
    }
    record_terminal(operation_id, attempt, "roster", "success");
}

void SessionWriter::apply_roster_body(const RosterResult &result)
{
    bool unchanged;
    uint64_t operation_id = result.operation_id;
    if (pin_holds_input()) {
        apply_roster_while_pin(result);
        return;
    }
    uint32_t attempt = roster_attempt_;
    valid_ = result.valid;
    if (!result.parse_ok || result.http_status != 200 || result.valid == 0) {
        clear_roster_inflight();
        if (result.valid == 0 && result.http_status == 200 && result.parse_ok) {
            if (stage_ != kStageMuted && stage_ != kStageWifiUnavailable) {
                apply_stage(kStageConnecting, "invalid_ready");
            }
            selectable_cards_ = 0;
        } else if (stage_ == kStageConnecting) {
            selectable_cards_ = 0;
        }
        record_terminal(operation_id, attempt, "roster", "failure");
        return;
    }
    unchanged = same_membership(result);
    if (result.user_count <= 0) {
        apply_stage(kStageEmptyHangout, "empty_roster");
        selectable_cards_ = 0;
        user_count_ = 0;
        focus_stable_ = 0;
        focus_id_[0] = 0;
        memset(users_, 0, sizeof(users_));
    } else {
        char previous[kMaxId];
        copy_text(previous, kMaxId, focus_id_);
        preserve_focus(result);
        remember_users(result);
        apply_stage(kStageRoster, "roster_ready");
        selectable_cards_ = user_count_;
        if (previous[0] != 0 && text_eq(previous, focus_id_)) {
            focus_stable_ = 1;
        }
    }
    if (!unchanged) {
        last_roster_change_ms_ = now_ms_;
    }
    clear_roster_inflight();
    if (stage_ == kStageRoster || stage_ == kStageEmptyHangout) {
        next_roster_due_ms_ = now_ms_ + roster_period_ms();
        roster_timer_armed_ = true;
    }
    record_terminal(operation_id, attempt, "roster", "success");
}

void SessionWriter::drain()
{
    int i;
    int count = queue_count_;
    RosterResult pending[kRosterResultQueueCapacity];
    for (i = 0; i < count; ++i) {
        pending[i] = queue_[i];
    }
    queue_count_ = 0;
    for (i = 0; i < count; ++i) {
        RosterResult result = pending[i];
        /* Identity was fixed when the result was queued. Do not assign the
           current or next request id here. */
        if (result.operation_id == 0) {
            continue;
        }
        if (!roster_inflight_ || result.operation_id != roster_operation_id_) {
            record_terminal(result.operation_id, 1, "roster", "stale");
            continue;
        }
        apply_roster_body(result);
    }
}

void SessionWriter::submit_roster(const RosterResult &result, int64_t now_ms)
{
    now_ms_ = now_ms;
    if (!enqueue_roster(result)) {
        return;
    }
    drain();
}

void SessionWriter::deliver_roster_completion(const RosterResult &result, int64_t now_ms)
{
    submit_roster(result, now_ms);
}

RosterResult classify_roster_http(uint64_t operation_id, int perform_ok, int http_status,
                                  const char *body, int body_len)
{
    RosterResult result;
    int valid_at;
    int valid = 0;
    int cursor = 0;
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    if (!perform_ok) {
        result.http_status = 0;
        result.parse_ok = 0;
        result.valid = 0;
        return result;
    }
    result.http_status = http_status;
    if (!fixture_json_ok(body, body_len)) {
        result.parse_ok = 0;
        result.valid = 0;
        return result;
    }
    result.parse_ok = 1;
    valid_at = find_key(body, body_len, "\"valid\"");
    if (valid_at >= 0) {
        if (!read_json_int_after(body, body_len, valid_at, &valid)) {
            result.parse_ok = 0;
            result.valid = 0;
            result.user_count = 0;
            return result;
        }
    }
    result.valid = valid;
    while (cursor < body_len && result.user_count < kMaxUsers) {
        int id_at = find_key(body + cursor, body_len - cursor, "\"id\"");
        char id[kMaxId];
        int next;
        if (id_at < 0) {
            break;
        }
        next = read_json_string_after(body + cursor, body_len - cursor, id_at, id, kMaxId);
        if (next < 0) {
            result.parse_ok = 0;
            result.valid = 0;
            result.user_count = 0;
            return result;
        }
        set_user(&result.users[result.user_count], id);
        result.user_count += 1;
        cursor += next;
    }
    if (cursor < body_len && find_key(body + cursor, body_len - cursor, "\"id\"") >= 0) {
        result.parse_ok = 0;
        result.valid = 0;
        result.user_count = 0;
    }
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

void SessionWriter::on_roster_timeout()
{
    uint64_t operation_id;
    uint32_t attempt;
    bool hold;
    if (!roster_inflight_ || now_ms_ < roster_deadline_ms_) {
        return;
    }
    if (roster_periodic_ && roster_attempt_ == 1 && !roster_hold_) {
        roster_attempt_ = 2;
        roster_deadline_ms_ = now_ms_ + kRequestTimeoutMs;
        return;
    }
    operation_id = roster_operation_id_;
    attempt = roster_attempt_;
    hold = roster_hold_;
    clear_roster_inflight();
    if (!hold && stage_ != kStageMuted && stage_ != kStageWifiUnavailable && !pin_holds_input() &&
        (stage_ == kStageRoster || stage_ == kStageEmptyHangout)) {
        apply_stage(kStageConnecting, "roster_timeout");
        selectable_cards_ = 0;
    }
    record_terminal(operation_id, attempt, "roster", "timed_out");
}

void SessionWriter::on_heartbeat_due()
{
    begin_health(now_ms_, "periodic");
    next_heartbeat_due_ms_ = now_ms_ + kHeartbeatPeriodMs;
}

void SessionWriter::on_roster_due()
{
    if (stage_ != kStageRoster && stage_ != kStageEmptyHangout) {
        roster_timer_armed_ = false;
        return;
    }
    begin_roster(now_ms_, true, false);
}

void SessionWriter::advance_to(int64_t target_ms)
{
    if (target_ms < now_ms_) {
        return;
    }
    while (now_ms_ < target_ms) {
        int64_t timeout_at = roster_inflight_ ? roster_deadline_ms_ : 0x7fffffffffffffffLL;
        int64_t heart_at = heartbeat_armed_ ? next_heartbeat_due_ms_ : 0x7fffffffffffffffLL;
        int64_t roster_at = (roster_timer_armed_ && !roster_inflight_) ? next_roster_due_ms_
                                                                       : 0x7fffffffffffffffLL;
        int64_t pin_at = pin_next_event_ms();
        int64_t next = timeout_at;
        if (heart_at < next) {
            next = heart_at;
        }
        if (roster_at < next) {
            next = roster_at;
        }
        if (pin_at < next) {
            next = pin_at;
        }
        if (next > target_ms) {
            now_ms_ = target_ms;
            note_pin_presented_time();
            break;
        }
        now_ms_ = next;
        if (pin_at == next) {
            on_pin_clock();
        }
        if (timeout_at == next) {
            on_roster_timeout();
        }
        if (heart_at == next) {
            on_heartbeat_due();
        }
        if (roster_at == next && !roster_inflight_) {
            on_roster_due();
        }
        note_pin_presented_time();
    }
}

void SessionWriter::arm_scenario(const char *scenario)
{
    copy_text(armed_scenario_, 32, scenario);
}

void SessionWriter::publish(const char *scenario, const char *terminal)
{
    uint64_t operation_id = next_operation_id_;
    next_operation_id_ += 1;
    arm_scenario(scenario);
    stage_at_roster_start_ = stage_;
    record_terminal(operation_id, 1, "roster", terminal);
}

void SessionWriter::annotate_last(const char *parsed_keys, int grace_ms, int clear_stale)
{
    TerminalRecord *record;
    if (event_count_ <= 0) {
        return;
    }
    record = &events_[event_count_ - 1];
    if (parsed_keys != 0) {
        copy_text(record->parsed_keys, 80, parsed_keys);
    }
    record->grace_ms = grace_ms;
    record->clear_stale = clear_stale;
}

int SessionWriter::stage() const
{
    return stage_;
}
const char *SessionWriter::stage_name() const
{
    return stage_token(stage_);
}
int SessionWriter::user_count() const
{
    return user_count_;
}
const char *SessionWriter::focus_id() const
{
    return focus_id_;
}
const char *SessionWriter::user_at(int index) const
{
    if (index < 0 || index >= user_count_) {
        return "";
    }
    return users_[index].id;
}
int SessionWriter::selectable_cards() const
{
    return selectable_cards_;
}
int SessionWriter::stale() const
{
    return stale_;
}
int SessionWriter::missed_beats() const
{
    return missed_beats_;
}
int SessionWriter::inflight_requests() const
{
    return roster_inflight_ ? 1 : 0;
}
int SessionWriter::touch_during_wait() const
{
    return roster_touch_;
}
int SessionWriter::queue_count() const
{
    return queue_count_;
}
int SessionWriter::overflow_terminals() const
{
    return overflow_terminals_;
}
int SessionWriter::rejected_results() const
{
    return rejected_results_;
}
bool SessionWriter::roster_inflight() const
{
    return roster_inflight_;
}
uint32_t SessionWriter::roster_attempt() const
{
    return roster_attempt_;
}
uint64_t SessionWriter::roster_operation_id() const
{
    return roster_operation_id_;
}
int64_t SessionWriter::now_ms() const
{
    return now_ms_;
}
int64_t SessionWriter::next_roster_due_ms() const
{
    return next_roster_due_ms_;
}
int64_t SessionWriter::next_heartbeat_due_ms() const
{
    return next_heartbeat_due_ms_;
}
int SessionWriter::heartbeat_period_ms() const
{
    return kHeartbeatPeriodMs;
}
bool SessionWriter::has_wrong_pin() const
{
    return pin_phase_ == kPinPhaseWrong;
}
uint32_t SessionWriter::generation() const
{
    return generation_;
}
const char *SessionWriter::session_id() const
{
    return session_id_;
}
int SessionWriter::event_count() const
{
    return event_count_;
}
const TerminalRecord &SessionWriter::event_at(int index) const
{
    return events_[index];
}

int64_t SessionWriter::quiet_anchor_ms() const
{
    if (last_user_activity_ms_ > last_roster_change_ms_) {
        return last_user_activity_ms_;
    }
    return last_roster_change_ms_;
}

int SessionWriter::roster_period_ms() const
{
    if (now_ms_ - quiet_anchor_ms() >= kIdleQuietMs) {
        return kIdleRosterPeriodMs;
    }
    return kActiveRosterPeriodMs;
}

int SessionWriter::terminal_count_for(uint64_t operation_id) const
{
    int i;
    int count = 0;
    for (i = 0; i < event_count_; ++i) {
        if (events_[i].operation_id == operation_id && events_[i].terminal[0] != 0) {
            count += events_[i].terminal_count;
        }
    }
    return count;
}

const char *SessionWriter::terminal_for(uint64_t operation_id) const
{
    int i;
    for (i = 0; i < event_count_; ++i) {
        if (events_[i].operation_id == operation_id && events_[i].terminal_count == 1) {
            return events_[i].terminal;
        }
    }
    return "";
}

int format_trace_line(const TerminalRecord &record, char *buffer, int capacity)
{
    return snprintf(
        buffer, (size_t)capacity,
        "LAB07_TRACE schema=lab07-trace/v1 event=scenario_terminal seq=%" PRIu32 " monotonic_us=%lld "
        "session_id=%s user_id=%s operation_id=%llu generation=%" PRIu32 " scenario=%s class=%s "
        "terminal=%s terminal_count=%d attempt=%" PRIu32 " stage=%s user_count=%d selectable_cards=%d "
        "focus_id=%s focus_stable=%d stale=%d missed_beats=%d inflight_requests=%d "
        "touch_during_wait=%d queue_count=%d roster_period_ms=%d heartbeat_period_ms=%d valid=%d "
        "stage_unchanged=%d parsed_keys=%s grace_ms=%d clear_stale=%d",
        record.generation, (long long)record.monotonic_us, record.session_id, record.user_id,
        (unsigned long long)record.operation_id, record.generation, record.scenario, record.cls,
        record.terminal, record.terminal_count, record.attempt, record.stage, record.user_count,
        record.selectable_cards, record.focus_id, record.focus_stable, record.stale,
        record.missed_beats, record.inflight_requests, record.touch_during_wait, record.queue_count,
        record.roster_period_ms, record.heartbeat_period_ms, record.valid, record.stage_unchanged,
        record.parsed_keys[0] ? record.parsed_keys : "none", record.grace_ms, record.clear_stale);
}

/* Catalog and on-screen controls complete a request they just started.
   They copy that submission id onto the synthetic body. enqueue, drain,
   submit_roster, and deliver_roster_completion do not fill a missing id. */
static void submit_for_inflight(SessionWriter *session, const RosterResult &result, int64_t now_ms)
{
    RosterResult identified = result;
    if (session->roster_inflight() && identified.operation_id == 0) {
        identified.operation_id = session->roster_operation_id();
    }
    session->submit_roster(identified, now_ms);
}

void run_scripted_catalog(SessionWriter *session)
{
    static const char *kEight[] = {"su-01", "su-02", "su-03", "su-04",
                                   "su-05", "su-06", "su-07", "su-08"};
    static const char *kReorder[] = {"su-08", "su-07", "su-06", "su-05",
                                     "su-04", "su-03", "su-02", "su-01"};
    int step;
    RosterResult eight = users_from(kEight, 8, 1);
    RosterResult reorder = users_from(kReorder, 8, 1);
    RosterResult empty = users_from(kEight, 0, 1);
    RosterResult invalid = users_from(kEight, 1, 0);

    session->boot(0, false);
    session->publish("R-wifi-absent", "success");

    session->boot(0, true);
    session->arm_scenario("R-invalid-200");
    submit_for_inflight(session, invalid, 0);

    session->boot(0, true);
    session->arm_scenario("R-empty");
    submit_for_inflight(session, empty, 0);

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->request_roster(0);
    session->arm_scenario("R-eight");
    submit_for_inflight(session, reorder, 0);

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    session->advance_to(kActiveRosterPeriodMs);
    session->note_transport_failure();
    session->arm_scenario("R-retry");
    submit_for_inflight(session, eight, session->now_ms());

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    session->advance_to(kActiveRosterPeriodMs);
    session->note_transport_failure();
    session->arm_scenario("R-retry-fail");
    session->note_transport_failure();

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    for (step = 1; step <= 10; ++step) {
        session->advance_to((int64_t)step * kActiveRosterPeriodMs);
        session->ack_health();
        submit_for_inflight(session, eight, session->now_ms());
    }
    session->publish("R-idle", "success");

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    for (step = 1; step <= 3; ++step) {
        session->advance_to((int64_t)step * kHeartbeatPeriodMs);
        submit_for_inflight(session, eight, session->now_ms());
    }
    session->publish("R-stale-beat", "failure");

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    session->advance_to(kActiveRosterPeriodMs);
    session->arm_scenario("R-mute");
    session->mute(session->now_ms());

    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    session->begin_hold_probe(100);
    session->note_touch_during_wait();
    session->arm_scenario("R-timeout-ui");
    session->advance_to(100 + kRequestTimeoutMs);

    session->publish("R-wire", "success");
    session->annotate_last("run_id|fixture_version|fixture_sha256|server_version", 0, 0);
    session->publish("R-stale-clock", "success");
    session->annotate_last("grace_ms|clear_stale", 0, 0);
}

namespace
{

const char *kControlEight[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
const char *kControlSeven[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07"};
const char *kControlReorder[] = {"su-08", "su-07", "su-06", "su-05", "su-04", "su-03", "su-02", "su-01"};
const char *kControlOne[] = {"su-01"};

void submit_plain(SessionWriter *session, const char *const *ids, int count, int valid)
{
    RosterResult result = users_from(ids, count, valid);
    if (session->stage() == kStageMuted) {
        return;
    }
    if (session->stage() == kStageWifiUnavailable) {
        session->set_wifi(true, session->now_ms());
    }
    if (!session->roster_inflight()) {
        session->request_roster(session->now_ms());
    }
    submit_for_inflight(session, result, session->now_ms());
}

void submit_armed(SessionWriter *session, const char *const *ids, int count, int valid,
                  const char *scenario)
{
    RosterResult result = users_from(ids, count, valid);
    if (session->stage() == kStageMuted) {
        return;
    }
    if (session->stage() == kStageWifiUnavailable) {
        session->set_wifi(true, session->now_ms());
    }
    if (!session->roster_inflight()) {
        session->request_roster(session->now_ms());
    }
    session->arm_scenario(scenario);
    submit_for_inflight(session, result, session->now_ms());
}

void show_roster(SessionWriter *session)
{
    if (session->stage() == kStageMuted) {
        return;
    }
    if (session->stage() != kStageRoster) {
        submit_plain(session, kControlEight, 8, 1);
    }
}

void start_due_roster(SessionWriter *session)
{
    if (session->roster_inflight() || session->stage() == kStageMuted) {
        return;
    }
    session->ack_health();
    if (session->next_roster_due_ms() > session->now_ms()) {
        session->advance_to(session->next_roster_due_ms());
    }
}

void fail_checks(SessionWriter *session, bool twice)
{
    show_roster(session);
    start_due_roster(session);
    if (!session->roster_inflight()) {
        return;
    }
    session->note_transport_failure();
    if (twice) {
        session->arm_scenario("R-retry-fail");
        session->note_transport_failure();
        return;
    }
    session->arm_scenario("R-retry");
    submit_for_inflight(session, users_from(kControlEight, 8, 1), session->now_ms());
}

void apply_mute(SessionWriter *session)
{
    if (session->stage() == kStageMuted) {
        return;
    }
    show_roster(session);
    start_due_roster(session);
    if (!session->roster_inflight() && session->stage() != kStageMuted &&
        session->stage() != kStageWifiUnavailable) {
        session->request_roster(session->now_ms());
    }
    if (session->roster_inflight()) {
        session->arm_scenario("R-mute");
    }
    session->mute(session->now_ms());
}

void apply_idle(SessionWriter *session)
{
    int step;
    RosterResult eight = users_from(kControlEight, 8, 1);
    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    session->ack_health();
    for (step = 1; step <= 10; ++step) {
        session->advance_to((int64_t)step * kActiveRosterPeriodMs);
        session->ack_health();
        submit_for_inflight(session, eight, session->now_ms());
    }
    session->publish("R-idle", "success");
}

void apply_stale_beats(SessionWriter *session)
{
    int step;
    RosterResult eight = users_from(kControlEight, 8, 1);
    session->boot(0, true);
    submit_for_inflight(session, eight, 0);
    for (step = 1; step <= 3; ++step) {
        session->advance_to((int64_t)step * kHeartbeatPeriodMs);
        submit_for_inflight(session, eight, session->now_ms());
    }
    session->publish("R-stale-beat", "failure");
}

int64_t begin_manual_hold(SessionWriter *session)
{
    int64_t now;
    if (session->stage() == kStageMuted) {
        return -1;
    }
    if (session->stage() != kStageRoster) {
        submit_plain(session, kControlEight, 8, 1);
    }
    if (session->stage() != kStageRoster || session->roster_inflight()) {
        return -1;
    }
    now = session->now_ms();
    session->begin_hold_probe(now);
    if (!session->roster_inflight()) {
        return -1;
    }
    return now + kRequestTimeoutMs;
}

} /* namespace */

int64_t apply_manual_control(SessionWriter *session, int action)
{
    if (session == 0) {
        return action == kManualHold ? -1 : 0;
    }
    if (action == kManualWifiDown) {
        /* Name the row only after the stage is wifi_unavailable. */
        session->set_wifi(false, session->now_ms());
        session->publish("R-wifi-absent", "success");
    } else if (action == kManualWifiReturn) {
        session->set_wifi(true, session->now_ms());
        submit_for_inflight(session, users_from(kControlEight, 8, 1), session->now_ms());
    } else if (action == kManualEmpty) {
        submit_armed(session, kControlEight, 0, 1, "R-empty");
    } else if (action == kManualOne) {
        submit_plain(session, kControlOne, 1, 1);
    } else if (action == kManualEight) {
        submit_plain(session, kControlEight, 8, 1);
    } else if (action == kManualAdd) {
        submit_plain(session, kControlSeven, 7, 1);
        submit_plain(session, kControlEight, 8, 1);
    } else if (action == kManualRemove) {
        submit_plain(session, kControlEight, 8, 1);
        submit_plain(session, kControlSeven, 7, 1);
    } else if (action == kManualReorder) {
        submit_plain(session, kControlEight, 8, 1);
        submit_armed(session, kControlReorder, 8, 1, "R-eight");
    } else if (action == kManualFailOnce) {
        fail_checks(session, false);
    } else if (action == kManualFailTwice) {
        fail_checks(session, true);
    } else if (action == kManualInvalid) {
        submit_armed(session, kControlEight, 1, 0, "R-invalid-200");
    } else if (action == kManualHold) {
        return begin_manual_hold(session);
    } else if (action == kManualMute) {
        apply_mute(session);
    } else if (action == kManualUnmute) {
        session->unmute(session->now_ms());
        if (session->roster_inflight()) {
            submit_for_inflight(session, users_from(kControlEight, 8, 1), session->now_ms());
        }
    } else if (action == kManualIdleClock) {
        apply_idle(session);
    } else if (action == kManualStaleBeats) {
        apply_stale_beats(session);
    }
    return 0;
}

void complete_manual_hold(SessionWriter *session, int64_t deadline_ms)
{
    if (session == 0) {
        return;
    }
    /* Ack first so a heartbeat inside this advance cannot take the scenario name. */
    session->ack_health();
    session->arm_scenario("R-timeout-ui");
    session->advance_to(deadline_ms);
}

} /* namespace lab07 */
