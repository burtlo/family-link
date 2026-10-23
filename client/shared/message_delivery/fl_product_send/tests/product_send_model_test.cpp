#include "fl_product_send_classify.hpp"
#include "fl_product_send_journey.hpp"
#include "fl_product_send_outbox.hpp"
#include "fl_product_send_p1_wire.hpp"
#include "fl_product_send_picker.hpp"
#include "fl_product_send_sha256.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL product_send_model_test.cpp:%d %s\n", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static uint8_t g_audio[32768];
static uint8_t g_sketch[64];

fl_product_send::FrozenPayload make_payload(const char *client_id, uint32_t audio_bytes, int sketch)
{
    fl_product_send::FrozenPayload payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.client_message_id, client_id, sizeof(payload.client_message_id) - 1);
    strncpy(payload.recipient_user_ids[0], "user-jordan", fl_product_send::kMaxUserId - 1);
    payload.recipient_count = 1;
    payload.audio_byte_count = audio_bytes;
    payload.duration_ms = audio_bytes / 32u;
    payload.sketch_present = sketch;
    payload.sketch_byte_count = sketch ? 8u : 0u;
    memset(g_audio, 0x5a, audio_bytes);
    memset(g_sketch, 1, sizeof(g_sketch));
    return payload;
}

int pump_once(fl_product_send::SendOutbox &outbox, int64_t now_ms,
              int (*exchange)(const fl_product_send::HttpAction *, fl_product_send::SendTransportResult *))
{
    fl_product_send::HttpAction action;
    fl_product_send::SendTransportResult result;
    if (!outbox.prepare_http_action(&action, now_ms)) {
        return 0;
    }
    memset(&result, 0, sizeof(result));
    if (exchange(&action, &result) != 0) {
        result.transport_ok = 0;
    } else {
        result.transport_ok = 1;
    }
    return outbox.apply_transport_result(action, result, now_ms);
}

int test_part_size_and_count()
{
    CHECK(fl_product_send::kPartSizeBytes == 16384);
    CHECK(fl_product_send::part_count_for_bytes(16384) == 1);
    CHECK(fl_product_send::part_count_for_bytes(16385) == 2);
    CHECK(fl_product_send::part_count_for_bytes(16000) == 1);
    return g_failures;
}

int test_lost_receipt_lookup()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *action,
                       fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\","
            "\"message_id\":\"msg-lost\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        static const char *receipt =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\","
            "\"message_id\":\"msg-lost\",\"state\":\"accepted\"}";
        result->http_status = 200;
        result->body_len = 0;
        result->body = "";
        if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != nullptr) {
            result->response_lost = 1;
            result->transport_ok = 1;
            return 0;
        }
        if (strcmp(action->method, "POST") == 0 && strcmp(action->path, "/v2/outgoing") == 0) {
            result->body = open;
            result->body_len = (int)strlen(open);
        } else if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
        } else if (strcmp(action->method, "GET") != 0) {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        } else {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        }
        result->response_lost = 0;
        result->transport_ok = 1;
        return 0;
    };
    for (int step = 0; step < 20 && outbox.ui_phase() != fl_product_send::kSendUiSent; ++step) {
        if (!pump_once(outbox, 0, exchange)) {
            break;
        }
    }
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiSent);
    return g_failures;
}

int test_identity_conflict_discard()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("bbbbbbbb-cccc-dddd-eeee-ffffffffffff", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *,
                       fl_product_send::SendTransportResult *result) -> int {
        result->transport_ok = 1;
        result->http_status = 409;
        result->body = "{\"error\":\"identity_conflict\"}";
        result->body_len = (int)strlen(result->body);
        return 0;
    };
    pump_once(outbox, 0, exchange);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiCouldntSend);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    return g_failures;
}

int test_offline_queue()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("cccccccc-dddd-eeee-ffff-000000000001", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.set_wifi(0);
    outbox.begin_submit(0);
    fl_product_send::HttpAction action;
    CHECK(outbox.prepare_http_action(&action, 0) == 0);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiQueuedWait);
    outbox.set_wifi(1);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    return g_failures;
}

