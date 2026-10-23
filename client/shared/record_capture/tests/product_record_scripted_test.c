#include "lab10_capture.h"
#include "lab10_render.h"
#include "lab10_ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint64_t now_us;
} fake_clock_t;

typedef struct {
    lab10_capture_t *capture;
    lab10_capture_session_id_t session_id;
    fake_clock_t *clock;
} touch_context_t;

static uint64_t fake_now(void *context)
{
    return ((fake_clock_t *)context)->now_us;
}

static void submit_ui_touch(const lab10_ui_touch_event_t *event, void *context)
{
    touch_context_t *touch = context;
    assert(event != NULL && touch != NULL);
    assert(event->route == LAB10_ROUTE_CANVAS ||
           event->route == LAB10_ROUTE_PALETTE);
    assert(lab10_capture_submit(
               touch->capture, touch->session_id,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = touch->clock->now_us,
                   .display_x = event->display_x,
                   .display_y = event->display_y,
                   .touch_id = event->touch_id,
                   .phase = event->phase,
                   .route = event->route,
                   .color_index = event->color_index,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
}

static void record_published_color(const lab10_render_pixel_event_t *event,
                                   void *context)
{
    uint8_t *published_color_mask = context;
    assert(event != NULL && published_color_mask != NULL);
    if (event->color_index > LAB10_COLOR_BACKGROUND &&
        event->color_index < LAB10_SWATCH_COUNT) {
        *published_color_mask |= (uint8_t)(1u << event->color_index);
    }
}

static lab10_ui_scripted_capture_summary_t completion_summary(
    uint32_t points, uint32_t strokes, uint32_t color_changes,
    uint8_t color_mask, bool trace_clean)
{
    return (lab10_ui_scripted_capture_summary_t){
        .accepted_point_count = points,
        .stroke_count = strokes,
        .color_change_count = color_changes,
        .accepted_color_mask = color_mask,
        .trace_clean = trace_clean,
    };
}

static void test_terminal_presentation_waits_for_scripted_qualification(void)
{
    lab10_ui_scripted_qualification_t qualification;
    lab10_ui_scripted_qualification_begin(&qualification, true);

    lab10_screen_state_t state = LAB10_SCREEN_FAILED;
    assert(!lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_FAILED);

    lab10_ui_scripted_qualification_resolve(&qualification, true);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_COMPLETED);
    /* Repeated presentation attempts remain qualified if the mailbox is full. */
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_COMPLETED);

    lab10_ui_scripted_qualification_begin(&qualification, true);
    assert(!qualification.decision_resolved);
    assert(!lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    lab10_ui_scripted_qualification_resolve(&qualification, false);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_FAILED);
}

static void test_terminal_failure_and_manual_duration_mappings(void)
{
    const lab10_ui_terminal_kind_t failures[] = {
        LAB10_UI_TERMINAL_POINT_CAP,
        LAB10_UI_TERMINAL_QUEUE_OVERFLOW,
        LAB10_UI_TERMINAL_FAILED,
    };
    lab10_ui_scripted_qualification_t qualification;
    lab10_ui_scripted_qualification_begin(&qualification, true);
    lab10_screen_state_t state = LAB10_SCREEN_COMPLETED;
    for (size_t index = 0; index < sizeof(failures) / sizeof(failures[0]);
         ++index) {
        assert(lab10_ui_terminal_screen_state(
            failures[index], &qualification, &state));
        assert(state == LAB10_SCREEN_FAILED);
    }
    /* A scripted session must never pass through an unqualified terminal. */
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_COMPLETED, &qualification, &state));
    assert(state == LAB10_SCREEN_FAILED);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_STOPPED, &qualification, &state));
    assert(state == LAB10_SCREEN_STOPPED);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_CANCELLED, &qualification, &state));
    assert(state == LAB10_SCREEN_CANCELLED);

    lab10_ui_scripted_qualification_begin(&qualification, false);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_COMPLETED, &qualification, &state));
    assert(state == LAB10_SCREEN_COMPLETED);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_COMPLETED);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_STOPPED, &qualification, &state));
    assert(state == LAB10_SCREEN_STOPPED);
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_CANCELLED, &qualification, &state));
    assert(state == LAB10_SCREEN_CANCELLED);
}

