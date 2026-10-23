#include "fl_product_send_classify.hpp"
#include "fl_product_send_journey.hpp"
#include "fl_product_send_outbox.hpp"
#include "fl_product_send_stage.hpp"
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
const char kTestSender[] = "user-maya";

void fill_member(fl_product_send::HangoutMember *member, const char *id, const char *name, const char *avatar,
                 const char *card)
{
    memset(member, 0, sizeof(*member));
    if (id != 0) {
        strncpy(member->user_id, id, sizeof(member->user_id) - 1);
    }
    if (name != 0) {
        strncpy(member->display_name, name, sizeof(member->display_name) - 1);
    }
    if (avatar != 0) {
        strncpy(member->avatar_id, avatar, sizeof(member->avatar_id) - 1);
    }
    if (card != 0) {
        strncpy(member->card_background_id, card, sizeof(member->card_background_id) - 1);
    }
}

void read_header(fl_product_send::RecipientPicker &picker, char *out, int cap)
{
    picker.header_text(out, cap);
}

void sign_in_sender(fl_product_send::SendOutbox &outbox) { outbox.set_signed_in_user(kTestSender); }

fl_product_send::FrozenPayload make_payload(const char *client_id, uint32_t audio_bytes, int sketch)
{
    fl_product_send::FrozenPayload payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.client_message_id, client_id, sizeof(payload.client_message_id) - 1);
    strncpy(payload.sender_user_id, kTestSender, sizeof(payload.sender_user_id) - 1);
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
    sign_in_sender(outbox);
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
    sign_in_sender(outbox);
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
    sign_in_sender(outbox);
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
    const char *client_id = "dddddddd-eeee-ffff-0000-111111111111";
    auto payload = make_payload(client_id, 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *action,
                       fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"dddddddd-eeee-ffff-0000-111111111111\","
            "\"message_id\":\"msg-cancel\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        static const char *receipt =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"dddddddd-eeee-ffff-0000-111111111111\","
            "\"message_id\":\"msg-cancel\",\"state\":\"accepted\"}";
        result->http_status = 200;
        result->response_lost = 0;
        result->body = open;
        result->body_len = (int)strlen(open);
        if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != 0) {
            result->response_lost = 1;
            result->body = "";
            result->body_len = 0;
            return 0;
        }
        if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
            return 0;
        }
        if (strcmp(action->method, "GET") == 0) {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        }
        return 0;
    };
    int lookups = 0;
    for (int step = 0; step < 12 && outbox.ui_phase() != fl_product_send::kSendUiSent; ++step) {
        fl_product_send::HttpAction action;
        fl_product_send::SendTransportResult result;
        if (!outbox.prepare_http_action(&action, 0)) {
            break;
        }
        if (strcmp(action.method, "GET") == 0) {
            fl_product_send::HttpAction again;
            lookups += 1;
            outbox.request_cancel();
            CHECK(outbox.prepare_http_action(&again, 0) == 1);
            CHECK(strcmp(again.method, "GET") == 0);
            CHECK(outbox.left_wait() == 0);
            CHECK(outbox.terminal() != fl_product_send::kSendTerminalCancelled);
        }
        memset(&result, 0, sizeof(result));
        if (exchange(&action, &result) != 0) {
            result.transport_ok = 0;
        } else {
            result.transport_ok = 1;
        }
        outbox.apply_transport_result(action, result, 0);
    }
    CHECK(lookups >= 1);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalAccepted);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiSent);
    CHECK(strcmp(outbox.client_message_id(), client_id) == 0);
    return g_failures;
}

int test_exp045_recipient_fallback()
{
    fl_product_send::HangoutMember member;
    fl_product_send::RecipientPicker picker;
    fl_product_send::RecipientCard card;
    memset(&member, 0, sizeof(member));
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 1);
    CHECK(card.use_card_fallback == 1);
    CHECK(strcmp(card.card_background_id, "card-bg-01") == 0);

    fill_member(&member, "user-jordan", "Jordan", "avatar-nope", "card-bg-99");
    card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 1);
    CHECK(card.use_card_fallback == 1);
    CHECK(strcmp(card.card_background_id, "card-bg-01") == 0);
    CHECK(picker.load_members(&member, 1, kTestSender) == 1);
    picker.toggle_selection(0);
    CHECK(picker.is_selected(0) == 1);
    CHECK(picker.selected_count() == 1);
    card = picker.card_for_index(0);
    CHECK(card.use_initials_fallback == 1);
    CHECK(card.use_card_fallback == 1);

    fill_member(&member, "user-jordan", "Jordan", "avatar-01", "card-bg-06");
    card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 0);
    CHECK(card.use_card_fallback == 0);
    CHECK(strcmp(card.avatar_id, "avatar-01") == 0);
    CHECK(strcmp(card.card_background_id, "card-bg-06") == 0);

    fill_member(&member, "user-jordan", "Jordan", "avatar-08", "card-bg-08");
    card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 0);
    CHECK(card.use_card_fallback == 0);
    CHECK(strcmp(card.card_background_id, "card-bg-08") == 0);

    fill_member(&member, "user-jordan", "Jordan", "avatar-zz", "card-bg-02");
    card = fl_product_send::resolve_recipient_fallback(member);
    CHECK(card.use_initials_fallback == 1);
    CHECK(card.use_card_fallback == 0);
    CHECK(strcmp(card.card_background_id, "card-bg-02") == 0);
    return g_failures;
}

