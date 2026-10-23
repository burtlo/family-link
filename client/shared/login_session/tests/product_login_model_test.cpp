#include "fl_product_login_catalog.hpp"
#include "fl_product_login_pollers.hpp"
#include "fl_product_login_p1_wire.hpp"
#include "fl_product_login_session.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s:%d %s\n", "model_test.cpp", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

fl_product_login::RosterResult roster(const char *const *ids, int count, int valid, int http_status, int parse_ok)
{
    fl_product_login::RosterResult result;
    memset(&result, 0, sizeof(result));
    result.http_status = http_status;
    result.valid = valid;
    result.parse_ok = parse_ok;
    fl_product_login::fill_users(&result, ids, count);
    return result;
}

const char *kEight[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
const char *kReorder[] = {"su-08", "su-07", "su-06", "su-05", "su-04", "su-03", "su-02", "su-01"};
const char *kRemoved[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07"};
const char *kAdded[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};

void complete_current(fl_product_login::SessionWriter *session, fl_product_login::RosterResult result, int64_t now_ms)
{
    if (session->roster_inflight()) {
        result.operation_id = session->roster_operation_id();
    }
    session->submit_roster(result, now_ms);
}

void establish(fl_product_login::SessionWriter *session)
{
    session->boot(0, true);
    complete_current(session, roster(kEight, 8, 1, 200, 1), 0);
    session->ack_health();
}

int test_cadence()
{
    fl_product_login::SessionWriter session;
    int step;
    g_failures = 0;
    establish(&session);
    CHECK(session.roster_period_ms() == fl_product_login::kActiveRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == fl_product_login::kHeartbeatPeriodMs);
    CHECK(session.next_roster_due_ms() == fl_product_login::kActiveRosterPeriodMs);
    for (step = 1; step <= 10; ++step) {
        int64_t due = (int64_t)step * fl_product_login::kActiveRosterPeriodMs;
        CHECK(session.next_roster_due_ms() == due);
        CHECK(session.roster_period_ms() == fl_product_login::kActiveRosterPeriodMs);
        session.advance_to(due);
        CHECK(session.roster_inflight());
        session.ack_health();
        complete_current(&session, roster(kEight, 8, 1, 200, 1), due);
        CHECK(session.stage() == fl_product_login::kStageRoster);
    }
    CHECK(session.now_ms() == fl_product_login::kIdleQuietMs);
    CHECK(session.roster_period_ms() == fl_product_login::kIdleRosterPeriodMs);
    CHECK(session.next_roster_due_ms() == fl_product_login::kIdleQuietMs + fl_product_login::kIdleRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == fl_product_login::kHeartbeatPeriodMs);
    CHECK(session.next_heartbeat_due_ms() == fl_product_login::kIdleQuietMs + fl_product_login::kHeartbeatPeriodMs);
    session.note_user_activity(fl_product_login::kIdleQuietMs + 100);
    CHECK(session.roster_period_ms() == fl_product_login::kActiveRosterPeriodMs);
    CHECK(session.next_roster_due_ms() == fl_product_login::kIdleQuietMs + 100 + fl_product_login::kActiveRosterPeriodMs);
    session.mute(session.now_ms());
    session.advance_to(session.next_heartbeat_due_ms());
    CHECK(session.stage() == fl_product_login::kStageMuted);
    CHECK(session.heartbeat_period_ms() == fl_product_login::kHeartbeatPeriodMs);
    return g_failures;
}

int test_timeout()
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.begin_hold_probe(100);
    operation_id = session.roster_operation_id();
    session.note_touch_during_wait();
    session.advance_to(100 + fl_product_login::kRequestTimeoutMs - 1);
    CHECK(session.roster_inflight());
    CHECK(session.terminal_count_for(operation_id) == 0);
    session.advance_to(100 + fl_product_login::kRequestTimeoutMs);
    CHECK(session.terminal_for(operation_id) != 0);
    CHECK(strcmp(session.terminal_for(operation_id), "timed_out") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.touch_during_wait() == 1);
    CHECK(session.has_wrong_pin() == false);
    return g_failures;
}

int test_one_retry()
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.advance_to(fl_product_login::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    CHECK(session.roster_attempt() == 1);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    session.note_transport_failure();
    CHECK(session.roster_attempt() == 2);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    CHECK(session.terminal_count_for(operation_id) == 0);
    session.arm_scenario("R-retry");
    complete_current(&session, roster(kEight, 8, 1, 200, 1), session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageRoster);
    CHECK(strcmp(session.focus_id(), "su-01") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    CHECK(session.event_at(session.event_count() - 1).attempt == 2);
    return g_failures;
}

int test_retry_fail()
{
    fl_product_login::SessionWriter session;
    char focus[16];
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.advance_to(fl_product_login::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    session.note_transport_failure();
    session.note_transport_failure();
    CHECK(session.stage() == fl_product_login::kStageConnecting);
    CHECK(session.selectable_cards() == 0);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "failure") == 0);
    return g_failures;
}

int test_invalid_not_ready()
{
    fl_product_login::SessionWriter session;
    fl_product_login::RosterResult invalid = roster(kEight, 1, 0, 200, 1);
    uint64_t operation_id;
    g_failures = 0;
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    session.arm_scenario("R-invalid-200");
    complete_current(&session, invalid, 0);
    CHECK(invalid.valid == 0);
    CHECK(session.selectable_cards() == 0);
    CHECK(session.stage() == fl_product_login::kStageConnecting);
    CHECK(session.terminal_count_for(operation_id) == 1);
    return g_failures;
}

int test_invalid_json_keeps_focus()
{
    fl_product_login::SessionWriter session;
    fl_product_login::RosterResult bad = roster(kEight, 0, 1, 200, 0);
    char focus[16];
    g_failures = 0;
    establish(&session);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.request_roster(10);
    complete_current(&session, bad, 10);
    CHECK(session.terminal_count_for(session.event_at(session.event_count() - 1).operation_id) == 1);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.user_count() == 8);
    session.request_roster(20);
    bad.http_status = 503;
    bad.parse_ok = 1;
    complete_current(&session, bad, 20);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    return g_failures;
}