static lab10_ui_scripted_schedule_t new_schedule(uint64_t start_us)
{
    lab10_ui_scripted_schedule_t schedule;
    assert(lab10_ui_scripted_schedule_init(
        &schedule, start_us, LAB10_CAPTURE_DURATION_CAP_US,
        LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
        LAB10_UI_SCRIPTED_MOVE_INTERVAL_US));
    return schedule;
}

static lab10_ui_scripted_poll_result_t poll_schedule(
    lab10_ui_scripted_schedule_t *schedule, uint64_t now_us,
    lab10_ui_scripted_capture_state_t state,
    const lab10_ui_scripted_capture_summary_t *summary,
    lab10_ui_scripted_event_t *event)
{
    return lab10_ui_scripted_schedule_poll(schedule, now_us, state, summary,
                                           event);
}

static lab10_ui_scripted_poll_result_t poll_duration_completion(
    const lab10_ui_scripted_capture_summary_t *summary)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    lab10_ui_scripted_event_t event;
    assert(poll_schedule(&schedule, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    return poll_schedule(&schedule,
                         start_us + LAB10_CAPTURE_DURATION_CAP_US,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, summary,
                         &event);
}

static void test_malformed_duration_cap_cannot_qualify(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    lab10_ui_scripted_event_t event;
    assert(poll_schedule(&schedule, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    const lab10_ui_scripted_poll_result_t result = poll_schedule(
        &schedule, start_us + LAB10_CAPTURE_DURATION_CAP_US,
        LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL,
        &(lab10_ui_scripted_capture_summary_t){
            .accepted_point_count = 3416u,
            .stroke_count = 18u,
            .color_change_count = 17u,
            .accepted_color_mask = 0x3eu,
            .trace_clean = true,
        },
        &event);
    assert(result == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_qualification_t qualification;
    lab10_ui_scripted_qualification_begin(&qualification, true);
    /* The caller may resolve only with duration counters validated; an
     * OTHER_TERMINAL decision is explicitly rejected even if metrics qualify. */
    lab10_ui_scripted_qualification_resolve(
        &qualification, result == LAB10_UI_SCRIPTED_POLL_COMPLETE);
    lab10_screen_state_t state;
    assert(lab10_ui_terminal_screen_state(
        LAB10_UI_TERMINAL_DURATION_CAP, &qualification, &state));
    assert(state == LAB10_SCREEN_FAILED);
}

static void test_scripted_scheduler_timing_telemetry(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    lab10_ui_scripted_event_t event;
    const lab10_ui_scripted_capture_summary_t summary = {0};

    assert(poll_schedule(&schedule, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &summary, &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(poll_schedule(&schedule, start_us + UINT64_C(25000),
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, &summary, &event) ==
           LAB10_UI_SCRIPTED_POLL_WAIT);
    assert(poll_schedule(&schedule, start_us + UINT64_C(60000),
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, &summary, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);

    assert(schedule.poll_count == 3u);
    assert(schedule.max_poll_gap_us == UINT64_C(35000));
    assert(schedule.event_count == 2u);
    assert(schedule.max_event_deadline_lateness_us == UINT64_C(24000));
    assert(schedule.late_event_count == 1u);
}

static lab10_ui_scripted_canvas_observation_t diagnostic_observation(
    uint8_t color_mask, uint8_t final_color_mask, uint32_t format,
    bool supported)
{
    return (lab10_ui_scripted_canvas_observation_t){
        .renderer_published_color_mask = color_mask,
        .canvas_sampled_color_mask = color_mask,
        .canvas_readback_color_mask = supported ? color_mask : 0u,
        .canvas_color_match_mask = supported ? color_mask : 0u,
        .canvas_final_color_mask = supported ? final_color_mask : 0u,
        .canvas_color_format = format,
        .canvas_readback_supported = supported,
    };
}

static void assert_diagnostic_snapshot_equal(
    const lab10_ui_scripted_diagnostic_snapshot_t *left,
    const lab10_ui_scripted_diagnostic_snapshot_t *right)
{
    assert(left->capture_session_id == right->capture_session_id);
    assert(left->accepted_points == right->accepted_points);
    assert(left->renderer_published_color_mask ==
           right->renderer_published_color_mask);
    assert(left->canvas_sampled_color_mask ==
           right->canvas_sampled_color_mask);
    assert(left->canvas_readback_color_mask ==
           right->canvas_readback_color_mask);
    assert(left->canvas_color_match_mask == right->canvas_color_match_mask);
    assert(left->canvas_final_color_mask == right->canvas_final_color_mask);
    assert(left->canvas_color_format == right->canvas_color_format);
    assert(left->canvas_readback_supported == right->canvas_readback_supported);
    assert(left->renderer_drain_complete == right->renderer_drain_complete);
}

static void test_scripted_diagnostic_waits_for_drain_and_retries_frozen_payload(
    void)
{
    lab10_ui_scripted_diagnostic_lifecycle_t lifecycle = {0};
    lab10_ui_scripted_diagnostic_snapshot_t snapshot = {
        .capture_session_id = UINT64_C(41),
        .accepted_points = 300u,
        .renderer_published_color_mask = 0x02u,
        .canvas_sampled_color_mask = 0x02u,
        .canvas_final_color_mask = 0x3eu,
        .canvas_color_format = 16u,
    };
    assert(lab10_ui_scripted_diagnostic_begin(&lifecycle, &snapshot));
    assert(lifecycle.pending && lifecycle.reported);
    assert(snapshot.canvas_final_color_mask == 0u);

    const lab10_ui_scripted_canvas_observation_t final_observation =
        diagnostic_observation(0x3eu, 0x12u, 32u, true);
    const lab10_ui_scripted_renderer_drain_t not_drained = {
        .source_point_count = 299u,
        .source_cursor = 298u,
        .pending_point_count = 2u,
        .frame_idle = false,
    };
    assert(!lab10_ui_scripted_diagnostic_prepare_delivery(
        &lifecycle, &snapshot, true, UINT64_C(41), true, &not_drained,
        &final_observation));
    assert(snapshot.renderer_published_color_mask == 0x02u);
    assert(snapshot.canvas_final_color_mask == 0u);
    assert(!snapshot.renderer_drain_complete);

    const lab10_ui_scripted_renderer_drain_t drained = {
        .source_point_count = 300u,
        .source_cursor = 300u,
        .pending_point_count = 0u,
        .frame_idle = true,
    };
    assert(lab10_ui_scripted_diagnostic_prepare_delivery(
        &lifecycle, &snapshot, true, UINT64_C(41), true, &drained,
        &final_observation));
    assert(snapshot.renderer_drain_complete);
    assert(snapshot.renderer_published_color_mask == 0x3eu);
    assert(snapshot.canvas_readback_color_mask == 0x3eu);
    assert(snapshot.canvas_final_color_mask == 0x12u);

    /* A full mailbox keeps this exact completed snapshot for the next poll. */
    const lab10_ui_scripted_diagnostic_snapshot_t frozen = snapshot;
    lab10_ui_scripted_diagnostic_finish_delivery(&lifecycle, &snapshot, false);
    assert(lifecycle.pending && lifecycle.snapshot_frozen);
    const lab10_ui_scripted_canvas_observation_t next_session_observation =
        diagnostic_observation(0x04u, 0x3eu, 16u, false);
    assert(lab10_ui_scripted_diagnostic_prepare_delivery(
        &lifecycle, &snapshot, true, UINT64_C(42), true, &drained,
        &next_session_observation));
    assert_diagnostic_snapshot_equal(&snapshot, &frozen);

    lab10_ui_scripted_diagnostic_finish_delivery(&lifecycle, &snapshot, true);
    assert(!lifecycle.pending && lifecycle.reported);
    lab10_ui_scripted_diagnostic_allow_next_session(&lifecycle);
    assert(!lifecycle.reported);

    lab10_ui_scripted_diagnostic_snapshot_t next_snapshot = {
        .capture_session_id = UINT64_C(42),
        .accepted_points = 120u,
        .renderer_published_color_mask = 0x04u,
        .canvas_sampled_color_mask = 0x04u,
    };
    assert(lab10_ui_scripted_diagnostic_begin(&lifecycle, &next_snapshot));
    assert(next_snapshot.capture_session_id == UINT64_C(42));
    assert(next_snapshot.renderer_published_color_mask == 0x04u);
    assert(lifecycle.pending && lifecycle.reported);
}

static void test_scripted_diagnostic_rollover_keeps_old_masks_and_releases_latch(
    void)
{
    lab10_ui_scripted_diagnostic_lifecycle_t lifecycle = {0};
    lab10_ui_scripted_diagnostic_snapshot_t snapshot = {
        .capture_session_id = UINT64_C(71),
        .accepted_points = 240u,
        .renderer_published_color_mask = 0x0au,
        .canvas_sampled_color_mask = 0x0au,
        .canvas_readback_color_mask = 0x02u,
        .canvas_color_match_mask = 0x02u,
        .canvas_final_color_mask = 0u,
        .canvas_color_format = 16u,
        .canvas_readback_supported = true,
    };
    assert(lab10_ui_scripted_diagnostic_begin(&lifecycle, &snapshot));
    const lab10_ui_scripted_diagnostic_snapshot_t old_frozen = snapshot;
    const lab10_ui_scripted_renderer_drain_t new_session_drain = {
        .source_point_count = 900u,
        .source_cursor = 900u,
        .pending_point_count = 0u,
        .frame_idle = true,
    };
    const lab10_ui_scripted_canvas_observation_t new_session_observation =
        diagnostic_observation(0x3eu, 0x3eu, 32u, true);
    assert(lab10_ui_scripted_diagnostic_prepare_delivery(
        &lifecycle, &snapshot, true, UINT64_C(72), true,
        &new_session_drain, &new_session_observation));
    assert(!snapshot.renderer_drain_complete);
    assert(snapshot.canvas_final_color_mask == 0u);
    assert_diagnostic_snapshot_equal(&snapshot, &old_frozen);

    /* Repeated full-mailbox retries must not borrow the active session's masks. */
    lab10_ui_scripted_diagnostic_finish_delivery(&lifecycle, &snapshot, false);
    assert(lifecycle.pending && lifecycle.snapshot_frozen);
    assert(lab10_ui_scripted_diagnostic_prepare_delivery(
        &lifecycle, &snapshot, true, UINT64_C(72), true,
        &new_session_drain, &new_session_observation));
    assert_diagnostic_snapshot_equal(&snapshot, &old_frozen);

    lab10_ui_scripted_diagnostic_finish_delivery(&lifecycle, &snapshot, true);
    assert(!lifecycle.pending && !lifecycle.reported);
    lab10_ui_scripted_diagnostic_allow_next_session(&lifecycle);
    lab10_ui_scripted_diagnostic_snapshot_t next_snapshot = {
        .capture_session_id = UINT64_C(72),
        .accepted_points = 900u,
        .renderer_published_color_mask = 0x3eu,
        .canvas_sampled_color_mask = 0x3eu,
    };
    assert(lab10_ui_scripted_diagnostic_begin(&lifecycle, &next_snapshot));
    assert(next_snapshot.capture_session_id == UINT64_C(72));
    assert(next_snapshot.accepted_points == 900u);
    assert(next_snapshot.renderer_published_color_mask == 0x3eu);
    assert(lifecycle.pending && lifecycle.reported);
}

static void test_completion_count_bounds_and_relationship(void)
{
    assert(LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS == 3400u);
    assert(LAB10_UI_SCRIPTED_MAX_COMPLETION_POINTS == 4095u);
    assert(LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY == 4096u);
    const lab10_ui_scripted_capture_summary_t valid_minimum =
        completion_summary(3400u,
                           LAB10_UI_SCRIPTED_MIN_COMPLETION_STROKES,
                           LAB10_UI_SCRIPTED_MIN_COMPLETION_COLOR_CHANGES,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    const lab10_ui_scripted_capture_summary_t valid_maximum =
        completion_summary(4095u,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_COLOR_CHANGES,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    assert(poll_duration_completion(&valid_minimum) ==
           LAB10_UI_SCRIPTED_POLL_COMPLETE);
    assert(poll_duration_completion(&valid_maximum) ==
           LAB10_UI_SCRIPTED_POLL_COMPLETE);

    const lab10_ui_scripted_capture_summary_t too_many_strokes =
        completion_summary(3600u,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES + 1u,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    const lab10_ui_scripted_capture_summary_t too_many_color_changes =
        completion_summary(3600u,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_STROKES,
                           LAB10_UI_SCRIPTED_MAX_COMPLETION_COLOR_CHANGES + 1u,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    const lab10_ui_scripted_capture_summary_t mismatched_extra_palette =
        completion_summary(3600u, 6u, 6u,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    const lab10_ui_scripted_capture_summary_t mismatched_extra_stroke =
        completion_summary(3600u, 7u, 5u,
                           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    assert(poll_duration_completion(&too_many_strokes) ==
           LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
    assert(poll_duration_completion(&too_many_color_changes) ==
           LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
    assert(poll_duration_completion(&mismatched_extra_palette) ==
           LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
    assert(poll_duration_completion(&mismatched_extra_stroke) ==
           LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
}

static void test_only_a_clean_multicolor_duration_cap_qualifies(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    const uint64_t cap_us = LAB10_CAPTURE_DURATION_CAP_US;
    const lab10_ui_scripted_capture_summary_t complete = completion_summary(
        3600u, LAB10_UI_SCRIPTED_STROKE_COUNT,
        LAB10_UI_SCRIPTED_STROKE_COUNT - 1u,
        LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    lab10_ui_scripted_event_t event;

    lab10_ui_scripted_schedule_t stopped = new_schedule(start_us);
    assert(poll_schedule(&stopped, start_us,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_DOWN);
    assert(poll_schedule(&stopped, start_us + UINT64_C(60000000),
                         LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL,
                         &complete, &event) ==
           LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t early = new_schedule(start_us);
    assert(poll_schedule(&early, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(poll_schedule(&early, start_us + cap_us - 1u,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &complete,
                         &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    const lab10_ui_scripted_capture_summary_t white_only = completion_summary(
        3900u, 1u, 0u, (uint8_t)(1u << LAB10_COLOR_WHITE), true);
    lab10_ui_scripted_schedule_t white = new_schedule(start_us);
    assert(poll_schedule(&white, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(poll_schedule(&white, start_us + cap_us,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &white_only,
                         &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t insufficient = new_schedule(start_us);
    assert(poll_schedule(&insufficient, start_us,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    const lab10_ui_scripted_capture_summary_t too_few = completion_summary(
        3399u,
        LAB10_UI_SCRIPTED_STROKE_COUNT, LAB10_UI_SCRIPTED_STROKE_COUNT - 1u,
        LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    assert(poll_schedule(&insufficient, start_us + cap_us,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &too_few,
                         &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t lost = new_schedule(start_us);
    assert(poll_schedule(&lost, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    const lab10_ui_scripted_capture_summary_t dirty = completion_summary(
        3600u, LAB10_UI_SCRIPTED_STROKE_COUNT,
        LAB10_UI_SCRIPTED_STROKE_COUNT - 1u,
        LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, false);
    assert(poll_schedule(&lost, start_us + cap_us,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &dirty,
                         &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t at_point_cap = new_schedule(start_us);
    assert(poll_schedule(&at_point_cap, start_us,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
                         &(lab10_ui_scripted_capture_summary_t){0}, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    const lab10_ui_scripted_capture_summary_t capped = completion_summary(
        4096u,
        LAB10_UI_SCRIPTED_STROKE_COUNT,
        LAB10_UI_SCRIPTED_STROKE_COUNT - 1u,
        LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK, true);
    assert(poll_schedule(&at_point_cap, start_us + cap_us,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &capped,
                         &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
}

static void test_delayed_tick_never_emits_a_catch_up_burst(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    const lab10_ui_scripted_capture_summary_t active =
        completion_summary(0u, 0u, 0u, 0u, true);
    lab10_ui_scripted_event_t event;
    assert(poll_schedule(&schedule, start_us,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, &active, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_DOWN);

    const uint64_t delayed_now = start_us + UINT64_C(10000000);
    assert(poll_schedule(&schedule, delayed_now,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, &active, &event) ==
           LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_MOVE);
    assert(poll_schedule(&schedule, delayed_now,
                         LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, &active, &event) ==
           LAB10_UI_SCRIPTED_POLL_WAIT);
}

static void test_scripted_multicolor_strokes_render_until_duration_cap(void)
{
    fake_clock_t clock = {.now_us = UINT64_C(1000000)};
    lab10_capture_t *capture = lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = &clock,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);

    touch_context_t touch = {
        .capture = capture,
        .session_id = session,
        .clock = &clock,
    };
    lab10_ui_model_t model;
    lab10_ui_model_init(&model);
    assert(lab10_ui_enter_fab_bloom(&model) == 1u);
    assert(lab10_ui_finish_entry(&model) == 2u);
    lab10_ui_set_hooks(&model, &(lab10_ui_hooks_t){
                                    .on_touch_routed = submit_ui_touch,
                                    .context = &touch,
                                });

    lab10_render_t *renderer = lab10_render_create(&(lab10_render_config_t){
        .mode = LAB10_RENDER_MODE_SEGMENT,
        .role = LAB10_RENDER_ROLE_CANDIDATE,
        .max_points_per_update = 1u,
        .max_raster_ops_per_update = 8u,
        .max_publication_ops_per_update = 8u,
    });
    assert(renderer != NULL);
    uint8_t published_color_mask = 0u;
    assert(lab10_render_set_hooks(
        renderer, &(lab10_render_hooks_t){
                      .on_publish_pixel = record_published_color,
                      .context = &published_color_mask,
                  }));
    assert(lab10_render_begin(renderer, session));

    lab10_ui_scripted_schedule_t schedule = new_schedule(clock.now_us);
    uint32_t down_events = 0u;
    uint32_t move_events = 0u;
    uint32_t up_events = 0u;
    uint32_t palette_events = 0u;
    uint64_t last_point_event_us = 0u;
    uint64_t min_move_interval_us = UINT64_MAX;
    uint64_t max_move_interval_us = 0u;
    uint64_t last_up_us = 0u;
    uint64_t longest_pen_up_gap_us = 0u;
    uint64_t last_scripted_event_us = 0u;
    lab10_ui_scripted_event_t event;

    for (uint64_t elapsed_us = 0u;
         elapsed_us <= LAB10_CAPTURE_DURATION_CAP_US;
         elapsed_us += UINT64_C(20000)) {
        clock.now_us = UINT64_C(1000000) + elapsed_us;
        lab10_capture_stats_t stats;
        assert(lab10_capture_get_stats(capture, session, &stats));
        const lab10_ui_scripted_capture_summary_t summary =
            completion_summary(stats.accepted_point_count, stats.stroke_count,
                               stats.color_change_count,
                               stats.accepted_color_mask,
                               stats.queue_overflow_count == 0u &&
                                   stats.trace_loss_count == 0u);
        const lab10_ui_scripted_capture_state_t capture_state =
            stats.status == LAB10_CAPTURE_STATUS_ACTIVE
                ? LAB10_UI_SCRIPTED_CAPTURE_ACTIVE
                : stats.terminal_outcome ==
                          LAB10_CAPTURE_OUTCOME_DURATION_CAP
                      ? LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP
                      : LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL;
        const lab10_ui_scripted_poll_result_t result = poll_schedule(
            &schedule, clock.now_us, capture_state, &summary, &event);
        if (result == LAB10_UI_SCRIPTED_POLL_EVENT) {
            last_scripted_event_us = clock.now_us;
            assert(lab10_ui_handle_touch(&model, event.phase,
                                         event.display_x, event.display_y,
                                         event.touch_id) ==
                   (event.phase == LAB10_PHASE_PALETTE_CHANGE
                        ? LAB10_ROUTE_PALETTE
                        : LAB10_ROUTE_CANVAS));
            if (event.phase == LAB10_PHASE_DOWN) {
                ++down_events;
                if (last_up_us != 0u) {
                    const uint64_t gap = clock.now_us - last_up_us;
                    if (gap > longest_pen_up_gap_us) {
                        longest_pen_up_gap_us = gap;
                    }
                }
                last_point_event_us = clock.now_us;
            } else if (event.phase == LAB10_PHASE_MOVE) {
                ++move_events;
                const uint64_t interval = clock.now_us - last_point_event_us;
                if (interval < min_move_interval_us) {
                    min_move_interval_us = interval;
                }
                if (interval > max_move_interval_us) {
                    max_move_interval_us = interval;
                }
                last_point_event_us = clock.now_us;
            } else if (event.phase == LAB10_PHASE_UP) {
                ++up_events;
                last_up_us = clock.now_us;
            } else {
                assert(event.phase == LAB10_PHASE_PALETTE_CHANGE);
                ++palette_events;
            }
        } else {
            assert(result == LAB10_UI_SCRIPTED_POLL_WAIT);
        }
        (void)lab10_capture_poll(capture, session);
        assert(lab10_render_update(renderer, capture, session).valid);
        assert(lab10_capture_get_stats(capture, session, &stats));
        if (stats.status == LAB10_CAPTURE_STATUS_TERMINAL) {
            break;
        }
    }

    lab10_capture_stats_t stats;
    assert(lab10_capture_get_stats(capture, session, &stats));
    assert(stats.status == LAB10_CAPTURE_STATUS_TERMINAL);
    assert(stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP);
    assert(stats.terminal_event_count == 1u);
    assert(stats.duration_cap_hit_count == 1u);
    assert(stats.stroke_count == LAB10_UI_SCRIPTED_STROKE_COUNT);
    assert(stats.stroke_boundary_count ==
           LAB10_UI_SCRIPTED_STROKE_COUNT - 1u);
    assert(stats.color_change_count ==
           LAB10_UI_SCRIPTED_STROKE_COUNT - 1u);
    assert(stats.accepted_color_mask ==
           LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK);
    assert(stats.queue_overflow_count == 0u);
    assert(stats.trace_loss_count == 0u);
    assert(stats.accepted_point_count >=
           LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS);
    assert(stats.accepted_point_count < LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY);
    assert(down_events == LAB10_UI_SCRIPTED_STROKE_COUNT);
    assert(up_events == LAB10_UI_SCRIPTED_STROKE_COUNT - 1u);
    assert(palette_events == LAB10_UI_SCRIPTED_STROKE_COUNT - 1u);
    assert(move_events + down_events ==
           LAB10_UI_SCRIPTED_STROKE_COUNT *
               LAB10_UI_SCRIPTED_POINTS_PER_STROKE);
    assert(max_move_interval_us > min_move_interval_us);
    /* A 20 ms UI poll can quantize deadline boundaries to one poll interval. */
    assert(min_move_interval_us >= UINT64_C(20000));
    assert(max_move_interval_us <= LAB10_UI_SCRIPTED_CADENCE_MAX_US +
                                      UINT64_C(20000));
    assert(longest_pen_up_gap_us >=
           LAB10_UI_SCRIPTED_PEN_UP_GAP_US +
               LAB10_UI_SCRIPTED_PAUSE_US);
    assert(last_scripted_event_us <=
           UINT64_C(1000000) + LAB10_CAPTURE_DURATION_CAP_US -
               UINT64_C(10000000));
    assert(lab10_ui_selected_color(&model) == LAB10_COLOR_RED);

    const lab10_ui_scripted_capture_summary_t summary = completion_summary(
        stats.accepted_point_count, stats.stroke_count,
        stats.color_change_count, stats.accepted_color_mask,
        stats.queue_overflow_count == 0u && stats.trace_loss_count == 0u);
    assert(poll_schedule(&schedule,
                         UINT64_C(1000000) + LAB10_CAPTURE_DURATION_CAP_US,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &summary,
                         &event) == LAB10_UI_SCRIPTED_POLL_COMPLETE);
    assert(poll_schedule(&schedule,
                         UINT64_C(1000000) + LAB10_CAPTURE_DURATION_CAP_US,
                         LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, &summary,
                         &event) == LAB10_UI_SCRIPTED_POLL_WAIT);

    lab10_capture_snapshot_t snapshot;
    assert(lab10_capture_snapshot(capture, session, &snapshot));
    assert(snapshot.point_count == stats.accepted_point_count);
    uint8_t accepted_colors = 0u;
    uint8_t stroke_colors[LAB10_UI_SCRIPTED_STROKE_COUNT + 1u] = {0};
    uint32_t stroke_points[LAB10_UI_SCRIPTED_STROKE_COUNT + 1u] = {0};
    bool edge_adjacent_point_seen = false;
    for (size_t index = 0u; index < snapshot.point_count; ++index) {
        const lab10_capture_point_t *point = &snapshot.points[index];
        assert(point->stroke_id > 0u &&
               point->stroke_id <= LAB10_UI_SCRIPTED_STROKE_COUNT);
        assert(point->color_index > LAB10_COLOR_BACKGROUND &&
               point->color_index <= LAB10_COLOR_BLUE);
        accepted_colors |= (uint8_t)(1u << point->color_index);
        if (stroke_points[point->stroke_id] == 0u) {
            assert(point->phase == LAB10_PHASE_DOWN);
            stroke_colors[point->stroke_id] = point->color_index;
        } else {
            assert(point->phase == LAB10_PHASE_MOVE);
            assert(point->color_index == stroke_colors[point->stroke_id]);
        }
        ++stroke_points[point->stroke_id];
        if (point->display_x <= 1 || point->display_y <= 1 ||
            point->display_x >= LAB10_CANVAS_WIDTH - 2 ||
            point->display_y >= LAB10_CANVAS_HEIGHT - 2) {
            edge_adjacent_point_seen = true;
        }
    }
    assert(accepted_colors == LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK);
    assert(edge_adjacent_point_seen);
    for (uint32_t stroke = 1u; stroke <= LAB10_UI_SCRIPTED_STROKE_COUNT;
         ++stroke) {
        assert(stroke_points[stroke] >= 180u);
        assert(stroke_points[stroke] <= LAB10_UI_SCRIPTED_POINTS_PER_STROKE);
    }

    for (unsigned attempt = 0u; attempt < 5000u; ++attempt) {
        lab10_render_metrics_t metrics;
        assert(lab10_render_get_metrics(renderer, session, &metrics));
        if (metrics.pending_point_count == 0u) {
            break;
        }
        assert(lab10_render_update(renderer, capture, session).valid);
        assert(attempt + 1u < 5000u);
    }
    lab10_render_metrics_t render_metrics;
    assert(lab10_render_get_metrics(renderer, session, &render_metrics));
    assert(render_metrics.pending_point_count == 0u);
    assert(published_color_mask == LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK);

    size_t rendered_pixel_count = 0u;
    const uint8_t *rendered_pixels =
        lab10_render_pixels(renderer, session, &rendered_pixel_count);
    assert(rendered_pixels != NULL);
    assert(rendered_pixel_count == LAB10_RENDER_LOGICAL_PIXEL_COUNT);
    uint8_t final_color_mask = 0u;
    for (size_t index = 0u; index < rendered_pixel_count; ++index) {
        const uint8_t color = rendered_pixels[index];
        if (color > LAB10_COLOR_BACKGROUND && color < LAB10_SWATCH_COUNT) {
            final_color_mask |= (uint8_t)(1u << color);
        }
    }
    printf("LAB10_SCRIPTED_RENDERER_FINAL canvas_final_color_mask=0x%02x\n",
           (unsigned)final_color_mask);
    assert(final_color_mask == LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK);

    uint8_t raster_colors = 0u;
    for (size_t index = 0u; index < snapshot.point_count; ++index) {
        const lab10_capture_point_t *point = &snapshot.points[index];
        uint8_t color = LAB10_COLOR_BACKGROUND;
        assert(lab10_render_pixel_at(renderer, session, point->logical_x,
                                     point->logical_y, &color));
        if (color == point->color_index) {
            raster_colors |= (uint8_t)(1u << color);
        }
    }
    assert((raster_colors & LAB10_UI_SCRIPTED_REQUIRED_COLOR_MASK) != 0u);
    assert((raster_colors & (uint8_t)(raster_colors - 1u)) != 0u);

    lab10_render_destroy(renderer);
    lab10_capture_destroy(capture);
}

int main(void)
{
    test_terminal_presentation_waits_for_scripted_qualification();
    test_terminal_failure_and_manual_duration_mappings();
    test_malformed_duration_cap_cannot_qualify();
    test_scripted_scheduler_timing_telemetry();
    test_scripted_diagnostic_waits_for_drain_and_retries_frozen_payload();
    test_scripted_diagnostic_rollover_keeps_old_masks_and_releases_latch();
    test_completion_count_bounds_and_relationship();
    test_only_a_clean_multicolor_duration_cap_qualifies();
    test_delayed_tick_never_emits_a_catch_up_burst();
    test_scripted_multicolor_strokes_render_until_duration_cap();
    puts("LAB10_SCRIPTED_NATIVE_TEST_PASS");
    return 0;
}