int test_picker_header()
{
    fl_product_send::RecipientPicker picker;
    fl_product_send::HangoutMember members[4];
    char header[96];
    fill_member(&members[0], "user-jordan", "Jordan", "avatar-01", "card-bg-02");
    fill_member(&members[1], "user-sam", "Sam", "avatar-02", "card-bg-03");
    fill_member(&members[2], "user-alex", "Alexandria", "avatar-03", "card-bg-04");
    fill_member(&members[3], "user-chris", "Christopher", "avatar-04", "card-bg-05");
    CHECK(picker.load_members(members, 4, kTestSender) == 4);
    CHECK(picker.selected_count() == 0);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "Send a message to ...") == 0);

    picker.toggle_selection(0);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Jordan") == 0);

    picker.toggle_selection(1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Jordan, Sam") == 0);

    picker.toggle_selection(0);
    picker.toggle_selection(1);
    picker.toggle_selection(1);
    picker.toggle_selection(0);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Sam, Jordan") == 0);

    picker.toggle_selection(0);
    picker.toggle_selection(1);
    picker.toggle_selection(3);
    picker.toggle_selection(2);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Christopher +1") == 0);

    picker.reset();
    fill_member(&members[0], "user-mary", "Mary Jane", "avatar-01", "card-bg-01");
    fill_member(&members[1], "user-alex", "Alexandria Extra", "avatar-02", "card-bg-02");
    CHECK(picker.load_members(members, 2, kTestSender) == 2);
    picker.toggle_selection(0);
    picker.toggle_selection(1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Mary +1") == 0);

    picker.reset();
    fill_member(&members[0], "user-long", "Christopher Alexander!", "avatar-01", "card-bg-01");
    CHECK(picker.load_members(members, 1, kTestSender) == 1);
    picker.toggle_selection(0);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Christopher Alexander!") == 0);
    picker.reset();
    fill_member(&members[0], "user-long", "Christopher Alexander!!", "avatar-01", "card-bg-01");
    CHECK(picker.load_members(members, 1, kTestSender) == 1);
    picker.toggle_selection(0);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Christopher +0") == 0);
    return g_failures;
}

int test_picker_focus()
{
    fl_product_send::RecipientPicker picker;
    fl_product_send::HangoutMember members[3];
    fill_member(&members[0], "user-jordan", "Jordan", "avatar-01", "card-bg-02");
    fill_member(&members[1], "user-sam", "Sam", "avatar-02", "card-bg-03");
    fill_member(&members[2], kTestSender, "Maya", "avatar-03", "card-bg-04");
    CHECK(picker.load_members(members, 3, kTestSender) == 2);
    CHECK(picker.member_count() == 2);
    CHECK(strcmp(picker.user_id_at(0), "user-jordan") == 0);
    CHECK(strcmp(picker.user_id_at(1), "user-sam") == 0);
    CHECK(picker.selected_count() == 0);
    CHECK(picker.empty_copy_shown() == 0);
    CHECK(picker.focused_index() == 0);
    picker.set_focus(1);
    CHECK(picker.focused_index() == 1);
    CHECK(picker.selected_count() == 0);
    CHECK(picker.is_selected(0) == 0);
    CHECK(picker.is_selected(1) == 0);
    picker.toggle_selection(0);
    CHECK(picker.is_selected(0) == 1);
    CHECK(picker.focused_index() == 1);
    picker.set_focus(0);
    CHECK(picker.is_selected(0) == 1);
    CHECK(picker.is_selected(1) == 0);
    CHECK(picker.focused_index() == 0);
    picker.set_focus(5);
    CHECK(picker.focused_index() == 0);
    CHECK(picker.selected_count() == 1);

    picker.reset();
    fill_member(&members[0], kTestSender, "Maya", "avatar-01", "card-bg-01");
    CHECK(picker.load_members(members, 1, kTestSender) == 0);
    CHECK(picker.empty_copy_shown() == 1);
    CHECK(picker.selected_count() == 0);
    CHECK(picker.member_count() == 0);
    return g_failures;
}

int test_picker_refresh()
{
    fl_product_send::RecipientPicker picker;
    fl_product_send::HangoutMember members[4];
    char header[96];
    fill_member(&members[0], "user-jordan", "Jordan", "avatar-01", "card-bg-02");
    fill_member(&members[1], "user-sam", "Sam", "avatar-02", "card-bg-03");
    CHECK(picker.load_members(members, 2, kTestSender) == 2);
    picker.toggle_selection(1);
    picker.toggle_selection(0);
    picker.set_focus(1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Sam, Jordan") == 0);

    fill_member(&members[0], "user-jordan", "Jordan", "avatar-01", "card-bg-02");
    fill_member(&members[1], "user-sam", "Sam", "avatar-02", "card-bg-03");
    fill_member(&members[2], "user-riley", "Riley", "avatar-05", "card-bg-05");
    CHECK(picker.refresh_members(members, 3, kTestSender) == 3);
    CHECK(picker.is_selected(0) == 1);
    CHECK(picker.is_selected(1) == 1);
    CHECK(picker.is_selected(2) == 0);
    CHECK(picker.selected_count() == 2);
    CHECK(picker.focused_index() == 1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Sam, Jordan") == 0);
    CHECK(picker.empty_copy_shown() == 0);

    fill_member(&members[0], "user-sam", "Sam", "avatar-02", "card-bg-03");
    fill_member(&members[1], "user-riley", "Riley", "avatar-05", "card-bg-05");
    CHECK(picker.refresh_members(members, 2, kTestSender) == 2);
    CHECK(picker.member_count() == 2);
    CHECK(strcmp(picker.user_id_at(0), "user-sam") == 0);
    CHECK(strcmp(picker.user_id_at(1), "user-riley") == 0);
    CHECK(picker.is_selected(0) == 1);
    CHECK(picker.is_selected(1) == 0);
    CHECK(picker.selected_count() == 1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "To Sam") == 0);
    CHECK(picker.empty_copy_shown() == 0);

    fill_member(&members[0], kTestSender, "Maya", "avatar-03", "card-bg-01");
    CHECK(picker.refresh_members(members, 1, kTestSender) == 0);
    CHECK(picker.member_count() == 0);
    CHECK(picker.selected_count() == 0);
    CHECK(picker.empty_copy_shown() == 1);
    read_header(picker, header, (int)sizeof(header));
    CHECK(strcmp(header, "Send a message to ...") == 0);
    return g_failures;
}

int test_exp044_refresh_wait()
{
    fl_product_send::SendJourney journey;
    journey.set_refresh_failed(1);
    journey.set_wifi(1);
    auto payload = make_payload("eeeeeeee-ffff-0000-1111-222222222222", 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.outbox().begin_submit(0);
    fl_product_send::HttpAction action;
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(strcmp(journey.body_label(), "connecting") == 0);
    {
        char kept[fl_product_send::kMaxClientMessageId];
        strncpy(kept, journey.outbox().client_message_id(), sizeof(kept) - 1);
        kept[sizeof(kept) - 1] = '\0';
        journey.set_refresh_failed(0);
        CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
        CHECK(strcmp(journey.outbox().client_message_id(), kept) == 0);
        CHECK(strcmp(action.method, "POST") == 0);
        CHECK(strcmp(action.path, "/v2/outgoing") == 0);
    }
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
    journey.on_same_user_signed_in(kTestSender);
    CHECK(journey.mute_held_finished() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    return g_failures;
}

int test_malformed_success_wait()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("44444444-5555-6666-7777-888888888888", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
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

int test_exp010_empty_copy()
{
    fl_product_send::RecipientPicker picker;
    fl_product_send::HangoutMember member;
    fl_product_send::RecipientCard card;
    memset(&member, 0, sizeof(member));
    strncpy(member.user_id, "user-jordan", sizeof(member.user_id) - 1);
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    CHECK(picker.load_members(&member, 1, kTestSender) == 1);
    CHECK(picker.selected_count() == 0);
    CHECK(picker.empty_copy_shown() == 0);
    card = picker.card_for_index(0);
    CHECK(strcmp(card.display_name, "Jordan") == 0);
    CHECK(picker.load_members(&member, 1, "user-jordan") == 0);
    CHECK(picker.empty_copy_shown() == 1);
    CHECK(picker.load_members(0, 0, kTestSender) == 0);
    CHECK(picker.empty_copy_shown() == 1);
    return g_failures;
}

int test_exp028_wait_body()
{
    fl_product_send::SendOutbox outbox;
    fl_product_send::SendJourney journey;
    fl_product_send::HttpAction action;
    fl_product_send::SendTransportResult result;
    const char *client_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    const char *open =
        "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\","
        "\"message_id\":\"msg-body\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
    auto payload = make_payload(client_id, 16000, 0);
    CHECK(strcmp(fl_product_send::ui_phase_label(fl_product_send::kSendUiPreparing), "preparing") == 0);
    CHECK(strcmp(fl_product_send::ui_phase_label(fl_product_send::kSendUiSending), "sending") == 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(outbox.body_label(), "preparing") == 0);
    memset(&result, 0, sizeof(result));
    result.transport_ok = 1;
    result.http_status = 200;
    result.body = open;
    result.body_len = (int)strlen(open);
    CHECK(outbox.apply_transport_result(action, result, 0) == 1);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "PUT") == 0);
    CHECK(strcmp(outbox.body_label(), "sending") == 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.set_wifi(0);
    journey.outbox().begin_submit(0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(strcmp(journey.body_label(), "no Wi-Fi") == 0);
    journey.set_wifi(1);
    journey.set_refresh_failed(1);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(strcmp(journey.body_label(), "connecting") == 0);
    return g_failures;
}

int test_exp014_receipt_state()
{
    fl_product_send::SendOutbox outbox;
    fl_product_send::SendOutbox lookup;
    fl_product_send::SendOutbox unknown;
    const char *client_id = "12121212-3434-5656-7878-909090909090";
    const char *lookup_id = "13131313-3434-5656-7878-909090909090";
    auto payload = make_payload(client_id, 16000, 0);
    auto lookup_payload = make_payload(lookup_id, 16000, 0);
    auto unknown_payload = make_payload("14141414-3434-5656-7878-909090909090", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *action,
                       fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"12121212-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-open\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        static const char *receipt =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"12121212-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-open\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":1}}";
        result->http_status = 200;
        result->response_lost = 0;
        result->body = open;
        result->body_len = (int)strlen(open);
        if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
        } else if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != 0) {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        }
        return 0;
    };
    for (int step = 0; step < 6 && outbox.ui_phase() != fl_product_send::kSendUiQueuedWait; ++step) {
        pump_once(outbox, 0, exchange);
    }
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalNone);
    CHECK(outbox.ui_phase() != fl_product_send::kSendUiSent);

    CHECK(lookup.bind_payload(lookup_payload, g_audio, 0));
    sign_in_sender(lookup);
    lookup.begin_submit(0);
    auto lookup_exchange = [](const fl_product_send::HttpAction *action,
                              fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"13131313-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-look\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        static const char *receipt =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"13131313-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-look\",\"state\":\"published\",\"audio\":{\"parts_acknowledged\":1}}";
        result->http_status = 200;
        result->response_lost = 0;
        result->body = open;
        result->body_len = (int)strlen(open);
        if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != 0) {
            result->response_lost = 1;
            result->body = "";
            result->body_len = 0;
        } else if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
        } else if (strcmp(action->method, "GET") == 0) {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        }
        return 0;
    };
    for (int step = 0; step < 8; ++step) {
        fl_product_send::HttpAction action;
        if (!lookup.prepare_http_action(&action, 0)) {
            break;
        }
        pump_once(lookup, 0, lookup_exchange);
        if (strcmp(action.method, "GET") == 0 && lookup.ui_phase() == fl_product_send::kSendUiQueuedWait) {
            break;
        }
    }
    CHECK(lookup.ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(lookup.terminal() == fl_product_send::kSendTerminalNone);
    CHECK(lookup.ui_phase() != fl_product_send::kSendUiSent);

    CHECK(unknown.bind_payload(unknown_payload, g_audio, 0));
    sign_in_sender(unknown);
    unknown.begin_submit(0);
    auto unknown_exchange = [](const fl_product_send::HttpAction *,
                               fl_product_send::SendTransportResult *result) -> int {
        result->http_status = 200;
        result->body = "{\"schema\":\"fl.not-a-receipt/9\",\"state\":\"accepted\"}";
        result->body_len = (int)strlen(result->body);
        return 0;
    };
    pump_once(unknown, 0, unknown_exchange);
    CHECK(unknown.ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(unknown.terminal() == fl_product_send::kSendTerminalNone);
    CHECK(unknown.ui_phase() != fl_product_send::kSendUiSent);
    return g_failures;
}

int test_exp041_http_415()
{
    fl_product_send::SendOutbox outbox;
    auto payload = make_payload("15151515-3434-5656-7878-909090909090", 16000, 0);
    CHECK(fl_product_send::classify_http_result(1, 1, 415, "no", 2, 0) == fl_product_send::kCom013Discard);
    CHECK(fl_product_send::classify_http_result(1, 1, 413, "no", 2, 0) == fl_product_send::kCom013Discard);
    CHECK(fl_product_send::classify_http_result(1, 1, 422, "no", 2, 0) == fl_product_send::kCom013Discard);
    CHECK(fl_product_send::classify_http_result(1, 1, 409, "no", 2, 0) == fl_product_send::kCom013Discard);
    CHECK(fl_product_send::classify_http_result(1, 1, 400, "no", 2, 0) == fl_product_send::kCom013Discard);
    CHECK(fl_product_send::classify_http_result(1, 1, 401, "no", 2, 0) == fl_product_send::kCom013Queue);
    CHECK(fl_product_send::classify_http_result(1, 1, 403, "no", 2, 0) == fl_product_send::kCom013Queue);
    CHECK(fl_product_send::classify_http_result(1, 1, 500, "no", 2, 0) == fl_product_send::kCom013Queue);
    CHECK(fl_product_send::classify_http_result(0, 1, 415, "no", 2, 0) == fl_product_send::kCom013Queue);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *,
                       fl_product_send::SendTransportResult *result) -> int {
        result->http_status = 415;
        result->body = "{\"error\":\"unsupported_media\"}";
        result->body_len = (int)strlen(result->body);
        return 0;
    };
    pump_once(outbox, 0, exchange);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiCouldntSend);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    CHECK(outbox.ui_phase() != fl_product_send::kSendUiSent);
    return g_failures;
}

int test_exp034_couldnt_send_dwell()
{
    fl_product_send::SendOutbox outbox;
    fl_product_send::HttpAction action;
    auto payload = make_payload("16161616-3434-5656-7878-909090909090", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *,
                       fl_product_send::SendTransportResult *result) -> int {
        result->http_status = 409;
        result->body = "{\"error\":\"identity_conflict\"}";
        result->body_len = (int)strlen(result->body);
        return 0;
    };
    pump_once(outbox, 0, exchange);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiCouldntSend);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    outbox.advance_to(fl_product_send::kSentHoldMs - 1);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiCouldntSend);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    CHECK(outbox.prepare_http_action(&action, fl_product_send::kSentHoldMs - 1) == 0);
    outbox.advance_to(fl_product_send::kSentHoldMs);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    CHECK(outbox.prepare_http_action(&action, fl_product_send::kSentHoldMs) == 0);
    outbox.advance_to(fl_product_send::kSentHoldMs + 5000);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(outbox.terminal() == fl_product_send::kSendTerminalDiscarded);
    return g_failures;
}

