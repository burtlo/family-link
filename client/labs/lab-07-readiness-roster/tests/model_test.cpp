#include "pollers.hpp"
#include "session.hpp"

#include <stdio.h>
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

lab07::RosterResult roster(const char *const *ids, int count, int valid, int http_status, int parse_ok)
{
    lab07::RosterResult result;
    memset(&result, 0, sizeof(result));
    result.http_status = http_status;
    result.valid = valid;
    result.parse_ok = parse_ok;
    lab07::fill_users(&result, ids, count);
    return result;
}

const char *kEight[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
const char *kReorder[] = {"su-08", "su-07", "su-06", "su-05", "su-04", "su-03", "su-02", "su-01"};
const char *kRemoved[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07"};
const char *kAdded[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};

void establish(lab07::SessionWriter *session)
{
    lab07::RosterResult eight = roster(kEight, 8, 1, 200, 1);
    session->boot(0, true);
    session->submit_roster(eight, 0);
    session->ack_health();
}

int test_cadence()
{
    lab07::SessionWriter session;
    int step;
    g_failures = 0;
    establish(&session);
    CHECK(session.roster_period_ms() == lab07::kActiveRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == lab07::kHeartbeatPeriodMs);
    CHECK(session.next_roster_due_ms() == lab07::kActiveRosterPeriodMs);
    for (step = 1; step <= 10; ++step) {
        int64_t due = (int64_t)step * lab07::kActiveRosterPeriodMs;
        CHECK(session.next_roster_due_ms() == due);
        CHECK(session.roster_period_ms() == lab07::kActiveRosterPeriodMs);
        session.advance_to(due);
        CHECK(session.roster_inflight());
        session.ack_health();
        session.submit_roster(roster(kEight, 8, 1, 200, 1), due);
        CHECK(session.stage() == lab07::kStageRoster);
    }
    CHECK(session.now_ms() == lab07::kIdleQuietMs);
    CHECK(session.roster_period_ms() == lab07::kIdleRosterPeriodMs);
    CHECK(session.next_roster_due_ms() == lab07::kIdleQuietMs + lab07::kIdleRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == lab07::kHeartbeatPeriodMs);
    CHECK(session.next_heartbeat_due_ms() == lab07::kIdleQuietMs + lab07::kHeartbeatPeriodMs);
    session.note_user_activity(lab07::kIdleQuietMs + 100);
    CHECK(session.roster_period_ms() == lab07::kActiveRosterPeriodMs);
    CHECK(session.next_roster_due_ms() == lab07::kIdleQuietMs + 100 + lab07::kActiveRosterPeriodMs);
    session.mute(session.now_ms());
    session.advance_to(session.next_heartbeat_due_ms());
    CHECK(session.stage() == lab07::kStageMuted);
    CHECK(session.heartbeat_period_ms() == lab07::kHeartbeatPeriodMs);
    return g_failures;
}

int test_timeout()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.begin_hold_probe(100);
    operation_id = session.roster_operation_id();
    session.note_touch_during_wait();
    session.advance_to(100 + lab07::kRequestTimeoutMs - 1);
    CHECK(session.roster_inflight());
    CHECK(session.terminal_count_for(operation_id) == 0);
    session.advance_to(100 + lab07::kRequestTimeoutMs);
    CHECK(session.terminal_for(operation_id) != 0);
    CHECK(strcmp(session.terminal_for(operation_id), "timed_out") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.touch_during_wait() == 1);
    CHECK(session.has_wrong_pin() == false);
    return g_failures;
}

int test_one_retry()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.advance_to(lab07::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    CHECK(session.roster_attempt() == 1);
    CHECK(session.stage() == lab07::kStageRoster);
    session.note_transport_failure();
    CHECK(session.roster_attempt() == 2);
    CHECK(session.stage() == lab07::kStageRoster);
    CHECK(session.terminal_count_for(operation_id) == 0);
    session.arm_scenario("R-retry");
    session.submit_roster(roster(kEight, 8, 1, 200, 1), session.now_ms());
    CHECK(session.stage() == lab07::kStageRoster);
    CHECK(strcmp(session.focus_id(), "su-01") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "success") == 0);
    CHECK(session.event_at(session.event_count() - 1).attempt == 2);
    return g_failures;
}

