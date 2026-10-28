#include "pin_catalog.hpp"

#include "pin_http.hpp"

#include <stdio.h>
#include <string.h>

namespace lab08
{
namespace
{

void enter_pin(lab07::SessionWriter *session, const char *pin)
{
    int i;
    for (i = 0; pin[i] != 0; ++i) {
        session->enter_pin_digit(pin[i], session->now_ms());
    }
}

void deliver_outcome(lab07::SessionWriter *session, const char *body)
{
    uint64_t operation_id = session->pin_operation_id();
    uint32_t generation = session->pin_key_generation();
    deliver_classified_pin(session, operation_id, generation, session->session_id(), session->focus_id(), 1,
                           200, body, (int)strlen(body), session->now_ms());
}

void clear_wrong_copy(lab07::SessionWriter *session)
{
    if (session->pin_phase() == lab07::kPinPhaseWrong) {
        session->advance_to(session->now_ms() + lab07::kWrongPinCopyMs);
    }
}

void restore_roster(lab07::SessionWriter *session)
{
    static const char *kIds[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
    lab07::RosterResult result;
    if (session->stage() == lab07::kStageMuted) {
        return;
    }
    if (session->stage() == lab07::kStageRoster && !session->roster_inflight()) {
        return;
    }
    memset(&result, 0, sizeof(result));
    session->request_roster(session->now_ms());
    if (!session->roster_inflight()) {
        return;
    }
    result.operation_id = session->roster_operation_id();
    result.http_status = 200;
    result.parse_ok = 1;
    result.valid = 1;
    lab07::fill_users(&result, kIds, 8);
    session->deliver_roster_completion(result, session->now_ms());
}

} /* namespace */

void prime_roster(lab07::SessionWriter *session)
{
    static const char *kIds[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
    lab07::RosterResult result;
    memset(&result, 0, sizeof(result));
    session->boot(0, true);
    result.operation_id = session->roster_operation_id();
    result.http_status = 200;
    result.parse_ok = 1;
    result.valid = 1;
    lab07::fill_users(&result, kIds, 8);
    session->deliver_roster_completion(result, 0);
    session->ack_health();
}

void run_clock_catalog(lab07::SessionWriter *session)
{
    int i;
    prime_roster(session);
    session->select_pin_user("su-01", session->now_ms());
    session->arm_pin_scenario("K-timeouts");
    enter_pin(session, "9999");
    session->advance_to(session->now_ms() + 11000);
    restore_roster(session);

    clear_wrong_copy(session);
    session->select_pin_user("su-01", session->now_ms());
    session->press_boot(session->now_ms());
    session->select_pin_user("su-01", session->now_ms());
    session->enter_pin_digit('1', session->now_ms());
    session->enter_pin_digit('2', session->now_ms());
    session->press_boot(session->now_ms());
    enter_pin(session, "1111");
    session->press_boot(session->now_ms());
    session->arm_pin_scenario("K-boot");
    deliver_outcome(session, "{\"outcome\":\"wrong\",\"side_effects\":1}");

    clear_wrong_copy(session);
    session->select_pin_user("su-02", session->now_ms());
    enter_pin(session, "9999");
    session->arm_pin_scenario("K-wrong");
    deliver_outcome(session, "{\"outcome\":\"wrong\",\"side_effects\":1}");

    clear_wrong_copy(session);
    session->select_pin_user("su-01", session->now_ms());
    for (i = 0; i < 5; ++i) {
        clear_wrong_copy(session);
        enter_pin(session, "9999");
        if (i == 4) {
            session->arm_pin_scenario("K-five");
        }
        deliver_outcome(session, "{\"outcome\":\"wrong\",\"side_effects\":1}");
    }

    session->select_pin_user("su-02", session->now_ms());
    session->note_remote_observation(0, "user_switch");
    session->publish("K-switch-user", "cancelled");

    session->select_pin_user("su-01", session->now_ms());
    session->enter_pin_digit('1', session->now_ms());
    session->arm_pin_scenario("K-idle");
    session->advance_to(session->now_ms() + lab07::kPartialPinIdleMs);

    session->select_pin_user("su-01", session->now_ms());
    enter_pin(session, "9999");
    session->arm_pin_scenario("K-mute-local");
    session->mute(session->now_ms());
}

int64_t apply_manual_pin(lab07::SessionWriter *session, int action)
{
    int64_t now = session->now_ms();
    if (session->stage() == lab07::kStageConnecting && session->user_count() == 0) {
        prime_roster(session);
        now = session->now_ms();
    }
    if (action == kManualSelectUser) {
        session->select_pin_user("su-01", now);
        return 0;
    }
    if (action == kManualDigit) {
        session->enter_pin_digit('1', now);
        return 0;
    }
    if (action == kManualBoot) {
        session->press_boot(now);
        return 0;
    }
    if (action == kManualWrongPin) {
        session->select_pin_user("su-01", now);
        session->begin_pin_request("su-01", "9999", session->now_ms());
        return 0;
    }
    if (action == kManualMute) {
        session->mute(now);
        return 0;
    }
    if (action == kManualUnmute) {
        session->unmute(now);
        return 0;
    }
    if (action == kManualSwitchUser) {
        session->select_pin_user("su-02", now);
        return 0;
    }
    if (action == kManualAdvanceCopy) {
        session->advance_to(now + lab07::kWrongPinCopyMs);
        return 0;
    }
    if (action == kManualAdvanceCooldown) {
        session->advance_to(now + 1000);
        return 0;
    }
    if (action == kManualAdvanceIdle) {
        session->advance_to(now + lab07::kPartialPinIdleMs);
        return 0;
    }
    return 0;
}

void run_model_catalog(lab07::SessionWriter *session)
{
    run_clock_catalog(session);
    session->unmute(session->now_ms());
    restore_roster(session);
    session->select_pin_user("su-02", session->now_ms());
    enter_pin(session, "1234");
    session->arm_pin_scenario("K-ok");
    deliver_outcome(session, "{\"outcome\":\"ok\",\"side_effects\":1}");

    session->select_pin_user("su-01", session->now_ms());
    enter_pin(session, "1234");
    session->advance_to(session->now_ms() + 1000);
    session->advance_to(session->now_ms() + 1000);
    session->arm_pin_scenario("K-lost");
    deliver_outcome(session, "{\"outcome\":\"ok\",\"side_effects\":1}");

    session->note_remote_observation(1, "concurrent");
    session->publish("K-concurrent", "failure");
    session->note_remote_observation(1, "mismatch");
    session->publish("K-mismatch", "failure");
    session->select_pin_user("su-02", session->now_ms());
    enter_pin(session, "1234");
    session->note_remote_observation(1, "mute_server");
    session->arm_pin_scenario("K-mute-server");
    session->mute(session->now_ms());
}

void emit_pin_traces(const lab07::SessionWriter &session, PinTraceAccount *account)
{
    int i;
    for (i = 0; i < session.event_count(); ++i) {
        const lab07::TerminalRecord &record = session.event_at(i);
        char line[1200];
        int formatted;
        int written;
        if (strncmp(record.scenario, "K-", 2) != 0) {
            continue;
        }
        formatted = lab07::format_pin_trace_line(record, line, (int)sizeof(line));
        if (formatted < 0) {
            if (account != nullptr) {
                account->malformed += 1;
            }
            continue;
        }
        if (formatted >= (int)sizeof(line)) {
            if (account != nullptr) {
                account->oversized += 1;
            }
            continue;
        }
        written = printf("%s\n", line);
        if (account != nullptr && written < formatted + 1) {
            account->dropped += 1;
        }
    }
}

} /* namespace lab08 */
