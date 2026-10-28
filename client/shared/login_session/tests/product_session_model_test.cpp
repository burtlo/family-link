#include "fl_product_login_p1_wire.hpp"
#include "fl_product_login_session.hpp"
#include "fl_product_login_workers.hpp"

#include <stdio.h>
#include <string.h>

namespace
{

int g_failures = 0;

void check(bool ok, const char *text)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond)

const char *kTokenA = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

void establish(fl_product_login::SessionWriter *session)
{
    const char *kEight[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
    fl_product_login::RosterResult result;
    memset(&result, 0, sizeof(result));
    result.http_status = 200;
    result.valid = 1;
    result.parse_ok = 1;
    fl_product_login::fill_users(&result, kEight, 8);
    session->boot(0, true);
    if (session->roster_inflight()) {
        result.operation_id = session->roster_operation_id();
    }
    session->submit_roster(result, 0);
    session->ack_health();
}

fl_product_login::PinResult pin_success(fl_product_login::SessionWriter *session, const char *user,
                                        uint64_t operation_id)
{
    fl_product_login::PinResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    result.generation = session->pin_key_generation();
    strncpy(result.session_id, session->session_id(), sizeof(result.session_id) - 1);
    strncpy(result.user_id, user, sizeof(result.user_id) - 1);
    strncpy(result.session_token, kTokenA, sizeof(result.session_token) - 1);
    result.session_token_valid = 1;
    result.outcome = fl_product_login::kPinOutcomeSuccess;
    result.http_status = 200;
    result.parse_ok = 1;
    return result;
}

uint64_t submit_pin(fl_product_login::SessionWriter *session, const char *user)
{
    session->select_pin_user(user, session->now_ms());
    session->enter_pin_digit('1', session->now_ms());
    session->enter_pin_digit('2', session->now_ms());
    session->enter_pin_digit('3', session->now_ms());
    session->enter_pin_digit('4', session->now_ms());
    return session->pin_operation_id();
}

int test_refresh_extends_bound()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    uint64_t refresh_id;
    fl_product_login::RefreshResult refresh;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    CHECK(session.credential_active() == 1);
    CHECK(session.stage() == fl_product_login::kStageSignedIn);
    session.advance_to(session.now_ms() + fl_product_login::kSessionRefreshPeriodMs);
    CHECK(session.refresh_inflight() == 1);
    refresh_id = session.refresh_operation_id();
    memset(&refresh, 0, sizeof(refresh));
    refresh.operation_id = refresh_id;
    refresh.perform_ok = 1;
    refresh.http_status = 200;
    refresh.parse_ok = 1;
    session.deliver_refresh_completion(refresh, session.now_ms());
    CHECK(session.credential_active() == 1);
    session.advance_to(session.now_ms() + fl_product_login::kSessionCredentialBoundMs - 1000);
    CHECK(session.credential_expired() == 0);
    return g_failures;
}

int test_refresh_failure_stays_signed_in()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::RefreshResult refresh;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    session.advance_to(session.now_ms() + fl_product_login::kSessionRefreshPeriodMs);
    memset(&refresh, 0, sizeof(refresh));
    refresh.operation_id = session.refresh_operation_id();
    refresh.perform_ok = 1;
    refresh.http_status = 401;
    refresh.parse_ok = 0;
    session.deliver_refresh_completion(refresh, session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageSignedIn);
    CHECK(session.signed_in() == 1);
    return g_failures;
}

int test_bound_expires_credential()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    session.advance_to(session.now_ms() + fl_product_login::kSessionCredentialBoundMs + 1);
    CHECK(session.credential_expired() == 1);
    CHECK(session.credential_active() == 0);
    CHECK(session.stage() == fl_product_login::kStageSignedIn);
    return g_failures;
}

