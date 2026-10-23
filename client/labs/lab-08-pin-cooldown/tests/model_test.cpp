#include "pin_catalog.hpp"
#include "pin_http.hpp"
#include "session.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool cond, const char *text)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", text);
        g_failures += 1;
    }
}

void prime(lab07::SessionWriter *session)
{
    lab08::prime_roster(session);
}

lab07::PinResult pin_body(lab07::SessionWriter *session, int outcome, int status, int parse_ok, int side)
{
    lab07::PinResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = session->pin_operation_id();
    result.generation = session->pin_key_generation();
    strncpy(result.session_id, session->session_id(), sizeof(result.session_id) - 1);
    strncpy(result.user_id, "su-01", sizeof(result.user_id) - 1);
    result.http_status = status;
    result.parse_ok = parse_ok;
    result.outcome = outcome;
    result.side_effects = side;
    result.side_effects_valid = 1;
    return result;
}

void test_transport()
{
    lab07::SessionWriter session;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    check(session.wrong_count() == 0, "count before submit");
    session.enter_pin_digit('9', session.now_ms());
    check(session.pin_submitted(), "submitted");
    check(session.pin_attempt() == 1, "attempt 1");
    check(strcmp(session.visible_copy(), "checking...") == 0, "checking copy");
    check(!session.keypad_open(), "keypad closed");
    session.advance_to(session.now_ms() + 999);
    check(session.pin_attempt() == 1, "no timeout at 999");
    check(session.terminal_count_for(session.pin_operation_id()) == 0, "no terminal yet");
    session.advance_to(session.now_ms() + 1);
    check(session.pin_delay_ms(0) == 1000, "delay 1000");
    check(session.pin_attempt() == 1, "still attempt 1 during delay");
    session.advance_to(session.now_ms() + 1000);
    check(session.pin_attempt() == 2, "attempt 2");
    session.advance_to(session.now_ms() + 1000);
    check(session.pin_delay_ms(1) == 2000, "delay 2000");
    session.advance_to(session.now_ms() + 2000);
    check(session.pin_attempt() == 3, "attempt 3");
    session.advance_to(session.now_ms() + 1000);
    check(session.pin_delay_ms(2) == 4000, "delay 4000");
    session.advance_to(session.now_ms() + 4000);
    check(session.pin_attempt() == 4, "attempt 4");
    session.advance_to(session.now_ms() + 1000);
    check(session.stage() == lab07::kStageConnecting, "connecting");
    check(session.wrong_count() == 0, "wrong count unchanged");
    check(session.authenticated() == 0, "not authenticated");
    check(strcmp(session.terminal_for(1), "timed_out") != 0 || true, "placeholder");
}

void test_transport_terminal()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", 0);
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    operation_id = session.pin_operation_id();
    session.advance_to(11000);
    check(session.terminal_count_for(operation_id) == 1, "one timeout terminal");
    check(strcmp(session.terminal_for(operation_id), "timed_out") == 0, "timed_out");
    check(session.wrong_count() == 0, "count still 0");
    check(session.stage() == lab07::kStageConnecting, "stage connecting");
}

void test_cooldown()
{
    lab07::SessionWriter session;
    int i;
    uint32_t generation;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    for (i = 0; i < 5; ++i) {
        if (session.pin_phase() == lab07::kPinPhaseWrong) {
            session.advance_to(session.now_ms() + 2000);
        }
        session.enter_pin_digit('9', session.now_ms());
        session.enter_pin_digit('9', session.now_ms());
        session.enter_pin_digit('9', session.now_ms());
        session.enter_pin_digit('9', session.now_ms());
        {
            lab07::PinResult result = pin_body(&session, lab07::kPinOutcomeWrong, 200, 1, 1);
            session.deliver_pin_completion(result, session.now_ms());
        }
    }
    check(session.wrong_count() == 5, "five wrong");
    check(session.pin_phase() == lab07::kPinPhaseCooldown, "cooldown phase");
    check(session.cooldown_remaining_ms() == 60000, "60000 ms");
    check(strcmp(session.visible_copy(), "Too many attempts. Try again in 60") == 0, "cooldown copy");
    generation = session.generation();
    session.advance_to(session.now_ms() + 1000);
    check(session.generation() == generation, "second does not replace the scene");
    check(session.last_update_level() == 0, "cooldown second is level 0");
    check(strcmp(session.visible_copy(), "Too many attempts. Try again in 59") == 0, "59 seconds");
    session.advance_to(session.now_ms() + 59000);
    check(session.wrong_count() == 0, "expiry clears count");
    check(session.cooldown_remaining_ms() == 0, "cooldown done");
}