int test_exp033_sender_binding()
{
    fl_product_send::SendJourney journey;
    fl_product_send::HttpAction action;
    auto payload = make_payload("17171717-3434-5656-7878-909090909090", 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    journey.outbox().begin_submit(0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    journey.set_signed_in_user("user-sam");
    CHECK(journey.outbox().signed_in_matches_sender() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    journey.on_mute_after_finish(1);
    journey.set_signed_in_user(kTestSender);
    CHECK(journey.mute_held_finished() == 1);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    journey.on_same_user_signed_in("user-sam");
    CHECK(journey.mute_held_finished() == 1);
    CHECK(journey.outbox().signed_in_matches_sender() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
    journey.on_same_user_signed_in(kTestSender);
    CHECK(journey.mute_held_finished() == 0);
    CHECK(journey.outbox().signed_in_matches_sender() == 1);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "POST") == 0);
    return g_failures;
}

int test_exp031_leave_wait_skips_sent()
{
    fl_product_send::SendJourney journey;
    const char *client_id = "18181818-3434-5656-7878-909090909090";
    auto payload = make_payload(client_id, 16000, 0);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.outbox().begin_submit(0);
    auto exchange = [](const fl_product_send::HttpAction *action,
                       fl_product_send::SendTransportResult *result) -> int {
        static const char *open =
            "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"18181818-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-left\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}";
        static const char *receipt =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"18181818-3434-5656-7878-909090909090\","
            "\"message_id\":\"msg-left\",\"state\":\"accepted\"}";
        result->http_status = 200;
        result->response_lost = 0;
        result->body = open;
        result->body_len = (int)strlen(open);
        if (strcmp(action->method, "POST") == 0 && strstr(action->path, "/complete") != 0) {
            result->response_lost = 1;
            result->body = "";
            result->body_len = 0;
        } else if (strcmp(action->method, "PUT") == 0) {
            result->body = "{}";
            result->body_len = 2;
        } else if (strcmp(action->method, "GET") == 0) {
            result->body = receipt;
            result->body_len = (int)strlen(receipt);
        }
        return 0;
    };
    int lookups = 0;
    for (int step = 0; step < 12 && journey.outbox().terminal() != fl_product_send::kSendTerminalAccepted; ++step) {
        fl_product_send::HttpAction action;
        fl_product_send::SendTransportResult result;
        if (!journey.outbox().prepare_http_action(&action, 0)) {
            break;
        }
        if (strcmp(action.method, "GET") == 0) {
            fl_product_send::HttpAction again;
            lookups += 1;
            journey.leave_wait();
            CHECK(journey.outbox().left_wait() == 1);
            CHECK(journey.outbox().prepare_http_action(&again, 0) == 1);
            CHECK(strcmp(again.method, "GET") == 0);
            CHECK(journey.outbox().terminal() != fl_product_send::kSendTerminalCancelled);
        }
        memset(&result, 0, sizeof(result));
        if (exchange(&action, &result) != 0) {
            result.transport_ok = 0;
        } else {
            result.transport_ok = 1;
        }
        journey.outbox().apply_transport_result(action, result, 0);
    }
    CHECK(lookups >= 1);
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalAccepted);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(journey.outbox().has_pending_http() == 0);
    CHECK(strcmp(journey.outbox().client_message_id(), client_id) == 0);
    journey.advance_to(fl_product_send::kSentHoldMs + 1);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalAccepted);
    return g_failures;
}