int test_retry_fail()
{
    lab07::SessionWriter session;
    char focus[16];
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.advance_to(lab07::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    session.note_transport_failure();
    session.note_transport_failure();
    CHECK(session.stage() == lab07::kStageConnecting);
    CHECK(session.selectable_cards() == 0);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(strcmp(session.terminal_for(operation_id), "failure") == 0);
    return g_failures;
}

int test_invalid_not_ready()
{
    lab07::SessionWriter session;
    lab07::RosterResult invalid = roster(kEight, 1, 0, 200, 1);
    uint64_t operation_id;
    g_failures = 0;
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    session.arm_scenario("R-invalid-200");
    session.submit_roster(invalid, 0);
    CHECK(invalid.valid == 0);
    CHECK(session.selectable_cards() == 0);
    CHECK(session.stage() == lab07::kStageConnecting);
    CHECK(session.terminal_count_for(operation_id) == 1);
    return g_failures;
}

int test_invalid_json_keeps_focus()
{
    lab07::SessionWriter session;
    lab07::RosterResult bad = roster(kEight, 0, 1, 200, 0);
    char focus[16];
    g_failures = 0;
    establish(&session);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.request_roster(10);
    session.submit_roster(bad, 10);
    CHECK(session.terminal_count_for(session.event_at(session.event_count() - 1).operation_id) == 1);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.user_count() == 8);
    session.request_roster(20);
    bad.http_status = 503;
    bad.parse_ok = 1;
    session.submit_roster(bad, 20);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.stage() == lab07::kStageRoster);
    return g_failures;
}

int test_mute_stale()
{
    lab07::SessionWriter session;
    lab07::RosterResult late = roster(kEight, 8, 1, 200, 1);
    uint64_t operation_id;
    g_failures = 0;
    establish(&session);
    session.advance_to(lab07::kActiveRosterPeriodMs);
    operation_id = session.roster_operation_id();
    session.mute(session.now_ms());
    CHECK(strcmp(session.terminal_for(operation_id), "stale") == 0);
    CHECK(session.stage() == lab07::kStageMuted);
    late.operation_id = operation_id;
    session.submit_roster(late, session.now_ms());
    CHECK(strcmp(session.terminal_for(operation_id), "stale") == 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    CHECK(session.stage() == lab07::kStageMuted);
    return g_failures;
}

int test_mute_release()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    const lab07::TerminalRecord *found = 0;
    int i;
    g_failures = 0;
    establish(&session);
    session.advance_to(lab07::kActiveRosterPeriodMs);
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
    CHECK(session.stage() == lab07::kStageConnecting);
    CHECK(session.roster_inflight());
    return g_failures;
}

int test_stage_enum()
{
    const char *tokens[] = {
        lab07::kTokenWifiUnavailable, lab07::kTokenConnecting, lab07::kTokenRoster,
        lab07::kTokenEmptyHangout, lab07::kTokenMuted
    };
    int i;
    int j;
    g_failures = 0;
    CHECK(lab07::kStageWifiUnavailable != lab07::kStageConnecting);
    CHECK(lab07::kStageConnecting != lab07::kStageRoster);
    CHECK(lab07::kStageRoster != lab07::kStageEmptyHangout);
    CHECK(lab07::kStageEmptyHangout != lab07::kStageMuted);
    for (i = 0; i < 5; ++i) {
        for (j = i + 1; j < 5; ++j) {
            CHECK(strcmp(tokens[i], tokens[j]) != 0);
        }
    }
    CHECK(strcmp(lab07::stage_token(lab07::kStageWifiUnavailable), lab07::kTokenWifiUnavailable) == 0);
    CHECK(strcmp(lab07::stage_token(lab07::kStageConnecting), lab07::kTokenConnecting) == 0);
    CHECK(strcmp(lab07::stage_token(lab07::kStageRoster), lab07::kTokenRoster) == 0);
    CHECK(strcmp(lab07::stage_token(lab07::kStageEmptyHangout), lab07::kTokenEmptyHangout) == 0);
    CHECK(strcmp(lab07::stage_token(lab07::kStageMuted), lab07::kTokenMuted) == 0);
    return g_failures;
}

int test_stale_flag()
{
    lab07::SessionWriter session;
    lab07::HealthPoller health(&session);
    int step;
    int stage_before;
    g_failures = 0;
    session.boot(0, true);
    session.submit_roster(roster(kEight, 8, 1, 200, 1), 0);
    stage_before = session.stage();
    for (step = 1; step <= 3; ++step) {
        session.advance_to((int64_t)step * lab07::kHeartbeatPeriodMs);
        CHECK(session.missed_beats() == step);
        session.submit_roster(roster(kEight, 8, 1, 200, 1), session.now_ms());
    }
    CHECK(session.stale() == 1);
    CHECK(session.missed_beats() == 3);
    CHECK(session.stage() == stage_before);
    CHECK(session.stage() == lab07::kStageRoster);
    CHECK(session.user_count() == 8);
    health.submit_ack();
    CHECK(session.stale() == 1);
    CHECK(session.stage() == lab07::kStageRoster);
    return g_failures;
}

