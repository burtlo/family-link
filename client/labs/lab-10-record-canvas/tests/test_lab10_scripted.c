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
    assert(event->route == LAB10_ROUTE_CANVAS);
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

static lab10_ui_scripted_schedule_t new_schedule(uint64_t start_us)
{
    lab10_ui_scripted_schedule_t schedule;
    assert(lab10_ui_scripted_schedule_init(
        &schedule, start_us, LAB10_CAPTURE_DURATION_CAP_US,
        LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
        LAB10_UI_SCRIPTED_MOVE_INTERVAL_US));
    return schedule;
}

static void test_early_terminal_and_point_cap_never_complete(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_event_t event;

    lab10_ui_scripted_schedule_t stopped = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &stopped, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_DOWN);
    assert(lab10_ui_scripted_schedule_poll(
               &stopped, start_us + UINT64_C(60000000),
               LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL, 1000,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
    assert(lab10_ui_scripted_schedule_poll(
               &stopped, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, 4096,
               &event) == LAB10_UI_SCRIPTED_POLL_WAIT);

    lab10_ui_scripted_schedule_t early_duration = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &early_duration, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &early_duration, start_us + LAB10_CAPTURE_DURATION_CAP_US - 1u,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, 4095,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t capped = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &capped, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &capped, start_us + LAB10_UI_SCRIPTED_MOVE_INTERVAL_US,
               LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
               LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY - 1u,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_MOVE);
    assert(lab10_ui_scripted_schedule_poll(
               &capped, start_us + LAB10_UI_SCRIPTED_MOVE_INTERVAL_US + 1u,
               LAB10_UI_SCRIPTED_CAPTURE_ACTIVE,
               LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t insufficient = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &insufficient, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &insufficient, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS - 1u,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);

    lab10_ui_scripted_schedule_t point_cap = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &point_cap, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &point_cap, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
}

static void test_completion_boundary_excludes_point_cap(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_event_t event;

    lab10_ui_scripted_schedule_t below_cap = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &below_cap, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &below_cap, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY - 1u,
               &event) == LAB10_UI_SCRIPTED_POLL_COMPLETE);

    lab10_ui_scripted_schedule_t at_cap = new_schedule(start_us);
    assert(lab10_ui_scripted_schedule_poll(
               &at_cap, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &at_cap, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY,
               &event) == LAB10_UI_SCRIPTED_POLL_INTERRUPTED);
}

static void test_inclusive_lower_bound_meets_completion_minimum(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    lab10_ui_scripted_event_t event;
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, start_us + LAB10_CAPTURE_DURATION_CAP_US,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP, 3500,
               &event) == LAB10_UI_SCRIPTED_POLL_COMPLETE);
}

static void test_delayed_ui_tick_skips_missed_slots_without_burst(void)
{
    const uint64_t start_us = UINT64_C(1000000);
    lab10_ui_scripted_schedule_t schedule = new_schedule(start_us);
    lab10_ui_scripted_event_t event;
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, start_us, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 0,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_DOWN);

    const uint64_t delayed_now = start_us + UINT64_C(10000000);
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, delayed_now, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 1,
               &event) == LAB10_UI_SCRIPTED_POLL_EVENT);
    assert(event.phase == LAB10_PHASE_MOVE);
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, delayed_now, LAB10_UI_SCRIPTED_CAPTURE_ACTIVE, 2,
               &event) == LAB10_UI_SCRIPTED_POLL_WAIT);
}

