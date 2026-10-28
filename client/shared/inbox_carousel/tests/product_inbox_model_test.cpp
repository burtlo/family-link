#include "fl_product_inbox_carousel.hpp"
#include "fl_product_inbox_flsk2.hpp"
#include "fl_product_inbox_p1_wire.hpp"
#include "fl_product_inbox_presentation.hpp"
#include "fl_product_inbox_wire.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL %s:%d %s\n", "product_inbox_model_test.cpp", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

const char kSnapshotA[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":131072,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_user_id\":\"u2\",\"sender_name\":\"Sam\","
    "\"sender_avatar_id\":\"bad-avatar\",\"sender_card_background_id\":\"bad-card\","
    "\"duration_ms\":500,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kSnapshotARev1[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari Renamed\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_user_id\":\"u2\",\"sender_name\":\"Sam\","
    "\"sender_avatar_id\":\"bad-avatar\",\"sender_card_background_id\":\"bad-card\","
    "\"duration_ms\":500,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kSnapshotSketchA[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":1,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{"
    "\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":131072,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
    "\"sketch\":{\"format_id\":\"flsk2\",\"byte_count\":8,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}"
    "}}]}";

const char kSnapshotAPlusC[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":3,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"next_cursor\":null,"
    "\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_user_id\":\"u1\",\"sender_name\":\"Ari\","
    "\"sender_avatar_id\":\"avatar-01\",\"sender_card_background_id\":\"card-bg-02\","
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_user_id\":\"u2\",\"sender_name\":\"Sam\","
    "\"sender_avatar_id\":\"avatar-02\",\"sender_card_background_id\":\"card-bg-03\","
    "\"duration_ms\":500,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}},"
    "{\"message_id\":\"msg-c\",\"sender_user_id\":\"u3\",\"sender_name\":\"Jordan\","
    "\"sender_avatar_id\":\"avatar-03\",\"sender_card_background_id\":\"card-bg-04\","
    "\"duration_ms\":800,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":8000,"
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}}}"
    "]}";

const char kSnapshotMissingLast[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":3,"
    "\"last_viewed_message_id\":\"missing\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\",\"duration_ms\":500,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}},"
    "{\"message_id\":\"msg-c\",\"sender_name\":\"Jordan\",\"duration_ms\":800,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":8000,"
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}}}"
    "]}";

const char kSnapshotOrderAcb[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":3,"
    "\"last_viewed_message_id\":\"msg-c\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-c\",\"sender_name\":\"Jordan\",\"duration_ms\":800,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":8000,"
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\",\"duration_ms\":500,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kSnapshotDeleteMiddle[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":3,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-c\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}},"
    "{\"message_id\":\"msg-c\",\"sender_name\":\"Jordan\",\"duration_ms\":800,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":8000,"
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}}}"
    "]}";

const char kSnapshotDeleteOldest[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":3,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\",\"duration_ms\":500,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}},"
    "{\"message_id\":\"msg-c\",\"sender_name\":\"Jordan\",\"duration_ms\":800,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":8000,"
    "\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}}}"
    "]}";

const char kSnapshotEmpty[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":0,"
    "\"last_viewed_message_id\":null,\"has_more\":false,\"items\":[]}";

const char kSnapshotEmptyFirst[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":0,"
    "\"last_viewed_message_id\":null,\"has_more\":false,\"items\":[]}";

const char kSnapshotOneNew[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":1,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}}"
    "]}";

const char kSnapshotPlayPair[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":131072,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
    "\"sketch\":{\"format_id\":\"flsk2\",\"byte_count\":8,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\",\"duration_ms\":500,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kSnapshotCoverPlay[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":1,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":1000,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":65536,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
    "\"sketch\":{\"format_id\":\"flsk2\",\"byte_count\":16,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

const char kSnapshotShortPair[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"last_viewed_message_id\":\"msg-a\",\"has_more\":false,\"items\":["
    "{\"message_id\":\"msg-a\",\"sender_name\":\"Ari\",\"duration_ms\":128,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":4096,"
    "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
    "\"sketch\":{\"format_id\":\"flsk2\",\"byte_count\":16,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}},"
    "{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\",\"duration_ms\":500,"
    "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":16000,"
    "\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}"
    "]}";

void open_inbox(fl_product_inbox::CarouselSession &session, uint32_t generation)
{
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(generation);
}

int test_snapshot_apply()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(7);
    CHECK(session.deliver_inbox_worker_result(7, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.deliver_inbox_worker_result(7, 1, 200, kSnapshotARev1, (int)strlen(kSnapshotARev1), 10) ==
          1);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.message_at(0)->sender_name, "Ari Renamed") == 0);
    CHECK(session.selected_index() == 0);
    CHECK(session.deliver_inbox_worker_result(7, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 20) ==
          1);
    CHECK(session.message_count() == 3);
    CHECK(strcmp(session.message_at(2)->message_id, "msg-c") == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.unread_badge_shown() == 0);
    return g_failures;
}

int test_last_viewed_coalesce()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    session.select_index(1, 0);
    session.advance_to(500);
    CHECK(session.pending_last_viewed_message_id()[0] == 0);
    session.advance_to(1000);
    CHECK(strcmp(session.pending_last_viewed_message_id(), "msg-b") == 0);
    session.select_index(0, 2000);
    session.advance_to(2500);
    CHECK(session.pending_last_viewed_message_id()[0] == 0);
    session.advance_to(3000);
    CHECK(strcmp(session.pending_last_viewed_message_id(), "msg-a") == 0);
    return g_failures;
}

int test_connectivity_no_play()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint32_t op;
    int play_head;
    int buffered;
    int delivered = 0;
    g_failures = 0;
    memset(chunk, 1, sizeof(chunk));
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    while (delivered < (int)fl_product_inbox::kAudioPrefixBytes + (int)sizeof(chunk)) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        delivered += (int)sizeof(chunk);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    play_head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    CHECK(play_head > 0);
    CHECK(buffered > 0);
    session.set_wifi(false, 0);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNoWifi);
    CHECK(session.play_disabled() == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 10) == 0);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.play_head_ms() == play_head);
    session.set_wifi(true, 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.playback_operation_id() == 0);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.toggle_transport(100) == 0);
    CHECK(session.play_head_ms() == play_head);
    return g_failures;
}

int test_prefix_and_hash_pause()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint32_t op;
    int play_head;
    g_failures = 0;
    memset(chunk, 2, sizeof(chunk));
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    CHECK(op != 0);
    for (int i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.prefix_play_ready() == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    play_head = session.play_head_ms();
    CHECK(play_head > 0);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 1, 0, 100) == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackCantPlay);
    CHECK(session.play_head_ms() == play_head);
    session.advance_to(2099);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackCantPlay);
    session.advance_to(2100);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.play_disabled() == 0);
    CHECK(session.transport_state() == fl_product_inbox::kTransportPlay);
    return g_failures;
}

