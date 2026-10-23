#include "fl_product_inbox_carousel.hpp"
#include "fl_product_inbox_flsk2.hpp"
#include "fl_product_inbox_p1_wire.hpp"
#include "fl_product_inbox_presentation.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

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
    "\"duration_ms\":1000,\"media\":{\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
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
    "\"audio\":{\"format_id\":\"pcm_s16le\",\"byte_count\":32000,"
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

int test_snapshot_apply()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
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
    uint8_t chunk[1024];
    g_failures = 0;
    memset(chunk, 1, sizeof(chunk));
    session.boot(0);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    session.select_index(0, 0);
    session.deliver_audio_chunk(chunk, (int)sizeof(chunk), 0, 1, 0);
    session.toggle_transport(0);
    session.set_wifi(false, 0);
    CHECK(session.play_disabled() == 1);
    CHECK(session.deliver_audio_chunk(chunk, (int)sizeof(chunk), 0, 1, 0) == 0);
    CHECK(session.audio_bytes_buffered() == 0);
    session.set_wifi(true, 0);
    session.deliver_inbox_worker_result(0, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.toggle_transport(100) == 0);
    return g_failures;
}

int test_prefix_and_hash_pause()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[4096];
    g_failures = 0;
    memset(chunk, 2, sizeof(chunk));
    session.boot(0);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    session.select_index(0, 0);
    for (int i = 0; i < 8; ++i) {
        session.deliver_audio_chunk(chunk, (int)sizeof(chunk), 0, 1, 0);
    }
    CHECK(session.prefix_play_ready() == 1);
    session.toggle_transport(0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    session.deliver_audio_chunk(chunk, (int)sizeof(chunk), 1, 0, 100);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackCantPlay);
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
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    CHECK(session.message_count() == 2);
    session.deliver_inbox_worker_result(0, 0, 503, "{}", 2, 100);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    CHECK(session.message_count() == 2);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    CHECK(session.recovery_fetch_due(100) == 0);
    CHECK(session.recovery_fetch_due(3100) == 1);
    return g_failures;
}

int test_inbox_fetch_60s()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    CHECK(session.inbox_fetch_due(59999) == 0);
    CHECK(session.inbox_fetch_due(60000) == 1);
    session.deliver_inbox_worker_result(0, 0, 503, "{}", 2, 100);
    CHECK(session.inbox_fetch_due(70000) == 0);
    return g_failures;
}

int test_refresh_503_keeps_inbox()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    session.select_index(1, 0);
    session.deliver_inbox_worker_result(0, 0, 503, "{}", 2, 50);
    CHECK(session.message_count() == 2);
    CHECK(session.selected_index() == 1);
    CHECK(strcmp(session.selected_message_id(), "msg-b") == 0);
    CHECK(session.connectivity_slot() == fl_product_inbox::kSlotConnecting);
    return g_failures;
}

int test_level1_new_card()
{
    fl_product_inbox::CarouselSession session;
    g_failures = 0;
    session.boot(0);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotA, (int)strlen(kSnapshotA), 0);
    CHECK(session.last_update_level() == 1);
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotAPlusC, (int)strlen(kSnapshotAPlusC), 10);
    CHECK(session.last_update_level() == 1);
    CHECK(session.message_count() == 3);
    CHECK(strcmp(session.message_at(2)->message_id, "msg-c") == 0);
    CHECK(strcmp(session.selected_message_id(), "msg-a") == 0);
    return g_failures;
}

int test_sketch_through_session()
{
    fl_product_inbox::CarouselSession session;
    uint8_t chunk[1024];
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
    session.deliver_inbox_worker_result(0, 1, 200, kSnapshotSketchA, (int)strlen(kSnapshotSketchA), 0);
    session.select_index(0, 0);
    for (int i = 0; i < 8; ++i) {
        session.deliver_audio_chunk(chunk, (int)sizeof(chunk), 0, 1, 0);
    }
    session.toggle_transport(0);
    CHECK(session.playback_phase() == fl_product_inbox::kPlaybackPlaying);
    CHECK(session.deliver_sketch_chunk(sketch, 8, 0) == 1);
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
    CHECK(session.deliver_inbox_worker_result(0, 1, 200, body, (int)len, 0) == 1);
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
    } else if (strcmp(name, "presentation") == 0) {
        failures = test_presentation_fallback();
    } else if (strcmp(name, "flsk2") == 0) {
        failures = test_flsk2_vectors();
    } else if (strcmp(name, "recovery-fetch") == 0) {
        failures = test_recovery_fetch_connecting();
    } else if (strcmp(name, "inbox-fetch-60s") == 0) {
        failures = test_inbox_fetch_60s();
    } else if (strcmp(name, "refresh-503") == 0) {
        failures = test_refresh_503_keeps_inbox();
    } else if (strcmp(name, "level1-new-card") == 0) {
        failures = test_level1_new_card();
    } else if (strcmp(name, "sketch-session") == 0) {
        failures = test_sketch_through_session();
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
        failures += test_presentation_fallback();
        failures += test_flsk2_vectors();
        failures += test_recovery_fetch_connecting();
        failures += test_inbox_fetch_60s();
        failures += test_refresh_503_keeps_inbox();
        failures += test_level1_new_card();
        failures += test_sketch_through_session();
    }
    if (failures != 0) {
        fprintf(stderr, "product_inbox_model_test failures=%d\n", failures);
        return 1;
    }
    printf("product_inbox_model_test ok\n");
    return 0;
}