int test_client_message_uuid()
{
    fl_product_send::SendOutbox outbox;
    auto desk = make_payload("device-capture", 16000, 0);
    auto empty = make_payload("", 16000, 0);
    auto bare = make_payload("aaaaaaaaaaaabbbbccccddddeeeeeeeeeeee", 16000, 0);
    auto upper = make_payload("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", 16000, 0);
    auto good = make_payload("19191919-3434-5656-7878-909090909090", 16000, 0);
    CHECK(outbox.bind_payload(desk, g_audio, 0) == 0);
    outbox.begin_submit(0);
    CHECK(outbox.active() == 0);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(outbox.bind_payload(empty, g_audio, 0) == 0);
    CHECK(outbox.bind_payload(bare, g_audio, 0) == 0);
    CHECK(outbox.bind_payload(upper, g_audio, 0) == 1);
    CHECK(outbox.bind_payload(good, g_audio, 0) == 1);
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
            sign_in_sender(outbox);
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

struct FakeDesk {
    int playing;
    int paused;
    int play_head_ms;
    int can_start;
    int discards;
    int cancels;
    int finishes;
    char selected[48];
};

void fake_pause(void *ctx, int64_t now_ms)
{
    FakeDesk *desk = static_cast<FakeDesk *>(ctx);
    (void)now_ms;
    if (desk->playing) {
        desk->playing = 0;
        desk->paused = 1;
    }
}

const char *fake_selected(void *ctx)
{
    return static_cast<FakeDesk *>(ctx)->selected;
}

int fake_can_start(void *ctx)
{
    return static_cast<FakeDesk *>(ctx)->can_start;
}

void fake_discard(void *ctx)
{
    static_cast<FakeDesk *>(ctx)->discards += 1;
}

void fake_cancel(void *ctx)
{
    static_cast<FakeDesk *>(ctx)->cancels += 1;
}

int fake_finish(void *ctx)
{
    static_cast<FakeDesk *>(ctx)->finishes += 1;
    return 1;
}

void reset_desk(FakeDesk *desk)
{
    memset(desk, 0, sizeof(*desk));
    desk->can_start = 1;
    desk->playing = 1;
    desk->play_head_ms = 1280;
    strncpy(desk->selected, "msg-jordan", sizeof(desk->selected) - 1);
}

void fill_hooks(FakeDesk *desk, fl_product_send::SendStageHooks *hooks)
{
    memset(hooks, 0, sizeof(*hooks));
    hooks->playback = desk;
    hooks->capture = desk;
    hooks->pause_for_record = fake_pause;
    hooks->selected_message_id = fake_selected;
    hooks->can_start_second_recording = fake_can_start;
    hooks->discard_in_progress = fake_discard;
    hooks->cancel_recording = fake_cancel;
    hooks->finish_take = fake_finish;
}

void select_jordan(fl_product_send::SendJourney &journey)
{
    fl_product_send::HangoutMember member;
    memset(&member, 0, sizeof(member));
    strncpy(member.user_id, "user-jordan", sizeof(member.user_id) - 1);
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    journey.picker().load_members(&member, 1, kTestSender);
    journey.picker().toggle_selection(0);
}

fl_product_send::SendStageResult press(fl_product_send::SendStageReducer &stage,
                                       fl_product_send::SendIntent intent,
                                       fl_product_send::SendJourney &journey,
                                       fl_product_send::SendStageHooks *hooks, int signed_in)
{
    return stage.apply(intent, journey, hooks, signed_in, 0);
}

/* Accept the create POST so the outbox phase becomes streaming. */
int open_stream(fl_product_send::SendJourney &journey)
{
    fl_product_send::HttpAction action;
    fl_product_send::SendTransportResult transport;
    char body[320];
    const char *client_id = journey.outbox().client_message_id();
    int wrote;
    if (journey.outbox().prepare_http_action(&action, 0) != 1) {
        return 0;
    }
    wrote = snprintf(body, sizeof(body),
                     "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"%s\","
                     "\"message_id\":\"msg-stream\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}",
                     client_id);
    if (wrote <= 0 || wrote >= (int)sizeof(body)) {
        return 0;
    }
    memset(&transport, 0, sizeof(transport));
    transport.transport_ok = 1;
    transport.http_status = 200;
    transport.body = body;
    transport.body_len = wrote;
    if (journey.outbox().apply_transport_result(action, transport, 0) != 1) {
        return 0;
    }
    return journey.ui_phase() == fl_product_send::kSendUiSending ? 1 : 0;
}

int test_stage_buttons()
{
    FakeDesk desk;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::SendStageResult result;
    fl_product_send::HttpAction action;
    char client_id[fl_product_send::kMaxClientMessageId];
    int discards_before = 0;

    reset_desk(&desk);
    fill_hooks(&desk, &hooks);

    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.opened_volume == 1);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(stage.place() == fl_product_send::kSendPlaceInbox);
    CHECK(desk.discards == 0);
    CHECK(journey.outbox().active() == 0);
    CHECK(desk.paused == 0);
    CHECK(desk.play_head_ms == 1280);

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 0);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceInbox);
    CHECK(desk.paused == 0);

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    CHECK(result.show_split_pane == 1);
    CHECK(result.show_record_canvas == 0);
    CHECK(desk.paused == 1);
    CHECK(desk.playing == 0);
    CHECK(desk.play_head_ms == 1280);
    CHECK(strcmp(desk.selected, "msg-jordan") == 0);
    CHECK(strcmp(journey.remembered_inbox_selection(), "msg-jordan") == 0);
    CHECK(journey.picker().selected_count() == 0);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlacePicker);
    CHECK(desk.finishes == 0);
    CHECK(desk.discards == 0);
    CHECK(journey.outbox().active() == 0);

    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.opened_volume == 0);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(desk.discards == 0);
    CHECK(desk.cancels == 0);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalNone);

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(stage.place() == fl_product_send::kSendPlacePicker);
    select_jordan(journey);
    CHECK(journey.picker().selected_count() == 1);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceWipe);
    CHECK(result.show_record_canvas == 1);
    CHECK(desk.finishes == 0);
    CHECK(journey.outbox().active() == 0);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceWipe);
    CHECK(desk.finishes == 0);
    CHECK(desk.discards == 0);

    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(result.opened_volume == 0);
    CHECK(desk.discards == 1);
    CHECK(desk.cancels == 1);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');
    CHECK(journey.picker().selected_count() == 0);

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    select_jordan(journey);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceWipe);
    result = press(stage, fl_product_send::kSendIntentWipeComplete, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceRecording);
    CHECK(result.show_record_canvas == 1);
    discards_before = desk.discards;
    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(result.opened_volume == 0);
    CHECK(desk.discards == discards_before + 1);
    CHECK(desk.finishes == 0);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');

    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    select_jordan(journey);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    result = press(stage, fl_product_send::kSendIntentWipeComplete, journey, &hooks, 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceRecording);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.finished == 1);
    CHECK(result.upload_ready == 0);
    CHECK(result.place == fl_product_send::kSendPlaceFinishReverse);
    CHECK(result.show_record_canvas == 1);
    CHECK(result.show_split_pane == 0);
    CHECK(result.reversed == 1);
    CHECK(desk.finishes == 1);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(journey.outbox().active() == 0);
    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceFinishReverse);
    CHECK(desk.discards == discards_before + 1);
    result = stage.advance(fl_product_send::kSendRingReverseMs - 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceFinishReverse);
    CHECK(journey.outbox().active() == 0);
    result = stage.advance(fl_product_send::kSendRingReverseMs);
    CHECK(result.place == fl_product_send::kSendPlaceReceipt);
    CHECK(result.upload_ready == 1);
    CHECK(result.show_split_pane == 1);
    CHECK(result.show_record_canvas == 0);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(journey.outbox().active() == 0);
    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceReceipt);
    CHECK(journey.outbox().left_wait() == 0);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceReceipt);

    {
        auto payload = make_payload("19191919-3434-5656-7878-909090909090", 16000, 0);
        CHECK(journey.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(journey.outbox());
        journey.set_wifi(1);
        journey.outbox().begin_submit(0);
        CHECK(journey.ui_phase() == fl_product_send::kSendUiPreparing);
        result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
        CHECK(result.ignored == 1);
        CHECK(stage.place() == fl_product_send::kSendPlaceReceipt);
        CHECK(journey.outbox().left_wait() == 0);
        CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalNone);
        journey.set_wifi(0);
        CHECK(journey.outbox().prepare_http_action(&action, 0) == 0);
        CHECK(journey.ui_phase() == fl_product_send::kSendUiQueuedWait);
        CHECK(strcmp(journey.body_label(), "no Wi-Fi") == 0);
        strncpy(client_id, journey.outbox().client_message_id(), sizeof(client_id) - 1);
        client_id[sizeof(client_id) - 1] = '\0';
        result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
        CHECK(result.left_wait == 1);
        CHECK(result.place == fl_product_send::kSendPlaceInbox);
        CHECK(journey.outbox().left_wait() == 1);
        CHECK(journey.outbox().terminal() != fl_product_send::kSendTerminalCancelled);
        CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(strcmp(journey.outbox().client_message_id(), client_id) == 0);
        CHECK(journey.outbox().active() == 1);
        journey.set_wifi(1);
        CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
        CHECK(strcmp(action.method, "POST") == 0);
    }

    /* SCN-028: after Boot from the no-Wi-Fi wait, inbox red returns to that receipt. */
    discards_before = desk.discards;
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceReceipt);
    CHECK(result.place != fl_product_send::kSendPlacePicker);
    CHECK(result.place != fl_product_send::kSendPlaceRecording);
    CHECK(result.ignored == 0);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiQueuedWait ||
          journey.ui_phase() == fl_product_send::kSendUiPreparing);
    CHECK(strcmp(journey.outbox().client_message_id(), client_id) == 0);
    CHECK(journey.outbox().active() == 1);
    CHECK(journey.outbox().left_wait() == 1);
    CHECK(journey.outbox().terminal() != fl_product_send::kSendTerminalCancelled);
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalNone);
    CHECK(desk.finishes == 1);
    CHECK(desk.discards == discards_before);

    {
        fl_product_send::SendStageReducer blocked;
        fl_product_send::SendJourney queued;
        FakeDesk full;
        fl_product_send::SendStageHooks full_hooks;
        auto payload = make_payload("20202020-3434-5656-7878-909090909090", 16000, 0);
        reset_desk(&full);
        full.can_start = 0;
        fill_hooks(&full, &full_hooks);
        CHECK(queued.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(queued.outbox());
        queued.outbox().begin_submit(0);
        CHECK(queued.ui_phase() == fl_product_send::kSendUiPreparing);
        /* SCN-028: preparing is still pending, even when a second take cannot start. */
        result = press(blocked, fl_product_send::kSendIntentRed, queued, &full_hooks, 1);
        CHECK(result.ignored == 0);
        CHECK(result.place == fl_product_send::kSendPlaceReceipt);
        CHECK(result.place != fl_product_send::kSendPlacePicker);
        CHECK(result.place != fl_product_send::kSendPlaceRecording);
        CHECK(blocked.place() == fl_product_send::kSendPlaceReceipt);
        CHECK(queued.ui_phase() == fl_product_send::kSendUiPreparing);
        CHECK(full.paused == 0);
        CHECK(full.playing == 1);
        CHECK(full.finishes == 0);
        CHECK(full.discards == 0);
        CHECK(strcmp(queued.outbox().client_message_id(), payload.client_message_id) == 0);
        CHECK(queued.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(queued.outbox().active() == 1);
        CHECK(queued.outbox().left_wait() == 0);
    }

    {
        fl_product_send::SendStageReducer blocked;
        fl_product_send::SendJourney streaming;
        FakeDesk full;
        fl_product_send::SendStageHooks full_hooks;
        auto payload = make_payload("30303030-3434-5656-7878-909090909090", 16000, 0);
        reset_desk(&full);
        full.can_start = 0;
        fill_hooks(&full, &full_hooks);
        CHECK(streaming.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(streaming.outbox());
        streaming.set_wifi(1);
        streaming.outbox().begin_submit(0);
        CHECK(open_stream(streaming) == 1);
        CHECK(streaming.ui_phase() == fl_product_send::kSendUiSending);
        result = press(blocked, fl_product_send::kSendIntentRed, streaming, &full_hooks, 1);
        CHECK(result.ignored == 1);
        CHECK(result.place == fl_product_send::kSendPlaceInbox);
        CHECK(blocked.place() == fl_product_send::kSendPlaceInbox);
        CHECK(full.paused == 0);
        CHECK(full.playing == 1);
        CHECK(full.finishes == 0);
        CHECK(full.discards == 0);
        CHECK(full.cancels == 0);
        CHECK(strcmp(streaming.outbox().client_message_id(), payload.client_message_id) == 0);
        CHECK(streaming.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(streaming.outbox().active() == 1);
        CHECK(streaming.ui_phase() == fl_product_send::kSendUiSending);
    }

    {
        /* SCN-057: streaming inbox red opens the picker and keeps this upload. */
        fl_product_send::SendStageReducer streaming_stage;
        fl_product_send::SendJourney streaming;
        FakeDesk stream_desk;
        fl_product_send::SendStageHooks stream_hooks;
        auto payload = make_payload("31313131-3434-5656-7878-909090909090", 16000, 0);
        reset_desk(&stream_desk);
        fill_hooks(&stream_desk, &stream_hooks);
        CHECK(streaming.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(streaming.outbox());
        streaming.set_wifi(1);
        streaming.outbox().begin_submit(0);
        CHECK(open_stream(streaming) == 1);
        CHECK(streaming.ui_phase() == fl_product_send::kSendUiSending);
        result = press(streaming_stage, fl_product_send::kSendIntentRed, streaming, &stream_hooks, 1);
        CHECK(result.place == fl_product_send::kSendPlacePicker);
        CHECK(result.place != fl_product_send::kSendPlaceRecording);
        CHECK(result.show_split_pane == 1);
        CHECK(result.show_record_canvas == 0);
        CHECK(result.paused == 1);
        CHECK(result.remembered == 1);
        CHECK(stream_desk.paused == 1);
        CHECK(stream_desk.playing == 0);
        CHECK(strcmp(streaming.remembered_inbox_selection(), "msg-jordan") == 0);
        CHECK(streaming.picker().selected_count() == 0);
        CHECK(strcmp(streaming.outbox().client_message_id(), payload.client_message_id) == 0);
        CHECK(streaming.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(streaming.outbox().active() == 1);
        CHECK(streaming.outbox().left_wait() == 0);
        CHECK(streaming.ui_phase() == fl_product_send::kSendUiSending);
        CHECK(stream_desk.finishes == 0);
        CHECK(stream_desk.discards == 0);
        CHECK(stream_desk.cancels == 0);
    }

    {
        fl_product_send::SendStageReducer muted;
        fl_product_send::SendJourney capture;
        FakeDesk mute_desk;
        fl_product_send::SendStageHooks mute_hooks;
        reset_desk(&mute_desk);
        fill_hooks(&mute_desk, &mute_hooks);
        result = press(muted, fl_product_send::kSendIntentRed, capture, &mute_hooks, 1);
        select_jordan(capture);
        result = press(muted, fl_product_send::kSendIntentRed, capture, &mute_hooks, 1);
        result = press(muted, fl_product_send::kSendIntentWipeComplete, capture, &mute_hooks, 1);
        CHECK(muted.place() == fl_product_send::kSendPlaceRecording);
        result = press(muted, fl_product_send::kSendIntentMute, capture, &mute_hooks, 1);
        CHECK(muted.place() == fl_product_send::kSendPlaceInbox);
        CHECK(mute_desk.discards == 1);
        CHECK(mute_desk.cancels == 1);
        CHECK(capture.mute_discarded_in_progress() == 1);
        CHECK(capture.outbox().active() == 0);
    }

    {
        fl_product_send::SendStageReducer muted;
        fl_product_send::SendJourney held;
        FakeDesk mute_desk;
        fl_product_send::SendStageHooks mute_hooks;
        auto payload = make_payload("21212121-3434-5656-7878-909090909090", 16000, 0);
        reset_desk(&mute_desk);
        fill_hooks(&mute_desk, &mute_hooks);
        CHECK(held.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(held.outbox());
        held.outbox().begin_submit(0);
        CHECK(held.ui_phase() == fl_product_send::kSendUiPreparing);
        result = press(muted, fl_product_send::kSendIntentRed, held, &mute_hooks, 1);
        CHECK(result.place == fl_product_send::kSendPlaceReceipt);
        CHECK(muted.place() != fl_product_send::kSendPlacePicker);
        CHECK(muted.place() != fl_product_send::kSendPlaceRecording);
        result = press(muted, fl_product_send::kSendIntentMute, held, &mute_hooks, 1);
        CHECK(muted.place() == fl_product_send::kSendPlaceInbox);
        CHECK(mute_desk.discards == 0);
        CHECK(mute_desk.cancels == 0);
        CHECK(mute_desk.finishes == 0);
        CHECK(held.mute_discarded_in_progress() == 0);
        CHECK(strcmp(held.outbox().client_message_id(), payload.client_message_id) == 0);
        CHECK(held.outbox().active() == 1);
        CHECK(held.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(held.outbox().left_wait() == 0);
        CHECK(held.mute_held_finished() == 1);
        CHECK(held.outbox().prepare_http_action(&action, 0) == 0);
        CHECK(held.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
        CHECK(strcmp(held.outbox().client_message_id(), payload.client_message_id) == 0);
    }

    {
        /* Mute during a second capture discards only that take. The stream stays. */
        fl_product_send::SendStageReducer muted;
        fl_product_send::SendJourney held;
        FakeDesk mute_desk;
        fl_product_send::SendStageHooks mute_hooks;
        auto payload = make_payload("32323232-3434-5656-7878-909090909090", 16000, 0);
        reset_desk(&mute_desk);
        fill_hooks(&mute_desk, &mute_hooks);
        CHECK(held.bind_test_payload(payload, g_audio, 0));
        sign_in_sender(held.outbox());
        held.set_wifi(1);
        held.outbox().begin_submit(0);
        CHECK(open_stream(held) == 1);
        CHECK(held.ui_phase() == fl_product_send::kSendUiSending);
        result = press(muted, fl_product_send::kSendIntentRed, held, &mute_hooks, 1);
        CHECK(result.place == fl_product_send::kSendPlacePicker);
        select_jordan(held);
        CHECK(held.picker().selected_count() == 1);
        result = press(muted, fl_product_send::kSendIntentRed, held, &mute_hooks, 1);
        CHECK(result.place == fl_product_send::kSendPlaceWipe);
        result = press(muted, fl_product_send::kSendIntentWipeComplete, held, &mute_hooks, 1);
        CHECK(muted.place() == fl_product_send::kSendPlaceRecording);
        CHECK(held.ui_phase() == fl_product_send::kSendUiSending);
        CHECK(strcmp(held.outbox().client_message_id(), payload.client_message_id) == 0);
        result = press(muted, fl_product_send::kSendIntentMute, held, &mute_hooks, 1);
        CHECK(muted.place() == fl_product_send::kSendPlaceInbox);
        CHECK(mute_desk.discards == 1);
        CHECK(mute_desk.cancels == 1);
        CHECK(mute_desk.finishes == 0);
        CHECK(held.mute_discarded_in_progress() == 0);
        CHECK(strcmp(held.outbox().client_message_id(), payload.client_message_id) == 0);
        CHECK(held.outbox().active() == 1);
        CHECK(held.outbox().terminal() == fl_product_send::kSendTerminalNone);
        CHECK(held.outbox().left_wait() == 0);
        CHECK(held.ui_phase() == fl_product_send::kSendUiSending);
        CHECK(held.mute_held_finished() == 1);
        CHECK(held.outbox().prepare_http_action(&action, 0) == 0);
    }

    return g_failures;
}

int uuid_shape(const char *id)
{
    static const int groups[5] = {8, 4, 4, 4, 12};
    int index = 0;
    int group;
    if (id == 0) {
        return 0;
    }
    for (group = 0; group < 5; ++group) {
        int count;
        if (group > 0) {
            if (id[index] != '-') {
                return 0;
            }
            index += 1;
        }
        for (count = 0; count < groups[group]; ++count) {
            char c = id[index];
            int hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (hex == 0) {
                return 0;
            }
            index += 1;
        }
    }
    return id[index] == '\0' ? 1 : 0;
}

int mic_unavailable(void *ctx)
{
    (void)ctx;
    return 0;
}

int test_couldnt_record()
{
    FakeDesk desk;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::SendStageResult result;
    int64_t shown;

    reset_desk(&desk);
    fill_hooks(&desk, &hooks);
    hooks.capture_can_start = mic_unavailable;
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, 0);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    select_jordan(journey);
    shown = 1000;
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, shown);
    CHECK(result.place == fl_product_send::kSendPlaceCouldntRecord);
    CHECK(result.reversed == 0);
    CHECK(result.show_record_canvas == 0);
    CHECK(result.show_couldnt_record == 1);
    CHECK(result.show_split_pane == 1);
    CHECK(strcmp(stage.lower_status(), "couldn't record") == 0);
    CHECK(journey.picker().selected_count() == 1);
    CHECK(strcmp(journey.picker().user_id_at(0), "user-jordan") == 0);
    CHECK(journey.capture_client_message_id()[0] == '\0');
    CHECK(journey.frozen_recipient_count() == 0);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');
    CHECK(desk.finishes == 0);
    CHECK(desk.discards == 0);
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, shown + 200);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceCouldntRecord);
    CHECK(journey.picker().selected_count() == 1);
    result = stage.advance(shown + fl_product_send::kSendCouldntRecordHoldMs - 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceCouldntRecord);
    result = stage.advance(shown + fl_product_send::kSendCouldntRecordHoldMs);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    CHECK(strcmp(stage.lower_status(), "") == 0);
    CHECK(journey.picker().selected_count() == 1);
    CHECK(strcmp(journey.picker().user_id_at(0), "user-jordan") == 0);

    {
        fl_product_send::SendStageReducer boot_stage;
        fl_product_send::SendJourney boot_journey;
        result = boot_stage.apply(fl_product_send::kSendIntentRed, boot_journey, &hooks, 1, 0);
        select_jordan(boot_journey);
        result = boot_stage.apply(fl_product_send::kSendIntentRed, boot_journey, &hooks, 1, 0);
        CHECK(boot_stage.place() == fl_product_send::kSendPlaceCouldntRecord);
        result = boot_stage.apply(fl_product_send::kSendIntentBoot, boot_journey, &hooks, 1, 400);
        CHECK(result.place == fl_product_send::kSendPlaceInbox);
        CHECK(result.opened_volume == 0);
        CHECK(boot_journey.outbox().active() == 0);
    }

    {
        fl_product_send::SendStageReducer rev;
        fl_product_send::SendJourney started;
        fl_product_send::SendStageHooks ready;
        FakeDesk rev_desk;
        int discards;
        reset_desk(&rev_desk);
        fill_hooks(&rev_desk, &ready);
        result = rev.apply(fl_product_send::kSendIntentRed, started, &ready, 1, 0);
        select_jordan(started);
        result = rev.apply(fl_product_send::kSendIntentRed, started, &ready, 1, 0);
        CHECK(result.place == fl_product_send::kSendPlaceWipe);
        CHECK(started.frozen_recipient_count() == 1);
        CHECK(uuid_shape(started.capture_client_message_id()) == 1);
        discards = rev_desk.discards;
        result = rev.apply(fl_product_send::kSendIntentCaptureFailed, started, &ready, 1, 0);
        CHECK(result.reversed == 1);
        CHECK(result.place == fl_product_send::kSendPlaceReverse);
        CHECK(result.show_record_canvas == 1);
        CHECK(rev_desk.discards == discards + 1);
        CHECK(started.picker().selected_count() == 1);
        CHECK(strcmp(started.picker().user_id_at(0), "user-jordan") == 0);
        CHECK(started.capture_client_message_id()[0] == '\0');
        CHECK(started.outbox().active() == 0);
        result = rev.advance(fl_product_send::kSendRingReverseMs - 1);
        CHECK(rev.place() == fl_product_send::kSendPlaceReverse);
        result = rev.advance(fl_product_send::kSendRingReverseMs);
        CHECK(result.place == fl_product_send::kSendPlaceCouldntRecord);
        CHECK(strcmp(rev.lower_status(), "couldn't record") == 0);
        result = rev.apply(fl_product_send::kSendIntentRed, started, &ready, 1, fl_product_send::kSendRingReverseMs);
        CHECK(result.ignored == 1);
        CHECK(rev.place() == fl_product_send::kSendPlaceCouldntRecord);
        result = rev.advance(fl_product_send::kSendRingReverseMs + fl_product_send::kSendCouldntRecordHoldMs);
        CHECK(result.place == fl_product_send::kSendPlacePicker);
        CHECK(started.picker().selected_count() == 1);
        CHECK(strcmp(started.picker().user_id_at(0), "user-jordan") == 0);
        CHECK(started.outbox().active() == 0);
        CHECK(rev_desk.finishes == 0);
    }

    {
        fl_product_send::SendStageReducer done;
        fl_product_send::SendJourney take;
        fl_product_send::SendStageHooks ready;
        FakeDesk done_desk;
        char capture_id[fl_product_send::kMaxClientMessageId];
        reset_desk(&done_desk);
        fill_hooks(&done_desk, &ready);
        result = done.apply(fl_product_send::kSendIntentRed, take, &ready, 1, 0);
        select_jordan(take);
        result = done.apply(fl_product_send::kSendIntentRed, take, &ready, 1, 0);
        result = done.apply(fl_product_send::kSendIntentWipeComplete, take, &ready, 1, 0);
        CHECK(done.place() == fl_product_send::kSendPlaceRecording);
        strncpy(capture_id, take.capture_client_message_id(), sizeof(capture_id) - 1);
        capture_id[sizeof(capture_id) - 1] = '\0';
        result = done.apply(fl_product_send::kSendIntentTakeFinished, take, &ready, 1, 0);
        CHECK(result.finished == 1);
        CHECK(result.upload_ready == 0);
        CHECK(result.place == fl_product_send::kSendPlaceFinishReverse);
        CHECK(result.show_record_canvas == 1);
        CHECK(done_desk.finishes == 0);
        CHECK(take.outbox().active() == 0);
        CHECK(take.ui_phase() == fl_product_send::kSendUiHidden);
        CHECK(strcmp(take.capture_client_message_id(), capture_id) == 0);
        CHECK(take.frozen_recipient_count() == 1);
        result = done.advance(fl_product_send::kSendRingReverseMs - 1);
        CHECK(done.place() == fl_product_send::kSendPlaceFinishReverse);
        CHECK(take.outbox().active() == 0);
        result = done.advance(fl_product_send::kSendRingReverseMs);
        CHECK(result.place == fl_product_send::kSendPlaceReceipt);
        CHECK(result.upload_ready == 1);
        CHECK(result.show_split_pane == 1);
        CHECK(take.outbox().active() == 0);
        CHECK(take.ui_phase() == fl_product_send::kSendUiHidden);
    }

    return g_failures;
}

int test_frozen_recipients()
{
    FakeDesk desk;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::SendStageResult result;
    fl_product_send::HangoutMember members[2];
    fl_product_send::HangoutMember refreshed[2];
    char capture_id[fl_product_send::kMaxClientMessageId];
    char first[fl_product_send::kMaxUserId];
    char second[fl_product_send::kMaxUserId];

    reset_desk(&desk);
    fill_hooks(&desk, &hooks);
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, 0);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    fill_member(&members[0], "user-jordan", "Jordan", "avatar-01", "card-bg-01");
    fill_member(&members[1], "user-avery", "Avery", "avatar-02", "card-bg-02");
    CHECK(journey.picker().load_members(members, 2, kTestSender) == 2);
    journey.picker().toggle_selection(0);
    journey.picker().toggle_selection(1);
    CHECK(journey.picker().selected_count() == 2);
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, 0);
    CHECK(result.place == fl_product_send::kSendPlaceWipe);
    CHECK(journey.frozen_recipient_count() == 2);
    CHECK(strcmp(journey.frozen_recipient_id(0), "user-jordan") == 0);
    CHECK(strcmp(journey.frozen_recipient_id(1), "user-avery") == 0);
    CHECK(uuid_shape(journey.capture_client_message_id()) == 1);
    CHECK(journey.outbox().active() == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');
    strncpy(capture_id, journey.capture_client_message_id(), sizeof(capture_id) - 1);
    capture_id[sizeof(capture_id) - 1] = '\0';
    strncpy(first, journey.frozen_recipient_id(0), sizeof(first) - 1);
    first[sizeof(first) - 1] = '\0';
    strncpy(second, journey.frozen_recipient_id(1), sizeof(second) - 1);
    second[sizeof(second) - 1] = '\0';

    fill_member(&refreshed[0], "user-jordan", "Jordan", "avatar-01", "card-bg-01");
    fill_member(&refreshed[1], "user-sam", "Sam", "avatar-03", "card-bg-03");
    CHECK(journey.picker().refresh_members(refreshed, 2, kTestSender) == 2);
    CHECK(journey.picker().selected_count() == 1);
    CHECK(strcmp(journey.picker().user_id_at(0), "user-jordan") == 0);
    CHECK(journey.frozen_recipient_count() == 2);
    CHECK(strcmp(journey.frozen_recipient_id(0), first) == 0);
    CHECK(strcmp(journey.frozen_recipient_id(1), second) == 0);
    CHECK(strcmp(journey.capture_client_message_id(), capture_id) == 0);
    CHECK(journey.outbox().client_message_id()[0] == '\0');
    CHECK(journey.outbox().active() == 0);
    result = stage.apply(fl_product_send::kSendIntentRed, journey, &hooks, 1, 50);
    CHECK(result.ignored == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceWipe);
    CHECK(strcmp(journey.capture_client_message_id(), capture_id) == 0);

    return g_failures;
}

struct PartStore {
    uint8_t bytes[16484];
    uint32_t len;
    uint32_t max_request;
    int calls;
    int saw_zero;
    int saw_next;
};

int read_part_store(void *ctx, int kind, uint32_t offset, uint8_t *dest, uint32_t len, uint32_t *out_read)
{
    PartStore *store = static_cast<PartStore *>(ctx);
    if (store == 0 || dest == 0 || out_read == 0 || kind != 1) {
        return 0;
    }
    store->calls += 1;
    if (len > store->max_request) {
        store->max_request = len;
    }
    if (offset == 0) {
        store->saw_zero = 1;
    }
    if (offset == fl_product_send::kPartSizeBytes) {
        store->saw_next = 1;
    }
    if (offset > store->len || len > store->len - offset) {
        return 0;
    }
    memcpy(dest, store->bytes + offset, len);
    *out_read = len;
    return 1;
}

int apply_body(fl_product_send::SendOutbox &outbox, const fl_product_send::HttpAction &action, const char *body,
               int64_t now_ms)
{
    fl_product_send::SendTransportResult result;
    memset(&result, 0, sizeof(result));
    result.transport_ok = 1;
    result.http_status = 200;
    result.body = body != 0 ? body : "";
    result.body_len = (int)strlen(result.body);
    return outbox.apply_transport_result(action, result, now_ms);
}

int reach_receipt(fl_product_send::SendStageReducer &stage, fl_product_send::SendJourney &journey,
                  fl_product_send::SendStageHooks *hooks)
{
    fl_product_send::SendStageResult result;
    result = press(stage, fl_product_send::kSendIntentRed, journey, hooks, 1);
    if (result.place != fl_product_send::kSendPlacePicker) {
        return 0;
    }
    select_jordan(journey);
    result = press(stage, fl_product_send::kSendIntentRed, journey, hooks, 1);
    if (result.place != fl_product_send::kSendPlaceWipe) {
        return 0;
    }
    result = press(stage, fl_product_send::kSendIntentWipeComplete, journey, hooks, 1);
    if (result.place != fl_product_send::kSendPlaceRecording) {
        return 0;
    }
    result = press(stage, fl_product_send::kSendIntentRed, journey, hooks, 1);
    if (result.place != fl_product_send::kSendPlaceFinishReverse || result.upload_ready != 0) {
        return 0;
    }
    result = stage.advance(fl_product_send::kSendRingReverseMs, journey);
    return result.place == fl_product_send::kSendPlaceReceipt && result.upload_ready == 1;
}

int test_stored_part_read()
{
    fl_product_send::SendOutbox outbox;
    fl_product_send::HttpAction action;
    fl_product_send::MediaPartReader reader;
    fl_product_send::FrozenPayload payload;
    PartStore store;
    uint8_t digest[32];
    char original_hex[65];
    char open_body[320];
    uint32_t seq = 0;
    const char *client_id = "23232323-3434-5656-7878-909090909092";
    const char *not_this = "00000000-1111-2222-3333-444444444444";
    static_assert(sizeof(fl_product_send::SendOutbox) < 65536, "part window, not a full take");
    memset(&store, 0, sizeof(store));
    store.len = (uint32_t)sizeof(store.bytes);
    memset(store.bytes, 0x11, sizeof(store.bytes));
    fl_product_send::sha256_bytes(store.bytes, sizeof(store.bytes), digest);
    fl_product_send::sha256_hex(digest, original_hex);
    payload = make_payload(client_id, store.len, 0);
    memset(&reader, 0, sizeof(reader));
    reader.ctx = &store;
    reader.read_at = read_part_store;
    CHECK(outbox.bind_stored(payload, reader) == 1);
    CHECK(outbox.part_size_bytes() == fl_product_send::kPartSizeBytes);
    store.bytes[0] = 0x22;
    store.bytes[100] = 0x44;
    store.bytes[fl_product_send::kPartSizeBytes] = 0x33;
    sign_in_sender(outbox);
    outbox.begin_submit(0);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiPreparing);
    CHECK(outbox.ui_phase() != fl_product_send::kSendUiSent);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "POST") == 0);
    snprintf(open_body, sizeof(open_body),
             "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"%s\","
             "\"message_id\":\"msg-part\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}",
             client_id);
    CHECK(apply_body(outbox, action, open_body, 0) == 1);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiSending);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "PUT") == 0);
    CHECK(action.body != store.bytes);
    CHECK(action.body != store.bytes + fl_product_send::kPartSizeBytes);
    CHECK(action.body_len == fl_product_send::kPartSizeBytes);
    CHECK(action.body[0] == 0x22);
    CHECK(action.body[100] == 0x44);
    CHECK(memcmp(action.body, store.bytes, fl_product_send::kPartSizeBytes) == 0);
    outbox.note_http_sent(&seq);
    {
        fl_product_send::SendTransportResult late;
        memset(&late, 0, sizeof(late));
        late.transport_ok = 1;
        late.http_status = 200;
        late.body = "{}";
        late.body_len = 2;
        CHECK(outbox.apply_http_result(not_this, seq, action, late, 0) == 0);
        CHECK(outbox.audio_part_sequence() == 0);
        CHECK(outbox.http_inflight() == 0);
        CHECK(outbox.ui_phase() != fl_product_send::kSendUiSent);
    }
    CHECK(apply_body(outbox, action, "{}", 0) == 1);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "PUT") == 0);
    CHECK(action.body_len == store.len - fl_product_send::kPartSizeBytes);
    CHECK(action.body[0] == 0x33);
    CHECK(memcmp(action.body, store.bytes + fl_product_send::kPartSizeBytes, action.body_len) == 0);
    CHECK(apply_body(outbox, action, "{}", 0) == 1);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "POST") == 0);
    CHECK(strstr(action.path, "/complete") != 0);
    CHECK(action.body != 0);
    CHECK(strstr(reinterpret_cast<const char *>(action.body), original_hex) != 0);
    CHECK(store.max_request <= (uint32_t)fl_product_send::kPartSizeBytes);
    CHECK(store.max_request > 0);
    CHECK(store.saw_zero == 1);
    CHECK(store.saw_next == 1);
    CHECK(store.calls >= 4);
    return g_failures;
}