int test_mute_stale()
{
    fl_product_login::SessionWriter session;
    fl_product_login::RosterResult late = roster(kEight, 8, 1, 200, 1);
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.advance_to(fl_product_login::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    session.mute(session.now_ms());
    CHECK(strcmp(session.terminal_for(operation_id), "stale") == 0);
    CHECK(session.stage() == fl_product_login::kStageMuted);
    late.operation_id = operation_id;
    session.submit_roster(late, session.now_ms());
    CHECK(strcmp(session.terminal_for(operation_id), "stale") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.stage() == fl_product_login::kStageMuted);
    return g_failures;
}

int test_mute_release()
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    const fl_product_login::TerminalRecord *found = 0;
    int i;
    g_failures = 0;
    establish(&session);
    session.advance_to(fl_product_login::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    session.arm_scenario("R-mute");
    session.mute(session.now_ms());
    for (i = 0; i < session.event_count(); ++i) {
        if (session.event_at(i).operation_id == operation_id) {
            found = &session.event_at(i);
        }
    }
    CHECK(found != 0);
    CHECK(strcmp(found->terminal, "stale") == 0);
    CHECK(found->terminal_count == 1);
    CHECK(strcmp(found->stage, "muted") == 0);
    CHECK(found->selectable_cards == 0);
    CHECK(found->inflight_requests == 0);
    session.unmute(session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageConnecting);
    CHECK(session.roster_inflight());
    return g_failures;
}

int test_stage_enum()
{
    const char *tokens[] = {
        fl_product_login::kTokenWifiUnavailable, fl_product_login::kTokenConnecting, fl_product_login::kTokenRoster,
        fl_product_login::kTokenEmptyHangout, fl_product_login::kTokenMuted
    };
    int i;
    int j;
    g_failures = 0;
    CHECK(fl_product_login::kStageWifiUnavailable != fl_product_login::kStageConnecting);
    CHECK(fl_product_login::kStageConnecting != fl_product_login::kStageRoster);
    CHECK(fl_product_login::kStageRoster != fl_product_login::kStageEmptyHangout);
    CHECK(fl_product_login::kStageEmptyHangout != fl_product_login::kStageMuted);
    for (i = 0; i < 5; ++i) {
        for (j = i + 1; j < 5; ++j) {
            CHECK(strcmp(tokens[i], tokens[j]) != 0);
        }
    }
    CHECK(strcmp(fl_product_login::stage_token(fl_product_login::kStageWifiUnavailable), fl_product_login::kTokenWifiUnavailable) == 0);
    CHECK(strcmp(fl_product_login::stage_token(fl_product_login::kStageConnecting), fl_product_login::kTokenConnecting) == 0);
    CHECK(strcmp(fl_product_login::stage_token(fl_product_login::kStageRoster), fl_product_login::kTokenRoster) == 0);
    CHECK(strcmp(fl_product_login::stage_token(fl_product_login::kStageEmptyHangout), fl_product_login::kTokenEmptyHangout) == 0);
    CHECK(strcmp(fl_product_login::stage_token(fl_product_login::kStageMuted), fl_product_login::kTokenMuted) == 0);
    return g_failures;
}

int test_stale_flag()
{
    fl_product_login::SessionWriter session;
    fl_product_login::HealthPoller health(&session);
    int step;
    int stage_before;
    g_failures = 0;
    session.boot(0, true);
    complete_current(&session, roster(kEight, 8, 1, 200, 1), 0);
    stage_before = session.stage();
    for (step = 1; step <= 3; ++step) {
        session.advance_to((int64_t)step * fl_product_login::kHeartbeatPeriodMs);
        CHECK(session.missed_beats() == step);
        complete_current(&session, roster(kEight, 8, 1, 200, 1), session.now_ms());
    }
    CHECK(session.stale() == 1);
    CHECK(session.missed_beats() == 3);
    CHECK(session.stage() == stage_before);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    CHECK(session.user_count() == 8);
    health.submit_ack();
    CHECK(session.stale() == 1);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    return g_failures;
}

int test_wifi_empty_eight_and_overflow()
{
    fl_product_login::SessionWriter session;
    fl_product_login::RosterPoller roster_poller(&session);
    char focus[16];
    int i;
    g_failures = 0;
    session.boot(0, false);
    CHECK(session.stage() == fl_product_login::kStageWifiUnavailable);
    CHECK(session.selectable_cards() == 0);
    session.set_wifi(true, 5);
    CHECK(session.roster_inflight());
    complete_current(&session, roster(kEight, 0, 1, 200, 1), 5);
    CHECK(session.stage() == fl_product_login::kStageEmptyHangout);
    CHECK(session.user_count() == 0);
    session.request_roster(6);
    complete_current(&session, roster(kEight, 8, 1, 200, 1), 6);
    CHECK(session.user_count() == 8);
    CHECK(session.selectable_cards() == 8);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.request_roster(7);
    complete_current(&session, roster(kReorder, 8, 1, 200, 1), 7);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    session.request_roster(8);
    complete_current(&session, roster(kRemoved, 7, 1, 200, 1), 8);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    session.request_roster(9);
    complete_current(&session, roster(kAdded, 8, 1, 200, 1), 9);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.user_count() == 8);
    for (i = 0; i < 8; ++i) {
        CHECK(session.user_at(i)[0] != 0);
    }
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    for (i = 0; i < fl_product_login::kRosterResultQueueCapacity; ++i) {
        fl_product_login::RosterResult extra = roster(kEight, 8, 1, 200, 1);
        extra.operation_id = 1000 + (uint64_t)i;
        CHECK(roster_poller.submit(extra));
    }
    {
        fl_product_login::RosterResult extra = roster(kEight, 8, 1, 200, 1);
        extra.operation_id = 2000;
        CHECK(roster_poller.submit(extra) == false);
    }
    CHECK(session.queue_count() == fl_product_login::kRosterResultQueueCapacity);
    CHECK(session.overflow_terminals() == 1);
    CHECK(session.rejected_results() == 1);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    return g_failures;
}