int test_wifi_empty_eight_and_overflow()
{
    lab07::SessionWriter session;
    lab07::RosterPoller roster_poller(&session);
    char focus[16];
    int i;
    g_failures = 0;
    session.boot(0, false);
    CHECK(session.stage() == lab07::kStageWifiUnavailable);
    CHECK(session.selectable_cards() == 0);
    session.set_wifi(true, 5);
    CHECK(session.roster_inflight());
    session.submit_roster(roster(kEight, 0, 1, 200, 1), 5);
    CHECK(session.stage() == lab07::kStageEmptyHangout);
    CHECK(session.user_count() == 0);
    session.request_roster(6);
    session.submit_roster(roster(kEight, 8, 1, 200, 1), 6);
    CHECK(session.user_count() == 8);
    CHECK(session.selectable_cards() == 8);
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    focus[sizeof(focus) - 1] = 0;
    session.request_roster(7);
    session.submit_roster(roster(kReorder, 8, 1, 200, 1), 7);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    session.request_roster(8);
    session.submit_roster(roster(kRemoved, 7, 1, 200, 1), 8);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    session.request_roster(9);
    session.submit_roster(roster(kAdded, 8, 1, 200, 1), 9);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.user_count() == 8);
    for (i = 0; i < 8; ++i) {
        CHECK(session.user_at(i)[0] != 0);
    }
    strncpy(focus, session.focus_id(), sizeof(focus) - 1);
    for (i = 0; i < lab07::kRosterResultQueueCapacity; ++i) {
        lab07::RosterResult extra = roster(kEight, 8, 1, 200, 1);
        extra.operation_id = 1000 + (uint64_t)i;
        CHECK(roster_poller.submit(extra));
    }
    {
        lab07::RosterResult extra = roster(kEight, 8, 1, 200, 1);
        extra.operation_id = 2000;
        CHECK(roster_poller.submit(extra) == false);
    }
    CHECK(session.queue_count() == lab07::kRosterResultQueueCapacity);
    CHECK(session.overflow_terminals() == 1);
    CHECK(session.rejected_results() == 1);
    CHECK(strcmp(session.focus_id(), focus) == 0);
    CHECK(session.stage() == lab07::kStageRoster);
    return g_failures;
}

int test_one_terminal()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    g_failures = 0;
    session.boot(0, true);
    operation_id = session.roster_operation_id();
    session.submit_roster(roster(kEight, 8, 1, 200, 1), 0);
    CHECK(session.terminal_count_for(operation_id) == 1);
    {
        lab07::RosterResult again = roster(kEight, 8, 1, 200, 1);
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
    lab07::SessionWriter session;
    int64_t deadline;
    int counts[10];
    int wire = 0;
    int stale_clock = 0;
    int i;
    g_failures = 0;
    memset(counts, 0, sizeof(counts));
    session.boot(0, true);
    lab07::apply_manual_control(&session, lab07::kManualWifiDown);
    lab07::apply_manual_control(&session, lab07::kManualWifiReturn);
    lab07::apply_manual_control(&session, lab07::kManualEmpty);
    lab07::apply_manual_control(&session, lab07::kManualOne);
    lab07::apply_manual_control(&session, lab07::kManualEight);
    lab07::apply_manual_control(&session, lab07::kManualAdd);
    lab07::apply_manual_control(&session, lab07::kManualRemove);
    lab07::apply_manual_control(&session, lab07::kManualReorder);
    lab07::apply_manual_control(&session, lab07::kManualFailOnce);
    lab07::apply_manual_control(&session, lab07::kManualFailTwice);
    lab07::apply_manual_control(&session, lab07::kManualInvalid);
    deadline = lab07::apply_manual_control(&session, lab07::kManualHold);
    CHECK(deadline == session.now_ms() + lab07::kRequestTimeoutMs);
    session.note_touch_during_wait();
    lab07::complete_manual_hold(&session, deadline);
    lab07::apply_manual_control(&session, lab07::kManualMute);
    lab07::apply_manual_control(&session, lab07::kManualUnmute);
    lab07::apply_manual_control(&session, lab07::kManualIdleClock);
    lab07::apply_manual_control(&session, lab07::kManualStaleBeats);
    CHECK(session.event_count() > 0);
    CHECK(session.event_count() < lab07::kMaxEvents);
    for (i = 0; i < session.event_count(); ++i) {
        char line[1024];
        char token[64];
        int id;
        const lab07::TerminalRecord &record = session.event_at(i);
        lab07::format_trace_line(record, line, (int)sizeof(line));
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
    CHECK(session.stage() == lab07::kStageRoster);
    CHECK(session.stale() == 1);
    CHECK(session.missed_beats() == 3);
    CHECK(session.roster_period_ms() == lab07::kActiveRosterPeriodMs);
    CHECK(session.heartbeat_period_ms() == lab07::kHeartbeatPeriodMs);
    return g_failures;
}

int test_emit()
{
    lab07::SessionWriter session;
    int i;
    g_failures = 0;
    lab07::run_scripted_catalog(&session);
    for (i = 0; i < session.event_count(); ++i) {
        char line[1024];
        const lab07::TerminalRecord &record = session.event_at(i);
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
