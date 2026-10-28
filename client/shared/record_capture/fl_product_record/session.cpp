#include "fl_product_record_session.hpp"

#include "fl_product_record_flsk2.hpp"

#include "fl_product_record_lab10.h"

#include <new>
#include <string.h>

namespace fl_product_record
{
namespace
{

/* Ease-in-out quint, the lab ring path. Endpoints are the promoted radii. */
int32_t ease_in_out_quint(int32_t elapsed, int32_t duration, int32_t from, int32_t to)
{
    uint64_t e;
    uint64_t d;
    uint64_t d5;
    int64_t span_i;
    if (elapsed <= 0) {
        return from;
    }
    if (elapsed >= duration || duration <= 0) {
        return to;
    }
    e = (uint64_t)elapsed;
    d = (uint64_t)duration;
    d5 = d * d * d * d * d;
    span_i = (int64_t)to - (int64_t)from;
    if (elapsed * 2 <= duration) {
        uint64_t e5 = e * e * e * e * e;
        return from + (int32_t)((span_i * (int64_t)(16ull * e5)) / (int64_t)d5);
    }
    {
        uint64_t remain = d - e;
        uint64_t r5 = remain * remain * remain * remain * remain;
        int64_t kept = (int64_t)d5 - (int64_t)(16ull * r5);
        return from + (int32_t)((span_i * kept) / (int64_t)d5);
    }
}

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

lab10_capture_t *as_capture(void *handle)
{
    return static_cast<lab10_capture_t *>(handle);
}

lab10_render_t *as_renderer(void *handle)
{
    return static_cast<lab10_render_t *>(handle);
}

} /* namespace */

RecordSession::RecordSession()
    : capture_(0),
      renderer_(0),
      session_id_(0),
      start_ms_(0),
      now_ms_(0),
      wipe_start_ms_(0),
      reverse_start_ms_(0),
      reverse_from_radius_(LAB10_RING_RADIUS_START),
      recording_(false),
      wiping_(false),
      reversing_(false),
      ink_overflow_(false),
      mic_(kMicReady),
      last_finish_(kFinishNone),
      notice_(kSessionNoticeNone),
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
    wiping_ = false;
    reversing_ = false;
    ink_overflow_ = false;
    mic_ = kMicReady;
    last_finish_ = kFinishNone;
    notice_ = kSessionNoticeNone;
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
        /* SEGMENT mode caps every update at one point regardless of the budget below. */
        rcfg.mode = LAB10_RENDER_MODE_BOUNDED_BATCH;
        rcfg.role = LAB10_RENDER_ROLE_CONTROL;
        /* One update per 50 ms presentation tick; a fast finger crosses ~100 logical px. */
        rcfg.max_points_per_update = 16;
        rcfg.max_raster_ops_per_update = 128;
        rcfg.max_publication_ops_per_update = 128;
        renderer_ = lab10_render_create(&rcfg);
    }
}

int64_t RecordSession::ring_elapsed_ms(int64_t origin_ms) const
{
    int64_t elapsed = now_ms_ - origin_ms;
    if (elapsed < 0) {
        return 0;
    }
    return elapsed;
}