int test_pause_for_record()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    int play_head = 0;
    g_failures = 0;
    memset(chunk, 2, sizeof(chunk));
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    {
        uint32_t op = session.playback_operation_id();
        for (int i = 0; i < 8; ++i) {
            CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        }
        CHECK(session.prefix_play_ready() == 1);
        CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 10) == 1);
    }
    play_head = session.play_head_ms();
    CHECK(play_head > 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    session.pause_for_record(20);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_head_ms() == play_head);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.settings_pause_active() == 0);
    return g_failures;
}

int test_presentation_fallback()
{
    fl_product_inbox::InboxMessage message;
    fl_product_inbox::SenderPresentation view;
    g_failures = 0;
    memset(&message, 0, sizeof(message));
    strncpy(message.sender_name, "Sam", sizeof(message.sender_name) - 1);
    strncpy(message.sender_avatar_id, "nope", sizeof(message.sender_avatar_id) - 1);
    strncpy(message.sender_card_background_id, "nope", sizeof(message.sender_card_background_id) - 1);
    view = fl_product_inbox::resolve_sender_presentation(message);
    CHECK(view.use_initials_fallback == 1);
    CHECK(view.use_card_fallback == 1);
    CHECK(strcmp(view.card_background_id, fl_product_inbox::kFallbackCardBackground) == 0);
    return g_failures;
}

int test_recovery_fetch_connecting()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.message_count() == 2);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.recovery_fetch_due(100) == 0);
    CHECK(session.recovery_fetch_due(3100) == 1);
    {
        const char lower[] =
            "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":0,"
            "\"last_viewed_message_id\":\"msg-b\",\"has_more\":false,"
            "\"items\":[{\"message_id\":\"msg-b\",\"sender_name\":\"Sam\","
            "\"duration_ms\":1,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\","
            "\"byte_count\":1,\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}}}]}";
        CHECK(session.deliver_inbox_worker_result(1, 1, 200, lower, (int)strlen(lower), 200) == 0);
    }
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    return g_failures;
}

int test_inbox_fetch_60s()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.inbox_fetch_due(59999) == 0);
    CHECK(session.inbox_fetch_due(60000) == 1);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    CHECK(session.inbox_fetch_due(70000) == 0);
    return g_failures;
}

int test_refresh_503_keeps_inbox()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 50);
    CHECK(session.message_count() == 2);
    CHECK(session.selected_index() == 1);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    return g_failures;
}

int test_refresh_failure_stays()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[8];
    uint32_t op;
    uint32_t generation;
    int play_head;
    int buffered;
    int sketch_len;
    int selected;
    char selected_id[48];
    g_failures = 0;
    memset(chunk, 2, sizeof(chunk));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0;
    sketch[3] = 0xf8;
    sketch[4] = 0;
    sketch[5] = 0;
    sketch[6] = 0;
    sketch[7] = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(9);
    CHECK(session.deliver_inbox_worker_result(9, 1, 200, kSnapshotPlayPair, (int)strlen(kSnapshotPlayPair),
                                              0) == 1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    {
        int i;
        for (i = 0; i < 8; ++i) {
            CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        }
    }
    CHECK(session.prefix_play_ready() == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 10) == 1);
    CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 8, 10) == 1);
    play_head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    sketch_len = session.sketch_bytes_stored();
    selected = session.selected_index();
    generation = session.session_generation();
    strncpy(selected_id, session.selected_message_id(), sizeof(selected_id) - 1);
    selected_id[sizeof(selected_id) - 1] = 0;
    CHECK(play_head > 0);
    CHECK(play_head < (int)session.message_at(0)->duration_ms);
    CHECK(buffered < (int)session.message_at(0)->audio.byte_count);
    CHECK(sketch_len == 8);
    CHECK(strcmp(selected_id, "msg-a") == 0);
    session.note_refresh_failure(40);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.play_disabled() == 1);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.selected_index() == selected);
    CHECK(strcmp(session.selected_message_id(), selected_id) == 0);
    CHECK(session.sketch_bytes_stored() == sketch_len);
    CHECK(session.session_generation() == generation);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 50) == 0);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.sketch_bytes_stored() == sketch_len);
    session.note_refresh_success(60);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.playback_phase() != fl_product_inbox::kPlaybackPlaying);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.selected_index() == selected);
    CHECK(strcmp(session.selected_message_id(), selected_id) == 0);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.session_generation() == generation);
    return g_failures;
}

int test_level1_new_card()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.last_update_level() == 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 10) ==
          1);
    CHECK(session.last_update_level() == 1);
    CHECK(session.message_count() == 3);
    CHECK(strcmp(session.message_at(2)->message_id, "msg-c") == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    return g_failures;
}

int test_sketch_through_session()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[8];
    g_failures = 0;
    memset(chunk, 3, sizeof(chunk));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0;
    sketch[3] = 0xf8;
    sketch[4] = 0;
    sketch[5] = 0;
    sketch[6] = 0;
    sketch[7] = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    {
        uint32_t op = session.playback_operation_id();
        for (int i = 0; i < 8; ++i) {
            CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        }
        CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
        CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 8, 0) == 1);
    }
    CHECK(session.sketch_points_rendered() == 1);
    CHECK(session.sketch_visible() == 1);
    CHECK(session.portrait_hidden() == 1);
    return g_failures;
}

int test_flsk2_vectors()
{
    const char *hex_vectors[] = {
        "0a0a00f8000000000b0a00f80a000000323200f864000000333200f86e000000",
        "3c3c00f800000000",
        "000000f800000000010100f805000000020200f80a000000030300f80f000000040400f814000000050500f819000000",
    };
    int sizes[] = {8, 16, 64, 256};
    g_failures = 0;
    for (int v = 0; v < 3; ++v) {
        uint8_t data[256];
        int len = 0;
        for (const char *p = hex_vectors[v]; *p; p += 2) {
            char pair[3] = {p[0], p[1], 0};
            data[len++] = (uint8_t)strtoul(pair, 0, 16);
        }
        uint16_t count = 0;
        CHECK(fl_product_inbox::flsk2_validate(data, (size_t)len, &count) == 1);
        uint16_t rendered = 0;
        uint16_t pixels[fl_product_inbox::kSketchPixels];
        memset(pixels, 0xff, sizeof(pixels));
        fl_product_inbox::flsk2_paint_prefix(pixels, fl_product_inbox::kSketchPixels, data, (size_t)len,
                                             10000, &rendered);
        CHECK(rendered == count);
    }
    for (int i = 0; i < 4; ++i) {
        uint8_t buf[256];
        memset(buf, 0, sizeof(buf));
        int bytes = sizes[i];
        uint16_t points = (uint16_t)(bytes / 8);
        for (uint16_t p = 0; p < points; ++p) {
            buf[p * 8] = (uint8_t)(p % 120);
            buf[p * 8 + 1] = (uint8_t)((p * 2) % 120);
        }
        CHECK(fl_product_inbox::flsk2_validate(buf, (size_t)bytes, 0) == 1);
    }
    return g_failures;
}

int test_newest_fallback()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotMissingLast, (int)strlen(kSnapshotMissingLast),
                                              0) == 1);
    CHECK(session.message_count() == 3);
    CHECK(session.selected_index() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-c") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.transport_state() == fl_product_inbox::kTransportPlay);
    CHECK(session.play_head_ms() == 0);
    return g_failures;
}

