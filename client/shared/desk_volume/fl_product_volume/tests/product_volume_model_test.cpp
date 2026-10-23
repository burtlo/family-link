#include "fl_product_inbox_carousel.hpp"
#include "fl_product_login_session.hpp"
#include "fl_product_volume_journey.hpp"
#include "fl_product_volume_stops.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL product_volume_model_test.cpp:%d %s\n", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

const char kSnapshot[] =
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

const char *kEight[] = {"su-01", "su-02", "su-03", "su-04", "su-05", "su-06", "su-07", "su-08"};
const char *kTokenA = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

int load_inbox(fl_product_inbox::CarouselSession &session)
{
    session.set_session_generation(1);
    return session.deliver_inbox_worker_result(1, 1, 200, kSnapshot, (int)strlen(kSnapshot), 0) == 1;
}

void establish_signed_in(fl_product_login::SessionWriter *login)
{
    fl_product_login::RosterResult roster;
    memset(&roster, 0, sizeof(roster));
    roster.http_status = 200;
    roster.valid = 1;
    roster.parse_ok = 1;
    fl_product_login::fill_users(&roster, kEight, 8);
    login->boot(0, true);
    if (login->roster_inflight()) {
        roster.operation_id = login->roster_operation_id();
    }
    login->submit_roster(roster, 0);
    login->ack_health();
    login->select_pin_user("su-01", login->now_ms());
    login->enter_pin_digit('1', login->now_ms());
    login->enter_pin_digit('2', login->now_ms());
    login->enter_pin_digit('3', login->now_ms());
    login->enter_pin_digit('4', login->now_ms());
    fl_product_login::PinResult pin;
    memset(&pin, 0, sizeof(pin));
    pin.operation_id = login->pin_operation_id();
    pin.generation = login->pin_key_generation();
    strncpy(pin.session_id, login->session_id(), sizeof(pin.session_id) - 1);
    strncpy(pin.user_id, "su-01", sizeof(pin.user_id) - 1);
    strncpy(pin.session_token, kTokenA, sizeof(pin.session_token) - 1);
    pin.session_token_valid = 1;
    pin.outcome = fl_product_login::kPinOutcomeSuccess;
    pin.http_status = 200;
    pin.parse_ok = 1;
    login->deliver_pin_completion(pin, login->now_ms());
}

int test_stop_table()
{
    CHECK(fl_product_volume::kVolumeStopCount == 13);
    CHECK(fl_product_volume::volume_codec_for_stop_index(0) == 0);
    CHECK(fl_product_volume::volume_codec_for_stop_index(1) == 78);
    CHECK(fl_product_volume::volume_codec_for_stop_index(12) == 100);
    CHECK(fl_product_volume::volume_stop_index_from_codec(100) == 12);
    CHECK(fl_product_volume::volume_stop_index_from_codec(79) == 1);
    CHECK(fl_product_volume::volume_stop_index_default() == 12);
    return g_failures;
}