void RecordSession::advance_to(int64_t now_ms)
{
    lab10_capture_stats_t stats;
    lab10_capture_t *capture = as_capture(capture_);
    now_ms_ = now_ms;
    if (reversing_) {
        if (ring_elapsed_ms(reverse_start_ms_) >= LAB10_RING_DURATION_MS) {
            reversing_ = false;
        }
        return;
    }
    if (wiping_) {
        if (ring_elapsed_ms(wipe_start_ms_) < LAB10_RING_DURATION_MS) {
            return;
        }
        if (!open_kept_capture()) {
            notice_ = kSessionNoticeCaptureFailed;
            return;
        }
        notice_ = kSessionNoticeWipeCompleted;
        return;
    }
    if (!recording_ || capture == 0 || session_id_ == 0) {
        return;
    }
    clock_us_ = (uint64_t)(now_ms_ - start_ms_) * 1000ULL;
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

int RecordSession::consume_notice()
{
    int notice = notice_;
    notice_ = kSessionNoticeNone;
    return notice;
}

bool RecordSession::capture_active() const
{
    return recording_;
}

bool RecordSession::wiping() const
{
    return wiping_;
}

bool RecordSession::reversing() const
{
    return reversing_;
}

int64_t RecordSession::now_ms() const
{
    return now_ms_;
}

int32_t RecordSession::ring_outer_radius() const
{
    if (recording_) {
        return LAB10_RING_RADIUS_COVERAGE;
    }
    if (reversing_) {
        return ease_in_out_quint((int32_t)ring_elapsed_ms(reverse_start_ms_), LAB10_RING_DURATION_MS,
                                 reverse_from_radius_, LAB10_RING_RADIUS_START);
    }
    if (!wiping_) {
        return LAB10_RING_RADIUS_START;
    }
    return ease_in_out_quint((int32_t)ring_elapsed_ms(wipe_start_ms_), LAB10_RING_DURATION_MS,
                             LAB10_RING_RADIUS_START, LAB10_RING_RADIUS_COVERAGE);
}

bool RecordSession::ink_overflow() const
{
    return ink_overflow_;
}

MicState RecordSession::mic_state() const
{
    return mic_;
}

FinishReason RecordSession::last_finish() const
{
    return last_finish_;
}

bool RecordSession::try_start(const char *client_message_id, MicState mic_at_start)
{
    if (recording_ || wiping_ || reversing_ || client_message_id == 0) {
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
    wipe_start_ms_ = now_ms_;
    wiping_ = true;
    recording_ = false;
    reversing_ = false;
    ink_overflow_ = false;
    mic_ = kMicReady;
    last_finish_ = kFinishNone;
    session_id_ = 0;
    clock_us_ = 0;
    return true;
}

bool RecordSession::open_kept_capture()
{
    lab10_capture_t *capture = as_capture(capture_);
    if (capture == 0) {
        begin_reverse(now_ms_);
        return false;
    }
    lab10_ui_finish_entry(&s_ui_model);
    session_id_ = lab10_capture_start(capture);
    if (session_id_ == LAB10_CAPTURE_INVALID_SESSION_ID) {
        begin_reverse(now_ms_);
        return false;
    }
    if (renderer_ != 0) {
        (void)lab10_render_begin(as_renderer(renderer_), session_id_);
    }
    wiping_ = false;
    recording_ = true;
    start_ms_ = now_ms_;
    clock_us_ = 0;
    ink_overflow_ = false;
    mic_ = kMicReady;
    last_finish_ = kFinishNone;
    return true;
}

void RecordSession::begin_finish_collapse(int64_t now_ms)
{
    if (reversing_) {
        return;
    }
    now_ms_ = now_ms;
    reverse_from_radius_ = LAB10_RING_RADIUS_COVERAGE;
    reverse_start_ms_ = now_ms_;
    recording_ = false;
    wiping_ = false;
    reversing_ = true;
}

void RecordSession::begin_reverse(int64_t now_ms)
{
    if (reversing_) {
        return;
    }
    now_ms_ = now_ms;
    reverse_from_radius_ = ring_outer_radius();
    reverse_start_ms_ = now_ms_;
    if (recording_ && capture_ != 0 && session_id_ != 0) {
        if (renderer_ != 0) {
            (void)lab10_render_reset(as_renderer(renderer_), session_id_);
        }
        (void)lab10_capture_cancel(as_capture(capture_), session_id_);
    }
    session_id_ = 0;
    recording_ = false;
    wiping_ = false;
    storage_.discard_in_progress();
    reversing_ = true;
}

bool RecordSession::push_pcm(const uint8_t *pcm, size_t len)
{
    if (pcm == 0 || len == 0 || wiping_ || reversing_ || !recording_) {
        return false;
    }
    return storage_.append_in_progress_audio(pcm, len);
}

void RecordSession::notify_mic_failed()
{
    if (wiping_) {
        mic_ = kMicUnavailableAtStart;
        begin_reverse(now_ms_);
        notice_ = kSessionNoticeCaptureFailed;
        return;
    }
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
        if (palette >= 0 && ui_phase == LAB10_PHASE_DOWN) {
            (void)lab10_ui_handle_touch(&s_ui_model, LAB10_PHASE_PALETTE_CHANGE, x, y, touch_id);
            /* Capture rejects canvas reports whose color differs from its own selection. */
            memset(&report, 0, sizeof(report));
            report.timestamp_us = clock_us_;
            report.display_x = x;
            report.display_y = y;
            report.touch_id = touch_id;
            report.phase = LAB10_PHASE_PALETTE_CHANGE;
            report.route = route;
            report.color_index = (uint8_t)palette;
            (void)lab10_capture_submit(capture, session_id_, &report);
            (void)lab10_capture_poll(capture, session_id_);
        }
        return true;
    }
    if (route != LAB10_ROUTE_CANVAS) {
        return false;
    }
    if (wiping_ || reversing_) {
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

void RecordSession::poll_capture()
{
    advance_to(now_ms_);
}

void RecordSession::render_update()
{
    if (!recording_ || capture_ == 0 || renderer_ == 0 || session_id_ == 0) {
        return;
    }
    (void)lab10_render_update(as_renderer(renderer_), as_capture(capture_), session_id_);
}

bool RecordSession::render_backlog(size_t *source_points, size_t *pending_points, size_t *drawn_last) const
{
    lab10_render_metrics_t metrics;
    if (!recording_ || renderer_ == 0 || session_id_ == 0 ||
        !lab10_render_get_metrics(as_renderer(renderer_), session_id_, &metrics)) {
        return false;
    }
    *source_points = metrics.source_point_count;
    *pending_points = metrics.pending_point_count;
    *drawn_last = metrics.points_processed_last_update;
    return true;
}

uint8_t RecordSession::selected_color() const
{
    return lab10_ui_selected_color(&s_ui_model);
}

bool RecordSession::finish_capture(FinishReason reason)
{
    lab10_capture_t *capture = as_capture(capture_);
    lab10_capture_snapshot_t snapshot;
    /* 32 KiB: never on the stack. This runs on the 12 KiB LVGL task. */
    uint8_t *flsk2 = 0;
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
    flsk2_len = 0;
    if (snapshot.point_count > 0) {
        CapturePointView *views = new (std::nothrow) CapturePointView[snapshot.point_count];
        flsk2 = new (std::nothrow) uint8_t[kMaxFlsk2Bytes];
        if (views != 0 && flsk2 != 0) {
            size_t i;
            for (i = 0; i < snapshot.point_count; ++i) {
                views[i].timestamp_us = snapshot.points[i].timestamp_us;
                views[i].logical_x = snapshot.points[i].logical_x;
                views[i].logical_y = snapshot.points[i].logical_y;
                views[i].color_index = snapshot.points[i].color_index;
            }
            flsk2_len = encode_flsk2(views, snapshot.point_count, flsk2, kMaxFlsk2Bytes, 0);
        }
        delete[] views;
    }
    if (flsk2_len > 0) {
        (void)storage_.write_in_progress_sketch(flsk2, flsk2_len);
    }
    delete[] flsk2;
    if (reason != kFinishCancelled) {
        (void)storage_.commit_in_progress_to_finished();
    } else {
        storage_.discard_in_progress();
    }
    if (renderer_ != 0) {
        (void)lab10_render_reset(as_renderer(renderer_), session_id_);
    }
    (void)lab10_capture_release_result(capture, session_id_);
    recording_ = false;
    wiping_ = false;
    last_finish_ = reason;
    session_id_ = 0;
    if (reason == kFinishDurationCap || reason == kFinishMicFailure) {
        notice_ = kSessionNoticeTakeFinished;
    }
    return true;
}

bool RecordSession::stop_recording()
{
    if (!recording_) {
        return false;
    }
    return finish_capture(kFinishRedStop);
}

void RecordSession::cancel_recording()
{
    if (recording_) {
        (void)finish_capture(kFinishCancelled);
        wiping_ = false;
        reversing_ = false;
        return;
    }
    storage_.discard_in_progress();
    wiping_ = false;
    reversing_ = false;
    session_id_ = 0;
}

void RecordSession::attach_media(MediaBackend *backend)
{
    storage_.attach_media(backend);
}

AttachedStorage &RecordSession::storage()
{
    return storage_;
}

void *RecordSession::capture_handle()
{
    return capture_;
}

void *RecordSession::renderer_handle()
{
    return renderer_;
}

uint64_t RecordSession::capture_clock_us() const
{
    return clock_us_;
}

} /* namespace fl_product_record */