int test_mute_clears_and_invalidate_schedule()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::InvalidateTicket job;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    session.mute(session.now_ms());
    CHECK(session.credential_active() == 0);
    CHECK(session.stage() == fl_product_login::kStageMuted);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    CHECK(job.attempt == 1);
    session.deliver_invalidate_completion(99, 1, 503, 1, session.now_ms());
    CHECK(session.invalidate_attempts() == 1);
    session.advance_to(session.now_ms() + fl_product_login::kInvalidateRetryDelay1Ms);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    CHECK(job.attempt == 2);
    return g_failures;
}

int test_late_pin_after_mute()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::PinResult late;
    char focus_before[16];
    uint32_t pin_gen_before;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    strncpy(focus_before, session.focus_id(), sizeof(focus_before) - 1);
    focus_before[sizeof(focus_before) - 1] = 0;
    pin_gen_before = session.pin_key_generation();
    session.mute(session.now_ms());
    late = pin_success(&session, "su-01", pin_id);
    session.deliver_pin_completion(late, session.now_ms());
    CHECK(session.credential_active() == 0);
    CHECK(session.stage() == fl_product_login::kStageMuted);
    CHECK(session.pin_key_generation() == pin_gen_before);
    CHECK(strcmp(session.focus_id(), focus_before) == 0);
    CHECK(strcmp(session.terminal_for(pin_id), "stale") == 0 ||
          strcmp(session.terminal_for(pin_id), "cancelled") == 0);
    return g_failures;
}

int test_inbox_fence_after_switch()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::WorkerTicket ticket;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    ticket = fl_product_login::begin_inbox_worker(&session, session.now_ms());
    CHECK(ticket.operation_id != 0);
    session.sign_in("su-02", session.now_ms());
    CHECK(fl_product_login::deliver_worker_completion(&session, ticket, ticket.generation, 200,
                                                      session.now_ms()) == 0);
    CHECK(session.scoped_applied() == 0);
    return g_failures;
}

int test_send_worker_stale_after_mute()
{
    fl_product_login::SessionWriter session;
    fl_product_login::WorkerTicket ticket;
    char focus_before[16];
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    strncpy(focus_before, session.focus_id(), sizeof(focus_before) - 1);
    focus_before[sizeof(focus_before) - 1] = 0;
    ticket = fl_product_login::begin_send_worker(&session, session.now_ms());
    CHECK(ticket.operation_id != 0);
    session.mute(session.now_ms());
    CHECK(fl_product_login::deliver_worker_completion(&session, ticket, ticket.generation, 200,
                                                      session.now_ms()) == 0);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.focus_id(), focus_before) == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "stale") == 0 ||
          strcmp(session.terminal_for(ticket.operation_id), "cancelled") == 0);
    return g_failures;
}

int test_send_worker_stale_after_switch()
{
    fl_product_login::SessionWriter session;
    fl_product_login::WorkerTicket ticket;
    char focus_before[16];
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    strncpy(focus_before, session.focus_id(), sizeof(focus_before) - 1);
    focus_before[sizeof(focus_before) - 1] = 0;
    ticket = fl_product_login::begin_send_worker(&session, session.now_ms());
    session.sign_in("su-02", session.now_ms());
    CHECK(strcmp(session.signed_user(), "su-02") == 0);
    CHECK(fl_product_login::deliver_worker_completion(&session, ticket, ticket.generation, 200,
                                                      session.now_ms()) == 0);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.focus_id(), focus_before) == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "stale") == 0);
    return g_failures;
}