int test_last_viewed_no_steal()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotOrderAcb, (int)strlen(kSnapshotOrderAcb), 10) ==
          1);
    CHECK(session.message_count() == 3);
    CHECK(strcmp(session.message_at(0)->message_id, "msg-a") == 0);
    CHECK(strcmp(session.message_at(1)->message_id, "msg-c") == 0);
    CHECK(strcmp(session.message_at(2)->message_id, "msg-b") == 0);
    CHECK(session.selected_index() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    return g_failures;
}

int test_delete_selected_middle()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 0) == 1);
    session.select_index(1, 0);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotDeleteMiddle, (int)strlen(kSnapshotDeleteMiddle),
                                              0) == 1);
    CHECK(session.message_count() == 2);
    CHECK(session.selected_index() == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    session.advance_to(999);
    CHECK(session.pending_last_viewed_message_id()[0] == 0);
    session.advance_to(1000);
    CHECK(strcmp(session.pending_last_viewed_message_id(), "msg-a") == 0);
    CHECK(session.last_viewed_write_due(1000) == 1);
    return g_failures;
}

int test_delete_selected_oldest()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 0) == 1);
    CHECK(session.selected_index() == 0);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotDeleteOldest, (int)strlen(kSnapshotDeleteOldest),
                                              0) == 1);
    CHECK(session.message_count() == 2);
    CHECK(session.selected_index() == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    return g_failures;
}

int test_delete_only_message()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) == 1);
    CHECK(session.message_count() == 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotEmpty, (int)strlen(kSnapshotEmpty), 10) == 1);
    CHECK(session.message_count() == 0);
    CHECK(session.selected_index() == -1);
    CHECK(session.selected_message_id()[0] == 0);
    CHECK(session.play_disabled() == 1);
    CHECK(session.transport_state() == fl_product_inbox::kTransportHidden);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.has_valid_inbox() == 1);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNone);
    return g_failures;
}

int test_selection_keeps_play_head()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[8];
    uint32_t op;
    int play_head;
    int rendered;
    int i;
    g_failures = 0;
    memset(chunk, 4, sizeof(chunk));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0;
    sketch[3] = 0xf8;
    sketch[4] = 0;
    sketch[5] = 0;
    sketch[6] = 0;
    sketch[7] = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotPlayPair, (int)strlen(kSnapshotPlayPair), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 8, 0) == 1);
    play_head = session.play_head_ms();
    rendered = session.sketch_points_rendered();
    CHECK(play_head > 0);
    CHECK(play_head < 1000);
    CHECK(rendered >= 1);
    session.select_index(1, 20);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.play_head_ms() == 0);
    CHECK(session.sketch_points_rendered() == 0);
    CHECK(session.playback_operation_id() == 0);
    session.select_index(0, 30);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.sketch_points_rendered() == rendered);
    CHECK(session.playback_operation_id() == 0);
    return g_failures;
}

int test_completed_replay()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[16];
    uint32_t op;
    g_failures = 0;
    memset(chunk, 5, sizeof(chunk));
    memset(sketch, 0, sizeof(sketch));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0xf8;
    sketch[8] = 12;
    sketch[9] = 12;
    sketch[10] = 0xf8;
    sketch[12] = 100;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotShortPair, (int)strlen(kSnapshotShortPair),
                                              0) == 1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 16, 0) == 1);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    CHECK(session.play_head_ms() == 128);
    CHECK(session.sketch_points_rendered() == 2);
    CHECK(session.playback_phase() != fl_product_inbox::kPlaybackPlaying);
    CHECK(session.transport_state() == fl_product_inbox::kTransportPlay);
    session.select_index(1, 10);
    session.select_index(0, 20);
    CHECK(session.play_head_ms() == 128);
    CHECK(session.sketch_points_rendered() == 2);
    CHECK(session.toggle_transport(30) == 1);
    CHECK(session.play_head_ms() == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.sketch_points_rendered() == 1);
    CHECK(session.playback_operation_id() != 0);
    return g_failures;
}

int test_wifi_drop_keeps_progress()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[8];
    uint32_t op;
    int play_head;
    int rendered;
    int buffered;
    int i;
    g_failures = 0;
    memset(chunk, 6, sizeof(chunk));
    sketch[0] = 8;
    sketch[1] = 8;
    sketch[2] = 0;
    sketch[3] = 0xf8;
    sketch[4] = 0;
    sketch[5] = 0;
    sketch[6] = 0;
    sketch[7] = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 8, 0) == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    play_head = session.play_head_ms();
    rendered = session.sketch_points_rendered();
    buffered = session.audio_bytes_buffered();
    CHECK(play_head > 0);
    CHECK(rendered >= 1);
    CHECK(buffered > 0);
    session.set_wifi(false, 50);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNoWifi);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.play_disabled() == 1);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.sketch_points_rendered() == rendered);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 60) == 0);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.sketch_points_rendered() == rendered);
    session.set_wifi(true, 70);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNone);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.sketch_points_rendered() == rendered);
    CHECK(session.audio_bytes_buffered() == buffered);
    return g_failures;
}

int test_chunks_cover_asset()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t sketch[16];
    uint32_t op;
    int i;
    int byte_count;
    int duration_ms;
    int chunks_to_cover;
    g_failures = 0;
    memset(chunk, 9, sizeof(chunk));
    memset(sketch, 0, sizeof(sketch));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0xf8;
    sketch[8] = 20;
    sketch[9] = 20;
    sketch[10] = 0xf8;
    sketch[12] = 0xe8;
    sketch[13] = 0x03;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotCoverPlay, (int)strlen(kSnapshotCoverPlay),
                                              0) == 1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    byte_count = (int)session.message_at(0)->audio.byte_count;
    duration_ms = (int)session.message_at(0)->duration_ms;
    chunks_to_cover = byte_count / (int)sizeof(chunk);
    CHECK(byte_count > (int)fl_product_inbox::kAudioPrefixBytes);
    CHECK(byte_count % (int)sizeof(chunk) == 0);
    CHECK(chunks_to_cover > 8);
    for (i = 0; i < 7; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackBuffering);
    CHECK(session.play_head_ms() == 0);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.audio_bytes_buffered() < byte_count);
    CHECK(session.play_head_ms() ==
          (int)(((int64_t)session.audio_bytes_buffered() * duration_ms) / byte_count));
    CHECK(session.play_head_ms() > (int)(((int64_t)sizeof(chunk) * duration_ms) / byte_count));
    CHECK(session.play_head_ms() < duration_ms);
    CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 16, 0) == 1);
    CHECK(session.sketch_points_rendered() == 1);
    for (i = 8; i < chunks_to_cover - 1; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
        CHECK(session.play_head_ms() < duration_ms);
        CHECK(session.audio_bytes_buffered() < byte_count);
    }
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    CHECK(session.audio_bytes_buffered() >= byte_count);
    CHECK(session.play_head_ms() == duration_ms);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.transport_state() == fl_product_inbox::kTransportPlay);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.sketch_points_rendered() == 2);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 1) == 0);
    CHECK(session.play_head_ms() == duration_ms);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    return g_failures;
}