int test_one_terminal()
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    complete_current(&session, roster(kEight, 8, 1, 200, 1), 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    {
        fl_product_login::RosterResult again = roster(kEight, 8, 1, 200, 1);
        again.operation_id = operation_id;
        session.submit_roster(again, 1);
    }
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    return g_failures;
}

const char *kDecided[] = {
    "R-wifi-absent", "R-invalid-200", "R-empty", "R-eight", "R-retry",
    "R-retry-fail", "R-idle", "R-stale-beat", "R-mute", "R-timeout-ui"
};

bool line_has(const char *line, const char *token)
{
    return strstr(line, token) != 0;
}

void require_tokens(const char *line, const char *const *tokens, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        CHECK(line_has(line, tokens[i]));
    }
    CHECK(line_has(line, "class=decided "));
    CHECK(line_has(line, "terminal_count=1 "));
    CHECK(line_has(line, "event=scenario_terminal "));
    CHECK(!line_has(line, "wrong_pin"));
}

int test_manual_traces()
{
    fl_product_login::SessionWriter session;
    int64_t deadline;
    int counts[10];
    int wire = 0;
    int stale_clock = 0;
    int i;
    g_failures = 0;
    memset(counts, 0, sizeof(counts));
    session.boot(0, true);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualWifiDown);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualWifiReturn);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualEmpty);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualOne);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualEight);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualAdd);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualRemove);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualReorder);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualFailOnce);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualFailTwice);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualInvalid);
    deadline = fl_product_login::apply_manual_control(&session, fl_product_login::kManualHold);
    CHECK(deadline == session.now_ms() + fl_product_login::kRequestTimeoutMs);
    session.note_touch_during_wait();
    fl_product_login::complete_manual_hold(&session, deadline);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualMute);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualUnmute);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualIdleClock);
    fl_product_login::apply_manual_control(&session, fl_product_login::kManualStaleBeats);
    CHECK(session.event_count() > 0);
    CHECK(session.event_count() < fl_product_login::kMaxEvents);
    for (i = 0; i < session.event_count(); ++i) {
        char line[1024];
        char token[64];
        int id;
        const fl_product_login::TerminalRecord &record = session.event_at(i);
        fl_product_login::format_trace_line(record, line, (int)sizeof(line));
        puts(line);
        CHECK(!line_has(line, "wrong_pin"));
        if (line_has(line, "scenario=R-wire ")) {
            wire += 1;
        }
        if (line_has(line, "scenario=R-stale-clock ")) {
            stale_clock += 1;
        }
        for (id = 0; id < 10; ++id) {
            snprintf(token, sizeof(token), "scenario=%s ", kDecided[id]);
            if (!line_has(line, token)) {
                continue;
            }
            counts[id] += 1;
            if (strcmp(kDecided[id], "R-wifi-absent") == 0) {
                const char *tokens[] = {"stage=wifi_unavailable ", "selectable_cards=0 "};
                require_tokens(line, tokens, 2);
            } else if (strcmp(kDecided[id], "R-invalid-200") == 0) {
                const char *tokens[] = {"valid=0 ", "selectable_cards=0 ", "stage=connecting "};
                require_tokens(line, tokens, 3);
            } else if (strcmp(kDecided[id], "R-empty") == 0) {
                const char *tokens[] = {"stage=empty_hangout "};
                require_tokens(line, tokens, 1);
            } else if (strcmp(kDecided[id], "R-eight") == 0) {
                const char *tokens[] = {"user_count=8 ", "focus_stable=1 "};
                require_tokens(line, tokens, 2);
            } else if (strcmp(kDecided[id], "R-retry") == 0) {
                const char *tokens[] = {"attempt=2 ", "stage=roster ", "stage_unchanged=1 "};
                require_tokens(line, tokens, 3);
            } else if (strcmp(kDecided[id], "R-retry-fail") == 0) {
                const char *tokens[] = {"stage=connecting "};
                require_tokens(line, tokens, 1);
            } else if (strcmp(kDecided[id], "R-idle") == 0) {
                const char *tokens[] = {"roster_period_ms=300000 ", "heartbeat_period_ms=60000 "};
                require_tokens(line, tokens, 2);
            } else if (strcmp(kDecided[id], "R-stale-beat") == 0) {
                const char *tokens[] = {" stale=1 ", "stage_unchanged=1 ", "missed_beats=3 "};
                require_tokens(line, tokens, 3);
            } else if (strcmp(kDecided[id], "R-mute") == 0) {
                const char *tokens[] = {"terminal=stale ", "stage=muted ", "selectable_cards=0 ",
                                        "inflight_requests=0 "};
                require_tokens(line, tokens, 4);
            } else if (strcmp(kDecided[id], "R-timeout-ui") == 0) {
                const char *tokens[] = {"terminal=timed_out ", "touch_during_wait=1 "};
                require_tokens(line, tokens, 2);
            }
        }
    }
    for (i = 0; i < 10; ++i) {
        if (counts[i] != 1) {
            fprintf(stderr, "scenario %s count %d\n", kDecided[i], counts[i]);
        }
        CHECK(counts[i] == 1);
    }
    CHECK(wire == 0);
    CHECK(stale_clock == 0);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    CHECK(session.stale() == 1);
    CHECK(session.missed_beats() == 3);
    CHECK(session.roster_period_ms() == fl_product_login::kActiveRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == fl_product_login::kHeartbeatPeriodMs);
    return g_failures;
}

