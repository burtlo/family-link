#include "fl_product_journey.hpp"

#include "fl_product_inbox_workers.hpp"
#include "fl_product_login_p1_wire.hpp"
#include "fl_product_login_workers.hpp"
#include "fl_product_send_picker.hpp"
#include "fl_product_send_types.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_journey {

namespace {

const char kInboxSnapshot[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_user_id\":\"u2\",\"sender_name\":\"Sam\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":500,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kInboxRev2[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-b\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_user_id\":\"u2\",\"sender_name\":\"Sam Renamed\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":500,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kRosterBody[] =
    "{\"schema\":\"fl.hangout-roster/2\",\"hangout_id\":\"h1\",\"revision\":1,"
    "\"users\":[{\"user_id\":\"user-maya\",\"display_name\":\"Maya\",\"avatar_id\":\"avatar-01\","
    "\"card_background_id\":\"card-bg-01\"},{\"user_id\":\"user-jordan\",\"display_name\":\"Jordan\","
    "\"avatar_id\":\"avatar-02\",\"card_background_id\":\"card-bg-02\"}]}";

const char kToken[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

static uint8_t g_send_audio[32768];

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL journey.cpp:%d %s\n", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void deliver_roster(HostJourney &journey)
{
    fl_product_login::SessionWriter &login = journey.login();
    const int64_t now_ms = login.now_ms();
    login.set_wifi(true, now_ms);
    login.ack_health();
    const uint64_t op = login.roster_operation_id();
    (void)fl_product_login::deliver_classified_roster(&login, op, 1, 200, kRosterBody, (int)strlen(kRosterBody),
                                                      now_ms);
}

fl_product_login::PinResult pin_ok(fl_product_login::SessionWriter &login, uint64_t operation_id)
{
    fl_product_login::PinResult result;
    memset(&result, 0, sizeof(result));
    result.operation_id = operation_id;
    result.generation = login.pin_key_generation();
    strncpy(result.session_id, login.session_id(), sizeof(result.session_id) - 1);
    strncpy(result.user_id, "user-maya", sizeof(result.user_id) - 1);
    strncpy(result.session_token, kToken, sizeof(result.session_token) - 1);
    result.session_token_valid = 1;
    result.outcome = fl_product_login::kPinOutcomeSuccess;
    result.http_status = 200;
    result.parse_ok = 1;
    return result;
}

void deliver_pin(HostJourney &journey)
{
    fl_product_login::SessionWriter &login = journey.login();
    const int64_t now_ms = login.now_ms();
    const uint64_t op = login.begin_pin_request("user-maya", "1234", now_ms);
    login.deliver_pin_completion(pin_ok(login, op), now_ms);
}

void load_inbox(HostJourney &journey)
{
    fl_product_inbox::CarouselSession &inbox = journey.inbox();
    inbox.set_wifi(true, 0);
    inbox.set_session_generation(1);
    CHECK(inbox.deliver_inbox_worker_result(1, 1, 200, kInboxSnapshot, (int)strlen(kInboxSnapshot), 0) == 1);
}

int scenario_boot_volume_mute(HostJourney &journey)
{
    journey.reset(0);
    journey.volume().on_power_on();
    journey.shell_goto(FL_SHELL_STAGE_ROSTER);
    load_inbox(journey);
    deliver_roster(journey);
    deliver_pin(journey);
    journey.shell_goto(FL_SHELL_STAGE_INBOX);
    CHECK(journey.signed_in() == 1);
    CHECK(journey.volume().enter_from_inbox(journey.inbox(), 0) == 1);
    journey.shell_goto(FL_SHELL_STAGE_VOLUME);
    CHECK(journey.volume_screen_active() == 1);
    journey.volume().set_stop_index(4);
    CHECK(journey.volume().mute_from_volume(journey.inbox(), journey.login(), 0) == 1);
    journey.shell_goto(FL_SHELL_STAGE_ROSTER);
    CHECK(journey.signed_in() == 0);
    printf("scenario=boot-volume-mute stage=%u signed_in=%d\n", (unsigned)journey.shell_stage(),
           journey.signed_in());
    return g_failures;
}

int scenario_roster_during_pin(HostJourney &journey)
{
    fl_product_login::SessionWriter &login = journey.login();
    journey.reset(0);
    deliver_roster(journey);
    login.select_pin_user("user-maya", 0);
    login.begin_pin_request("user-maya", "1", 0);
    const uint64_t roster_op = login.roster_operation_id();
    fl_product_login::deliver_classified_roster(&login, roster_op, 1, 200, kRosterBody, (int)strlen(kRosterBody), 1);
    CHECK(login.pin_phase() != fl_product_login::kPinPhaseIdle);
    CHECK(login.pin_phase() == fl_product_login::kPinPhaseEntry ||
           login.pin_phase() == fl_product_login::kPinPhaseChecking);
    printf("scenario=roster-during-pin pin_phase=%d selectable=%d\n", (int)login.pin_phase(),
           login.selectable_cards());
    return g_failures;
}

int scenario_inbox_snapshot_during_playback(HostJourney &journey)
{
    uint8_t chunk[2048];
    int delivered = 0;
    journey.reset(0);
    load_inbox(journey);
    journey.inbox().select_index(0, 0);
    CHECK(journey.inbox().toggle_transport(0) == 1);
    const uint32_t playback_op = journey.inbox().playback_operation_id();
    memset(chunk, 1, sizeof(chunk));
    while (delivered < (int)fl_product_inbox::kAudioPrefixBytes + (int)sizeof(chunk)) {
        (void)journey.inbox().deliver_audio_chunk(playback_op, journey.inbox().selected_message_id(), chunk,
                                                  (int)sizeof(chunk), 0, 1, 0);
        delivered += (int)sizeof(chunk);
    }
    const int duration_ms = (int)journey.inbox().message_at(0)->duration_ms;
    const int byte_count = (int)journey.inbox().message_at(0)->audio.byte_count;
    CHECK(journey.inbox().audio_bytes_buffered() >= byte_count);
    CHECK(journey.inbox().play_head_ms() == duration_ms);
    CHECK(journey.inbox().playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(journey.inbox().transport_state() == fl_product_inbox::kTransportPlay);
    CHECK(journey.inbox().deliver_inbox_worker_result(1, 1, 200, kInboxRev2, (int)strlen(kInboxRev2), 0) == 1);
    CHECK(journey.inbox().play_head_ms() == duration_ms);
    CHECK(journey.inbox().playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(strcmp(journey.inbox().selected_message_id(), "msg-a") == 0);
    printf("scenario=inbox-snapshot-during-playback play_head_ms=%d revision_ok=1\n", duration_ms);
    return g_failures;
}

int scenario_refresh_record_volume(HostJourney &journey)
{
    journey.reset(0);
    load_inbox(journey);
    deliver_roster(journey);
    deliver_pin(journey);
    CHECK(journey.volume().enter_from_inbox(journey.inbox(), 0) == 1);
    journey.volume().note_connectivity_lost();
    CHECK(journey.volume().volume_body_visible() == 1);
    CHECK(journey.volume().on_red_record() == 0);
    journey.send().set_refresh_failed(1);
    const int fetch_may_proceed = journey.send().inbox_fetch_may_proceed();
    CHECK(journey.volume().volume_body_visible() == 1);
    printf("scenario=refresh-during-record-volume volume_visible=%d fetch_may_proceed=%d\n",
           journey.volume().volume_body_visible(), fetch_may_proceed);
    return g_failures;
}

fl_product_send::FrozenPayload make_send_payload(const char *client_id)
{
    fl_product_send::FrozenPayload payload;
    memset(&payload, 0, sizeof(payload));
    strncpy(payload.client_message_id, client_id, sizeof(payload.client_message_id) - 1);
    strncpy(payload.recipient_user_ids[0], "user-jordan", fl_product_send::kMaxUserId - 1);
    payload.recipient_count = 1;
    payload.audio_byte_count = 16000;
    payload.duration_ms = 500;
    memset(g_send_audio, 0x5a, payload.audio_byte_count);
    return payload;
}

int scenario_send_inbox_overlap(HostJourney &journey)
{
    fl_product_send::SendJourney &send = journey.send();
    fl_product_send::HangoutMember member;
    journey.reset(0);
    load_inbox(journey);
    send.remember_inbox_selection("msg-a");
    send.set_session_generation(1);
    send.set_wifi(1);
    memset(&member, 0, sizeof(member));
    strncpy(member.user_id, "user-jordan", sizeof(member.user_id) - 1);
    strncpy(member.display_name, "Jordan", sizeof(member.display_name) - 1);
    CHECK(send.picker().load_members(&member, 1, "user-maya") == 1);
    send.picker().toggle_selection(0);
    CHECK(send.bind_test_payload(make_send_payload("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"), g_send_audio, 0) == 1);
    CHECK(send.auto_submit_when_ready(0) == 1);
    CHECK(send.inbox_fetch_may_proceed() == 1);
    (void)journey.inbox().deliver_inbox_worker_result(1, 1, 200, kInboxRev2, (int)strlen(kInboxRev2), 0);
    CHECK(strcmp(send.restored_inbox_selection(), "msg-a") == 0);
    printf("scenario=send-while-inbox-changes restored=%s streaming=%d\n", send.restored_inbox_selection(),
           send.streaming_active());
    return g_failures;
}

int scenario_mute_during_media_fetch(HostJourney &journey)
{
    journey.reset(0);
    load_inbox(journey);
    deliver_roster(journey);
    deliver_pin(journey);
    journey.inbox().select_index(0, 0);
    fl_product_login::WorkerTicket ticket = fl_product_inbox::begin_media_worker(&journey.login(), 0);
    CHECK(ticket.operation_id != 0);
    journey.login().mute(0);
    CHECK(journey.signed_in() == 0);
    CHECK(fl_product_login::deliver_worker_completion(&journey.login(), ticket, ticket.generation, 200, 0) == 0);
    printf("scenario=mute-during-media-fetch signed_in=%d worker_rejected=1\n", journey.signed_in());
    return g_failures;
}

int scenario_boot_pending_send(HostJourney &journey)
{
    fl_product_send::SendJourney &send = journey.send();
    journey.reset(0);
    load_inbox(journey);
    send.set_wifi(1);
    send.remember_inbox_selection("msg-b");
    CHECK(send.bind_test_payload(make_send_payload("bbbbbbbb-cccc-dddd-eeee-ffffffffffff"), g_send_audio, 0) == 1);
    send.outbox().begin_submit(0);
    CHECK(send.streaming_active() == 1);
    CHECK(journey.volume().on_red_record() == 1);
    printf("scenario=boot-during-pending-send streaming=%d red_ok=%d\n", send.streaming_active(),
           journey.volume().on_red_record());
    return g_failures;
}

} /* namespace */

HostJourney::HostJourney() : now_ms_(0) { reset(0); }

void HostJourney::reset(int boot_muted)
{
    now_ms_ = 0;
    login_.boot(0, boot_muted != 0);
    inbox_.boot(0);
    volume_storage_ = fl_product_volume::VolumeStorage();
    volume_ = fl_product_volume::DeskVolumeJourney();
    volume_.bind_storage(&volume_storage_);
    volume_.on_power_on();
    send_.reset();
    fl_shell_session_init(&shell_, 1u, FL_SHELL_STAGE_ROSTER);
}

void HostJourney::advance(int64_t now_ms)
{
    now_ms_ = now_ms;
    login_.advance_to(now_ms);
    login_.drain();
    inbox_.advance_to(now_ms);
    send_.advance_to(now_ms);
}

fl_product_login::SessionWriter &HostJourney::login() { return login_; }

fl_product_inbox::CarouselSession &HostJourney::inbox() { return inbox_; }

fl_product_volume::DeskVolumeJourney &HostJourney::volume() { return volume_; }

fl_product_volume::VolumeStorage &HostJourney::volume_storage() { return volume_storage_; }

fl_product_send::SendJourney &HostJourney::send() { return send_; }

fl_shell_session_t &HostJourney::shell() { return shell_; }

void HostJourney::shell_goto(fl_shell_stage_t stage)
{
    uint32_t op = shell_.operation_id_watermark + 1u;
    if (fl_shell_begin_stage_transition(&shell_, 1u, stage, op)) {
        (void)fl_shell_finish_stage_transition(&shell_, op);
    }
}

fl_shell_stage_t HostJourney::shell_stage() const { return shell_.panels[0].stage; }

int HostJourney::signed_in() const { return login_.signed_in(); }

int HostJourney::volume_screen_active() const
{
    return volume_.screen() == fl_product_volume::kVolumeScreenVolume ? 1 : 0;
}

int run_scripted_scenario(const char *scenario_id)
{
    g_failures = 0;
    HostJourney journey;
    if (strcmp(scenario_id, "boot-volume-mute") == 0) {
        return scenario_boot_volume_mute(journey);
    }
    if (strcmp(scenario_id, "roster-during-pin") == 0) {
        return scenario_roster_during_pin(journey);
    }
    if (strcmp(scenario_id, "inbox-snapshot-during-playback") == 0) {
        return scenario_inbox_snapshot_during_playback(journey);
    }
    if (strcmp(scenario_id, "refresh-during-record-volume") == 0) {
        return scenario_refresh_record_volume(journey);
    }
    if (strcmp(scenario_id, "send-while-inbox-changes") == 0) {
        return scenario_send_inbox_overlap(journey);
    }
    if (strcmp(scenario_id, "mute-during-media-fetch") == 0) {
        return scenario_mute_during_media_fetch(journey);
    }
    if (strcmp(scenario_id, "boot-during-pending-send") == 0) {
        return scenario_boot_pending_send(journey);
    }
    fprintf(stderr, "unknown scenario: %s\n", scenario_id);
    return 1;
}

} /* namespace fl_product_journey */