int test_health_ack_valid_only()
{
    fl_product_login::SessionWriter session;
    const char *ok =
        "{\"schema\":\"fl.endpoint-health-ack/2\",\"accepted\":true,\"server_time\":\"2026-10-23T00:00:00Z\"}";
    const char *invalid_with_time =
        "{\"schema\":\"fl.endpoint-health-ack/2\",\"accepted\":false,\"server_time\":\"2026-10-23T01:00:00Z\"}";
    g_failures = 0;
    establish(&session);
    session.advance_to(fl_product_login::kHeartbeatPeriodMs);
    CHECK(session.health_ack_pending() == 1);
    CHECK(session.health_ack_count() == 0);
    CHECK(session.health_server_time()[0] == 0);
    session.deliver_health_completion(1, 200, ok, (int)strlen(ok), session.now_ms());
    CHECK(session.health_ack_pending() == 0);
    CHECK(session.health_ack_count() == 1);
    CHECK(strcmp(session.health_server_time(), "2026-10-23T00:00:00Z") == 0);
    session.advance_to(session.now_ms() + fl_product_login::kHeartbeatPeriodMs);
    session.deliver_health_completion(1, 200, "{}", 2, session.now_ms());
    CHECK(session.health_ack_pending() == 1);
    CHECK(session.health_ack_count() == 1);
    CHECK(strcmp(session.health_server_time(), "2026-10-23T00:00:00Z") == 0);
    session.deliver_health_completion(1, 200, invalid_with_time, (int)strlen(invalid_with_time),
                                      session.now_ms());
    CHECK(session.health_ack_count() == 1);
    CHECK(strcmp(session.health_server_time(), "2026-10-23T00:00:00Z") == 0);
    return g_failures;
}

fl_product_login::ScopedResult scoped_success(const fl_product_login::ScopedTicket &ticket, int http_status)
{
    fl_product_login::ScopedResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = ticket.operation_id;
    result.generation = ticket.generation;
    strncpy(result.user_id, ticket.user_id, sizeof(result.user_id) - 1);
    strncpy(result.session_id, ticket.session_id, sizeof(result.session_id) - 1);
    strncpy(result.kind, ticket.kind, sizeof(result.kind) - 1);
    result.http_status = http_status;
    result.parse_ok = 1;
    result.body_ok = 1;
    return result;
}

int test_late_pin_after_sign_in_b()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::PinResult late;
    char focus_after_switch[16];
    uint32_t pin_gen_before;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    pin_gen_before = session.pin_key_generation();
    session.sign_in("su-02", session.now_ms());
    CHECK(strcmp(session.signed_user(), "su-02") == 0);
    CHECK(session.stage() == fl_product_login::kStageSignedIn);
    strncpy(focus_after_switch, session.focus_id(), sizeof(focus_after_switch) - 1);
    focus_after_switch[sizeof(focus_after_switch) - 1] = 0;
    late = pin_success(&session, "su-01", pin_id);
    session.deliver_pin_completion(late, session.now_ms());
    CHECK(strcmp(session.signed_user(), "su-02") == 0);
    CHECK(session.credential_active() == 0);
    CHECK(session.pin_key_generation() == pin_gen_before);
    CHECK(strcmp(session.focus_id(), focus_after_switch) == 0);
    CHECK(session.terminal_count_for(pin_id) == 1);
    CHECK(strcmp(session.terminal_for(pin_id), "stale") == 0 ||
          strcmp(session.terminal_for(pin_id), "cancelled") == 0);
    return g_failures;
}

int test_mute_nonblocking_invalidate()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::InvalidateTicket job;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    session.mute(session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageMuted);
    CHECK(session.credential_active() == 0);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    return g_failures;
}

int test_lost_invalidate_local()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_id;
    fl_product_login::InvalidateTicket job;
    g_failures = 0;
    establish(&session);
    pin_id = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_id), session.now_ms());
    session.mute(session.now_ms());
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    session.deliver_invalidate_completion(99, 1, 503, 1, session.now_ms());
    session.advance_to(session.now_ms() + fl_product_login::kInvalidateRetryDelay1Ms);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    session.deliver_invalidate_completion(99, 2, 503, 1, session.now_ms());
    session.advance_to(session.now_ms() + fl_product_login::kInvalidateRetryDelay2Ms);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    session.deliver_invalidate_completion(99, 3, 503, 1, session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageMuted);
    CHECK(session.credential_active() == 0);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 0);
    return g_failures;
}