int test_subtitle_overflow()
{
    fl_product_send::SendJourney journey;
    fl_product_send::HangoutMember members[2];
    char names[2][fl_product_send::kMaxName];
    char line[96];
    fill_member(&members[0], "user-chris", "Christopher", "avatar-01", "card-bg-01");
    fill_member(&members[1], "user-bart", "Bartholomewson", "avatar-02", "card-bg-02");
    CHECK(journey.picker().load_members(members, 2, kTestSender) == 2);
    journey.picker().toggle_selection(0);
    journey.picker().toggle_selection(1);
    CHECK(journey.picker().selected_count() == 2);
    journey.freeze_capture_recipients();
    journey.capture_receipt_names();
    journey.receipt_title(line, (int)sizeof(line));
    CHECK(strcmp(line, "sending to ...") == 0);
    journey.receipt_subtitle(line, (int)sizeof(line));
    CHECK(strcmp(line, "Christopher +1") == 0);
    journey.clear_capture_freeze();
    journey.receipt_subtitle(line, (int)sizeof(line));
    CHECK(strcmp(line, "Christopher +1") == 0);
    memset(names, 0, sizeof(names));
    strncpy(names[0], "Jordan", sizeof(names[0]) - 1);
    strncpy(names[1], "Sam", sizeof(names[1]) - 1);
    journey.install_receipt_names(names, 2);
    journey.receipt_subtitle(line, (int)sizeof(line));
    CHECK(strcmp(line, "Jordan, Sam") == 0);
    memset(names, 0, sizeof(names));
    strncpy(names[0], "Abcdefghijklmnopqrstuvwxyz", sizeof(names[0]) - 1);
    journey.install_receipt_names(names, 1);
    journey.receipt_subtitle(line, (int)sizeof(line));
    CHECK(strcmp(line, "Abcdefghijklmnopqrstuvwxyz") == 0);
    return g_failures;
}