int test_mute_retires_generation()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint32_t op;
    int i;
    g_failures = 0;
    memset(chunk, 7, sizeof(chunk));
    open_inbox(session, 4);
    CHECK(session.deliver_inbox_worker_result(4, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.play_head_ms() > 0);
    session.on_mute(10);
    CHECK(session.play_head_ms() == 0);
    CHECK(session.deliver_inbox_worker_result(4, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 20) == 0);
    CHECK(session.message_count() == 2);
    CHECK(session.inbox_revision() == 1);
    CHECK(session.play_head_ms() == 0);
    return g_failures;
}

int test_generation_zero()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    CHECK(session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 0);
    CHECK(session.message_count() == 0);
    CHECK(session.has_valid_inbox() == 0);
    session.set_session_generation(5);
    CHECK(session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 10) == 0);
    CHECK(session.message_count() == 0);
    CHECK(session.has_valid_inbox() == 0);
    return g_failures;
}

int test_connecting_waits()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.recovery_fetch_due(3099) == 0);
    CHECK(session.recovery_fetch_due(3100) == 1);
    session.advance_to(3099);
    CHECK(session.recovery_fetch_due(3100) == 1);
    session.advance_to(3100);
    CHECK(session.recovery_fetch_due(3100) == 0);
    CHECK(session.recovery_fetch_due(4099) == 0);
    CHECK(session.recovery_fetch_due(4100) == 1);
    session.advance_to(4100);
    CHECK(session.recovery_fetch_due(6099) == 0);
    CHECK(session.recovery_fetch_due(6100) == 1);
    session.advance_to(6100);
    CHECK(session.recovery_fetch_due(10099) == 0);
    CHECK(session.recovery_fetch_due(10100) == 1);
    session.advance_to(10100);
    CHECK(session.recovery_fetch_due(40099) == 0);
    CHECK(session.recovery_fetch_due(40100) == 1);
    session.advance_to(40100);
    CHECK(session.recovery_fetch_due(70099) == 0);
    CHECK(session.recovery_fetch_due(70100) == 1);
    return g_failures;
}

int test_snapshot_overflow()
{
    fl_product_inbox::CarouselSession session;
    char body[32768];
    int used;
    int i;
    fl_product_inbox::InboxParseResult parsed;
    g_failures = 0;
    open_inbox(session, 3);
    CHECK(session.deliver_inbox_worker_result(3, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.message_count() == 2);
    used = snprintf(body, sizeof(body),
                    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":9,\"total_count\":65,"
                    "\"last_viewed_message_id\":null,\"has_more\":false,\"items\":[");
    CHECK(used > 0);
    for (i = 0; i < 65; ++i) {
        int room = (int)sizeof(body) - used;
        int n = snprintf(body + used, (size_t)room,
                         "%s{\"message_id\":\"m%02d\",\"sender_name\":\"N\",\"duration_ms\":10,"
                         "\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":100,"
                         "\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}}",
                         i == 0 ? "" : ",", i);
        CHECK(n > 0);
        CHECK(n < room);
        used += n;
    }
    {
        int room = (int)sizeof(body) - used;
        int n = snprintf(body + used, (size_t)room, "]}");
        CHECK(n > 0);
        CHECK(n < room);
        used += n;
    }
    CHECK(fl_product_inbox::parse_inbox_snapshot(body, used, used, &parsed) == 0);
    CHECK(parsed.overflow == 1);
    CHECK(parsed.parse_ok == 0);
    CHECK(parsed.message_count == 0);
    CHECK(session.deliver_inbox_worker_result(3, 1, 200, body, used, 50) == 0);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.message_at(0)->message_id, "msg-a") == 0);
    CHECK(strcmp(session.message_at(1)->message_id, "msg-b") == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.has_valid_inbox() == 1);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.play_disabled() == 1);
    return g_failures;
}

int test_connecting_before_snapshot()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNoWifi);
    CHECK(session.play_disabled() == 1);
    CHECK(session.has_valid_inbox() == 0);
    session.set_wifi(true, 0);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.play_disabled() == 1);
    CHECK(session.has_valid_inbox() == 0);
    CHECK(session.message_count() == 0);
    CHECK(session.toggle_transport(0) == 0);
    CHECK(session.transport_state() != fl_product_inbox::kTransportPlay || session.play_disabled() == 1);
    return g_failures;
}

int test_empty_then_first_message()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotEmptyFirst, (int)strlen(kSnapshotEmptyFirst),
                                              0) == 1);
    CHECK(session.has_valid_inbox() == 1);
    CHECK(session.message_count() == 0);
    CHECK(session.selected_index() == -1);
    CHECK(session.play_disabled() == 1);
    CHECK(session.transport_state() == fl_product_inbox::kTransportHidden);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNone);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotOneNew, (int)strlen(kSnapshotOneNew), 10) == 1);
    CHECK(session.message_count() == 1);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.play_disabled() == 0);
    CHECK(session.transport_state() == fl_product_inbox::kTransportPlay);
    return g_failures;
}

int test_sketch_validation_keeps_playing()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    uint8_t bad[8];
    uint32_t op;
    int play_head;
    int i;
    g_failures = 0;
    memset(chunk, 8, sizeof(chunk));
    memset(bad, 0, sizeof(bad));
    bad[0] = 200;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    play_head = session.play_head_ms();
    CHECK(session.deliver_sketch_chunk(op, "msg-a", bad, 8, 5) == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.sketch_points_rendered() == 0);
    CHECK(session.play_head_ms() == play_head);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 6) == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.play_head_ms() > play_head);
    return g_failures;
}

void tick_wire(fl_product_inbox::CarouselSession &session, fl_product_inbox::InboxWirePlanner &planner,
               fl_product_inbox::InboxWireSlots *slots, int64_t now_ms)
{
    int ordinary = session.inbox_fetch_due(now_ms);
    int recovery = session.recovery_fetch_due(now_ms);
    session.advance_to(now_ms);
    planner.collect(session, slots, now_ms, ordinary, recovery);
}

int uuid_shaped(const char *text)
{
    static const int kGroups[] = {8, 4, 4, 4, 12};
    int pos = 0;
    int group;
    int i;
    if (text == 0) {
        return 0;
    }
    for (group = 0; group < 5; ++group) {
        if (group > 0) {
            if (text[pos] != '-') {
                return 0;
            }
            pos += 1;
        }
        for (i = 0; i < kGroups[group]; ++i) {
            char c = text[pos];
            pos += 1;
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                return 0;
            }
        }
    }
    return text[pos] == 0;
}

int test_wire_inbox_poll()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    char selected[48];
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    strncpy(selected, session.selected_message_id(), sizeof(selected) - 1);
    selected[sizeof(selected) - 1] = 0;
    CHECK(session.inbox_fetch_due(59999) == 0);
    CHECK(session.inbox_fetch_due(60000) == 1);
    tick_wire(session, planner, &slots, 59999);
    CHECK(slots.inbox == 0);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 1);
    CHECK(strcmp(slots.inbox_job.method, "GET") == 0);
    CHECK(strcmp(slots.inbox_job.path, "/v2/inbox") == 0);
    CHECK(slots.inbox_job.generation == 1);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 0);
    planner.apply_inbox(session, 1, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 60100);
    CHECK(session.message_count() == 3);
    CHECK(strcmp(session.selected_message_id(), selected) == 0);
    CHECK(session.selected_index() == 1);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    return g_failures;
}