int test_lab09_i_mute()
{
    fl_product_login::SessionWriter session;
    fl_product_login::ScopedTicket ticket;
    fl_product_login::ScopedResult result;
    int stage_before;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.arm_scoped_scenario("I-mute");
    ticket = session.begin_scoped_operation("inbox", session.now_ms());
    CHECK(ticket.operation_id != 0);
    session.mute(session.now_ms());
    stage_before = session.stage();
    CHECK(stage_before == fl_product_login::kStageMuted);
    result = scoped_success(ticket, 200);
    CHECK(session.deliver_scoped_completion(result, session.now_ms()) == 0);
    CHECK(session.stage() == stage_before);
    CHECK(session.scoped_applied() == 0);
    CHECK(result.generation == ticket.generation);
    CHECK(strcmp(session.focus_id(), "su-01") == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "stale") == 0 ||
          strcmp(session.terminal_for(ticket.operation_id), "cancelled") == 0);
    return g_failures;
}

int test_lab09_i_signout()
{
    fl_product_login::SessionWriter session;
    fl_product_login::ScopedTicket ticket;
    fl_product_login::ScopedResult result;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.arm_scoped_scenario("I-signout");
    ticket = session.begin_scoped_operation("profile", session.now_ms());
    session.sign_out(session.now_ms());
    result = scoped_success(ticket, 200);
    CHECK(session.deliver_scoped_completion(result, session.now_ms()) == 0);
    CHECK(session.stage() == fl_product_login::kStageSignedOut);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "cancelled") == 0);
    return g_failures;
}

int test_lab09_i_switch()
{
    fl_product_login::SessionWriter session;
    fl_product_login::ScopedTicket ticket;
    fl_product_login::ScopedResult result;
    int stage_b;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.arm_scoped_scenario("I-switch");
    ticket = session.begin_scoped_operation("inbox", session.now_ms());
    session.sign_in("su-02", session.now_ms());
    stage_b = session.stage();
    result = scoped_success(ticket, 200);
    CHECK(session.deliver_scoped_completion(result, session.now_ms()) == 0);
    CHECK(session.stage() == stage_b);
    CHECK(strcmp(session.signed_user(), "su-02") == 0);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "stale") == 0);
    return g_failures;
}

int test_lab09_i_push()
{
    fl_product_login::SessionWriter session;
    fl_product_login::ScopedTicket ticket;
    fl_product_login::ScopedResult result;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.arm_scoped_scenario("I-push");
    ticket = session.begin_scoped_operation("inbox", session.now_ms());
    result = scoped_success(ticket, 200);
    CHECK(session.deliver_scoped_completion(result, session.now_ms()) == 1);
    CHECK(session.scoped_applied() == 1);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "success") == 0);
    CHECK(session.terminal_count_for(ticket.operation_id) == 1);
    return g_failures;
}

int test_lab09_i_expire_client()
{
    fl_product_login::SessionWriter session;
    fl_product_login::ScopedTicket ticket;
    fl_product_login::ScopedResult result;
    int stage_before;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.arm_scoped_scenario("I-expire-client");
    ticket = session.begin_scoped_operation("media", session.now_ms());
    stage_before = session.stage();
    CHECK(session.scoped_generation() == ticket.generation);
    result = scoped_success(ticket, 401);
    result.expired = 1;
    CHECK(session.deliver_scoped_completion(result, session.now_ms()) == 0);
    CHECK(session.stage() == stage_before);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.terminal_for(ticket.operation_id), "failure") == 0);
    return g_failures;
}

