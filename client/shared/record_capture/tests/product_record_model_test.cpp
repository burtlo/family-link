#include "fl_product_record_flsk2.hpp"
#include "fl_product_record_session.hpp"
#include "fl_product_record_types.hpp"

#include "fl_product_record_lab10.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {

int g_failures = 0;

void check(bool ok, const char *text, int line)
{
    if (!ok) {
        fprintf(stderr, "FAIL product_record_model_test.cpp:%d %s\n", line, text);
        g_failures += 1;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct TempMedia {
    char path[64];
    fl_product_record::FileMediaBackend backend;

    TempMedia() { path[0] = '\0'; }

    bool open()
    {
        static const char kTemplate[] = "/tmp/flrecXXXXXX";
        int fd;
        memcpy(path, kTemplate, sizeof(kTemplate));
        fd = mkstemp(path);
        if (fd < 0) {
            path[0] = '\0';
            return false;
        }
        if (close(fd) != 0) {
            (void)unlink(path);
            path[0] = '\0';
            return false;
        }
        return backend.open_path(path, true);
    }

    ~TempMedia()
    {
        backend.close();
        if (path[0] != '\0') {
            (void)unlink(path);
        }
    }
};

void fill_bytes(uint8_t *dst, size_t len, uint32_t seed)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        dst[i] = (uint8_t)(((seed + (uint32_t)i) * 17u + 3u) & 0xffu);
    }
}

bool append_audio_chunks(fl_product_record::AttachedStorage *storage, uint32_t total, uint32_t seed,
                         const size_t *chunks, size_t chunk_count)
{
    uint8_t buf[4096];
    uint32_t offset = 0;
    size_t chunk_i = 0;
    if (storage == 0 || chunks == 0 || chunk_count == 0) {
        return false;
    }
    while (offset < total) {
        size_t n = chunks[chunk_i % chunk_count];
        if (n > sizeof(buf)) {
            return false;
        }
        if ((uint32_t)n > total - offset) {
            n = (size_t)(total - offset);
        }
        fill_bytes(buf, n, seed + offset);
        if (!storage->append_in_progress_audio(buf, n)) {
            return false;
        }
        offset += (uint32_t)n;
        chunk_i += 1;
    }
    return storage->in_progress_audio_bytes() == total;
}

bool read_matches(fl_product_record::AttachedStorage *storage, const char *id, fl_product_record::MediaKind kind,
                  uint32_t total, uint32_t seed)
{
    uint8_t got[1024];
    uint8_t expect[1024];
    uint32_t offset = 0;
    if (storage == 0) {
        return false;
    }
    while (offset < total) {
        size_t want = sizeof(got);
        size_t n = 0;
        if ((uint32_t)want > total - offset) {
            want = (size_t)(total - offset);
        }
        if (!storage->read_finished(id, kind, offset, got, want, &n) || n != want) {
            return false;
        }
        fill_bytes(expect, want, seed + offset);
        if (memcmp(got, expect, want) != 0) {
            return false;
        }
        offset += (uint32_t)want;
    }
    return true;
}

struct FakeClock {
    uint64_t now_us;
};

uint64_t fake_now(void *context)
{
    return static_cast<FakeClock *>(context)->now_us;
}

void submit_report(lab10_capture_t *capture, lab10_capture_session_id_t session, FakeClock *clock,
                   int16_t x, int16_t y, lab10_touch_phase_t phase, uint8_t color)
{
    lab10_capture_raw_report_t report;
    memset(&report, 0, sizeof(report));
    clock->now_us += 1000;
    report.timestamp_us = clock->now_us;
    report.display_x = x;
    report.display_y = y;
    report.touch_id = 1;
    report.phase = phase;
    report.route = LAB10_ROUTE_CANVAS;
    report.color_index = color;
    (void)lab10_capture_submit(capture, session, &report);
}

void submit_stroke(lab10_capture_t *capture, lab10_capture_session_id_t session, FakeClock *clock,
                   int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color)
{
    const int steps = abs(x1 - x0) + abs(y1 - y0);
    int i;
    submit_report(capture, session, clock, x0, y0, LAB10_PHASE_DOWN, color);
    for (i = 1; i <= steps; ++i) {
        const int16_t x = (int16_t)(x0 + (x1 - x0) * i / steps);
        const int16_t y = (int16_t)(y0 + (y1 - y0) * i / steps);
        submit_report(capture, session, clock, x, y, LAB10_PHASE_MOVE, color);
        (void)lab10_capture_poll(capture, session);
    }
    submit_report(capture, session, clock, x1, y1, LAB10_PHASE_UP, color);
    (void)lab10_capture_poll(capture, session);
}