int test_cancel_during_lookup()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("dddddddd-eeee-ffff-0000-111111111111", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    outbox.request_cancel();
    auto exchange = [](const fl_product_send::HttpAction *action,
                       fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"dddddddd-eeee-ffff-0000-111111111111\","
            "\"message_id\":\"msg-cancel\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != 0) {
            result->response_lost = 1;
            result->transport_ok = 1;
            return 0;
        }
        result->transport_ok = 1;
        result->http_status = 200;
        result->body = open;
        result->body_len = (int)strlen(open);
        if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
        }
        if (strcmp(action->method, "GET") != 0 && strstr(action->path, "/complete") == 0 &&
            strcmp(action->method, "POST") != 0) {
            /* lookup path */
        }
        return 0;
    };
    int guard = 0;
    while (outbox.ui_phase() != fl_product_send::kSendUiHidden && guard < 30) {
        pump_once(outbox, 0, exchange);
        if (outbox.terminal() == fl_product_send::kSendTerminalCancelled) {
            break;
        }
        guard += 1;
    }
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalCancelled);
    return g_failures;
}

int test_exp045_recipient_fallback()
{
    fl_product_send::HangoutMember member;
    memset(&member, 0, sizeof(member));
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    fl_product_send::RecipientCard card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 1);
    CHECK(card.use_card_fallback == 1);
    CHECK(strcmp(card.card_background_id, "card-bg-01") == 0);
    return g_failures;
}