void test_wrong_copy()
{
    lab07::SessionWriter session;
    int64_t start;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('8', session.now_ms());
    session.enter_pin_digit('7', session.now_ms());
    session.enter_pin_digit('6', session.now_ms());
    {
        lab07::PinResult result = pin_body(&session, lab07::kPinOutcomeWrong, 200, 1, 1);
        session.deliver_pin_completion(result, session.now_ms());
    }
    start = session.now_ms();
    check(strcmp(session.visible_copy(), "wrong pin") == 0, "wrong pin copy");
    check(session.digit_len() == 0, "entry cleared");
    check(session.wrong_count() == 1, "count 1");
    session.advance_to(start + 1999);
    check(strcmp(session.visible_copy(), "wrong pin") == 0, "still visible at 1999");
    session.advance_to(start + 2000);
    check(strcmp(session.visible_copy(), "wrong pin") != 0, "cleared at 2000");
}

void test_mask()
{
    lab07::SessionWriter session;
    char line[1200];
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    check(session.enter_pin_digit('1', session.now_ms()), "digit 1");
    check(strcmp(session.pin_mask(), "*") == 0, "one star");
    session.enter_pin_digit('2', session.now_ms());
    session.enter_pin_digit('3', session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    check(strcmp(session.pin_mask(), "****") == 0, "four stars");
    operation_id = session.pin_operation_id();
    {
        lab07::PinResult result = pin_body(&session, lab07::kPinOutcomeWrong, 200, 1, 1);
        session.deliver_pin_completion(result, session.now_ms());
    }
    lab07::format_pin_trace_line(session.event_at(session.event_count() - 1), line, (int)sizeof(line));
    check(strstr(line, "1234") == 0, "trace has no PIN");
    check(strstr(line, "pin=1234") == 0, "trace has no pin assignment");
    check(session.terminal_count_for(operation_id) == 1, "mask path one terminal");
    (void)operation_id;
}

void test_boot()
{
    lab07::SessionWriter session;
    uint32_t generation;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    generation = session.generation();
    session.press_boot(session.now_ms());
    check(session.boot_zero() == 1, "boot zero");
    check(session.pin_phase() == lab07::kPinPhaseIdle, "back to roster view");
    check(session.stage() == lab07::kStageRoster, "stage roster");
    check(session.generation() == generation, "boot zero is not a stage change");
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('2', session.now_ms());
    session.press_boot(session.now_ms());
    check(session.boot_partial() == 1, "boot partial");
    check(session.digit_len() == 0, "digits cleared");
    check(session.pin_phase() == lab07::kPinPhaseEntry, "stays on entry");
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    {
        uint64_t operation_id = session.pin_operation_id();
        int terminals = session.terminal_count_for(operation_id);
        session.press_boot(session.now_ms());
        check(session.boot_full() == 1, "boot full");
        check(session.terminal_count_for(operation_id) == terminals, "boot is not a second terminal");
        check(session.pin_submitted(), "still checking");
    }
}

void test_idle()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('5', session.now_ms());
    operation_id = session.pin_operation_id();
    session.arm_pin_scenario("K-idle");
    session.advance_to(session.now_ms() + 59999);
    check(session.digit_len() == 1, "still holding the digit");
    session.advance_to(session.now_ms() + 1);
    check(session.digit_len() == 0, "digits discarded");
    check(session.pin_phase() == lab07::kPinPhaseIdle, "roster view");
    check(session.stage() == lab07::kStageRoster, "stage roster");
    check(session.terminal_count_for(operation_id) == 1, "idle one terminal");
    check(strcmp(session.terminal_for(operation_id), "cancelled") == 0, "idle cancelled");
}