int test_wire_recovery_nowifi()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.set_wifi(false, 10);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 0);
    CHECK(slots.last_viewed == 0);
    CHECK(slots.audio == 0);
    CHECK(slots.sketch == 0);
    session.set_wifi(true, 20);
    tick_wire(session, planner, &slots, 20);
    CHECK(slots.inbox == 1);
    planner.apply_inbox(session, slots.inbox_job.generation, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.inbox_fetch_due(60000) == 0);
    CHECK(session.recovery_fetch_due(3099) == 0);
    CHECK(session.recovery_fetch_due(3100) == 1);
    tick_wire(session, planner, &slots, 3099);
    CHECK(slots.inbox == 0);
    tick_wire(session, planner, &slots, 3100);
    CHECK(slots.inbox == 1);
    CHECK(strcmp(slots.inbox_job.method, "GET") == 0);
    CHECK(strcmp(slots.inbox_job.path, "/v2/inbox") == 0);
    CHECK(slots.audio == 0);
    CHECK(slots.sketch == 0);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 0);
    CHECK(session.inbox_fetch_due(60000) == 0);
    return g_failures;
}

int test_wire_arm_fetch()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    int selected;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    selected = session.selected_index();
    session.set_wifi(false, 10);
    session.set_wifi(true, 20);
    CHECK(session.inbox_fetch_due(20) == 0);
    tick_wire(session, planner, &slots, 20);
    CHECK(slots.inbox == 1);
    CHECK(strcmp(slots.inbox_job.path, "/v2/inbox") == 0);
    planner.apply_inbox(session, slots.inbox_job.generation, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 30);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    session.note_refresh_success(40);
    tick_wire(session, planner, &slots, 40);
    CHECK(slots.inbox == 1);
    planner.apply_inbox(session, slots.inbox_job.generation, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 50);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackIdle);
    CHECK(session.playback_operation_id() == 0);
    CHECK(session.message_count() == 2);
    return g_failures;
}

int test_wire_last_viewed()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    char first_body[256];
    char first_uuid[40];
    int selected;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 0) ==
          1);
    session.select_index(1, 100);
    tick_wire(session, planner, &slots, 200);
    CHECK(slots.last_viewed == 0);
    session.select_index(0, 400);
    tick_wire(session, planner, &slots, 500);
    CHECK(slots.last_viewed == 0);
    session.select_index(2, 700);
    tick_wire(session, planner, &slots, 800);
    CHECK(slots.last_viewed == 0);
    tick_wire(session, planner, &slots, 1699);
    CHECK(slots.last_viewed == 0);
    tick_wire(session, planner, &slots, 1700);
    CHECK(slots.last_viewed == 1);
    CHECK(strcmp(slots.last_viewed_job.method, "PUT") == 0);
    CHECK(strcmp(slots.last_viewed_job.path, "/v2/inbox/last-viewed") == 0);
    CHECK(strcmp(slots.last_viewed_job.message_id, "msg-c") == 0);
    CHECK(strstr(slots.last_viewed_job.body, "\"schema\":\"fl.last-viewed-update/2\"") != 0);
    CHECK(strstr(slots.last_viewed_job.body, "msg-c") != 0);
    CHECK(uuid_shaped(slots.last_viewed_job.operation_uuid) == 1);
    CHECK(strstr(slots.last_viewed_job.body, slots.last_viewed_job.operation_uuid) != 0);
    strncpy(first_body, slots.last_viewed_job.body, sizeof(first_body) - 1);
    first_body[sizeof(first_body) - 1] = 0;
    strncpy(first_uuid, slots.last_viewed_job.operation_uuid, sizeof(first_uuid) - 1);
    first_uuid[sizeof(first_uuid) - 1] = 0;
    tick_wire(session, planner, &slots, 1700);
    CHECK(slots.last_viewed == 0);
    planner.apply_last_viewed(session, 1, first_body, (int)strlen(first_body), 0, 0, 1710);
    CHECK(session.selected_index() == 2);
    tick_wire(session, planner, &slots, 1720);
    CHECK(slots.last_viewed == 1);
    CHECK(strcmp(slots.last_viewed_job.body, first_body) == 0);
    CHECK(strcmp(slots.last_viewed_job.operation_uuid, first_uuid) == 0);
    planner.apply_last_viewed(session, 1, first_body, (int)strlen(first_body), 1, 0, 1730);
    CHECK(session.selected_index() == 2);
    tick_wire(session, planner, &slots, 1740);
    CHECK(slots.last_viewed == 1);
    CHECK(strcmp(slots.last_viewed_job.body, first_body) == 0);
    CHECK(strcmp(slots.last_viewed_job.operation_uuid, first_uuid) == 0);
    selected = session.selected_index();
    planner.apply_last_viewed(session, 1, first_body, (int)strlen(first_body), 1, 200, 1750);
    CHECK(session.selected_index() == selected);
    planner.apply_last_viewed(session, 1, first_body, (int)strlen(first_body), 1, 409, 1760);
    CHECK(session.selected_index() == selected);
    session.select_index(0, 2000);
    tick_wire(session, planner, &slots, 3000);
    CHECK(slots.last_viewed == 1);
    CHECK(strcmp(slots.last_viewed_job.operation_uuid, first_uuid) != 0);
    CHECK(strcmp(slots.last_viewed_job.message_id, "msg-a") == 0);
    selected = session.selected_index();
    planner.apply_last_viewed(session, 1, first_body, (int)strlen(first_body), 1, 200, 3010);
    CHECK(session.selected_index() == selected);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    return g_failures;
}

int test_wire_mute_generation()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    uint32_t generation;
    g_failures = 0;
    open_inbox(session, 4);
    CHECK(session.deliver_inbox_worker_result(4, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.message_count() == 2);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 1);
    generation = slots.inbox_job.generation;
    CHECK(generation == 4);
    session.on_mute(10);
    planner.apply_inbox(session, generation, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 20);
    CHECK(session.message_count() == 2);
    CHECK(session.inbox_revision() == 1);
    return g_failures;
}