int test_sent_dwell_restores_inbox()
{
    FakeDesk desk;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::HttpAction action;
    fl_product_send::SendStageResult result;
    char open_body[320];
    const char *client_id = "25252525-3434-5656-7878-909090909094";
    const char *not_accepted =
        "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"25252525-3434-5656-7878-909090909094\","
        "\"message_id\":\"msg-nope\",\"state\":\"open\"}";
    const char *accepted =
        "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"25252525-3434-5656-7878-909090909094\","
        "\"message_id\":\"msg-sent\",\"state\":\"accepted\"}";
    const int64_t t0 = 10000;
    auto payload = make_payload(client_id, 16000, 0);
    reset_desk(&desk);
    fill_hooks(&desk, &hooks);
    CHECK(reach_receipt(stage, journey, &hooks) == 1);
    CHECK(strcmp(journey.remembered_inbox_selection(), "msg-jordan") == 0);
    CHECK(desk.playing == 0);
    CHECK(desk.play_head_ms == 1280);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.outbox().begin_submit(t0);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiPreparing);
    CHECK(strcmp(journey.body_label(), "preparing") == 0);
    CHECK(journey.ui_phase() != fl_product_send::kSendUiSent);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    snprintf(open_body, sizeof(open_body),
             "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"%s\","
             "\"message_id\":\"msg-sent\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}",
             client_id);
    CHECK(apply_body(journey.outbox(), action, open_body, t0) == 1);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSending);
    CHECK(strcmp(journey.body_label(), "sending") == 0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    CHECK(apply_body(journey.outbox(), action, "{}", t0) == 1);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    CHECK(apply_body(journey.outbox(), action, not_accepted, t0) == 1);
    CHECK(journey.ui_phase() != fl_product_send::kSendUiSent);
    CHECK(journey.outbox().terminal() != fl_product_send::kSendTerminalAccepted);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    CHECK(apply_body(journey.outbox(), action, accepted, t0) == 1);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSent);
    CHECK(strcmp(journey.body_label(), "sent") == 0);
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalAccepted);
    result = stage.advance(t0 + fl_product_send::kSentHoldMs - 1, journey);
    CHECK(result.place == fl_product_send::kSendPlaceReceipt);
    CHECK(result.returned_inbox == 0);
    CHECK(stage.place() == fl_product_send::kSendPlaceReceipt);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSent);
    result = stage.advance(t0 + fl_product_send::kSentHoldMs, journey);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(result.returned_inbox == 1);
    CHECK(stage.place() == fl_product_send::kSendPlaceInbox);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(strcmp(journey.restored_inbox_selection(), "msg-jordan") == 0);
    CHECK(strcmp(journey.restored_inbox_selection(), "msg-sent") != 0);
    CHECK(desk.playing == 0);
    CHECK(strcmp(desk.selected, "msg-jordan") == 0);
    CHECK(desk.play_head_ms == 1280);
    return g_failures;
}