int test_health_lost_signed_out()
{
    fl_product_login::SessionWriter session;
    int step;
    int stage_before;
    g_failures = 0;
    establish(&session);
    session.sign_in("su-01", session.now_ms());
    session.sign_out(session.now_ms());
    CHECK(session.stage() == fl_product_login::kStageSignedOut);
    stage_before = session.stage();
    for (step = 1; step <= 3; ++step) {
        session.advance_to((int64_t)step * fl_product_login::kHeartbeatPeriodMs);
        CHECK(session.stage() == fl_product_login::kStageSignedOut);
        CHECK(session.stage() == stage_before);
        session.deliver_health_completion(1, 200, "{}", 2, session.now_ms());
        CHECK(session.stage() == fl_product_login::kStageSignedOut);
        CHECK(session.health_ack_count() == 0);
        CHECK(session.health_server_time()[0] == 0);
    }
    return g_failures;
}

int test_inbox_stale_prior_user()
{
    fl_product_login::SessionWriter session;
    uint64_t pin_a;
    fl_product_login::WorkerTicket ticket_a;
    g_failures = 0;
    establish(&session);
    pin_a = submit_pin(&session, "su-01");
    session.deliver_pin_completion(pin_success(&session, "su-01", pin_a), session.now_ms());
    ticket_a = fl_product_login::begin_inbox_worker(&session, session.now_ms());
    CHECK(ticket_a.operation_id != 0);
    session.sign_in("su-02", session.now_ms());
    CHECK(strcmp(session.signed_user(), "su-02") == 0);
    CHECK(fl_product_login::deliver_worker_completion(&session, ticket_a, ticket_a.generation, 200,
                                                      session.now_ms()) == 0);
    CHECK(session.scoped_applied() == 0);
    CHECK(strcmp(session.terminal_for(ticket_a.operation_id), "stale") == 0);
    return g_failures;
}

bool load_body_file(const char *path, char *buffer, int capacity, int *length)
{
    FILE *file = fopen(path, "rb");
    int n;
    if (file == 0 || buffer == 0 || capacity <= 0) {
        return false;
    }
    n = (int)fread(buffer, 1, (size_t)(capacity - 1), file);
    fclose(file);
    if (n < 0) {
        return false;
    }
    buffer[n] = 0;
    if (length != 0) {
        *length = n;
    }
    return true;
}

bool apply_hangout_file(fl_product_login::SessionWriter *session, const char *path)
{
    char body[16384];
    int len = 0;
    uint64_t operation_id;
    if (!load_body_file(path, body, (int)sizeof(body), &len)) {
        return false;
    }
    session->boot(0, true);
    operation_id = session->roster_operation_id();
    fl_product_login::deliver_classified_roster(session, operation_id, 1, 200, body, len, session->now_ms());
    session->ack_health();
    return true;
}

uint64_t pin_from_login_file(fl_product_login::SessionWriter *session, const char *roster_path, const char *user_id,
                             const char *pin, const char *login_path)
{
    char body[4096];
    int len = 0;
    uint64_t pin_id;
    if (!apply_hangout_file(session, roster_path)) {
        return 0;
    }
    pin_id = session->begin_pin_request(user_id, pin, session->now_ms());
    if (pin_id == 0 || !load_body_file(login_path, body, (int)sizeof(body), &len)) {
        return 0;
    }
    fl_product_login::deliver_classified_pin(session, pin_id, session->pin_key_generation(), session->session_id(),
                                             user_id, 1, 200, body, len, session->now_ms());
    return pin_id;
}