int test_wire_audio_job()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    uint8_t chunk[4096];
    uint32_t op;
    int head;
    int buffered;
    int i;
    g_failures = 0;
    memset(chunk, 4, sizeof(chunk));
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    tick_wire(session, planner, &slots, 10);
    CHECK(slots.audio == 1);
    CHECK(slots.audio_job.operation_id == op);
    CHECK(strcmp(slots.audio_job.message_id, session.selected_message_id()) == 0);
    CHECK(strcmp(slots.audio_job.method, "GET") == 0);
    CHECK(strstr(slots.audio_job.path, "/media/audio") != 0);
    CHECK(slots.audio_job.range_start == (uint32_t)session.audio_bytes_buffered());
    CHECK(strcmp(slots.audio_job.range_header, "bytes=0-") == 0);
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 10) == 1);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.play_head_ms() > 0);
    head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    CHECK(session.toggle_transport(20) == 1);
    planner.apply_audio(session, 1, op, "msg-a", 1, 206, chunk, (int)sizeof(chunk), 0, 1, 1, 30);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.toggle_transport(40) == 1);
    op = session.playback_operation_id();
    tick_wire(session, planner, &slots, 40);
    CHECK(slots.audio == 1);
    CHECK(slots.audio_job.operation_id == op);
    head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    planner.apply_audio(session, 1, op, "msg-a", 1, 401, chunk, (int)sizeof(chunk), 0, 1, 1, 50);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 60) ==
          1);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNone);
    session.set_wifi(false, 70);
    planner.apply_audio(session, 1, op, "msg-a", 1, 206, chunk, (int)sizeof(chunk), 0, 1, 1, 80);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    session.set_wifi(true, 90);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 90) ==
          1);
    CHECK(session.toggle_transport(100) == 1);
    op = session.playback_operation_id();
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    head = session.play_head_ms();
    planner.apply_sketch(session, 1, op, "msg-a", 0, 0, chunk, 8, 0, 1, 1, 110);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.play_head_ms() == head);
    CHECK(session.playback_operation_id() == op);
    return g_failures;
}

int test_wire_inbox_hidden()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.inbox_fetch_armed() == 0);
    CHECK(session.ui_visible() == 1);
    CHECK(session.inbox_fetch_due(59999) == 0);
    session.set_ui_visible(0);
    CHECK(session.inbox_fetch_due(60000) == 0);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 0);
    session.set_ui_visible(1);
    CHECK(session.inbox_fetch_due(60000) == 1);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 1);
    CHECK(strcmp(slots.inbox_job.method, "GET") == 0);
    CHECK(strcmp(slots.inbox_job.path, "/v2/inbox") == 0);
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 0);
    planner.apply_inbox(session, 1, 0, 503, "{}", 2, 60100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    session.set_ui_visible(0);
    CHECK(session.recovery_fetch_due(63100) == 0);
    tick_wire(session, planner, &slots, 63100);
    CHECK(slots.inbox == 0);
    session.set_ui_visible(1);
    CHECK(session.recovery_fetch_due(63100) == 1);
    tick_wire(session, planner, &slots, 63100);
    CHECK(slots.inbox == 1);
    tick_wire(session, planner, &slots, 63100);
    CHECK(slots.inbox == 0);
    return g_failures;
}

int test_wire_arm_survives_inflight()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    int selected;
    int phase;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    selected = session.selected_index();
    phase = session.playback_phase();
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 1);
    session.set_wifi(false, 60010);
    session.set_wifi(true, 60020);
    CHECK(session.inbox_fetch_armed() == 1);
    tick_wire(session, planner, &slots, 60020);
    CHECK(slots.inbox == 0);
    planner.apply_inbox(session, 1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 60030);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);
    tick_wire(session, planner, &slots, 60040);
    CHECK(slots.inbox == 1);
    tick_wire(session, planner, &slots, 60040);
    CHECK(slots.inbox == 0);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);

    open_inbox(session, 1);
    planner.reset();
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    CHECK(session.toggle_transport(0) == 1);
    selected = session.selected_index();
    phase = session.playback_phase();
    tick_wire(session, planner, &slots, 60000);
    CHECK(slots.inbox == 1);
    session.note_refresh_success(60010);
    CHECK(session.inbox_fetch_armed() == 1);
    tick_wire(session, planner, &slots, 60010);
    CHECK(slots.inbox == 0);
    planner.apply_inbox(session, 1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 60020);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);
    CHECK(session.playback_operation_id() != 0);
    tick_wire(session, planner, &slots, 60030);
    CHECK(slots.inbox == 1);
    tick_wire(session, planner, &slots, 60030);
    CHECK(slots.inbox == 0);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);

    open_inbox(session, 1);
    planner.reset();
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.select_index(1, 0);
    selected = session.selected_index();
    phase = session.playback_phase();
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.recovery_fetch_due(3100) == 1);
    tick_wire(session, planner, &slots, 3100);
    CHECK(slots.inbox == 1);
    session.note_refresh_success(3200);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.recovery_fetch_due(3200) == 1);
    tick_wire(session, planner, &slots, 3200);
    CHECK(slots.inbox == 0);
    planner.apply_inbox(session, 1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 3300);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotNone);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 3400);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.inbox_fetch_armed() == 1);
    CHECK(session.recovery_fetch_due(3400) == 1);
    CHECK(session.selected_index() == selected);
    CHECK(session.playback_phase() == phase);
    tick_wire(session, planner, &slots, 3400);
    CHECK(slots.inbox == 1);
    tick_wire(session, planner, &slots, 3400);
    CHECK(slots.inbox == 0);
    return g_failures;
}

int test_wire_media_cancelled()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxWirePlanner planner;
    fl_product_inbox::InboxWireSlots slots;
    uint8_t chunk[4096];
    uint8_t body[16];
    uint32_t op;
    int head;
    int buffered;
    int sketch;
    int i;
    g_failures = 0;
    memset(chunk, 4, sizeof(chunk));
    memset(body, 9, sizeof(body));
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.play_head_ms() > 0);
    head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    sketch = session.sketch_bytes_stored();
    tick_wire(session, planner, &slots, 10);
    CHECK(slots.audio == 1);
    CHECK(slots.sketch == 1);
    CHECK(session.media_fetch_allowed() == 1);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireAudio) == 0);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireSketch) == 0);
    session.set_wifi(false, 20);
    CHECK(session.media_fetch_allowed() == 0);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireAudio) == 1);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireSketch) == 1);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireInbox) == 0);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireLastViewed) == 0);
    planner.apply_audio(session, 1, op, "msg-a", 0, 0, nullptr, 0, 0, 1, 1, 30);
    planner.apply_sketch(session, 1, op, "msg-a", 0, 0, nullptr, 0, 0, 1, 1, 30);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.sketch_bytes_stored() == sketch);
    planner.apply_audio(session, 1, op, "msg-a", 1, 206, body, (int)sizeof(body), 0, 1, 1, 40);
    planner.apply_sketch(session, 1, op, "msg-a", 1, 206, body, 8, 0, 1, 1, 40);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.sketch_bytes_stored() == sketch);

    open_inbox(session, 1);
    planner.reset();
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    head = session.play_head_ms();
    buffered = session.audio_bytes_buffered();
    sketch = session.sketch_bytes_stored();
    tick_wire(session, planner, &slots, 10);
    CHECK(slots.audio == 1);
    CHECK(slots.sketch == 1);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 20);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.media_fetch_allowed() == 0);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireAudio) == 1);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireSketch) == 1);
    CHECK(planner.media_job_cancelled(session, fl_product_inbox::kInboxWireInbox) == 0);
    planner.apply_audio(session, 1, op, "msg-a", 0, 0, nullptr, 0, 0, 1, 1, 30);
    planner.apply_sketch(session, 1, op, "msg-a", 0, 0, nullptr, 0, 0, 1, 1, 30);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    CHECK(session.sketch_bytes_stored() == sketch);
    planner.apply_audio(session, 1, op, "msg-a", 1, 206, body, (int)sizeof(body), 0, 1, 1, 40);
    CHECK(session.play_head_ms() == head);
    CHECK(session.audio_bytes_buffered() == buffered);
    return g_failures;
}

