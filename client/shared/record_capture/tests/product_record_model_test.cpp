#include "fl_product_record_flsk2.hpp"
#include "fl_product_record_session.hpp"
#include "fl_product_record_types.hpp"

#include "fl_product_record_lab10.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void test_session_capture_signal_and_budget()
{
    fl_product_record::RecordSession session;
    session.boot(0);
    CHECK(!session.capture_active());
    CHECK(session.try_start("msg-a", fl_product_record::kMicReady));
    CHECK(session.capture_active());
    CHECK(session.storage().in_progress_audio_budget() == fl_product_record::kAudioByteBudget);
    CHECK(session.stop_recording());
    CHECK(!session.capture_active());
    CHECK(session.storage().finished_count() == 1);
}

void test_overlap_storage()
{
    fl_product_record::RecordSession session;
    session.boot(0);
    CHECK(session.try_start("msg-a", fl_product_record::kMicReady));
    CHECK(session.stop_recording());
    CHECK(session.storage().mark_finished_uploading("msg-a"));
    CHECK(session.storage().can_start_second_recording());
    CHECK(session.try_start("msg-b", fl_product_record::kMicReady));
    CHECK(session.capture_active());
}

void test_mic_and_ink_overflow()
{
    fl_product_record::RecordSession session;
    session.boot(0);
    CHECK(!session.try_start("msg-x", fl_product_record::kMicUnavailableAtStart));
    CHECK(session.try_start("msg-y", fl_product_record::kMicReady));
    session.notify_mic_failed();
    CHECK(!session.capture_active());
    CHECK(session.mic_state() == fl_product_record::kMicFailedDuringCapture);
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
    } else if (strcmp(name, "overflow") == 0) {
        test_ink_overflow_keeps_recording();
    } else {
        test_geometry_and_rail();
        test_logical_mapping();
        test_adjacent_pixels_and_strokes();
        test_tap_and_diagonal();
        test_session_capture_signal_and_budget();
        test_overlap_storage();
        test_mic_and_ink_overflow();
        test_ink_overflow_keeps_recording();
    }
    if (g_failures != 0) {
        fprintf(stderr, "%d failures\n", g_failures);
        return 1;
    }
    puts("PRODUCT_RECORD_MODEL_PASS");
    return 0;
}