bool allowed_terminal(const char *terminal)
{
    return strcmp(terminal, "success") == 0 || strcmp(terminal, "failure") == 0 ||
           strcmp(terminal, "cancelled") == 0 || strcmp(terminal, "stale") == 0 ||
           strcmp(terminal, "timed_out") == 0;
}

void check_one_terminal_each(fl_product_login::SessionWriter *session)
{
    int i;
    int j;
    for (i = 0; i < session->event_count(); ++i) {
        const fl_product_login::TerminalRecord &record = session->event_at(i);
        CHECK(record.terminal_count == 1);
        CHECK(allowed_terminal(record.terminal));
        CHECK(session->terminal_count_for(record.operation_id) == 1);
        for (j = i + 1; j < session->event_count(); ++j) {
            CHECK(session->event_at(j).operation_id != record.operation_id);
        }
    }
}

int test_request_identity()
{
    const char *invalid_body =
        "{\"schema\":\"fl.hangout-roster/2\",\"hangout_id\":\"h1\",\"revision\":1,"
        "\"users\":[{\"user_id\":\"su-01\"}]}";
    const char *missing_body = "{}";
    const char *malformed_body = "{\"schema\":\"fl.hangout-roster/2\",";
    fl_product_login::SessionWriter matched;
    fl_product_login::SessionWriter stale;
    fl_product_login::SessionWriter unnamed;
    fl_product_login::SessionWriter muted;
    fl_product_login::SessionWriter timed;
    fl_product_login::SessionWriter http_session;
    fl_product_login::RosterResult body;
    fl_product_login::RosterResult old_result;
    fl_product_login::RosterResult ghost;
    fl_product_login::RosterResult late;
    fl_product_login::RosterResult classified;
    uint64_t matched_id;
    uint64_t live_id;
    uint64_t newer;
    uint64_t old_id;
    uint64_t unmuted_id;
    uint64_t hold_id;
    uint64_t http_id;
    int events_before;
    int i;
    g_failures = 0;

    matched.boot(0, true);
    matched_id = matched.roster_operation_id();
    body = roster(kEight, 8, 1, 200, 1);
    CHECK(body.operation_id == 0);
    CHECK(matched.enqueue_roster(body));
    matched.drain();
    CHECK(matched.terminal_count_for(matched_id) == 0);
    CHECK(matched.roster_inflight());
    CHECK(matched.selectable_cards() == 0);
    CHECK(matched.user_count() == 0);
    CHECK(matched.stage() == fl_product_login::kStageConnecting);
    body.operation_id = matched_id;
    matched.deliver_roster_completion(body, 1);
    CHECK(strcmp(matched.terminal_for(matched_id), "success") == 0);
    CHECK(matched.terminal_count_for(matched_id) == 1);
    CHECK(matched.selectable_cards() == 8);
    matched.deliver_roster_completion(body, 2);
    CHECK(matched.terminal_count_for(matched_id) == 1);
    CHECK(strcmp(matched.terminal_for(matched_id), "success") == 0);

    stale.boot(0, true);
    live_id = stale.roster_operation_id();
    old_result = roster(kRemoved, 7, 1, 200, 1);
    old_result.operation_id = 9001;
    CHECK(stale.enqueue_roster(old_result));
    stale.drain();
    CHECK(strcmp(stale.terminal_for(9001), "stale") == 0);
    CHECK(stale.terminal_count_for(9001) == 1);
    CHECK(stale.terminal_count_for(live_id) == 0);
    CHECK(stale.roster_inflight());
    CHECK(stale.roster_operation_id() == live_id);
    CHECK(stale.selectable_cards() == 0);
    CHECK(stale.user_count() == 0);
    CHECK(stale.stage() == fl_product_login::kStageConnecting);

    unnamed.boot(0, true);
    complete_current(&unnamed, roster(kEight, 8, 1, 200, 1), 0);
    CHECK(unnamed.roster_inflight() == false);
    events_before = unnamed.event_count();
    ghost = roster(kRemoved, 7, 1, 200, 1);
    CHECK(ghost.operation_id == 0);
    CHECK(unnamed.enqueue_roster(ghost));
    newer = unnamed.begin_roster_request(unnamed.now_ms());
    CHECK(newer != 0);
    unnamed.drain();
    CHECK(unnamed.terminal_count_for(newer) == 0);
    CHECK(unnamed.roster_inflight());
    CHECK(unnamed.event_count() == events_before);
    CHECK(unnamed.user_count() == 8);
    CHECK(unnamed.selectable_cards() == 8);
    CHECK(unnamed.stage() == fl_product_login::kStageRoster);

    muted.boot(0, true);
    complete_current(&muted, roster(kEight, 8, 1, 200, 1), 0);
    muted.ack_health();
    muted.advance_to(fl_product_login::kActiveRosterPeriodMs);
    CHECK(muted.roster_inflight());
    old_id = muted.roster_operation_id();
    late = roster(kRemoved, 7, 1, 200, 1);
    late.operation_id = old_id;
    CHECK(muted.enqueue_roster(late));
    muted.mute(muted.now_ms());
    CHECK(strcmp(muted.terminal_for(old_id), "stale") == 0);
    CHECK(muted.stage() == fl_product_login::kStageMuted);
    CHECK(muted.selectable_cards() == 0);
    muted.unmute(muted.now_ms());
    unmuted_id = muted.roster_operation_id();
    CHECK(unmuted_id != old_id);
    CHECK(muted.roster_inflight());
    CHECK(muted.stage() == fl_product_login::kStageConnecting);
    CHECK(muted.selectable_cards() == 0);
    muted.drain();
    CHECK(muted.terminal_count_for(old_id) == 1);
    CHECK(strcmp(muted.terminal_for(old_id), "stale") == 0);
    CHECK(muted.terminal_count_for(unmuted_id) == 0);
    CHECK(muted.roster_operation_id() == unmuted_id);
    CHECK(muted.roster_inflight());
    CHECK(muted.stage() == fl_product_login::kStageConnecting);
    CHECK(muted.selectable_cards() == 0);
    CHECK(muted.user_count() == 8);

    {
        fl_product_login::SessionWriter ghosted;
        fl_product_login::RosterResult unidentified;
        const char *ghost_ids[] = {"su-ghost"};
        uint64_t active;
        uint64_t renewed;
        ghosted.boot(0, true);
        complete_current(&ghosted, roster(kEight, 8, 1, 200, 1), 0);
        CHECK(ghosted.stage() == fl_product_login::kStageRoster);
        CHECK(ghosted.user_count() == 8);
        ghosted.ack_health();
        ghosted.advance_to(fl_product_login::kActiveRosterPeriodMs);
        CHECK(ghosted.roster_inflight());
        active = ghosted.roster_operation_id();
        ghosted.mute(ghosted.now_ms());
        CHECK(strcmp(ghosted.terminal_for(active), "stale") == 0);
        ghosted.unmute(ghosted.now_ms());
        renewed = ghosted.roster_operation_id();
        CHECK(renewed != 0);
        CHECK(renewed != active);
        CHECK(ghosted.roster_inflight());
        unidentified = roster(ghost_ids, 1, 1, 200, 1);
        CHECK(unidentified.operation_id == 0);
        CHECK(strcmp(unidentified.users[0].id, "su-ghost") == 0);
        CHECK(ghosted.enqueue_roster(unidentified));
        ghosted.drain();
        CHECK(ghosted.terminal_count_for(renewed) == 0);
        CHECK(ghosted.terminal_for(renewed)[0] == 0);
        CHECK(ghosted.roster_inflight());
        CHECK(ghosted.roster_operation_id() == renewed);
        CHECK(ghosted.stage() == fl_product_login::kStageConnecting);
        CHECK(ghosted.selectable_cards() == 0);
        CHECK(strcmp(ghosted.focus_id(), "su-01") == 0);
        CHECK(ghosted.user_count() == 8);
        CHECK(strcmp(ghosted.terminal_for(active), "stale") == 0);
        CHECK(ghosted.terminal_count_for(active) == 1);
        check_one_terminal_each(&ghosted);
    }

    timed.boot(0, true);
    complete_current(&timed, roster(kEight, 8, 1, 200, 1), 0);
    timed.begin_hold_probe(100);
    hold_id = timed.roster_operation_id();
    timed.note_touch_during_wait();
    timed.advance_to(100 + fl_product_login::kRequestTimeoutMs);
    CHECK(strcmp(timed.terminal_for(hold_id), "timed_out") == 0);
    CHECK(timed.terminal_count_for(hold_id) == 1);

    http_session.boot(0, true);
    http_id = http_session.roster_operation_id();
    classified = fl_product_login::deliver_classified_roster(&http_session, http_id, 1, 404, missing_body,
                                                 (int)strlen(missing_body), 0);
    CHECK(classified.operation_id == http_id);
    CHECK(classified.http_status == 404);
    CHECK(strcmp(http_session.terminal_for(http_id), "failure") == 0);
    CHECK(http_session.selectable_cards() == 0);
    CHECK(http_session.stage() == fl_product_login::kStageConnecting);
    http_session.boot(1, true);
    http_id = http_session.roster_operation_id();
    classified = fl_product_login::deliver_classified_roster(&http_session, http_id, 1, 200, invalid_body,
                                                 (int)strlen(invalid_body), 1);
    CHECK(classified.valid == 0);
    CHECK(classified.parse_ok == 1);
    CHECK(strcmp(http_session.terminal_for(http_id), "failure") == 0);
    CHECK(http_session.selectable_cards() == 0);
    CHECK(http_session.stage() == fl_product_login::kStageConnecting);
    http_session.boot(2, true);
    http_id = http_session.roster_operation_id();
    classified = fl_product_login::deliver_classified_roster(&http_session, http_id, 1, 200, malformed_body,
                                                 (int)strlen(malformed_body), 2);
    CHECK(classified.parse_ok == 0);
    CHECK(strcmp(http_session.terminal_for(http_id), "failure") == 0);
    CHECK(http_session.selectable_cards() == 0);
    http_session.boot(3, true);
    http_id = http_session.roster_operation_id();
    classified = fl_product_login::deliver_classified_roster(
        &http_session, http_id, 0, 200,
        "{\"schema\":\"fl.hangout-roster/2\",\"hangout_id\":\"h1\",\"revision\":1,"
        "\"users\":[{\"user_id\":\"su-01\",\"display_name\":\"One\"}]}",
        120, 3);
    CHECK(classified.http_status == 0);
    CHECK(classified.parse_ok == 0);
    CHECK(strcmp(http_session.terminal_for(http_id), "failure") == 0);
    CHECK(strcmp(http_session.terminal_for(http_id), "timed_out") != 0);
    CHECK(http_session.selectable_cards() == 0);
    CHECK(http_session.stage() == fl_product_login::kStageConnecting);
    for (i = 0; i < http_session.event_count(); ++i) {
        CHECK(strcmp(http_session.event_at(i).scenario, "R-timeout-ui") != 0);
        CHECK(strcmp(http_session.event_at(i).terminal, "timed_out") != 0);
        CHECK(http_session.event_at(i).selectable_cards == 0);
    }

    {
        fl_product_login::SessionWriter pressed;
        fl_product_login::RosterPoller poller(&pressed);
        uint64_t active;
        pressed.boot(0, true);
        complete_current(&pressed, roster(kEight, 8, 1, 200, 1), 0);
        active = pressed.roster_operation_id();
        for (i = 0; i < fl_product_login::kRosterResultQueueCapacity; ++i) {
            fl_product_login::RosterResult extra = roster(kEight, 8, 1, 200, 1);
            extra.operation_id = 1000 + (uint64_t)i;
            CHECK(poller.submit(extra));
        }
        {
            fl_product_login::RosterResult rejected = roster(kRemoved, 7, 1, 200, 1);
            rejected.operation_id = 2000;
            CHECK(poller.submit(rejected) == false);
        }
        CHECK(pressed.overflow_terminals() == 1);
        CHECK(pressed.rejected_results() == 1);
        CHECK(strcmp(pressed.terminal_for(2000), "failure") == 0);
        CHECK(pressed.terminal_count_for(2000) == 1);
        CHECK(pressed.roster_operation_id() == active);
        CHECK(pressed.stage() == fl_product_login::kStageRoster);
        CHECK(pressed.user_count() == 8);
        CHECK(pressed.queue_count() == fl_product_login::kRosterResultQueueCapacity);
        check_one_terminal_each(&pressed);
    }

    check_one_terminal_each(&matched);
    check_one_terminal_each(&stale);
    check_one_terminal_each(&unnamed);
    check_one_terminal_each(&muted);
    check_one_terminal_each(&timed);
    check_one_terminal_each(&http_session);
    return g_failures;
}