static void test_long_active_scripted_stroke_reaches_duration_cap(void)
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
    assert(lab10_render_begin(renderer, session));

    lab10_ui_scripted_schedule_t schedule = new_schedule(clock.now_us);
    uint32_t down_events = 0u;
    uint32_t move_events = 0u;
    lab10_ui_scripted_event_t event;
    for (uint64_t elapsed_us = 0u;
         elapsed_us <= LAB10_CAPTURE_DURATION_CAP_US;
         elapsed_us += UINT64_C(20000)) {
        clock.now_us = UINT64_C(1000000) + elapsed_us;
        lab10_capture_stats_t stats;
        assert(lab10_capture_get_stats(capture, session, &stats));
        const lab10_ui_scripted_capture_state_t capture_state =
            stats.status == LAB10_CAPTURE_STATUS_ACTIVE
                ? LAB10_UI_SCRIPTED_CAPTURE_ACTIVE
                : stats.terminal_outcome == LAB10_CAPTURE_OUTCOME_DURATION_CAP
                      ? LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP
                      : LAB10_UI_SCRIPTED_CAPTURE_OTHER_TERMINAL;
        const lab10_ui_scripted_poll_result_t poll_result =
            lab10_ui_scripted_schedule_poll(&schedule, clock.now_us,
                                            capture_state,
                                            stats.accepted_point_count, &event);
        if (poll_result == LAB10_UI_SCRIPTED_POLL_EVENT) {
            if (event.phase == LAB10_PHASE_DOWN) {
                ++down_events;
            } else {
                assert(event.phase == LAB10_PHASE_MOVE);
                ++move_events;
            }
            assert(lab10_canvas_contains(event.display_x, event.display_y));
            assert(lab10_ui_handle_touch(&model, event.phase,
                                         event.display_x, event.display_y,
                                         event.touch_id) == LAB10_ROUTE_CANVAS);
        } else {
            assert(poll_result == LAB10_UI_SCRIPTED_POLL_WAIT);
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
    assert(stats.stroke_count == 1u);
    assert(stats.stroke_boundary_count == 0u);
    assert(stats.color_change_count == 0u);
    assert(stats.queue_overflow_count == 0u);
    assert(stats.trace_loss_count == 0u);
    assert(stats.accepted_point_count >=
           LAB10_UI_SCRIPTED_MIN_COMPLETION_POINTS);
    const uint64_t max_scripted_moves =
        (LAB10_CAPTURE_DURATION_CAP_US - 1u) /
        LAB10_UI_SCRIPTED_MOVE_INTERVAL_US;
    assert(stats.accepted_point_count <= max_scripted_moves + 1u);
    assert(stats.accepted_point_count == 4091u);
    assert(stats.accepted_point_count < LAB10_CAPTURE_ACCEPTED_POINT_CAPACITY);
    assert(down_events == 1u);
    assert(move_events == stats.accepted_point_count - 1u);
    assert(lab10_ui_selected_color(&model) == LAB10_COLOR_WHITE);

    assert(lab10_ui_scripted_schedule_poll(
               &schedule, clock.now_us,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               stats.accepted_point_count, &event) ==
           LAB10_UI_SCRIPTED_POLL_COMPLETE);
    assert(lab10_ui_scripted_schedule_poll(
               &schedule, clock.now_us,
               LAB10_UI_SCRIPTED_CAPTURE_DURATION_CAP,
               stats.accepted_point_count, &event) ==
           LAB10_UI_SCRIPTED_POLL_WAIT);

    lab10_capture_snapshot_t snapshot;
    assert(lab10_capture_snapshot(capture, session, &snapshot));
    assert(snapshot.point_count == stats.accepted_point_count);
    for (size_t index = 0u; index < snapshot.point_count; ++index) {
        const lab10_capture_point_t *point = &snapshot.points[index];
        assert(point->phase == (index == 0u ? LAB10_PHASE_DOWN
                                            : LAB10_PHASE_MOVE));
        assert(point->stroke_id == 1u);
        assert(point->color_index == LAB10_COLOR_WHITE);
    }

    uint8_t color = LAB10_COLOR_BACKGROUND;
    assert(lab10_render_pixel_at(renderer, session, 10u, 8u, &color));
    assert(color == LAB10_COLOR_WHITE);

    lab10_render_destroy(renderer);
    lab10_capture_destroy(capture);
}

int main(void)
{
    test_early_terminal_and_point_cap_never_complete();
    test_completion_boundary_excludes_point_cap();
    test_inclusive_lower_bound_meets_completion_minimum();
    test_delayed_ui_tick_skips_missed_slots_without_burst();
    test_long_active_scripted_stroke_reaches_duration_cap();
    puts("LAB10_SCRIPTED_NATIVE_TEST_PASS");
    return 0;
}