int host_p5_mute_after_credential(const char *roster_path, const char *user_id, const char *pin)
{
    fl_product_login::SessionWriter session;
    char response_path[512];
    char body[4096];
    int len = 0;
    char operation_text[40];
    uint64_t pin_id;
    fl_product_login::InvalidateTicket job;
    int i;
    g_failures = 0;
    if (!apply_hangout_file(&session, roster_path)) {
        fprintf(stderr, "missing roster %s\n", roster_path);
        return 1;
    }
    pin_id = session.begin_pin_request(user_id, pin, session.now_ms());
    CHECK(pin_id != 0);
    fl_product_login::format_p1_operation_id(pin_id, operation_text, (int)sizeof(operation_text));
    printf("operation_id=%s\n", operation_text);
    fflush(stdout);
    if (fgets(response_path, (int)sizeof(response_path), stdin) == 0) {
        fprintf(stderr, "missing response path\n");
        return 1;
    }
    for (i = 0; response_path[i] != 0; ++i) {
        if (response_path[i] == '\n' || response_path[i] == '\r') {
            response_path[i] = 0;
            break;
        }
    }
    if (!load_body_file(response_path, body, (int)sizeof(body), &len)) {
        fprintf(stderr, "missing response %s\n", response_path);
        return 1;
    }
    fl_product_login::deliver_classified_pin(&session, pin_id, session.pin_key_generation(), session.session_id(),
                                             user_id, 1, 200, body, len, session.now_ms());
    CHECK(session.credential_active() == 1);
    session.mute(session.now_ms());
    CHECK(session.credential_active() == 0);
    job = session.take_invalidate_job(session.now_ms());
    CHECK(job.active == 1);
    printf("credential_active=%d invalidate_active=%d\n", session.credential_active(), job.active);
    return g_failures;
}

int host_p5_inbox_stale_user(const char *roster_path, const char *user_a, const char *pin_a,
                             const char *login_a_path, const char *user_b)
{
    fl_product_login::SessionWriter session;
    fl_product_login::WorkerTicket ticket_a;
    g_failures = 0;
    if (pin_from_login_file(&session, roster_path, user_a, pin_a, login_a_path) == 0) {
        fprintf(stderr, "pin A failed\n");
        return 1;
    }
    ticket_a = fl_product_login::begin_inbox_worker(&session, session.now_ms());
    CHECK(ticket_a.operation_id != 0);
    session.sign_in(user_b, session.now_ms());
    CHECK(strcmp(session.signed_user(), user_b) == 0);
    CHECK(fl_product_login::deliver_worker_completion(&session, ticket_a, ticket_a.generation, 200,
                                                      session.now_ms()) == 0);
    CHECK(session.scoped_applied() == 0);
    printf("stale_terminal=%s scoped_applied=%d\n", session.terminal_for(ticket_a.operation_id),
           session.scoped_applied());
    return g_failures;
}