uint32_t pump_ari_playing(fl_product_inbox::CarouselSession &session)
{
    uint8_t chunk[4096];
    uint32_t op;
    int i;
    memset(chunk, 4, sizeof(chunk));
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    op = session.playback_operation_id();
    for (i = 0; i < 8; ++i) {
        CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
    }
    return op;
}

int test_chrome_selected()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxChrome chrome;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "From Ari") == 0);
    CHECK(strcmp(chrome.transport_label, "Play") == 0);
    CHECK(chrome.transport_hidden == 0);
    CHECK(chrome.transport_disabled == 0);
    CHECK(chrome.lower_kind == fl_product_inbox::kLowerCarousel);
    return g_failures;
}

int test_chrome_playing()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxChrome chrome;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    pump_ari_playing(session);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.transport_label, "Pause") == 0);
    CHECK(chrome.transport_hidden == 0);
    CHECK(chrome.transport_disabled == 0);
    return g_failures;
}

int test_chrome_empty()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxChrome chrome;
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotEmptyFirst, (int)strlen(kSnapshotEmptyFirst),
                                              0) == 1);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "Messages") == 0);
    CHECK(chrome.transport_hidden == 1);
    CHECK(strcmp(chrome.lower_text, "No messages") == 0);
    CHECK(chrome.lower_kind == fl_product_inbox::kLowerEmpty);
    return g_failures;
}

int test_chrome_connecting()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxChrome chrome;
    char status[32];
    g_failures = 0;
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    session.deliver_inbox_worker_result(1, 0, 503, "{}", 2, 100);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "From Ari") == 0);
    CHECK(strcmp(chrome.transport_label, "Play") == 0);
    CHECK(chrome.transport_hidden == 0);
    CHECK(chrome.transport_disabled == 1);
    CHECK(chrome.lower_kind == fl_product_inbox::kLowerConnecting);
    fl_product_inbox::format_connecting_status(1, status, (int)sizeof(status));
    CHECK(strcmp(status, "connecting.") == 0);
    fl_product_inbox::format_connecting_status(2, status, (int)sizeof(status));
    CHECK(strcmp(status, "connecting..") == 0);
    fl_product_inbox::format_connecting_status(3, status, (int)sizeof(status));
    CHECK(strcmp(status, "connecting...") == 0);
    session.set_wifi(false, 200);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "From Ari") == 0);
    CHECK(strcmp(chrome.transport_label, "Play") == 0);
    CHECK(chrome.transport_hidden == 0);
    CHECK(chrome.transport_disabled == 1);
    CHECK(chrome.lower_kind == fl_product_inbox::kLowerNoWifi);
    CHECK(strcmp(chrome.lower_text, "no Wi-Fi") == 0);
    return g_failures;
}

int test_chrome_cant_play()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxChrome chrome;
    uint8_t chunk[4096];
    uint32_t op;
    g_failures = 0;
    memset(chunk, 5, sizeof(chunk));
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    op = pump_ari_playing(session);
    CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 1, 0, 100) == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackCantPlay);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "can't play") == 0);
    CHECK(strcmp(chrome.transport_label, "Play") == 0);
    CHECK(chrome.transport_disabled == 0);
    CHECK(chrome.lower_kind == fl_product_inbox::kLowerCarousel);
    session.advance_to(2099);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "can't play") == 0);
    session.advance_to(2100);
    fl_product_inbox::inbox_chrome(session, &chrome);
    CHECK(strcmp(chrome.heading, "From Ari") == 0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPaused);
    return g_failures;
}

int test_chrome_duration()
{
    char text[16];
    g_failures = 0;
    fl_product_inbox::format_duration_mss(8000, text, (int)sizeof(text));
    CHECK(strcmp(text, "0:08") == 0);
    fl_product_inbox::format_duration_mss(134000, text, (int)sizeof(text));
    CHECK(strcmp(text, "2:14") == 0);
    return g_failures;
}

int test_chrome_card_face()
{
    fl_product_inbox::CarouselSession session;
    fl_product_inbox::InboxMessage message;
    fl_product_inbox::SenderPresentation view;
    uint8_t chunk[4096];
    uint8_t sketch[8];
    g_failures = 0;
    memset(&message, 0, sizeof(message));
    strncpy(message.sender_name, "Sam", sizeof(message.sender_name) - 1);
    strncpy(message.sender_avatar_id, "nope", sizeof(message.sender_avatar_id) - 1);
    strncpy(message.sender_card_background_id, "nope", sizeof(message.sender_card_background_id) - 1);
    view = fl_product_inbox::resolve_sender_presentation(message);
    CHECK(view.use_initials_fallback == 1);
    CHECK(view.use_card_fallback == 1);
    CHECK(strcmp(view.card_background_id, fl_product_inbox::kFallbackCardBackground) == 0);
    open_inbox(session, 1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0) == 1);
    CHECK(session.sketch_visible() == 0);
    CHECK(fl_product_inbox::card_face_portrait_hidden(session.sketch_visible()) == 0);
    memset(chunk, 3, sizeof(chunk));
    sketch[0] = 10;
    sketch[1] = 10;
    sketch[2] = 0;
    sketch[3] = 0xf8;
    sketch[4] = 0;
    sketch[5] = 0;
    sketch[6] = 0;
    sketch[7] = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0) ==
          1);
    session.select_index(0, 0);
    CHECK(session.toggle_transport(0) == 1);
    {
        uint32_t op = session.playback_operation_id();
        int i;
        for (i = 0; i < 8; ++i) {
            CHECK(session.deliver_audio_chunk(op, "msg-a", chunk, (int)sizeof(chunk), 0, 1, 0) == 1);
        }
        CHECK(session.deliver_sketch_chunk(op, "msg-a", sketch, 8, 0) == 1);
    }
    CHECK(session.sketch_visible() == 1);
    CHECK(fl_product_inbox::card_face_portrait_hidden(session.sketch_visible()) == 1);
    return g_failures;
}