int test_exp044_refresh_wait()
{
    fl_product_send::SendJourney journey;
    journey.set_refresh_failed(1);
    journey.set_wifi(1);
    auto payload = make_payload("eeeeeeee-ffff-0000-1111-222222222222", 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    journey.outbox().begin_submit(0);
    fl_product_send::HttpAction action;
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    return g_failures;
}

int test_exp046_inbox_fetch()
{
    fl_product_send::SendJourney journey;
    auto payload = make_payload("ffffffff-0000-1111-2222-333333333333", 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    journey.outbox().begin_submit(0);
    CHECK(journey.outbox().has_pending_http() == 1);
    CHECK(journey.inbox_fetch_may_proceed() == 1);
    return g_failures;
}

int test_mute_rules()
{
    fl_product_send::SendJourney journey;
    auto payload = make_payload("33333333-4444-5555-6666-777777777777", 16000, 0);
    journey.on_mute_during_capture();
    CHECK(journey.mute_discarded_in_progress() == 1);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    journey.outbox().begin_submit(0);
    journey.on_mute_after_finish(1);
    CHECK(journey.mute_held_finished() == 1);
    fl_product_send::HttpAction action;
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    journey.on_same_user_signed_in();
    CHECK(journey.mute_held_finished() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    return g_failures;
}

int test_malformed_success_wait()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("44444444-5555-6666-7777-888888888888", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *,
                       fl_product_send::SendTransportResult *result) -> int {
        result->transport_ok = 1;
        result->http_status = 200;
        result->body = "not-json-schema";
        result->body_len = (int)strlen(result->body);
        return 0;
    };
    pump_once(outbox, 0, exchange);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalNone);
    return g_failures;
}

int test_trace_no_media()
{
    fl_product_send::SendOutbox outbox;
    char line[fl_product_send::kTraceLineCap];
    auto payload = make_payload("11111111-2222-3333-4444-555555555555", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    CHECK(outbox.trace_line(line, (int)sizeof(line)) > 0);
    CHECK(strstr(line, "phase=preparing") != 0);
    CHECK(strstr(line, "5a") == 0);
    return g_failures;
}

int test_inbox_selection_restore()
{
    fl_product_send::SendJourney journey;
    fl_product_send::HangoutMember member;
    journey.remember_inbox_selection("msg-prior");
    memset(&member, 0, sizeof(member));
    strncpy(member.user_id, "user-jordan", sizeof(member.user_id) - 1);
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    CHECK(journey.picker().load_members(&member, 1, "user-maya") == 1);
    journey.picker().toggle_selection(0);
    auto payload = make_payload("22222222-3333-4444-5555-666666666666", 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    CHECK(journey.auto_submit_when_ready(0) == 1);
    CHECK(strcmp(journey.restored_inbox_selection(), "msg-prior") == 0);
    return g_failures;
}

int host_p8_drive()
{
    char line[512];
    fl_product_send::SendOutbox outbox;
    fl_product_send::FrozenPayload payload;
    memset(&payload, 0, sizeof(payload));
    printf("READY\n");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin) != 0) {
        if (strncmp(line, "BIND ", 5) == 0) {
            char client_id[64];
            unsigned audio_bytes = 0;
            sscanf(line + 5, "%63s %u", client_id, &audio_bytes);
            payload = make_payload(client_id, audio_bytes, 0);
            CHECK(outbox.bind_payload(payload, g_audio, 0));
            outbox.begin_submit(0);
            printf("BOUND\n");
            fflush(stdout);
        } else if (strncmp(line, "PUMP\n", 5) == 0) {
            fl_product_send::HttpAction action;
            if (!outbox.prepare_http_action(&action, 0)) {
                printf("IDLE phase=%s\n", fl_product_send::ui_phase_label(outbox.ui_phase()));
                fflush(stdout);
                continue;
            }
            printf("REQUEST %s %s %u\n", action.method, action.path, (unsigned)action.body_len);
            if (action.is_json && action.body_len > 0 && action.body != 0) {
                fwrite(action.body, 1, action.body_len, stdout);
                printf("\n");
            }
            fflush(stdout);
        } else if (strncmp(line, "RESULT ", 7) == 0) {
            int status = 0;
            int len = 0;
            int lost = 0;
            fl_product_send::SendTransportResult result;
            fl_product_send::HttpAction action;
            char body[fl_product_send::kHttpBodyCap];
            sscanf(line + 7, "%d %d %d", &status, &len, &lost);
            memset(body, 0, sizeof(body));
            if (len > 0 && len < (int)sizeof(body)) {
                if (fread(body, 1, (size_t)len, stdin) != (size_t)len) {
                    return 1;
                }
                if (fgetc(stdin) != '\n') {
                    return 1;
                }
            }
            memset(&result, 0, sizeof(result));
            result.http_status = status;
            result.body = body;
            result.body_len = len;
            result.transport_ok = lost ? 0 : 1;
            result.response_lost = lost ? 1 : 0;
            memset(&action, 0, sizeof(action));
            outbox.apply_transport_result(action, result, 0);
            printf("PHASE %s terminal=%d\n", fl_product_send::ui_phase_label(outbox.ui_phase()),
                   (int)outbox.terminal());
            fflush(stdout);
        } else if (strncmp(line, "QUIT\n", 5) == 0) {
            break;
        }
    }
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    if (strcmp(name, "part-size") == 0) {
        return test_part_size_and_count() == 0 ? 0 : 1;
    }
    if (strcmp(name, "lost-receipt") == 0) {
        return test_lost_receipt_lookup() == 0 ? 0 : 1;
    }
    if (strcmp(name, "identity-conflict") == 0) {
        return test_identity_conflict_discard() == 0 ? 0 : 1;
    }
    if (strcmp(name, "offline-queue") == 0) {
        return test_offline_queue() == 0 ? 0 : 1;
    }
    if (strcmp(name, "cancel-lookup") == 0) {
        return test_cancel_during_lookup() == 0 ? 0 : 1;
    }
    if (strcmp(name, "recipient-fallback") == 0) {
        return test_exp045_recipient_fallback() == 0 ? 0 : 1;
    }
    if (strcmp(name, "refresh-wait") == 0) {
        return test_exp044_refresh_wait() == 0 ? 0 : 1;
    }
    if (strcmp(name, "inbox-fetch") == 0) {
        return test_exp046_inbox_fetch() == 0 ? 0 : 1;
    }
    if (strcmp(name, "mute-rules") == 0) {
        return test_mute_rules() == 0 ? 0 : 1;
    }
    if (strcmp(name, "malformed-wait") == 0) {
        return test_malformed_success_wait() == 0 ? 0 : 1;
    }
    if (strcmp(name, "trace") == 0) {
        return test_trace_no_media() == 0 ? 0 : 1;
    }
    if (strcmp(name, "inbox-restore") == 0) {
        return test_inbox_selection_restore() == 0 ? 0 : 1;
    }
    if (strcmp(name, "host-p8-drive") == 0) {
        return host_p8_drive() == 0 ? 0 : 1;
    }
    test_part_size_and_count();
    test_lost_receipt_lookup();
    test_identity_conflict_discard();
    test_offline_queue();
    test_cancel_during_lookup();
    test_exp045_recipient_fallback();
    test_exp044_refresh_wait();
    test_exp046_inbox_fetch();
    test_mute_rules();
    test_trace_no_media();
    test_inbox_selection_restore();
    if (g_failures != 0) {
        fprintf(stderr, "product_send_model_test failures=%d\n", g_failures);
        return 1;
    }
    printf("product_send_model_test ok\n");
    return 0;
}