int host_p5_late_pin_after_mute(const char *roster_path, const char *user_id, const char *pin)
{
    fl_product_login::SessionWriter session;
    char body[4096];
    char response_path[512];
    int len = 0;
    char operation_text[40];
    uint64_t pin_id;
    int i;
    g_failures = 0;
    if (!apply_hangout_file(&session, roster_path)) {
        fprintf(stderr, "missing roster %s\n", roster_path);
        return 1;
    }
    pin_id = session.begin_pin_request(user_id, pin, session.now_ms());
    CHECK(pin_id != 0);
    fl_product_login::format_p1_operation_id(pin_id, operation_text, (int)sizeof(operation_text));
    printf("operation_id=%s\n", operation_text);
    fflush(stdout);
    if (fgets(response_path, (int)sizeof(response_path), stdin) == 0) {
        fprintf(stderr, "missing response path\n");
        return 1;
    }
    for (i = 0; response_path[i] != 0; ++i) {
        if (response_path[i] == '\n' || response_path[i] == '\r') {
            response_path[i] = 0;
            break;
        }
    }
    if (!load_body_file(response_path, body, (int)sizeof(body), &len)) {
        fprintf(stderr, "missing response %s\n", response_path);
        return 1;
    }
    session.mute(session.now_ms());
    fl_product_login::deliver_classified_pin(&session, pin_id, session.pin_key_generation(), session.session_id(),
                                             user_id, 1, 200, body, len, session.now_ms());
    printf("stage=%s credential_active=%d\n", session.stage_name(), session.credential_active());
    CHECK(session.stage() == fl_product_login::kStageMuted);
    CHECK(session.credential_active() == 0);
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    int failures = 0;
    if (strcmp(name, "refresh-bound") == 0) {
        failures = test_refresh_extends_bound();
    } else if (strcmp(name, "refresh-fail") == 0) {
        failures = test_refresh_failure_stays_signed_in();
    } else if (strcmp(name, "expire-bound") == 0) {
        failures = test_bound_expires_credential();
    } else if (strcmp(name, "mute-invalidate") == 0) {
        failures = test_mute_clears_and_invalidate_schedule();
    } else if (strcmp(name, "late-pin-mute") == 0) {
        failures = test_late_pin_after_mute();
    } else if (strcmp(name, "inbox-fence") == 0) {
        failures = test_inbox_fence_after_switch();
    } else if (strcmp(name, "send-fence-mute") == 0) {
        failures = test_send_worker_stale_after_mute();
    } else if (strcmp(name, "send-fence-switch") == 0) {
        failures = test_send_worker_stale_after_switch();
    } else if (strcmp(name, "inbox-stale-user") == 0) {
        failures = test_inbox_stale_prior_user();
    } else if (strcmp(name, "health-ack") == 0) {
        failures = test_health_ack_valid_only();
    } else if (strcmp(name, "late-pin-b") == 0) {
        failures = test_late_pin_after_sign_in_b();
    } else if (strcmp(name, "mute-invalidate-local") == 0) {
        failures = test_mute_nonblocking_invalidate();
        failures += test_lost_invalidate_local();
    } else if (strcmp(name, "lab09-scenarios") == 0) {
        failures += test_lab09_i_mute();
        failures += test_lab09_i_signout();
        failures += test_lab09_i_switch();
        failures += test_lab09_i_push();
        failures += test_lab09_i_expire_client();
    } else if (strcmp(name, "health-signed-out") == 0) {
        failures = test_health_lost_signed_out();
    } else if (strcmp(name, "host-p5-late-pin-mute") == 0) {
        if (argc < 5) {
            fprintf(stderr, "usage: host-p5-late-pin-mute <roster> <user> <pin>\n");
            return 2;
        }
        failures = host_p5_late_pin_after_mute(argv[2], argv[3], argv[4]);
    } else if (strcmp(name, "host-p5-mute-credential") == 0) {
        if (argc < 5) {
            fprintf(stderr, "usage: host-p5-mute-credential <roster> <user> <pin>\n");
            return 2;
        }
        failures = host_p5_mute_after_credential(argv[2], argv[3], argv[4]);
    } else if (strcmp(name, "host-p5-inbox-stale") == 0) {
        if (argc < 7) {
            fprintf(stderr, "usage: host-p5-inbox-stale <roster> <userA> <pinA> <loginA> <userB>\n");
            return 2;
        }
        failures = host_p5_inbox_stale_user(argv[2], argv[3], argv[4], argv[5], argv[6]);
    } else if (strcmp(name, "all") == 0) {
        failures += test_refresh_extends_bound();
        failures += test_refresh_failure_stays_signed_in();
        failures += test_bound_expires_credential();
        failures += test_mute_clears_and_invalidate_schedule();
        failures += test_late_pin_after_mute();
        failures += test_inbox_fence_after_switch();
        failures += test_send_worker_stale_after_mute();
        failures += test_send_worker_stale_after_switch();
        failures += test_inbox_stale_prior_user();
        failures += test_health_ack_valid_only();
        failures += test_late_pin_after_sign_in_b();
        failures += test_mute_nonblocking_invalidate();
        failures += test_lost_invalidate_local();
        failures += test_lab09_i_mute();
        failures += test_lab09_i_signout();
        failures += test_lab09_i_switch();
        failures += test_lab09_i_push();
        failures += test_lab09_i_expire_client();
        failures += test_health_lost_signed_out();
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