int test_leave_wait_keeps_place()
{
    FakeDesk desk;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::HttpAction action;
    fl_product_send::SendStageResult result;
    char open_body[320];
    const char *client_id = "26262626-3434-5656-7878-909090909095";
    const char *accepted =
        "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"26262626-3434-5656-7878-909090909095\","
        "\"message_id\":\"msg-left\",\"state\":\"accepted\"}";
    auto payload = make_payload(client_id, 16000, 0);
    reset_desk(&desk);
    fill_hooks(&desk, &hooks);
    CHECK(reach_receipt(stage, journey, &hooks) == 1);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.outbox().begin_submit(0);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    snprintf(open_body, sizeof(open_body),
             "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"%s\","
             "\"message_id\":\"msg-left\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}",
             client_id);
    CHECK(apply_body(journey.outbox(), action, open_body, 0) == 1);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSending);
    result = press(stage, fl_product_send::kSendIntentBoot, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(result.left_wait == 1);
    CHECK(journey.outbox().left_wait() == 1);
    CHECK(journey.outbox().active() == 1);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSending);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiSending);
    CHECK(journey.outbox().active() == 1);
    select_jordan(journey);
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceWipe);
    result = press(stage, fl_product_send::kSendIntentWipeComplete, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceRecording);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    CHECK(apply_body(journey.outbox(), action, "{}", 0) == 1);
    CHECK(journey.outbox().prepare_http_action(&action, 0) == 1);
    CHECK(apply_body(journey.outbox(), action, accepted, 0) == 1);
    CHECK(journey.outbox().terminal() == fl_product_send::kSendTerminalAccepted);
    CHECK(journey.ui_phase() == fl_product_send::kSendUiHidden);
    CHECK(journey.ui_phase() != fl_product_send::kSendUiSent);
    result = stage.advance(0, journey);
    CHECK(result.place == fl_product_send::kSendPlaceRecording);
    CHECK(result.returned_inbox == 0);
    result = stage.advance(fl_product_send::kSentHoldMs, journey);
    CHECK(stage.place() == fl_product_send::kSendPlaceRecording);
    CHECK(result.returned_inbox == 0);
    CHECK(journey.ui_phase() != fl_product_send::kSendUiSent);

    {
        fl_product_send::SendStageReducer back;
        fl_product_send::SendJourney waiting;
        fl_product_send::SendStageHooks again;
        FakeDesk again_desk;
        const char *back_id = "27272727-3434-5656-7878-909090909096";
        const char *back_accepted =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"27272727-3434-5656-7878-909090909096\","
            "\"message_id\":\"msg-back\",\"state\":\"accepted\"}";
        auto back_payload = make_payload(back_id, 16000, 0);
        reset_desk(&again_desk);
        fill_hooks(&again_desk, &again);
        CHECK(reach_receipt(back, waiting, &again) == 1);
        CHECK(waiting.bind_test_payload(back_payload, g_audio, 0));
        sign_in_sender(waiting.outbox());
        waiting.outbox().begin_submit(0);
        CHECK(waiting.outbox().prepare_http_action(&action, 0) == 1);
        snprintf(open_body, sizeof(open_body),
                 "{\"schema\":\"fl.send-open/2\",\"client_message_id\":\"%s\","
                 "\"message_id\":\"msg-back\",\"state\":\"open\",\"audio\":{\"parts_acknowledged\":0}}",
                 back_id);
        CHECK(apply_body(waiting.outbox(), action, open_body, 0) == 1);
        CHECK(waiting.outbox().prepare_http_action(&action, 0) == 1);
        CHECK(apply_body(waiting.outbox(), action, "{}", 0) == 1);
        waiting.leave_wait();
        CHECK(waiting.ui_phase() == fl_product_send::kSendUiSending);
        CHECK(waiting.outbox().prepare_http_action(&action, 0) == 1);
        CHECK(apply_body(waiting.outbox(), action, back_accepted, 0) == 1);
        CHECK(waiting.ui_phase() == fl_product_send::kSendUiHidden);
        CHECK(waiting.ui_phase() != fl_product_send::kSendUiSent);
        result = back.advance(0, waiting);
        CHECK(result.returned_inbox == 1);
        CHECK(result.place == fl_product_send::kSendPlaceInbox);
        CHECK(back.place() == fl_product_send::kSendPlaceInbox);
        CHECK(strcmp(waiting.restored_inbox_selection(), "msg-jordan") == 0);
    }
    return g_failures;
}

int test_sender_mismatch_no_action()
{
    fl_product_send::SendOutbox outbox;
    fl_product_send::HttpAction action;
    auto payload = make_payload("24242424-3434-5656-7878-909090909093", 16000, 0);
    CHECK(outbox.bind_payload(payload, g_audio, 0));
    outbox.begin_submit(0);
    CHECK(outbox.prepare_http_action(&action, 0) == 0);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(outbox.ui_phase() != fl_product_send::kSendUiSending);
    CHECK(outbox.ui_phase() != fl_product_send::kSendUiSent);
    outbox.set_signed_in_user("user-other");
    CHECK(outbox.signed_in_matches_sender() == 0);
    CHECK(outbox.prepare_http_action(&action, 0) == 0);
    CHECK(outbox.ui_phase() == fl_product_send::kSendUiQueuedWait);
    outbox.set_signed_in_user("");
    CHECK(outbox.prepare_http_action(&action, 0) == 0);
    sign_in_sender(outbox);
    CHECK(outbox.prepare_http_action(&action, 0) == 1);
    CHECK(strcmp(action.method, "POST") == 0);
    CHECK(strcmp(action.path, "/v2/outgoing") == 0);
    return g_failures;
}