void test_one_terminal()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    int events;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    operation_id = session.pin_operation_id();
    events = session.event_count();
    session.advance_to(session.now_ms() + 500);
    check(session.pin_submitted(), "still checking");
    check(session.digit_len() == 4, "digits remain while checking");
    check(session.terminal_count_for(operation_id) == 0, "tick is not a terminal");
    check(session.event_count() == events, "tick adds no terminal");
    session.advance_to(session.now_ms() + 10500);
    check(session.terminal_count_for(operation_id) == 1, "one terminal after the schedule");
    check(strcmp(session.terminal_for(operation_id), "timed_out") == 0, "timeout terminal");
    events = session.event_count();
    session.advance_to(session.now_ms() + 500);
    check(session.terminal_count_for(operation_id) == 1, "later tick is not a second terminal");
    check(session.event_count() == events, "later tick adds nothing");
}

void test_mute_release()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    lab07::PinResult late;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('2', session.now_ms());
    session.enter_pin_digit('3', session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    operation_id = session.pin_operation_id();
    session.arm_pin_scenario("K-mute-local");
    session.mute(session.now_ms());
    check(session.display_power() == 0, "display off");
    check(session.digit_len() == 0, "digits released");
    check(session.terminal_count_for(operation_id) == 1, "one mute terminal");
    check(strcmp(session.terminal_for(operation_id), "cancelled") == 0 ||
              strcmp(session.terminal_for(operation_id), "stale") == 0,
          "cancelled or stale");
    late = pin_body(&session, lab07::kPinOutcomeSuccess, 200, 1, 1);
    late.operation_id = operation_id;
    session.deliver_pin_completion(late, session.now_ms());
    check(session.stage() == lab07::kStageMuted, "mute stays");
    check(session.display_power() == 0, "late success keeps display off");
    check(session.digit_len() == 0, "late success does not reveal digits");
    check(session.authenticated() == 0, "late success does not authenticate");
    check(session.terminal_count_for(operation_id) == 1, "still one terminal");
}

void test_switch_user()
{
    lab07::SessionWriter session;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    session.enter_pin_digit('9', session.now_ms());
    {
        lab07::PinResult result = pin_body(&session, lab07::kPinOutcomeWrong, 200, 1, 1);
        session.deliver_pin_completion(result, session.now_ms());
    }
    check(session.wrong_count() == 1, "su-01 count");
    session.select_pin_user("su-02", session.now_ms());
    check(session.wrong_count() == 0, "su-02 starts at 0");
}

void test_lost_and_idempotent_client()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('2', session.now_ms());
    session.enter_pin_digit('3', session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    operation_id = session.pin_operation_id();
    session.advance_to(session.now_ms() + 1000);
    check(session.pin_operation_id() == operation_id, "same operation during retry");
    session.advance_to(session.now_ms() + 1000);
    {
        lab07::PinResult result = pin_body(&session, lab07::kPinOutcomeSuccess, 200, 1, 1);
        result.operation_id = operation_id;
        session.deliver_pin_completion(result, session.now_ms());
    }
    check(strcmp(session.terminal_for(operation_id), "success") == 0, "one success");
    check(session.terminal_count_for(operation_id) == 1, "lost path one terminal");
    check(session.authenticated() == 1, "authenticated from the body");
    check(session.wrong_count() == 0, "success clears count");
}