int test_http_apply_ok(const char *path)
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    char body[8192];
    FILE *file;
    int len;
    const char *terminal;
    g_failures = 0;
    file = fopen(path, "rb");
    if (file == 0) {
        fprintf(stderr, "missing body %s\n", path);
        return 1;
    }
    len = (int)fread(body, 1, sizeof(body), file);
    fclose(file);
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    fl_product_login::deliver_classified_roster(&session, operation_id, 1, 200, body, len, session.now_ms());
    terminal = session.terminal_for(operation_id);
    printf("http_apply_ok operation_id=%llu terminal=%s selectable_cards=%d stage=%s user_count=%d\n",
           (unsigned long long)operation_id, terminal, session.selectable_cards(), session.stage_name(),
           session.user_count());
    CHECK(strcmp(terminal, "success") == 0);
    CHECK(session.selectable_cards() > 0);
    CHECK(session.stage() == fl_product_login::kStageRoster);
    return g_failures;
}

int test_pin_classify(const char *perform_text, const char *status_text, const char *path, const char *user_id)
{
    char body[2048];
    FILE *file;
    int perform_ok = atoi(perform_text);
    int http_status = atoi(status_text);
    int len;
    fl_product_login::PinResult result;
    g_failures = 0;
    file = fopen(path, "rb");
    if (file == 0) {
        return 1;
    }
    len = (int)fread(body, 1, sizeof(body), file);
    fclose(file);
    result = fl_product_login::classify_pin_http(42, 1, "s-boot", user_id, perform_ok, http_status, body, len);
    printf("pin_classify outcome=%d parse_ok=%d http_status=%d pin_copy=masked\n", result.outcome, result.parse_ok,
           result.http_status);
    CHECK(result.parse_ok == 1);
    CHECK(result.outcome == fl_product_login::kPinOutcomeWrong);
    return g_failures;
}

