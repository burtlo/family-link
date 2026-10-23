#include "fl_product_record_session.hpp"

#include "fl_product_record_flsk2.hpp"

#include "fl_product_record_lab10.h"

#include <new>
#include <string.h>

namespace fl_product_record {
namespace {

struct ClockAdapter {
    RecordSession *session;
};

static ClockAdapter s_clock_adapter;
static lab10_ui_model_t s_ui_model;

uint64_t clock_now_us(void *context)
{
    ClockAdapter *adapter = static_cast<ClockAdapter *>(context);
    if (adapter == 0 || adapter->session == 0) {
        return 0;
    }
    return adapter->session->capture_clock_us();
}

lab10_capture_t *as_capture(void *handle) { return static_cast<lab10_capture_t *>(handle); }

lab10_render_t *as_renderer(void *handle) { return static_cast<lab10_render_t *>(handle); }

} /* namespace */

RecordSession::RecordSession()
    : capture_(0),
      renderer_(0),
      session_id_(0),
      start_ms_(0),
      now_ms_(0),
      recording_(false),
      ink_overflow_(false),
      mic_(kMicReady),
      last_finish_(kFinishNone),
      clock_us_(0)
{
}

RecordSession::~RecordSession()
{
    if (renderer_ != 0) {
        lab10_render_destroy(as_renderer(renderer_));
        renderer_ = 0;
    }
    if (capture_ != 0) {
        lab10_capture_destroy(as_capture(capture_));
        capture_ = 0;
    }
}

void RecordSession::boot(int64_t now_ms)
{
    now_ms_ = now_ms;
    reset_runtime();
}

void RecordSession::reset_runtime()
{
    recording_ = false;
    ink_overflow_ = false;
    mic_ = kMicReady;
    last_finish_ = kFinishNone;
    session_id_ = 0;
    clock_us_ = 0;
    s_clock_adapter.session = this;
    if (capture_ == 0) {
        lab10_capture_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.clock_now_us = clock_now_us;
        cfg.clock_context = &s_clock_adapter;
        cfg.initial_color_index = LAB10_COLOR_WHITE;
        capture_ = lab10_capture_create(&cfg);
    }
    if (renderer_ == 0 && capture_ != 0) {
        lab10_render_config_t rcfg;
        memset(&rcfg, 0, sizeof(rcfg));
        rcfg.mode = LAB10_RENDER_MODE_SEGMENT;
        rcfg.role = LAB10_RENDER_ROLE_CONTROL;
        renderer_ = lab10_render_create(&rcfg);
    }
}

void RecordSession::advance_to(int64_t now_ms)
{
    lab10_capture_stats_t stats;
    lab10_capture_t *capture = as_capture(capture_);
    now_ms_ = now_ms;
    if (!recording_ || capture == 0 || session_id_ == 0) {
        return;
    }
    clock_us_ = (uint64_t)(now_ms - start_ms_) * 1000ULL;
    (void)lab10_capture_poll(capture, session_id_);
    if (!lab10_capture_get_stats(capture, session_id_, &stats)) {
        return;
    }
    if (stats.point_cap_hit_count > 0) {
        ink_overflow_ = true;
    }
    if (stats.duration_cap_hit_count > 0 &&
        lab10_capture_status(capture, session_id_) == LAB10_CAPTURE_STATUS_TERMINAL) {
        (void)finish_capture(kFinishDurationCap);
    }
}

bool RecordSession::capture_active() const { return recording_; }

bool RecordSession::ink_overflow() const { return ink_overflow_; }

MicState RecordSession::mic_state() const { return mic_; }

FinishReason RecordSession::last_finish() const { return last_finish_; }

bool RecordSession::try_start(const char *client_message_id, MicState mic_at_start)
{
    lab10_capture_t *capture = as_capture(capture_);
    if (recording_ || client_message_id == 0) {
        return false;
    }
    if (mic_at_start == kMicUnavailableAtStart) {
        mic_ = kMicUnavailableAtStart;
        return false;
    }
    if (!storage_.can_start_second_recording()) {
        return false;
    }
    if (!storage_.begin_in_progress(client_message_id, kAudioByteBudget)) {
        return false;
    }
    lab10_ui_model_init(&s_ui_model);
    lab10_ui_enter_ring_wipe(&s_ui_model);
    lab10_ui_finish_entry(&s_ui_model);
    session_id_ = lab10_capture_start(capture);
    if (session_id_ == LAB10_CAPTURE_INVALID_SESSION_ID) {
        storage_.discard_in_progress();
        return false;
    }
    start_ms_ = now_ms_;
    clock_us_ = 0;
    recording_ = true;
    ink_overflow_ = false;
    mic_ = kMicReady;
    last_finish_ = kFinishNone;
    return true;
}

void RecordSession::notify_mic_failed()
{
    if (!recording_) {
        return;
    }
    mic_ = kMicFailedDuringCapture;
    (void)finish_capture(kFinishMicFailure);
}

bool RecordSession::route_touch(int16_t x, int16_t y, uint8_t phase, uint8_t touch_id)
{
    lab10_capture_t *capture = as_capture(capture_);
    lab10_touch_phase_t ui_phase;
    lab10_touch_route_t route;
    uint8_t color_index;
    lab10_capture_raw_report_t report;
    if (!recording_ || capture == 0 || session_id_ == 0) {
        return false;
    }
    switch (phase) {
    case 0:
        ui_phase = LAB10_PHASE_DOWN;
        break;
    case 1:
        ui_phase = LAB10_PHASE_MOVE;
        break;
    case 2:
        ui_phase = LAB10_PHASE_UP;
        break;
    default:
        ui_phase = LAB10_PHASE_LEAVE;
        break;
    }
    route = lab10_ui_route_touch(&s_ui_model, x, y);
    if (route == LAB10_ROUTE_PALETTE) {
        const int palette = lab10_palette_hit_test(x, y);
        if (palette >= 0) {
            (void)lab10_ui_handle_touch(&s_ui_model, LAB10_PHASE_PALETTE_CHANGE, x, y, touch_id);
        }
        return true;
    }
    if (route != LAB10_ROUTE_CANVAS) {
        return false;
    }
    color_index = lab10_ui_selected_color(&s_ui_model);
    memset(&report, 0, sizeof(report));
    report.timestamp_us = clock_us_;
    report.display_x = x;
    report.display_y = y;
    report.touch_id = touch_id;
    report.phase = ui_phase;
    report.route = route;
    report.color_index = color_index;
    (void)lab10_capture_submit(capture, session_id_, &report);
    (void)lab10_capture_poll(capture, session_id_);
    return true;
}

void RecordSession::poll_capture() { advance_to(now_ms_); }

bool RecordSession::finish_capture(FinishReason reason)
{
    lab10_capture_t *capture = as_capture(capture_);
    lab10_capture_snapshot_t snapshot;
    uint8_t flsk2[kMaxFlsk2Bytes];
    size_t flsk2_len;
    if (!recording_ || capture == 0 || session_id_ == 0) {
        return false;
    }
    if (reason == kFinishCancelled) {
        (void)lab10_capture_cancel(capture, session_id_);
    } else {
        (void)lab10_capture_stop(capture, session_id_);
    }
    (void)lab10_capture_poll(capture, session_id_);
    if (!lab10_capture_snapshot(capture, session_id_, &snapshot)) {
        storage_.discard_in_progress();
        recording_ = false;
        last_finish_ = reason;
        return false;
    }
    if (snapshot.point_count > 0) {
        CapturePointView *views = new CapturePointView[snapshot.point_count];
        size_t i;
        for (i = 0; i < snapshot.point_count; ++i) {
            views[i].timestamp_us = snapshot.points[i].timestamp_us;
            views[i].logical_x = snapshot.points[i].logical_x;
            views[i].logical_y = snapshot.points[i].logical_y;
            views[i].color_index = snapshot.points[i].color_index;
        }
        flsk2_len = encode_flsk2(views, snapshot.point_count, flsk2, sizeof(flsk2), 0);
        delete[] views;
    } else {
        flsk2_len = 0;
    }
    if (flsk2_len > 0) {
        (void)storage_.write_in_progress_sketch(flsk2, flsk2_len);
    }
    if (reason != kFinishCancelled) {
        (void)storage_.commit_in_progress_to_finished();
    } else {
        storage_.discard_in_progress();
    }
    (void)lab10_capture_release_result(capture, session_id_);
    recording_ = false;
    last_finish_ = reason;
    session_id_ = 0;
    return true;
}

bool RecordSession::stop_recording() { return finish_capture(kFinishRedStop); }

void RecordSession::cancel_recording() { (void)finish_capture(kFinishCancelled); }

AttachedStorage &RecordSession::storage() { return storage_; }

void *RecordSession::capture_handle() { return capture_; }

void *RecordSession::renderer_handle() { return renderer_; }

uint64_t RecordSession::capture_clock_us() const { return clock_us_; }

} /* namespace fl_product_record */