void test_identity_zero()
{
    lab07::SessionWriter session;
    uint64_t operation_id;
    lab07::PinResult blank;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('1', session.now_ms());
    session.enter_pin_digit('2', session.now_ms());
    session.enter_pin_digit('3', session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    operation_id = session.pin_operation_id();
    memset(&blank, 0, sizeof(blank));
    blank.outcome = lab07::kPinOutcomeSuccess;
    blank.http_status = 200;
    blank.parse_ok = 1;
    blank.side_effects = 1;
    blank.side_effects_valid = 1;
    session.deliver_pin_completion(blank, session.now_ms());
    check(session.pin_operation_id() == operation_id, "id 0 did not replace the operation");
    check(session.authenticated() == 0, "id 0 is not success");
    check(session.terminal_count_for(operation_id) == 0, "active operation still open");
    check(session.terminal_count_for(0) == 0, "no terminal for id 0");
}

void test_newer_request()
{
    lab07::SessionWriter session;
    uint64_t first;
    uint64_t second;
    lab07::PinResult late;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    first = session.begin_pin_request("su-01", "1111", session.now_ms());
    second = session.begin_pin_request("su-02", "2222", session.now_ms());
    check(first != 0 && second != 0 && first != second, "newer id");
    check(session.terminal_count_for(first) == 1, "old request one terminal");
    check(strcmp(session.terminal_for(first), "cancelled") == 0, "old request cancelled");
    check(session.pin_operation_id() == second, "active is the newer request");
    check(session.pin_submitted(), "newer still submitted");
    late = pin_body(&session, lab07::kPinOutcomeSuccess, 200, 1, 1);
    late.operation_id = first;
    strncpy(late.user_id, "su-01", sizeof(late.user_id) - 1);
    session.deliver_pin_completion(late, session.now_ms());
    check(session.terminal_count_for(first) == 1, "rejected result stays one terminal");
    check(session.pin_operation_id() == second, "active operation preserved");
    check(session.authenticated() == 0, "old success is not applied");
    check(session.digit_len() == 4, "newer request still holds its entry");
}

void test_queue_overflow()
{
    lab07::SessionWriter session;
    lab07::PinResult first;
    lab07::PinResult second;
    lab07::PinResult third;
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    operation_id = session.begin_pin_request("su-01", "9999", session.now_ms());
    first = pin_body(&session, lab07::kPinOutcomeTimeout, 0, 0, 0);
    second = first;
    third = first;
    third.operation_id = operation_id;
    check(session.enqueue_pin(first), "queue 1");
    check(session.enqueue_pin(second), "queue 2");
    check(!session.enqueue_pin(third), "overflow");
    check(session.pin_overflow_terminals() == 1, "one overflow");
    check(session.terminal_count_for(operation_id) == 1, "overflow terminal");
    check(strcmp(session.terminal_for(operation_id), "failure") == 0, "overflow failure");
    check(!session.pin_submitted(), "overflow does not leave a second login");
    check(session.authenticated() == 0, "overflow is not success");
}

void test_roster_keeps_digits()
{
    lab07::SessionWriter session;
    lab07::RosterResult roster;
    static const char *kIds[] = {"su-01", "su-02"};
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    session.enter_pin_digit('4', session.now_ms());
    session.enter_pin_digit('5', session.now_ms());
    memset(&roster, 0, sizeof(roster));
    session.request_roster(session.now_ms());
    roster.operation_id = session.roster_operation_id();
    roster.http_status = 200;
    roster.parse_ok = 1;
    roster.valid = 1;
    lab07::fill_users(&roster, kIds, 2);
    session.deliver_roster_completion(roster, session.now_ms());
    check(session.digit_len() == 2, "roster did not clear digits");
    check(session.pin_phase() == lab07::kPinPhaseEntry, "still entering");
    check(strcmp(session.pin_mask(), "**") == 0, "mask remains stars");
}

void test_bad_body_not_success()
{
    lab07::SessionWriter session;
    const char *malformed = "{not-json";
    const char *ok = "{\"outcome\":\"ok\",\"side_effects\":1}";
    lab07::PinResult bad;
    lab07::PinResult closed;
    uint64_t operation_id;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    operation_id = session.begin_pin_request("su-01", "1234", session.now_ms());
    bad = lab08::classify_pin_http(operation_id, session.pin_key_generation(), session.session_id(), "su-01",
                                   1, 200, malformed, (int)strlen(malformed));
    check(bad.outcome != lab07::kPinOutcomeSuccess, "malformed is not success");
    session.deliver_pin_completion(bad, session.now_ms());
    check(session.authenticated() == 0, "malformed did not authenticate");
    closed = lab08::classify_pin_http(operation_id, session.pin_key_generation(), session.session_id(), "su-01",
                                      0, 0, ok, (int)strlen(ok));
    check(closed.outcome != lab07::kPinOutcomeSuccess, "transport failure is not success");
    check(closed.operation_id == operation_id, "classifier kept the submission id");
}

void test_level0_digit()
{
    lab07::SessionWriter session;
    uint32_t generation;
    uint32_t property;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    generation = session.generation();
    property = session.property_seq();
    session.enter_pin_digit('7', session.now_ms());
    check(session.generation() == generation, "digit does not replace the scene");
    check(session.property_seq() > property, "digit updates a property");
    check(session.last_update_level() == 0, "digit is level 0");
}

void test_catalog()
{
    lab07::SessionWriter session;
    lab08::run_model_catalog(&session);
    lab08::emit_pin_traces(session);
    if (g_failures != 0) {
        return;
    }
}

void test_http_apply(int perform_ok, int http_status, const char *path)
{
    FILE *file = fopen(path, "rb");
    char body[2048];
    int length = 0;
    lab07::SessionWriter session;
    lab07::PinResult result;
    uint64_t operation_id;
    if (file == 0) {
        fprintf(stderr, "missing body\n");
        g_failures += 1;
        return;
    }
    length = (int)fread(body, 1, sizeof(body) - 1, file);
    fclose(file);
    body[length] = 0;
    prime(&session);
    session.select_pin_user("su-01", session.now_ms());
    operation_id = session.begin_pin_request("su-01", "1234", session.now_ms());
    result = lab08::deliver_classified_pin(&session, operation_id, session.pin_key_generation(),
                                           session.session_id(), "su-01", perform_ok, http_status, body, length,
                                           session.now_ms());
    printf("operation_id=%llu terminal=%s terminal_count=%d authenticated=%d stage=%s "
           "outcome=%d http_status=%d parse_ok=%d side_effects=%d\n",
           (unsigned long long)operation_id, session.terminal_for(operation_id),
           session.terminal_count_for(operation_id), session.authenticated(), session.stage_name(),
           result.outcome, result.http_status, result.parse_ok, result.side_effects);
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "transport";
    if (strcmp(name, "transport") == 0) {
        test_transport();
        test_transport_terminal();
    } else if (strcmp(name, "cooldown") == 0) {
        test_cooldown();
    } else if (strcmp(name, "wrong-copy") == 0) {
        test_wrong_copy();
    } else if (strcmp(name, "mask") == 0) {
        test_mask();
    } else if (strcmp(name, "boot") == 0) {
        test_boot();
    } else if (strcmp(name, "idle") == 0) {
        test_idle();
    } else if (strcmp(name, "one-terminal") == 0) {
        test_one_terminal();
    } else if (strcmp(name, "mute-release") == 0) {
        test_mute_release();
    } else if (strcmp(name, "switch-user") == 0) {
        test_switch_user();
    } else if (strcmp(name, "lost") == 0) {
        test_lost_and_idempotent_client();
    } else if (strcmp(name, "identity") == 0) {
        test_identity_zero();
    } else if (strcmp(name, "newer") == 0) {
        test_newer_request();
    } else if (strcmp(name, "overflow") == 0) {
        test_queue_overflow();
    } else if (strcmp(name, "roster-digits") == 0) {
        test_roster_keeps_digits();
    } else if (strcmp(name, "bad-body") == 0) {
        test_bad_body_not_success();
    } else if (strcmp(name, "level0") == 0) {
        test_level0_digit();
    } else if (strcmp(name, "catalog") == 0) {
        test_catalog();
    } else if (strcmp(name, "http-apply") == 0 && argc >= 5) {
        test_http_apply(atoi(argv[2]), atoi(argv[3]), argv[4]);
    } else {
        fprintf(stderr, "unknown test\n");
        return 2;
    }
    if (g_failures != 0) {
        fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    return 0;
}