int test_first_run_default_and_persist_fail()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    journey.bind_storage(&storage);
    journey.on_power_on();
    CHECK(journey.stop_index() == 12);
    CHECK(fl_product_volume::volume_codec_for_stop_index(journey.stop_index()) == 100);
    inbox.boot(0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    journey.set_stop_index(3);
    storage.set_fail_next_persist(1);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(storage.has_saved_stop() == 0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(journey.stop_index() == 3);
    return g_failures;
}

int test_red_ignored_and_connectivity()
{
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    inbox.boot(0);
    CHECK(journey.on_red_record() == 1);
    journey.enter_from_inbox(inbox, 0);
    CHECK(journey.on_red_record() == 0);
    journey.note_connectivity_lost();
    CHECK(journey.volume_body_visible() == 1);
    CHECK(journey.shows_numeric_readout() == 0);
    return g_failures;
}

int test_playback_pause_flag()
{
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    uint8_t chunk[4096];
    int delivered = 0;
    memset(chunk, 1, sizeof(chunk));
    inbox.boot(0);
    CHECK(load_inbox(inbox));
    inbox.select_index(1, 0);
    CHECK(inbox.toggle_transport(0) == 1);
    while (delivered < (int)fl_product_inbox::kAudioPrefixBytes) {
        const int step = (int)sizeof(chunk);
        (void)inbox.deliver_audio_chunk(chunk, step, 0, 1, 0);
        delivered += step;
    }
    CHECK(inbox.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    const int buffered = inbox.audio_bytes_buffered();
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(journey.playback_pause_armed() == 1);
    CHECK(inbox.settings_pause_active() == 1);
    CHECK(inbox.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(inbox.audio_bytes_buffered() == buffered);
    return g_failures;
}

int test_mute_signs_out()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    fl_product_login::SessionWriter login;
    journey.bind_storage(&storage);
    journey.on_power_on();
    inbox.boot(0);
    establish_signed_in(&login);
    CHECK(login.signed_in() == 1);
    journey.enter_from_inbox(inbox, 0);
    journey.set_stop_index(5);
    CHECK(journey.mute_from_volume(inbox, login, 0) == 1);
    CHECK(login.signed_in() == 0);
    CHECK(storage.has_saved_stop() == 1);
    CHECK(storage.saved_stop_index() == 5);
    CHECK(journey.screen() == fl_product_volume::kVolumeScreenInbox);
    return g_failures;
}

int test_signed_in_boot_return()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    fl_product_login::SessionWriter login;
    journey.bind_storage(&storage);
    journey.on_power_on();
    inbox.boot(0);
    establish_signed_in(&login);
    CHECK(login.signed_in() == 1);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(journey.screen() == fl_product_volume::kVolumeScreenVolume);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(login.signed_in() == 1);
    CHECK(journey.screen() == fl_product_volume::kVolumeScreenInbox);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(login.signed_in() == 1);
    return g_failures;
}

int test_play_head_boot_roundtrip()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    uint8_t chunk[4096];
    int delivered = 0;
    int play_head_before = 0;
    memset(chunk, 1, sizeof(chunk));
    journey.bind_storage(&storage);
    journey.on_power_on();
    inbox.boot(0);
    CHECK(load_inbox(inbox));
    inbox.select_index(0, 0);
    CHECK(inbox.toggle_transport(0) == 1);
    while (delivered < (int)fl_product_inbox::kAudioPrefixBytes + 4096) {
        const int step = (int)sizeof(chunk);
        (void)inbox.deliver_audio_chunk(chunk, step, 0, 1, 0);
        delivered += step;
    }
    play_head_before = inbox.play_head_ms();
    CHECK(play_head_before > 0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(inbox.play_head_ms() == play_head_before);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(inbox.selected_index() == 0);
    CHECK(strcmp(inbox.selected_message_id(), "msg-a") == 0);
    CHECK(inbox.play_head_ms() == play_head_before);
    return g_failures;
}

int test_restart_after_failed_persist()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    journey.bind_storage(&storage);
    journey.on_power_on();
    inbox.boot(0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    journey.set_stop_index(5);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(storage.saved_stop_index() == 5);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    journey.set_stop_index(9);
    storage.set_fail_next_persist(1);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(storage.saved_stop_index() == 5);
    CHECK(journey.stop_index() == 9);
    journey.on_power_on();
    CHECK(journey.stop_index() == 5);
    return g_failures;
}

int g_last_speaker_codec = -1;

void test_speaker_codec_hook(int codec) { g_last_speaker_codec = codec; }

int test_speaker_codec_callback()
{
    fl_product_volume::DeskVolumeJourney journey;
    g_last_speaker_codec = -1;
    journey.set_speaker_codec_callback(test_speaker_codec_hook);
    journey.set_stop_index(4);
    CHECK(g_last_speaker_codec == fl_product_volume::volume_codec_for_stop_index(4));
    journey.set_stop_index(7);
    CHECK(g_last_speaker_codec == fl_product_volume::volume_codec_for_stop_index(7));
    return g_failures;
}

int test_boot_roundtrip_journey()
{
    fl_product_volume::VolumeStorage storage;
    fl_product_volume::DeskVolumeJourney journey;
    fl_product_inbox::CarouselSession inbox;
    journey.bind_storage(&storage);
    journey.on_power_on();
    inbox.boot(0);
    CHECK(load_inbox(inbox));
    inbox.select_index(1, 0);
    const char *before_id = inbox.selected_message_id();
    CHECK(before_id != 0);
    CHECK(strcmp(before_id, "msg-b") == 0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    journey.set_stop_index(8);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(storage.saved_stop_index() == 8);
    CHECK(inbox.selected_index() == 1);
    CHECK(strcmp(inbox.selected_message_id(), before_id) == 0);
    CHECK(journey.enter_from_inbox(inbox, 0) == 1);
    CHECK(journey.stop_index() == 8);
    CHECK(journey.leave_to_inbox(inbox, 0) == 1);
    CHECK(inbox.selected_index() == 1);
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    if (strcmp(name, "stop-table") == 0) {
        return test_stop_table() == 0 ? 0 : 1;
    }
    if (strcmp(name, "persist-fail") == 0) {
        return test_first_run_default_and_persist_fail() == 0 ? 0 : 1;
    }
    if (strcmp(name, "red-ignored") == 0) {
        return test_red_ignored_and_connectivity() == 0 ? 0 : 1;
    }
    if (strcmp(name, "playback-pause") == 0) {
        return test_playback_pause_flag() == 0 ? 0 : 1;
    }
    if (strcmp(name, "mute-signout") == 0) {
        return test_mute_signs_out() == 0 ? 0 : 1;
    }
    if (strcmp(name, "boot-roundtrip") == 0) {
        return test_boot_roundtrip_journey() == 0 ? 0 : 1;
    }
    if (strcmp(name, "signed-in-boot") == 0) {
        return test_signed_in_boot_return() == 0 ? 0 : 1;
    }
    if (strcmp(name, "play-head-boot") == 0) {
        return test_play_head_boot_roundtrip() == 0 ? 0 : 1;
    }
    if (strcmp(name, "restart-persist") == 0) {
        return test_restart_after_failed_persist() == 0 ? 0 : 1;
    }
    if (strcmp(name, "speaker-callback") == 0) {
        return test_speaker_codec_callback() == 0 ? 0 : 1;
    }
    if (strcmp(name, "all") == 0) {
        test_stop_table();
        test_first_run_default_and_persist_fail();
        test_red_ignored_and_connectivity();
        test_playback_pause_flag();
        test_mute_signs_out();
        test_boot_roundtrip_journey();
        test_signed_in_boot_return();
        test_play_head_boot_roundtrip();
        test_restart_after_failed_persist();
        test_speaker_codec_callback();
        return g_failures == 0 ? 0 : 1;
    }
    fprintf(stderr, "unknown test %s\n", name);
    return 2;
}