int test_http_apply(const char *perform_text, const char *status_text, const char *path)
{
    fl_product_login::SessionWriter session;
    fl_product_login::RosterResult result;
    uint64_t operation_id;
    char body[2048];
    FILE *file;
    int perform_ok;
    int http_status;
    int len;
    int i;
    const char *terminal;
    g_failures = 0;
    perform_ok = atoi(perform_text);
    http_status = atoi(status_text);
    file = fopen(path, "rb");
    if (file == 0) {
        fprintf(stderr, "missing body %s\n", path);
        return 1;
    }
    len = (int)fread(body, 1, sizeof(body), file);
    fclose(file);
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    result = fl_product_login::deliver_classified_roster(&session, operation_id, perform_ok, http_status, body, len,
                                             session.now_ms());
    terminal = session.terminal_for(operation_id);
    printf("http_apply operation_id=%llu terminal=%s terminal_count=%d stage=%s selectable_cards=%d "
           "user_count=%d valid=%d http_status=%d parse_ok=%d scenario=%s held_response=0\n",
           (unsigned long long)operation_id, terminal[0] ? terminal : "none",
           session.terminal_count_for(operation_id), session.stage_name(), session.selectable_cards(),
           session.user_count(), result.valid, result.http_status, result.parse_ok,
           session.event_count() > 0 ? session.event_at(session.event_count() - 1).scenario : "none");
    CHECK(operation_id != 0);
    CHECK(result.operation_id == operation_id);
    CHECK(strcmp(terminal, "failure") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.selectable_cards() == 0);
    CHECK(session.stage() == fl_product_login::kStageConnecting);
    CHECK(strcmp(terminal, "timed_out") != 0);
    for (i = 0; i < session.event_count(); ++i) {
        CHECK(strcmp(session.event_at(i).scenario, "R-timeout-ui") != 0);
        CHECK(strcmp(session.event_at(i).terminal, "success") != 0);
    }
    check_one_terminal_each(&session);
    return g_failures;
}