void test_geometry_and_rail()
{
    static const int expected_x[] = {269, 291, 269, 291, 269, 291};
    unsigned index;
    for (index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry(index);
        CHECK(swatch != 0);
        CHECK(swatch->center_x == expected_x[index]);
    }
    CHECK(lab10_canvas_contains(239, 239));
    CHECK(!lab10_canvas_contains(240, 0));
    CHECK(lab10_rail_contains(240, 0));
    CHECK(lab10_palette_hit_test(269, 64) == LAB10_COLOR_RED);
}

void test_logical_mapping()
{
    uint8_t lx = 0;
    uint8_t ly = 0;
    lab10_map_to_logical(0, 0, &lx, &ly);
    CHECK(lx == 0 && ly == 0);
    lab10_map_to_logical(238, 238, &lx, &ly);
    CHECK(lx == 119 && ly == 119);
    CHECK(lx < fl_product_record::kLogicalWidth);
    CHECK(ly < fl_product_record::kLogicalHeight);
}

void test_adjacent_pixels_and_strokes()
{
    FakeClock clock = {1000};
    lab10_capture_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clock_now_us = fake_now;
    cfg.clock_context = &clock;
    cfg.initial_color_index = LAB10_COLOR_WHITE;
    lab10_capture_t *capture = lab10_capture_create(&cfg);
    lab10_capture_session_id_t session = lab10_capture_start(capture);
    lab10_capture_snapshot_t snapshot;
    bool adjacent = false;
    size_t i;
    CHECK(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_stroke(capture, session, &clock, 10, 10, 80, 10, LAB10_COLOR_WHITE);
    clock.now_us += 500000;
    submit_stroke(capture, session, &clock, 100, 100, 180, 100, LAB10_COLOR_WHITE);
    (void)lab10_capture_stop(capture, session);
    (void)lab10_capture_poll(capture, session);
    CHECK(lab10_capture_snapshot(capture, session, &snapshot));
    CHECK(snapshot.point_count > 4);
    for (i = 0; i < snapshot.point_count; ++i) {
        size_t j;
        for (j = i + 1; j < snapshot.point_count; ++j) {
            const lab10_capture_point_t *a = &snapshot.points[i];
            const lab10_capture_point_t *b = &snapshot.points[j];
            const int dx = abs((int)a->logical_x - (int)b->logical_x);
            const int dy = abs((int)a->logical_y - (int)b->logical_y);
            if (dx + dy == 1) {
                adjacent = true;
            }
        }
    }
    CHECK(adjacent);
    {
        uint32_t stroke_a = 0;
        uint32_t stroke_b = 0;
        size_t j;
        for (j = 0; j < snapshot.point_count; ++j) {
            if (snapshot.points[j].stroke_id == 0) {
                continue;
            }
            if (stroke_a == 0) {
                stroke_a = snapshot.points[j].stroke_id;
            } else if (snapshot.points[j].stroke_id != stroke_a && stroke_b == 0) {
                stroke_b = snapshot.points[j].stroke_id;
            }
        }
        CHECK(stroke_a != 0 && stroke_b != 0);
    }
    lab10_capture_destroy(capture);
}

void test_tap_and_diagonal()
{
    FakeClock clock = {0};
    lab10_capture_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clock_now_us = fake_now;
    cfg.clock_context = &clock;
    cfg.initial_color_index = LAB10_COLOR_GOLD;
    lab10_capture_t *capture = lab10_capture_create(&cfg);
    lab10_capture_session_id_t session = lab10_capture_start(capture);
    lab10_capture_snapshot_t snapshot;
    CHECK(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    submit_report(capture, session, &clock, 60, 60, LAB10_PHASE_DOWN, LAB10_COLOR_GOLD);
    submit_report(capture, session, &clock, 60, 60, LAB10_PHASE_UP, LAB10_COLOR_GOLD);
    submit_stroke(capture, session, &clock, 4, 4, 200, 200, LAB10_COLOR_GOLD);
    (void)lab10_capture_poll(capture, session);
    CHECK(lab10_capture_snapshot(capture, session, &snapshot));
    CHECK(snapshot.point_count >= 3);
    lab10_capture_destroy(capture);
}

void open_kept(fl_product_record::RecordSession &session, const char *id)
{
    CHECK(session.try_start(id, fl_product_record::kMicReady));
    CHECK(session.wiping());
    CHECK(!session.capture_active());
    session.advance_to(session.now_ms() + LAB10_RING_DURATION_MS);
    CHECK(session.capture_active());
    CHECK(!session.wiping());
    CHECK(session.consume_notice() == fl_product_record::kSessionNoticeWipeCompleted);
}

void test_session_capture_signal_and_budget()
{
    TempMedia media;
    fl_product_record::RecordSession session;
    uint8_t chunk[1024];
    uint8_t out[1024];
    size_t n = 0;
    session.boot(0);
    CHECK(media.open());
    session.attach_media(&media.backend);
    CHECK(!session.capture_active());
    CHECK(fl_product_record::kDurationCapMs == 180000);
    CHECK((uint64_t)fl_product_record::kDurationCapMs * 16ull * 2ull ==
          (uint64_t)fl_product_record::kAudioByteBudget);
    open_kept(session, "msg-a");
    CHECK(session.storage().in_progress_audio_budget() == fl_product_record::kAudioByteBudget);
    fill_bytes(chunk, sizeof(chunk), 42);
    CHECK(session.storage().append_in_progress_audio(chunk, sizeof(chunk)));
    CHECK(session.route_touch(20, 20, 0, 1));
    CHECK(session.route_touch(40, 30, 1, 1));
    CHECK(session.route_touch(48, 30, 2, 1));
    CHECK(session.stop_recording());
    CHECK(!session.capture_active());
    CHECK(session.storage().finished_count() == 1);
    CHECK(session.storage().read_finished("msg-a", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
    CHECK(n == sizeof(out));
    CHECK(memcmp(out, chunk, sizeof(out)) == 0);
    {
        const fl_product_record::StoredObject *slot = session.storage().finished_slot(0);
        uint8_t sketch[2048];
        size_t sketch_n = 0;
        CHECK(slot != 0);
        CHECK(slot->sketch_bytes > 0);
        CHECK(slot->sketch_bytes <= sizeof(sketch));
        CHECK(session.storage().read_finished("msg-a", fl_product_record::kMediaSketch, 0, sketch, slot->sketch_bytes,
                                              &sketch_n));
        CHECK(sketch_n == slot->sketch_bytes);
        CHECK(fl_product_record::validate_flsk2(sketch, sketch_n));
    }
}

void test_overlap_storage()
{
    TempMedia media;
    fl_product_record::RecordSession session;
    uint8_t first[128];
    uint8_t second[128];
    uint8_t out[128];
    size_t n = 0;
    session.boot(0);
    CHECK(media.open());
    session.attach_media(&media.backend);
    open_kept(session, "msg-a");
    fill_bytes(first, sizeof(first), 7);
    CHECK(session.storage().append_in_progress_audio(first, sizeof(first)));
    CHECK(session.stop_recording());
    CHECK(session.storage().mark_finished_uploading("msg-a"));
    CHECK(session.storage().can_start_second_recording());
    open_kept(session, "msg-b");
    CHECK(session.capture_active());
    fill_bytes(second, sizeof(second), 99);
    CHECK(session.storage().append_in_progress_audio(second, sizeof(second)));
    CHECK(session.storage().read_finished("msg-a", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
    CHECK(n == sizeof(out));
    CHECK(memcmp(out, first, sizeof(first)) == 0);
    session.cancel_recording();
    CHECK(!session.capture_active());
    CHECK(!session.storage().has_in_progress());
    CHECK(session.storage().read_finished("msg-a", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
    CHECK(n == sizeof(first));
    CHECK(memcmp(out, first, sizeof(first)) == 0);
    CHECK(session.storage().finished_count() == 1);
}

void test_mic_and_ink_overflow()
{
    TempMedia media;
    fl_product_record::RecordSession session;
    session.boot(0);
    CHECK(media.open());
    session.attach_media(&media.backend);
    CHECK(!session.try_start("msg-x", fl_product_record::kMicUnavailableAtStart));
    CHECK(!session.capture_active());
    CHECK(!session.wiping());
    CHECK(session.storage().finished_count() == 0);
    open_kept(session, "msg-y");
    session.notify_mic_failed();
    CHECK(!session.capture_active());
    CHECK(session.mic_state() == fl_product_record::kMicFailedDuringCapture);
    CHECK(session.storage().finished_count() == 1);
}

void test_third_start_refused_when_slots_full()
{
    TempMedia media;
    fl_product_record::RecordSession session;
    session.boot(0);
    CHECK(media.open());
    session.attach_media(&media.backend);
    open_kept(session, "msg-a");
    CHECK(session.stop_recording());
    open_kept(session, "msg-b");
    CHECK(session.stop_recording());
    CHECK(session.storage().finished_count() == 2);
    CHECK(!session.storage().can_start_second_recording());
    CHECK(!session.try_start("msg-c", fl_product_record::kMicReady));
    CHECK(session.storage().mark_finished_uploading("msg-a"));
    CHECK(!session.storage().can_start_second_recording());
    CHECK(!session.try_start("msg-c", fl_product_record::kMicReady));
}

void test_ink_overflow_keeps_recording()
{
    FakeClock clock = {0};
    lab10_capture_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clock_now_us = fake_now;
    cfg.clock_context = &clock;
    cfg.initial_color_index = LAB10_COLOR_WHITE;
    lab10_capture_t *capture = lab10_capture_create(&cfg);
    lab10_capture_session_id_t session = lab10_capture_start(capture);
    size_t i;
    submit_report(capture, session, &clock, 0, 0, LAB10_PHASE_DOWN, LAB10_COLOR_WHITE);
    for (i = 0; i < fl_product_record::kMaxSketchPoints + 8; ++i) {
        submit_report(capture, session, &clock, (int16_t)(i % 240), (int16_t)(i % 240),
                      LAB10_PHASE_MOVE, LAB10_COLOR_WHITE);
        (void)lab10_capture_poll(capture, session);
    }
    CHECK(lab10_capture_status(capture, session) == LAB10_CAPTURE_STATUS_ACTIVE);
    lab10_capture_destroy(capture);
}

void test_file_backend_append_and_read()
{
    TempMedia media;
    uint8_t raw[4] = {1, 2, 3, 4};
    uint8_t got[4];
    uint64_t offset = 0;
    CHECK(media.open());
    CHECK(media.backend.append(raw, sizeof(raw), &offset));
    CHECK(media.backend.flush());
    CHECK(media.backend.read_at(offset, got, sizeof(got)));
    CHECK(memcmp(got, raw, sizeof(raw)) == 0);
}

void test_chunked_append_and_readback()
{
    TempMedia media;
    fl_product_record::AttachedStorage storage;
    const size_t chunks[] = {1024, 1024, 1024, 1024, 4096, 4096, 4096, 4096};
    const uint32_t total = 20480;
    const uint32_t seed = 11;
    uint8_t part[16384];
    uint8_t expect[16384];
    uint8_t sketch_a[3] = {9, 8, 7};
    uint8_t sketch_b[3] = {4, 5, 6};
    uint8_t sketch_got[6];
    uint8_t window[16];
    size_t n = 0;
    CHECK(media.open());
    storage.attach_media(&media.backend);
    CHECK(storage.begin_in_progress("msg-chunk", total));
    CHECK(append_audio_chunks(&storage, total, seed, chunks, sizeof(chunks) / sizeof(chunks[0])));
    CHECK(storage.write_in_progress_sketch(sketch_a, sizeof(sketch_a)));
    CHECK(storage.write_in_progress_sketch(sketch_b, sizeof(sketch_b)));
    CHECK(storage.in_progress_sketch_bytes() == sizeof(sketch_a) + sizeof(sketch_b));
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.read_finished("msg-chunk", fl_product_record::kMediaAudio, 0, part, sizeof(part), &n));
    CHECK(n == sizeof(part));
    fill_bytes(expect, sizeof(expect), seed);
    CHECK(memcmp(part, expect, sizeof(part)) == 0);
    CHECK(storage.read_finished("msg-chunk", fl_product_record::kMediaAudio, 16384, part, sizeof(part), &n));
    CHECK(n == total - 16384);
    fill_bytes(expect, n, seed + 16384);
    CHECK(memcmp(part, expect, n) == 0);
    CHECK(storage.read_finished("msg-chunk", fl_product_record::kMediaAudio, 1000, window, sizeof(window), &n));
    CHECK(n == sizeof(window));
    fill_bytes(expect, sizeof(window), seed + 1000);
    CHECK(memcmp(window, expect, sizeof(window)) == 0);
    CHECK(read_matches(&storage, "msg-chunk", fl_product_record::kMediaAudio, total, seed));
    CHECK(storage.read_finished("msg-chunk", fl_product_record::kMediaSketch, 0, sketch_got, sizeof(sketch_got), &n));
    CHECK(n == sizeof(sketch_got));
    CHECK(memcmp(sketch_got, sketch_a, sizeof(sketch_a)) == 0);
    CHECK(memcmp(sketch_got + sizeof(sketch_a), sketch_b, sizeof(sketch_b)) == 0);
}

void test_budget_overflow_writes_nothing_extra()
{
    TempMedia media;
    fl_product_record::AttachedStorage storage;
    uint8_t chunk[600];
    uint8_t one = 0x44;
    CHECK(media.open());
    storage.attach_media(&media.backend);
    CHECK(storage.begin_in_progress("msg-budget", 1000));
    fill_bytes(chunk, sizeof(chunk), 5);
    CHECK(!storage.append_in_progress_audio(chunk, 1001));
    CHECK(storage.in_progress_audio_bytes() == 0);
    CHECK(storage.append_in_progress_audio(chunk, 600));
    CHECK(!storage.append_in_progress_audio(chunk, 500));
    CHECK(storage.in_progress_audio_bytes() == 600);
    CHECK(storage.append_in_progress_audio(chunk, 400));
    CHECK(storage.in_progress_audio_bytes() == 1000);
    CHECK(!storage.append_in_progress_audio(&one, 1));
    CHECK(storage.in_progress_audio_bytes() == 1000);
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_slot(0) != 0);
    CHECK(storage.finished_slot(0)->audio_bytes == 1000);
    CHECK(read_matches(&storage, "msg-budget", fl_product_record::kMediaAudio, 600, 5));
    {
        uint8_t tail[500];
        size_t n = 0;
        CHECK(storage.read_finished("msg-budget", fl_product_record::kMediaAudio, 600, tail, sizeof(tail), &n));
        CHECK(n == 400);
    }
}

void test_sketch_cap_and_overlap_read_while_append()
{
    TempMedia media;
    fl_product_record::AttachedStorage storage;
    const size_t chunks[] = {4096};
    const uint32_t total_a = 20480;
    const uint32_t seed_a = 21;
    const uint32_t seed_b = 80021;
    uint8_t extra = 1;
    uint8_t sketch[8];
    uint64_t first_offset = 0;
    CHECK(media.open());
    storage.attach_media(&media.backend);
    CHECK(storage.begin_in_progress("msg-sketch-cap", 0));
    CHECK(!storage.append_in_progress_audio(&extra, 1));
    CHECK(storage.in_progress_audio_bytes() == 0);
    {
        uint32_t wrote = 0;
        uint8_t block[4096];
        while (wrote < (uint32_t)fl_product_record::kMaxFlsk2Bytes) {
            fill_bytes(block, sizeof(block), 3000 + wrote);
            CHECK(storage.write_in_progress_sketch(block, sizeof(block)));
            wrote += (uint32_t)sizeof(block);
        }
        CHECK(storage.in_progress_sketch_bytes() == (uint32_t)fl_product_record::kMaxFlsk2Bytes);
        CHECK(!storage.write_in_progress_sketch(&extra, 1));
        CHECK(storage.in_progress_sketch_bytes() == (uint32_t)fl_product_record::kMaxFlsk2Bytes);
    }
    CHECK(storage.commit_in_progress_to_finished());
    storage.clear_finished_upload("msg-sketch-cap");
    CHECK(storage.finished_count() == 0);
    CHECK(storage.begin_in_progress("msg-a", total_a));
    CHECK(append_audio_chunks(&storage, total_a, seed_a, chunks, 1));
    fill_bytes(sketch, sizeof(sketch), 70);
    CHECK(storage.write_in_progress_sketch(sketch, sizeof(sketch)));
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_slot(0) != 0);
    first_offset = storage.finished_slot(0)->audio_offset;
    CHECK(storage.mark_finished_uploading("msg-a"));
    CHECK(storage.can_start_second_recording());
    CHECK(storage.begin_in_progress("msg-b", 8192));
    CHECK(!storage.can_start_second_recording());
    CHECK(append_audio_chunks(&storage, 4096, seed_b, chunks, 1));
    CHECK(read_matches(&storage, "msg-a", fl_product_record::kMediaAudio, total_a, seed_a));
    {
        uint8_t got[8];
        size_t n = 0;
        CHECK(storage.read_finished("msg-a", fl_product_record::kMediaSketch, 0, got, sizeof(got), &n));
        CHECK(n == sizeof(got));
        fill_bytes(sketch, sizeof(sketch), 70);
        CHECK(memcmp(got, sketch, sizeof(got)) == 0);
    }
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_count() == 2);
    CHECK(!storage.can_start_second_recording());
    CHECK(read_matches(&storage, "msg-b", fl_product_record::kMediaAudio, 4096, seed_b));
    CHECK(read_matches(&storage, "msg-a", fl_product_record::kMediaAudio, total_a, seed_a));
    storage.clear_finished_upload("msg-a");
    CHECK(storage.finished_count() == 1);
    {
        size_t n = 0;
        CHECK(!storage.read_finished("msg-a", fl_product_record::kMediaAudio, 0, sketch, 1, &n));
    }
    CHECK(read_matches(&storage, "msg-b", fl_product_record::kMediaAudio, 4096, seed_b));
    storage.clear_finished_upload("msg-b");
    CHECK(storage.finished_count() == 0);
    CHECK(storage.can_start_second_recording());
    CHECK(storage.begin_in_progress("msg-d", 128));
    CHECK(append_audio_chunks(&storage, 128, 4, chunks, 1));
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_slot(0) != 0);
    CHECK(storage.finished_slot(0)->audio_offset == first_offset);
}

void test_discard_does_not_corrupt_uploading_object()
{
    TempMedia media;
    fl_product_record::AttachedStorage storage;
    const size_t chunks[] = {1024};
    const uint32_t seed_a = 15;
    const uint32_t seed_b = 615;
    const uint32_t seed_c = 915;
    uint64_t audio_offset = 0;
    uint32_t audio_bytes = 0;
    CHECK(media.open());
    storage.attach_media(&media.backend);
    CHECK(storage.begin_in_progress("msg-a", 2048));
    CHECK(append_audio_chunks(&storage, 1024, seed_a, chunks, 1));
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_slot(0) != 0);
    audio_offset = storage.finished_slot(0)->audio_offset;
    audio_bytes = storage.finished_slot(0)->audio_bytes;
    CHECK(storage.mark_finished_uploading("msg-a"));
    CHECK(storage.begin_in_progress("msg-b", 2048));
    CHECK(append_audio_chunks(&storage, 1024, seed_b, chunks, 1));
    CHECK(read_matches(&storage, "msg-a", fl_product_record::kMediaAudio, 1024, seed_a));
    storage.discard_in_progress();
    CHECK(!storage.has_in_progress());
    CHECK(storage.finished_count() == 1);
    CHECK(read_matches(&storage, "msg-a", fl_product_record::kMediaAudio, 1024, seed_a));
    CHECK(storage.can_start_second_recording());
    CHECK(storage.begin_in_progress("msg-c", 2048));
    CHECK(append_audio_chunks(&storage, 1024, seed_c, chunks, 1));
    CHECK(storage.commit_in_progress_to_finished());
    CHECK(storage.finished_slot(1) != 0);
    CHECK(storage.finished_slot(1)->audio_offset == audio_offset + (uint64_t)audio_bytes);
    CHECK(read_matches(&storage, "msg-a", fl_product_record::kMediaAudio, 1024, seed_a));
    CHECK(read_matches(&storage, "msg-c", fl_product_record::kMediaAudio, 1024, seed_c));
}

void test_power_loss_keeps_committed_object()
{
    TempMedia media;
    char path_copy[64];
    uint8_t kept[256];
    uint8_t dropped[256];
    uint8_t out[256];
    size_t n = 0;
    CHECK(media.open());
    memcpy(path_copy, media.path, sizeof(path_copy));
    {
        fl_product_record::AttachedStorage storage;
        storage.attach_media(&media.backend);
        CHECK(storage.begin_in_progress("kept", 1000));
        fill_bytes(kept, sizeof(kept), 0);
        CHECK(storage.append_in_progress_audio(kept, sizeof(kept)));
        CHECK(storage.commit_in_progress_to_finished());
        CHECK(storage.mark_finished_uploading("kept"));
        CHECK(storage.begin_in_progress("dropped", 1000));
        memset(dropped, 0x5a, sizeof(dropped));
        CHECK(storage.append_in_progress_audio(dropped, sizeof(dropped)));
    }
    media.backend.close();
    {
        fl_product_record::FileMediaBackend reopened;
        fl_product_record::AttachedStorage restored;
        const fl_product_record::StoredObject *slot;
        CHECK(reopened.open_path(path_copy, false));
        restored.attach_media(&reopened);
        CHECK(!restored.has_in_progress());
        CHECK(restored.finished_count() == 1);
        slot = restored.finished_slot(0);
        CHECK(slot != 0);
        CHECK(slot->uploading == 1);
        CHECK(strcmp(slot->client_message_id, "kept") == 0);
        CHECK(restored.read_finished("kept", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
        CHECK(n == sizeof(out));
        CHECK(memcmp(out, kept, sizeof(kept)) == 0);
        CHECK(!restored.read_finished("dropped", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
        reopened.close();
    }
}

void test_wipe_keeps_audio_after_complete()
{
    TempMedia media;
    fl_product_record::RecordSession session;
    uint8_t during[16];
    uint8_t kept[16];
    uint8_t out[16];
    uint8_t quiet[32];
    size_t n = 0;
    size_t i;
    int64_t opened;
    session.boot(0);
    CHECK(media.open());
    session.attach_media(&media.backend);
    CHECK(LAB10_RING_DURATION_MS == 600);
    CHECK(LAB10_RING_ORIGIN_X == 160);
    CHECK(LAB10_RING_ORIGIN_Y == 282);
    CHECK(LAB10_RING_RADIUS_START == 42);
    CHECK(LAB10_RING_RADIUS_COVERAGE == 365);
    CHECK(LAB10_RING_BAND_WIDTH == 18);
    fill_bytes(during, sizeof(during), 1);
    fill_bytes(kept, sizeof(kept), 2);
    CHECK(session.try_start("msg-wipe", fl_product_record::kMicReady));
    CHECK(session.wiping());
    CHECK(!session.capture_active());
    CHECK(session.ring_outer_radius() == LAB10_RING_RADIUS_START);
    CHECK(!session.push_pcm(during, sizeof(during)));
    CHECK(session.storage().in_progress_audio_bytes() == 0);
    CHECK(!session.route_touch(20, 20, 0, 1));
    CHECK(!session.route_touch(30, 24, 1, 1));
    session.advance_to(LAB10_RING_DURATION_MS - 1);
    CHECK(session.wiping());
    CHECK(!session.capture_active());
    CHECK(!session.push_pcm(during, sizeof(during)));
    CHECK(session.ring_outer_radius() > LAB10_RING_RADIUS_START);
    CHECK(session.ring_outer_radius() < LAB10_RING_RADIUS_COVERAGE);
    session.advance_to(LAB10_RING_DURATION_MS);
    CHECK(!session.wiping());
    CHECK(session.capture_active());
    CHECK(session.ring_outer_radius() == LAB10_RING_RADIUS_COVERAGE);
    CHECK(session.consume_notice() == fl_product_record::kSessionNoticeWipeCompleted);
    CHECK(session.push_pcm(kept, sizeof(kept)));
    CHECK(session.route_touch(20, 20, 0, 1));
    CHECK(session.route_touch(40, 30, 1, 1));
    CHECK(session.route_touch(48, 30, 2, 1));
    memset(quiet, 0, sizeof(quiet));
    CHECK(session.push_pcm(quiet, sizeof(quiet)));
    opened = session.now_ms();
    session.advance_to(opened + 30000);
    CHECK(session.capture_active());
    CHECK(session.last_finish() == fl_product_record::kFinishNone);
    session.advance_to(opened + fl_product_record::kDurationCapMs - 1);
    CHECK(session.capture_active());
    session.advance_to(opened + fl_product_record::kDurationCapMs);
    CHECK(!session.capture_active());
    CHECK(session.last_finish() == fl_product_record::kFinishDurationCap);
    CHECK(session.consume_notice() == fl_product_record::kSessionNoticeTakeFinished);
    CHECK(session.storage().finished_count() == 1);
    CHECK(session.storage().read_finished("msg-wipe", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
    CHECK(n == sizeof(kept));
    CHECK(memcmp(out, kept, sizeof(kept)) == 0);
    {
        const fl_product_record::StoredObject *slot = session.storage().finished_slot(0);
        CHECK(slot != 0);
        CHECK(slot->sketch_bytes > 0);
        CHECK(slot->audio_bytes == sizeof(kept) + sizeof(quiet));
    }
    session.begin_finish_collapse(session.now_ms());
    CHECK(session.reversing());
    CHECK(session.ring_outer_radius() == LAB10_RING_RADIUS_COVERAGE);
    session.advance_to(session.now_ms() + 300);
    CHECK(session.reversing());
    CHECK(session.ring_outer_radius() < LAB10_RING_RADIUS_COVERAGE);
    CHECK(session.ring_outer_radius() > LAB10_RING_RADIUS_START);
    session.advance_to(session.now_ms() + LAB10_RING_DURATION_MS);
    CHECK(!session.reversing());
    CHECK(session.ring_outer_radius() == LAB10_RING_RADIUS_START);
    CHECK(session.storage().finished_count() == 1);
    CHECK(session.storage().read_finished("msg-wipe", fl_product_record::kMediaAudio, 0, out, sizeof(out), &n));
    CHECK(n == sizeof(kept));
    CHECK(memcmp(out, kept, sizeof(kept)) == 0);

    {
        TempMedia ink_media;
        fl_product_record::RecordSession ink;
        ink.boot(0);
        CHECK(ink_media.open());
        ink.attach_media(&ink_media.backend);
        CHECK(ink.try_start("msg-ink", fl_product_record::kMicReady));
        CHECK(!ink.route_touch(16, 16, 0, 1));
        ink.advance_to(ink.now_ms() + LAB10_RING_DURATION_MS);
        CHECK(ink.consume_notice() == fl_product_record::kSessionNoticeWipeCompleted);
        CHECK(ink.route_touch(8, 8, 0, 1));
        for (i = 0; i < fl_product_record::kMaxSketchPoints + 8; ++i) {
            const int16_t x = (int16_t)(8 + (int)((i % 110) * 2));
            const int16_t y = (int16_t)(8 + (int)((i / 110) * 2));
            CHECK(ink.route_touch(x, y, 1, 1));
        }
        ink.advance_to(ink.now_ms());
        CHECK(ink.capture_active());
        CHECK(ink.ink_overflow());
        CHECK(ink.last_finish() == fl_product_record::kFinishNone);
        CHECK(ink.stop_recording());
        CHECK(ink.storage().finished_count() == 1);
    }
}

} /* namespace */

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "all";
    if (strcmp(name, "geometry") == 0) {
        test_geometry_and_rail();
    } else if (strcmp(name, "mapping") == 0) {
        test_logical_mapping();
    } else if (strcmp(name, "capture") == 0) {
        test_adjacent_pixels_and_strokes();
        test_tap_and_diagonal();
    } else if (strcmp(name, "session") == 0) {
        test_session_capture_signal_and_budget();
        test_overlap_storage();
        test_mic_and_ink_overflow();
        test_third_start_refused_when_slots_full();
    } else if (strcmp(name, "overflow") == 0) {
        test_ink_overflow_keeps_recording();
    } else if (strcmp(name, "wipe") == 0) {
        test_wipe_keeps_audio_after_complete();
    } else if (strcmp(name, "media") == 0) {
        test_file_backend_append_and_read();
        test_chunked_append_and_readback();
        test_budget_overflow_writes_nothing_extra();
        test_sketch_cap_and_overlap_read_while_append();
        test_discard_does_not_corrupt_uploading_object();
        test_power_loss_keeps_committed_object();
    } else {
        test_geometry_and_rail();
        test_logical_mapping();
        test_adjacent_pixels_and_strokes();
        test_tap_and_diagonal();
        test_session_capture_signal_and_budget();
        test_overlap_storage();
        test_mic_and_ink_overflow();
        test_third_start_refused_when_slots_full();
        test_wipe_keeps_audio_after_complete();
        test_ink_overflow_keeps_recording();
        test_file_backend_append_and_read();
        test_chunked_append_and_readback();
        test_budget_overflow_writes_nothing_extra();
        test_sketch_cap_and_overlap_read_while_append();
        test_discard_does_not_corrupt_uploading_object();
        test_power_loss_keeps_committed_object();
    }
    if (g_failures != 0) {
        fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    puts("PRODUCT_RECORD_MODEL_PASS");
    return 0;
}