int test_phase7_session_bounds()
{
    fl_product_send::SendStageReducer stage;
    fl_product_send::SendJourney journey;
    fl_product_send::SendStageHooks hooks;
    fl_product_send::SendStageResult result;
    fl_product_send::HttpAction action;
    fl_product_send::SendTransportResult transport;
    FakeDesk desk;
    char kept[fl_product_send::kMaxClientMessageId];
    const char *client_id = "28282828-3434-5656-7878-909090909097";
    const int64_t t0 = 10000;
    auto payload = make_payload(client_id, 16000, 0);
    reset_desk(&desk);
    fill_hooks(&desk, &hooks);
    CHECK(reach_receipt(stage, journey, &hooks) == 1);
    CHECK(journey.bind_test_payload(payload, g_audio, 0));
    sign_in_sender(journey.outbox());
    journey.set_wifi(1);
    journey.outbox().begin_submit(t0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    result = press(stage, fl_product_send::kSendIntentMute, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlaceInbox);
    CHECK(desk.discards == 0);
    CHECK(journey.mute_discarded_in_progress() == 0);
    CHECK(journey.mute_held_finished() == 1);
    CHECK(strcmp(journey.outbox().client_message_id(), client_id) == 0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    journey.on_same_user_signed_in("user-sam");
    CHECK(journey.mute_held_finished() == 1);
    CHECK(journey.outbox().signed_in_matches_sender() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 0);
    stage.note_inbox();
    result = press(stage, fl_product_send::kSendIntentRed, journey, &hooks, 1);
    CHECK(result.place == fl_product_send::kSendPlacePicker);
    CHECK(result.place != fl_product_send::kSendPlaceReceipt);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 0);
    CHECK(strcmp(journey.outbox().client_message_id(), client_id) == 0);
    journey.on_same_user_signed_in(kTestSender);
    CHECK(journey.mute_held_finished() == 0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    CHECK(strcmp(action.method, "POST") == 0);
    strncpy(kept, journey.outbox().client_message_id(), sizeof(kept) - 1);
    kept[sizeof(kept) - 1] = '\0';
    journey.set_refresh_failed(1);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 0);
    CHECK(journey.outbox().ui_phase() == fl_product_send::kSendUiQueuedWait);
    CHECK(strcmp(journey.body_label(), "connecting") == 0);
    CHECK(stage.place() == fl_product_send::kSendPlacePicker);
    journey.set_wifi(0);
    CHECK(strcmp(journey.body_label(), "no Wi-Fi") == 0);
    journey.set_wifi(1);
    CHECK(strcmp(journey.body_label(), "connecting") == 0);
    journey.set_refresh_failed(0);
    CHECK(journey.outbox().prepare_http_action(&action, t0) == 1);
    CHECK(strcmp(journey.outbox().client_message_id(), kept) == 0);

    {
        fl_product_send::SendStageReducer dwell_stage;
        fl_product_send::SendJourney dwell;
        fl_product_send::SendStageHooks dwell_hooks;
        FakeDesk dwell_desk;
        char title[32];
        const char *dwell_id = "30303030-3434-5656-7878-909090909099";
        const char *refused = "{\"error\":\"identity_conflict\"}";
        auto dwell_payload = make_payload(dwell_id, 16000, 0);
        reset_desk(&dwell_desk);
        fill_hooks(&dwell_desk, &dwell_hooks);
        CHECK(reach_receipt(dwell_stage, dwell, &dwell_hooks) == 1);
        CHECK(dwell.bind_test_payload(dwell_payload, g_audio, 0));
        sign_in_sender(dwell.outbox());
        dwell.outbox().begin_submit(t0);
        CHECK(dwell.outbox().prepare_http_action(&action, t0) == 1);
        memset(&transport, 0, sizeof(transport));
        transport.transport_ok = 1;
        transport.http_status = 409;
        transport.body = refused;
        transport.body_len = (int)strlen(refused);
        CHECK(dwell.outbox().apply_transport_result(action, transport, t0) == 1);
        CHECK(dwell.outbox().ui_phase() == fl_product_send::kSendUiCouldntSend);
        CHECK(dwell.outbox().terminal() == fl_product_send::kSendTerminalDiscarded);
        CHECK(dwell.outbox().ui_phase() != fl_product_send::kSendUiSent);
        CHECK(strcmp(dwell.body_label(), "couldn't send") == 0);
        CHECK(strchr(dwell.body_label(), '.') == 0);
        dwell.receipt_title(title, (int)sizeof(title));
        CHECK(strcmp(title, "sending to ...") == 0);
        result = dwell_stage.advance(t0 + fl_product_send::kSentHoldMs - 1, dwell);
        CHECK(result.place == fl_product_send::kSendPlaceReceipt);
        CHECK(result.returned_inbox == 0);
        CHECK(dwell.outbox().ui_phase() == fl_product_send::kSendUiCouldntSend);
        CHECK(dwell.outbox().prepare_http_action(&action, t0 + fl_product_send::kSentHoldMs - 1) == 0);
        result = dwell_stage.advance(t0 + fl_product_send::kSentHoldMs, dwell);
        CHECK(result.place == fl_product_send::kSendPlaceInbox);
        CHECK(result.returned_inbox == 1);
        CHECK(dwell.outbox().ui_phase() == fl_product_send::kSendUiHidden);
        CHECK(dwell.outbox().terminal() == fl_product_send::kSendTerminalDiscarded);
        CHECK(dwell.outbox().ui_phase() != fl_product_send::kSendUiSent);
        CHECK(dwell.outbox().prepare_http_action(&action, t0 + fl_product_send::kSentHoldMs) == 0);
        CHECK(strcmp(dwell.outbox().client_message_id(), dwell_id) == 0);
    }

    {
        fl_product_send::SendJourney away;
        auto away_payload = make_payload("29292929-3434-5656-7878-909090909098", 16000, 0);
        const char *away_accepted =
            "{\"schema\":\"fl.send-receipt/2\",\"client_message_id\":\"29292929-3434-5656-7878-909090909098\","
            "\"message_id\":\"msg-away\",\"state\":\"accepted\"}";
        CHECK(away.bind_test_payload(away_payload, g_audio, 0));
        sign_in_sender(away.outbox());
        away.outbox().begin_submit(0);
        CHECK(away.outbox().prepare_http_action(&action, 0) == 1);
        away.on_mute_after_finish(1);
        memset(&transport, 0, sizeof(transport));
        transport.transport_ok = 1;
        transport.http_status = 200;
        transport.body = away_accepted;
        transport.body_len = (int)strlen(away_accepted);
        CHECK(away.outbox().apply_transport_result(action, transport, 0) == 1);
        CHECK(away.outbox().terminal() == fl_product_send::kSendTerminalAccepted);
        CHECK(away.outbox().ui_phase() == fl_product_send::kSendUiHidden);
        CHECK(away.outbox().ui_phase() != fl_product_send::kSendUiSent);
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
    if (strcmp(name, "empty-copy") == 0) {
        return test_exp010_empty_copy() == 0 ? 0 : 1;
    }
    if (strcmp(name, "wait-body") == 0) {
        return test_exp028_wait_body() == 0 ? 0 : 1;
    }
    if (strcmp(name, "receipt-state") == 0) {
        return test_exp014_receipt_state() == 0 ? 0 : 1;
    }
    if (strcmp(name, "http-415") == 0) {
        return test_exp041_http_415() == 0 ? 0 : 1;
    }
    if (strcmp(name, "couldnt-dwell") == 0) {
        return test_exp034_couldnt_send_dwell() == 0 ? 0 : 1;
    }
    if (strcmp(name, "sender-binding") == 0) {
        return test_exp033_sender_binding() == 0 ? 0 : 1;
    }
    if (strcmp(name, "leave-skips-sent") == 0) {
        return test_exp031_leave_wait_skips_sent() == 0 ? 0 : 1;
    }
    if (strcmp(name, "client-uuid") == 0) {
        return test_client_message_uuid() == 0 ? 0 : 1;
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
    if (strcmp(name, "stage-buttons") == 0) {
        return test_stage_buttons() == 0 ? 0 : 1;
    }
    if (strcmp(name, "couldnt-record") == 0) {
        return test_couldnt_record() == 0 ? 0 : 1;
    }
    if (strcmp(name, "frozen-recipients") == 0) {
        return test_frozen_recipients() == 0 ? 0 : 1;
    }
    if (strcmp(name, "picker-header") == 0) {
        return test_picker_header() == 0 ? 0 : 1;
    }
    if (strcmp(name, "picker-focus") == 0) {
        return test_picker_focus() == 0 ? 0 : 1;
    }
    if (strcmp(name, "picker-refresh") == 0) {
        return test_picker_refresh() == 0 ? 0 : 1;
    }
    if (strcmp(name, "stored-part") == 0) {
        return test_stored_part_read() == 0 ? 0 : 1;
    }
    if (strcmp(name, "subtitle-overflow") == 0) {
        return test_subtitle_overflow() == 0 ? 0 : 1;
    }
    if (strcmp(name, "sent-dwell") == 0) {
        return test_sent_dwell_restores_inbox() == 0 ? 0 : 1;
    }
    if (strcmp(name, "leave-wait-place") == 0) {
        return test_leave_wait_keeps_place() == 0 ? 0 : 1;
    }
    if (strcmp(name, "sender-mismatch") == 0) {
        return test_sender_mismatch_no_action() == 0 ? 0 : 1;
    }
    if (strcmp(name, "phase7-bounds") == 0) {
        return test_phase7_session_bounds() == 0 ? 0 : 1;
    }
    if (strcmp(name, "host-p8-drive") == 0) {
        return host_p8_drive() == 0 ? 0 : 1;
    }
    test_part_size_and_count();
    test_lost_receipt_lookup();
    test_identity_conflict_discard();
    test_offline_queue();
    test_cancel_during_lookup();
    test_exp010_empty_copy();
    test_exp028_wait_body();
    test_exp014_receipt_state();
    test_exp041_http_415();
    test_exp034_couldnt_send_dwell();
    test_exp033_sender_binding();
    test_exp031_leave_wait_skips_sent();
    test_client_message_uuid();
    test_exp045_recipient_fallback();
    test_exp044_refresh_wait();
    test_exp046_inbox_fetch();
    test_mute_rules();
    test_trace_no_media();
    test_inbox_selection_restore();
    test_stage_buttons();
    test_couldnt_record();
    test_frozen_recipients();
    test_picker_header();
    test_picker_focus();
    test_picker_refresh();
    test_stored_part_read();
    test_subtitle_overflow();
    test_sent_dwell_restores_inbox();
    test_leave_wait_keeps_place();
    test_sender_mismatch_no_action();
    test_phase7_session_bounds();
    if (g_failures != 0) {
        fprintf(stderr, "product_send_model_test failures=%d\n", g_failures);
        return 1;
    }
    printf("product_send_model_test ok\n");
    return 0;
}