fl_product_login::PinResult pin_body(fl_product_login::SessionWriter *session, int outcome, int http_status,
                                     int parse_ok, int side_effects)
{
    fl_product_login::PinResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = session->pin_operation_id();
    result.generation = session->pin_key_generation();
    strncpy(result.session_id, session->session_id(), sizeof(result.session_id) - 1);
    strncpy(result.user_id, session->focus_id(), sizeof(result.user_id) - 1);
    result.outcome = outcome;
    result.http_status = http_status;
    result.parse_ok = parse_ok;
    result.side_effects = side_effects;
    result.side_effects_valid = side_effects > 0 ? 1 : 0;
    if (outcome == fl_product_login::kPinOutcomeSuccess) {
        strncpy(result.session_token, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                sizeof(result.session_token) - 1);
        result.session_token_valid = 1;
    }
    return result;
}

void submit_wrong_pin(fl_product_login::SessionWriter *session)
{
    if (session->pin_phase() == fl_product_login::kPinPhaseWrong) {
        session->advance_to(session->now_ms() + fl_product_login::kWrongPinCopyMs);
    }
    session->enter_pin_digit('9', session->now_ms());
    session->enter_pin_digit('9', session->now_ms());
    session->enter_pin_digit('9', session->now_ms());
    session->enter_pin_digit('9', session->now_ms());
    {
        fl_product_login::PinResult result =
            pin_body(session, fl_product_login::kPinOutcomeWrong, 200, 1, 1);
        session->deliver_pin_completion(result, session->now_ms());
    }
}

int test_connecting_backoff()
{
    static const int kWaitMs[] = {1000, 2000, 4000, 30000};
    fl_product_login::SessionWriter session;
    int step;
    g_failures = 0;
    session.boot(0, true);
    for (step = 0; step < 3; ++step) {
        CHECK(session.roster_inflight());
        session.note_transport_failure();
        CHECK(session.connecting_backoff_index() == step + 1);
        CHECK(session.stage() == fl_product_login::kStageConnecting);
        session.advance_to(session.now_ms() + kWaitMs[step]);
        CHECK(session.roster_inflight());
    }
    session.note_transport_failure();
    CHECK(session.connecting_backoff_index() == 3);
    session.advance_to(session.now_ms() + kWaitMs[3]);
    CHECK(session.roster_inflight());
    session.note_transport_failure();
    CHECK(session.connecting_backoff_index() == 3);
    session.advance_to(session.now_ms() + kWaitMs[3]);
    CHECK(session.roster_inflight());
    return g_failures;
}