int host_p6_apply_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    char body[300000];
    size_t len;
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    if (file == 0) {
        return 1;
    }
    len = fread(body, 1, sizeof(body) - 1, file);
    fclose(file);
    body[len] = 0;
    session.boot(0);
    session.set_wifi(true, 0);
    session.set_session_generation(1);
    CHECK(session.deliver_inbox_worker_result(1, 1, 200, body, (int)len, 0) == 1);
    printf("message_count=%d revision=%u selected=%s\n", session.message_count(),
           session.inbox_revision(), session.selected_message_id());
    return g_failures;
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    int failures = 0;
    if (strcmp(name, "snapshot") == 0) {
        failures = test_snapshot_apply();
    } else if (strcmp(name, "last-viewed") == 0) {
        failures = test_last_viewed_coalesce();
    } else if (strcmp(name, "connectivity") == 0) {
        failures = test_connectivity_no_play();
    } else if (strcmp(name, "prefix-hash") == 0) {
        failures = test_prefix_and_hash_pause();
    } else if (strcmp(name, "pause-for-record") == 0) {
        failures = test_pause_for_record();
    } else if (strcmp(name, "presentation") == 0) {
        failures = test_presentation_fallback();
    } else if (strcmp(name, "chrome-selected") == 0) {
        failures = test_chrome_selected();
    } else if (strcmp(name, "chrome-playing") == 0) {
        failures = test_chrome_playing();
    } else if (strcmp(name, "chrome-empty") == 0) {
        failures = test_chrome_empty();
    } else if (strcmp(name, "chrome-connecting") == 0) {
        failures = test_chrome_connecting();
    } else if (strcmp(name, "chrome-cant-play") == 0) {
        failures = test_chrome_cant_play();
    } else if (strcmp(name, "chrome-duration") == 0) {
        failures = test_chrome_duration();
    } else if (strcmp(name, "chrome-card-face") == 0) {
        failures = test_chrome_card_face();
    } else if (strcmp(name, "flsk2") == 0) {
        failures = test_flsk2_vectors();
    } else if (strcmp(name, "recovery-fetch") == 0) {
        failures = test_recovery_fetch_connecting();
    } else if (strcmp(name, "inbox-fetch-60s") == 0) {
        failures = test_inbox_fetch_60s();
    } else if (strcmp(name, "refresh-503") == 0) {
        failures = test_refresh_503_keeps_inbox();
    } else if (strcmp(name, "refresh-failure-stays") == 0) {
        failures = test_refresh_failure_stays();
    } else if (strcmp(name, "level1-new-card") == 0) {
        failures = test_level1_new_card();
    } else if (strcmp(name, "sketch-session") == 0) {
        failures = test_sketch_through_session();
    } else if (strcmp(name, "newest-fallback") == 0) {
        failures = test_newest_fallback();
    } else if (strcmp(name, "last-viewed-no-steal") == 0) {
        failures = test_last_viewed_no_steal();
    } else if (strcmp(name, "delete-selected-middle") == 0) {
        failures = test_delete_selected_middle();
    } else if (strcmp(name, "delete-selected-oldest") == 0) {
        failures = test_delete_selected_oldest();
    } else if (strcmp(name, "delete-only-message") == 0) {
        failures = test_delete_only_message();
    } else if (strcmp(name, "selection-keeps-play-head") == 0) {
        failures = test_selection_keeps_play_head();
    } else if (strcmp(name, "completed-replay") == 0) {
        failures = test_completed_replay();
    } else if (strcmp(name, "chunks-cover-asset") == 0) {
        failures = test_chunks_cover_asset();
    } else if (strcmp(name, "wifi-drop-keeps-progress") == 0) {
        failures = test_wifi_drop_keeps_progress();
    } else if (strcmp(name, "mute-retires-generation") == 0) {
        failures = test_mute_retires_generation();
    } else if (strcmp(name, "generation-zero") == 0) {
        failures = test_generation_zero();
    } else if (strcmp(name, "connecting-waits") == 0) {
        failures = test_connecting_waits();
    } else if (strcmp(name, "snapshot-overflow") == 0) {
        failures = test_snapshot_overflow();
    } else if (strcmp(name, "connecting-before-snapshot") == 0) {
        failures = test_connecting_before_snapshot();
    } else if (strcmp(name, "empty-then-first-message") == 0) {
        failures = test_empty_then_first_message();
    } else if (strcmp(name, "sketch-validation-keeps-playing") == 0) {
        failures = test_sketch_validation_keeps_playing();
    } else if (strcmp(name, "wire-inbox-poll") == 0) {
        failures = test_wire_inbox_poll();
    } else if (strcmp(name, "wire-recovery-nowifi") == 0) {
        failures = test_wire_recovery_nowifi();
    } else if (strcmp(name, "wire-arm-fetch") == 0) {
        failures = test_wire_arm_fetch();
    } else if (strcmp(name, "wire-last-viewed") == 0) {
        failures = test_wire_last_viewed();
    } else if (strcmp(name, "wire-mute-generation") == 0) {
        failures = test_wire_mute_generation();
    } else if (strcmp(name, "wire-audio-job") == 0) {
        failures = test_wire_audio_job();
    } else if (strcmp(name, "wire-inbox-hidden") == 0) {
        failures = test_wire_inbox_hidden();
    } else if (strcmp(name, "wire-arm-survives-inflight") == 0) {
        failures = test_wire_arm_survives_inflight();
    } else if (strcmp(name, "wire-media-cancelled") == 0) {
        failures = test_wire_media_cancelled();
    } else if (strcmp(name, "host-p6-inbox") == 0) {
        if (argc < 3) {
            fprintf(stderr, "usage: host-p6-inbox <inbox.json>\n");
            return 1;
        }
        failures = host_p6_apply_fixture(argv[2]);
    } else {
        failures += test_snapshot_apply();
        failures += test_last_viewed_coalesce();
        failures += test_connectivity_no_play();
        failures += test_prefix_and_hash_pause();
        failures += test_pause_for_record();
        failures += test_presentation_fallback();
        failures += test_chrome_selected();
        failures += test_chrome_playing();
        failures += test_chrome_empty();
        failures += test_chrome_connecting();
        failures += test_chrome_cant_play();
        failures += test_chrome_duration();
        failures += test_chrome_card_face();
        failures += test_flsk2_vectors();
        failures += test_recovery_fetch_connecting();
        failures += test_inbox_fetch_60s();
        failures += test_refresh_503_keeps_inbox();
        failures += test_refresh_failure_stays();
        failures += test_level1_new_card();
        failures += test_sketch_through_session();
        failures += test_newest_fallback();
        failures += test_last_viewed_no_steal();
        failures += test_delete_selected_middle();
        failures += test_delete_selected_oldest();
        failures += test_delete_only_message();
        failures += test_selection_keeps_play_head();
        failures += test_completed_replay();
        failures += test_chunks_cover_asset();
        failures += test_wifi_drop_keeps_progress();
        failures += test_mute_retires_generation();
        failures += test_generation_zero();
        failures += test_connecting_waits();
        failures += test_snapshot_overflow();
        failures += test_connecting_before_snapshot();
        failures += test_empty_then_first_message();
        failures += test_sketch_validation_keeps_playing();
        failures += test_wire_inbox_poll();
        failures += test_wire_recovery_nowifi();
        failures += test_wire_arm_fetch();
        failures += test_wire_last_viewed();
        failures += test_wire_mute_generation();
        failures += test_wire_audio_job();
        failures += test_wire_inbox_hidden();
        failures += test_wire_arm_survives_inflight();
        failures += test_wire_media_cancelled();
    }
    if (failures != 0) {
        fprintf(stderr, "product_inbox_model_test failures=%d\n", failures);
        return 1;
    }
    printf("product_inbox_model_test ok\n");
    return 0;
}