int test_pin_lost_response()
{
    fl_product_login::SessionWriter session;
    uint64_t operation_id;
    fl_product_login::PinResult success;
    g_failures = 0;
    establish(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('2', session.now_ms());
    session.enter_pin_digit('3', session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    operation_id = session.pin_operation_id();
    session.advance_to(session.now_ms() + fl_product_login::kPinRetryDelay0Ms);
    CHECK(session.pin_operation_id() == operation_id);
    success = pin_body(&session, fl_product_login::kPinOutcomeSuccess, 200, 1, 1);
    success.operation_id = operation_id;
    session.deliver_pin_completion(success, session.now_ms());
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.authenticated() == 1);
    session.deliver_pin_completion(success, session.now_ms());
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    return g_failures;
}

int test_pin_cooldown_through_mute()
{
    fl_product_login::SessionWriter session;
    int64_t cooldown_until;
    int i;
    g_failures = 0;
    establish(&session);
    session.select_pin_user("su-01", session.now_ms());
    for (i = 0; i < 5; ++i) {
        submit_wrong_pin(&session);
    }
    CHECK(session.wrong_count() == 5);
    CHECK(session.pin_phase() == fl_product_login::kPinPhaseCooldown);
    CHECK(session.cooldown_remaining_ms() == fl_product_login::kPinCooldownMs);
    cooldown_until = session.now_ms() + session.cooldown_remaining_ms();
    session.mute(session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageMuted);
    session.advance_to(session.now_ms() + 25000);
    CHECK(session.cooldown_remaining_ms() > 0);
    CHECK(session.cooldown_remaining_ms() <= 35000);
    session.unmute(session.now_ms());
    CHECK(session.pin_phase() == fl_product_login::kPinPhaseCooldown);
    CHECK(session.cooldown_remaining_ms() > 0);
    session.advance_to(cooldown_until);
    CHECK(session.cooldown_remaining_ms() == 0);
    CHECK(session.wrong_count() == 0);
    return g_failures;
}

int test_pin_apply(const char *user_id, const char *path)
{
    fl_product_login::SessionWriter session;
    char body[4096];
    FILE *file;
    int len;
    uint64_t operation_id;
    fl_product_login::PinResult result;
    g_failures = 0;
    file = fopen(path, "rb");
    if (file == 0) {
        return 1;
    }
    len = (int)fread(body, 1, sizeof(body), file);
    fclose(file);
    establish(&session);
    session.select_pin_user(user_id, session.now_ms());
    operation_id = session.begin_pin_request(user_id, "9999", session.now_ms());
    CHECK(operation_id != 0);
    result = fl_product_login::deliver_classified_pin(&session, operation_id, session.pin_key_generation(),
                                                      session.session_id(), user_id, 1, 200, body, len,
                                                      session.now_ms());
    printf("pin_apply outcome=%d terminal=%s terminal_count=%d authenticated=%d\n", result.outcome,
           session.terminal_for(operation_id), session.terminal_count_for(operation_id), session.authenticated());
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    CHECK(session.authenticated() == 1);
    fl_product_login::deliver_classified_pin(&session, operation_id, session.pin_key_generation(),
                                             session.session_id(), user_id, 1, 200, body, len, session.now_ms());
    CHECK(session.terminal_count_for(operation_id) == 1);
    return g_failures;
}

int test_card_fallback()
{
    const char *body =
        "{\"schema\":\"fl.hangout-roster/2\",\"hangout_id\":\"h1\",\"revision\":1,"
        "\"users\":[{\"user_id\":\"u1\",\"display_name\":\"Ada\",\"avatar_id\":\"\",\"card_background_id\":\"bogus\"}]}";
    fl_product_login::SessionWriter session;
    uint64_t id;
    g_failures = 0;
    session.boot(0, true);
    id = session.roster_operation_id();
    fl_product_login::deliver_classified_roster(&session, id, 1, 200, body, (int)strlen(body), 1);
    CHECK(session.selectable_cards() == 1);
    CHECK(strcmp(session.user_card_background_at(0), "card-bg-01") == 0);
    CHECK(session.user_uses_initials_fallback_at(0) == 1);
    return g_failures;
}

int test_emit()
{
    fl_product_login::SessionWriter session;
    int i;
    g_failures = 0;
    fl_product_login::run_scripted_catalog(&session);
    for (i = 0; i < session.event_count(); ++i) {
        char line[1024];
        const fl_product_login::TerminalRecord &record = session.event_at(i);
        if (strcmp(record.cls, "decided") != 0 && strcmp(record.scenario, "R-wire") != 0 &&
            strcmp(record.scenario, "R-stale-clock") != 0) {
            continue;
        }
        format_trace_line(record, line, (int)sizeof(line));
        puts(line);
    }
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    int failures = 0;
    if (strcmp(name, "cadence") == 0) {
        failures = test_cadence();
    } else if (strcmp(name, "timeout") == 0) {
        failures = test_timeout();
    } else if (strcmp(name, "retry") == 0) {
        failures = test_one_retry();
    } else if (strcmp(name, "retry-fail") == 0) {
        failures = test_retry_fail();
    } else if (strcmp(name, "invalid") == 0) {
        failures = test_invalid_not_ready();
    } else if (strcmp(name, "invalid-json") == 0) {
        failures = test_invalid_json_keeps_focus();
    } else if (strcmp(name, "mute-stale") == 0) {
        failures = test_mute_stale();
    } else if (strcmp(name, "mute-release") == 0) {
        failures = test_mute_release();
    } else if (strcmp(name, "stage") == 0) {
        failures = test_stage_enum();
    } else if (strcmp(name, "stale") == 0) {
        failures = test_stale_flag();
    } else if (strcmp(name, "roster") == 0) {
        failures = test_wifi_empty_eight_and_overflow();
    } else if (strcmp(name, "terminal") == 0) {
        failures = test_one_terminal();
    } else if (strcmp(name, "emit") == 0) {
        failures = test_emit();
    } else if (strcmp(name, "manual-traces") == 0) {
        failures = test_manual_traces();
    } else if (strcmp(name, "identity") == 0) {
        failures = test_request_identity();
    } else if (strcmp(name, "connecting-backoff") == 0) {
        failures = test_connecting_backoff();
    } else if (strcmp(name, "pin-lost") == 0) {
        failures = test_pin_lost_response();
    } else if (strcmp(name, "pin-cooldown-mute") == 0) {
        failures = test_pin_cooldown_through_mute();
    } else if (strcmp(name, "pin-apply") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: pin-apply <user_id> <body-file>\n");
            return 2;
        }
        failures = test_pin_apply(argv[2], argv[3]);
    } else if (strcmp(name, "card-fallback") == 0) {
        failures = test_card_fallback();
    } else if (strcmp(name, "http-apply") == 0) {
        if (argc < 5) {
            fprintf(stderr, "usage: http-apply <perform_ok> <http_status> <body-file>\n");
            return 2;
        }
        failures = test_http_apply(argv[2], argv[3], argv[4]);
    } else if (strcmp(name, "http-apply-ok") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: http-apply-ok <body-file>\n");
            return 2;
        }
        failures = test_http_apply_ok(argv[2]);
    } else if (strcmp(name, "pin-classify") == 0) {
        if (argc < 6) {
            fprintf(stderr, "usage: pin-classify <perform_ok> <http_status> <body-file> <user_id>\n");
            return 2;
        }
        failures = test_pin_classify(argv[2], argv[3], argv[4], argv[5]);
    } else if (strcmp(name, "all") == 0) {
        failures += test_cadence();
        failures += test_timeout();
        failures += test_one_retry();
        failures += test_retry_fail();
        failures += test_invalid_not_ready();
        failures += test_invalid_json_keeps_focus();
        failures += test_mute_stale();
        failures += test_mute_release();
        failures += test_stage_enum();
        failures += test_stale_flag();
        failures += test_wifi_empty_eight_and_overflow();
        failures += test_one_terminal();
        failures += test_manual_traces();
        failures += test_request_identity();
    } else {
        fprintf(stderr, "unknown test %s\n", name);
        return 2;
    }
    if (failures != 0) {
        fprintf(stderr, "%d assertion(s) failed\n", failures);
        return 1;
    }
    return 0;
}
