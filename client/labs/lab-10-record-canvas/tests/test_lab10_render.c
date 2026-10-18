#include "lab10_render.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t now_us;
} fake_clock_t;

typedef struct {
    size_t raster_callbacks;
    size_t publication_callbacks;
    size_t invalidation_callbacks;
    size_t update_callbacks;
    size_t lock_callbacks;
    uint16_t max_raster;
    uint16_t max_publication;
    uint16_t max_points;
    uint8_t saw_white;
    uint8_t saw_red;
    uint8_t saw_blue;
    uint8_t saw_background;
} render_probe_t;

static uint64_t fake_now(void *context)
{
    return ((fake_clock_t *)context)->now_us;
}

static void on_raster(const lab10_render_pixel_event_t *event, void *context)
{
    render_probe_t *probe = context;
    assert(event != NULL);
    ++probe->raster_callbacks;
}

static void on_publish(const lab10_render_pixel_event_t *event, void *context)
{
    render_probe_t *probe = context;
    assert(event != NULL);
    ++probe->publication_callbacks;
    if (event->color_index == LAB10_COLOR_WHITE) {
        probe->saw_white = 1;
    } else if (event->color_index == LAB10_COLOR_RED) {
        probe->saw_red = 1;
    } else if (event->color_index == LAB10_COLOR_BLUE) {
        probe->saw_blue = 1;
    } else if (event->color_index == LAB10_COLOR_BACKGROUND) {
        probe->saw_background = 1;
    }
}

static void on_invalidation(const lab10_render_invalidation_t *invalidation,
                            void *context)
{
    render_probe_t *probe = context;
    assert(invalidation != NULL);
    assert(invalidation->min_x <= invalidation->max_x);
    assert(invalidation->min_y <= invalidation->max_y);
    ++probe->invalidation_callbacks;
}

static void on_update(const lab10_render_update_result_t *result, void *context)
{
    render_probe_t *probe = context;
    assert(result != NULL && result->valid);
    assert(result->raster_ops <= result->raster_budget);
    assert(result->publication_ops <= result->publication_budget);
    assert(result->points_processed <= result->point_budget);
    if (result->raster_ops > probe->max_raster) {
        probe->max_raster = result->raster_ops;
    }
    if (result->publication_ops > probe->max_publication) {
        probe->max_publication = result->publication_ops;
    }
    if (result->points_processed > probe->max_points) {
        probe->max_points = (uint16_t)result->points_processed;
    }
    ++probe->update_callbacks;
}

static void on_lock(lab10_render_lock_state_t state, void *context)
{
    render_probe_t *probe = context;
    assert(state == LAB10_RENDER_LOCK_UI_TASK_OWNED);
    ++probe->lock_callbacks;
}

static lab10_capture_t *new_capture(fake_clock_t *clock)
{
    return lab10_capture_create(&(lab10_capture_config_t){
        .clock_now_us = fake_now,
        .clock_context = clock,
        .initial_color_index = LAB10_COLOR_WHITE,
    });
}

static void submit_and_poll(lab10_capture_t *capture,
                            lab10_capture_session_id_t session_id,
                            fake_clock_t *clock, lab10_touch_phase_t phase,
                            lab10_touch_route_t route, int16_t x, int16_t y,
                            uint8_t color)
{
    ++clock->now_us;
    assert(lab10_capture_submit(
               capture, session_id,
               &(lab10_capture_raw_report_t){
                   .timestamp_us = clock->now_us,
                   .display_x = x,
                   .display_y = y,
                   .touch_id = 0,
                   .phase = phase,
                   .route = route,
                   .color_index = color,
               }) == LAB10_CAPTURE_SUBMIT_ENQUEUED);
    assert(lab10_capture_poll(capture, session_id) == 1);
}

static lab10_capture_session_id_t populate_capture(lab10_capture_t *capture,
                                                   fake_clock_t *clock)
{
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);

    submit_and_poll(capture, session, clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 10, 10, LAB10_COLOR_WHITE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_MOVE,
                    LAB10_ROUTE_CANVAS, 20, 10, LAB10_COLOR_WHITE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_MOVE,
                    LAB10_ROUTE_CANVAS, 22, 10, LAB10_COLOR_WHITE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_UP,
                    LAB10_ROUTE_CANVAS, 22, 10, LAB10_COLOR_WHITE);

    submit_and_poll(capture, session, clock, LAB10_PHASE_PALETTE_CHANGE,
                    LAB10_ROUTE_PALETTE, 269, 64, LAB10_COLOR_RED);
    submit_and_poll(capture, session, clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 40, 40, LAB10_COLOR_RED);
    submit_and_poll(capture, session, clock, LAB10_PHASE_MOVE,
                    LAB10_ROUTE_CANVAS, 42, 42, LAB10_COLOR_RED);
    submit_and_poll(capture, session, clock, LAB10_PHASE_UP,
                    LAB10_ROUTE_CANVAS, 42, 42, LAB10_COLOR_RED);

    submit_and_poll(capture, session, clock, LAB10_PHASE_PALETTE_CHANGE,
                    LAB10_ROUTE_PALETTE, 291, 28, LAB10_COLOR_BLUE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 80, 80, LAB10_COLOR_BLUE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_UP,
                    LAB10_ROUTE_CANVAS, 80, 80, LAB10_COLOR_BLUE);
    submit_and_poll(capture, session, clock, LAB10_PHASE_PALETTE_CHANGE,
                    LAB10_ROUTE_PALETTE, 269, 208, LAB10_COLOR_BACKGROUND);
    submit_and_poll(capture, session, clock, LAB10_PHASE_DOWN,
                    LAB10_ROUTE_CANVAS, 100, 100, LAB10_COLOR_BACKGROUND);
    submit_and_poll(capture, session, clock, LAB10_PHASE_UP,
                    LAB10_ROUTE_CANVAS, 100, 100, LAB10_COLOR_BACKGROUND);
    assert(lab10_capture_complete(capture, session));
    return session;
}

static lab10_render_t *new_renderer(lab10_render_mode_t mode,
                                    lab10_render_role_t role,
                                    uint16_t point_budget,
                                    uint16_t raster_budget,
                                    uint16_t publication_budget,
                                    render_probe_t *probe)
{
    lab10_render_t *renderer = lab10_render_create(&(lab10_render_config_t){
        .mode = mode,
        .role = role,
        .max_points_per_update = point_budget,
        .max_raster_ops_per_update = raster_budget,
        .max_publication_ops_per_update = publication_budget,
    });
    assert(renderer != NULL);
    assert(lab10_render_set_hooks(
        renderer,
        &(lab10_render_hooks_t){
            .on_raster_pixel = on_raster,
            .on_publish_pixel = on_publish,
            .on_invalidation = on_invalidation,
            .on_update = on_update,
            .on_lock_state = on_lock,
            .context = probe,
        }));
    return renderer;
}

static void drain_renderer(lab10_render_t *renderer,
                           lab10_capture_t *capture,
                           lab10_capture_session_id_t session,
                           bool whole_frame)
{
    bool done = false;
    for (unsigned attempt = 0; attempt < 5000; ++attempt) {
        const lab10_render_update_result_t result =
            lab10_render_update(renderer, capture, session);
        assert(result.valid);
        lab10_render_metrics_t metrics;
        assert(lab10_render_get_metrics(renderer, session, &metrics));
        if (whole_frame) {
            done = metrics.frame_completed_count > 0 &&
                   metrics.frame_phase == LAB10_RENDER_FRAME_IDLE &&
                   metrics.pending_point_count == 0;
        } else {
            done = metrics.pending_point_count == 0;
        }
        if (done) {
            return;
        }
    }
    assert(!"renderer did not drain within bounded test iterations");
}

static void assert_common_visible_pixels(const lab10_render_t *renderer,
                                         lab10_capture_session_id_t session)
{
    uint8_t color = 0;
    assert(lab10_render_pixel_at(renderer, session, 5, 5, &color));
    assert(color == LAB10_COLOR_WHITE);
    assert(lab10_render_pixel_at(renderer, session, 20, 20, &color));
    assert(color == LAB10_COLOR_RED);
    assert(lab10_render_pixel_at(renderer, session, 40, 40, &color));
    assert(color == LAB10_COLOR_BLUE);
}

static void test_modes_and_equivalence(void)
{
    const lab10_render_mode_t modes[] = {
        LAB10_RENDER_MODE_DIRECT_POINT,
        LAB10_RENDER_MODE_SEGMENT,
        LAB10_RENDER_MODE_BOUNDED_BATCH,
        LAB10_RENDER_MODE_WHOLE_FRAME,
    };
    uint8_t expected_segment[LAB10_RENDER_LOGICAL_PIXEL_COUNT];
    bool have_segment = false;

    for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        fake_clock_t clock = {.now_us = 1000};
        lab10_capture_t *capture = new_capture(&clock);
        assert(capture != NULL);
        const lab10_capture_session_id_t session =
            populate_capture(capture, &clock);
        render_probe_t probe = {0};
        const bool whole_frame = modes[index] == LAB10_RENDER_MODE_WHOLE_FRAME;
        lab10_render_t *renderer = new_renderer(
            modes[index],
            modes[index] == LAB10_RENDER_MODE_DIRECT_POINT
                ? LAB10_RENDER_ROLE_CONTROL
                : LAB10_RENDER_ROLE_CANDIDATE,
            modes[index] == LAB10_RENDER_MODE_BOUNDED_BATCH ? 3 : 1,
            modes[index] == LAB10_RENDER_MODE_WHOLE_FRAME ? 7 : 4,
            modes[index] == LAB10_RENDER_MODE_WHOLE_FRAME ? 5 : 4, &probe);
        assert(lab10_render_begin(renderer, session));
        assert(lab10_render_note_lock_state(
            renderer, session, LAB10_RENDER_LOCK_UI_TASK_OWNED));
        drain_renderer(renderer, capture, session, whole_frame);
        assert_common_visible_pixels(renderer, session);
        lab10_render_metrics_t metrics;
        assert(lab10_render_get_metrics(renderer, session, &metrics));
        assert(metrics.raster_ops_high_water <= metrics.effective_raster_budget);
        assert(metrics.publication_ops_high_water <=
               metrics.configured_publication_budget);
        assert(metrics.invalidation_count == probe.invalidation_callbacks);
        assert(probe.lock_callbacks == 1);
        assert(probe.saw_white && probe.saw_red && probe.saw_blue);
        assert(probe.saw_background);

        if (modes[index] == LAB10_RENDER_MODE_DIRECT_POINT) {
            uint8_t direct_color = 0;
            assert(lab10_render_pixel_at(renderer, session, 10, 5,
                                         &direct_color));
            assert(direct_color == LAB10_COLOR_WHITE);
        } else if (!have_segment) {
            const uint8_t *pixels =
                lab10_render_pixels(renderer, session, NULL);
            assert(pixels != NULL);
            memcpy(expected_segment, pixels, sizeof(expected_segment));
            have_segment = true;
        } else {
            const uint8_t *pixels =
                lab10_render_pixels(renderer, session, NULL);
            assert(pixels != NULL);
            assert(memcmp(expected_segment, pixels, sizeof(expected_segment)) ==
                   0);
        }
        lab10_render_destroy(renderer);
        lab10_capture_destroy(capture);
    }
}

static void test_backlog_invalidation_and_cleanup(void)
{
    fake_clock_t clock = {.now_us = 1000};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = populate_capture(capture, &clock);
    render_probe_t probe = {0};
    lab10_render_t *renderer =
        new_renderer(LAB10_RENDER_MODE_BOUNDED_BATCH,
                     LAB10_RENDER_ROLE_CANDIDATE, 2, 2, 2, &probe);
    assert(lab10_render_begin(renderer, session));
    const lab10_render_update_result_t first =
        lab10_render_update(renderer, capture, session);
    assert(first.valid && first.pending_point_count > 0);
    drain_renderer(renderer, capture, session, false);
    lab10_render_metrics_t metrics;
    assert(lab10_render_get_metrics(renderer, session, &metrics));
    assert(metrics.backlog_high_water > 0);
    assert(metrics.invalidation_count > 0);
    assert(probe.raster_callbacks >= probe.publication_callbacks);
    assert(probe.max_raster <= 2 && probe.max_publication <= 2);

    assert(!lab10_render_update(renderer, capture, session + 1).valid);
    assert(!lab10_render_get_metrics(renderer, session + 1, &metrics));
    assert(lab10_render_reset(renderer, session));
    assert(!lab10_render_reset(renderer, session));
    lab10_render_destroy(renderer);
    lab10_capture_destroy(capture);
}

static void populate_large_whole_frame_capture(lab10_capture_t *capture,
                                               fake_clock_t *clock,
                                               lab10_capture_session_id_t session)
{
    enum { POINT_COUNT = 512, POINTS_PER_COLOR = 128 };
    const uint8_t colors[] = {
        LAB10_COLOR_WHITE,
        LAB10_COLOR_RED,
        LAB10_COLOR_BLUE,
        LAB10_COLOR_BACKGROUND,
    };

    for (size_t index = 0; index < POINT_COUNT; ++index) {
        if (index > 0 && index % POINTS_PER_COLOR == 0) {
            const uint8_t color = colors[index / POINTS_PER_COLOR];
            const lab10_swatch_geometry_t *swatch =
                lab10_swatch_geometry(color);
            assert(swatch != NULL);
            submit_and_poll(capture, session, clock,
                            LAB10_PHASE_PALETTE_CHANGE, LAB10_ROUTE_PALETTE,
                            swatch->center_x, swatch->center_y, color);
        }

        const size_t slot = index < 3 * POINTS_PER_COLOR
                                ? index
                                : (index - 3 * POINTS_PER_COLOR) % 64;
        const int16_t display_x = (int16_t)((slot % 60) * 4);
        const int16_t display_y = (int16_t)((slot / 60) * 4);
        const uint8_t color = colors[index / POINTS_PER_COLOR];
        submit_and_poll(capture, session, clock, LAB10_PHASE_DOWN,
                        LAB10_ROUTE_CANVAS, display_x, display_y, color);
        submit_and_poll(capture, session, clock, LAB10_PHASE_UP,
                        LAB10_ROUTE_CANVAS, display_x, display_y, color);
    }

    assert(lab10_capture_complete(capture, session));
}

static void test_whole_frame_point_budget_and_output(void)
{
    fake_clock_t clock = {.now_us = 2000};
    lab10_capture_t *capture = new_capture(&clock);
    assert(capture != NULL);
    const lab10_capture_session_id_t session = lab10_capture_start(capture);
    assert(session != LAB10_CAPTURE_INVALID_SESSION_ID);
    populate_large_whole_frame_capture(capture, &clock, session);

    lab10_capture_snapshot_t snapshot;
    assert(lab10_capture_snapshot(capture, session, &snapshot));
    assert(snapshot.point_count == 512);
    for (size_t index = 0; index < snapshot.point_count; ++index) {
        assert(snapshot.points[index].boundary ==
               LAB10_CAPTURE_POINT_BOUNDARY_START);
        if (index > 0) {
            assert(snapshot.points[index].stroke_id !=
                   snapshot.points[index - 1].stroke_id);
        }
    }

    uint8_t expected[LAB10_RENDER_LOGICAL_PIXEL_COUNT];
    memset(expected, LAB10_COLOR_BACKGROUND, sizeof(expected));
    for (size_t index = 0; index < snapshot.point_count; ++index) {
        const lab10_capture_point_t *point = &snapshot.points[index];
        expected[(size_t)point->logical_y * LAB10_LOGICAL_WIDTH +
                 point->logical_x] = point->color_index;
    }

    render_probe_t probe = {0};
    lab10_render_t *renderer = new_renderer(
        LAB10_RENDER_MODE_WHOLE_FRAME, LAB10_RENDER_ROLE_CANDIDATE, 1, 512,
        512, &probe);
    assert(lab10_render_begin(renderer, session));
    assert(lab10_render_note_lock_state(
        renderer, session, LAB10_RENDER_LOCK_UI_TASK_OWNED));

    bool saw_clear = false;
    bool saw_replay = false;
    bool saw_publish = false;
    bool saw_staged_replay = false;
    bool completed = false;
    for (unsigned attempt = 0; attempt < 5000; ++attempt) {
        const lab10_render_update_result_t result =
            lab10_render_update(renderer, capture, session);
        assert(result.valid);
        assert(result.mode == LAB10_RENDER_MODE_WHOLE_FRAME);
        assert(result.point_budget == 1);
        assert(result.points_processed <= result.point_budget);
        assert(result.raster_ops <= 512);
        assert(result.publication_ops <= 512);
        if (result.frame_phase == LAB10_RENDER_FRAME_CLEAR) {
            saw_clear = true;
        } else if (result.frame_phase == LAB10_RENDER_FRAME_REPLAY) {
            saw_replay = true;
            uint8_t color = 0;
            assert(lab10_render_pixel_at(renderer, session, 80, 2, &color));
            assert(color == LAB10_COLOR_BACKGROUND);
            saw_staged_replay = true;
        } else if (result.frame_phase == LAB10_RENDER_FRAME_PUBLISH) {
            saw_publish = true;
        }
        if (result.frame_complete) {
            completed = true;
            break;
        }
    }
    assert(completed);
    assert(saw_clear && saw_replay && saw_publish && saw_staged_replay);
    assert(probe.max_points <= 1);
    assert(probe.max_raster <= 512);
    assert(probe.max_publication <= 512);

    lab10_render_metrics_t metrics;
    assert(lab10_render_get_metrics(renderer, session, &metrics));
    assert(metrics.frame_completed_count == 1);
    assert(metrics.frame_phase == LAB10_RENDER_FRAME_IDLE);
    assert(metrics.pending_point_count == 0);
    assert(metrics.configured_point_budget == 1);
    assert(metrics.raster_ops_high_water <= 512);
    assert(metrics.publication_ops_high_water <= 512);
    const uint8_t *pixels = lab10_render_pixels(renderer, session, NULL);
    assert(pixels != NULL);
    assert(memcmp(expected, pixels, sizeof(expected)) == 0);

    lab10_render_destroy(renderer);
    lab10_capture_destroy(capture);
}

static unsigned failing_allocations;

static void *fail_alloc(size_t size, void *context)
{
    (void)size;
    (void)context;
    ++failing_allocations;
    return NULL;
}

static void fail_free(void *memory, void *context)
{
    (void)memory;
    (void)context;
}

static void test_error_paths(void)
{
    assert(lab10_render_create(&(lab10_render_config_t){
               .max_raster_ops_per_update = 513,
           }) == NULL);
    assert(lab10_render_create(&(lab10_render_config_t){
               .max_publication_ops_per_update = 513,
           }) == NULL);
    assert(lab10_render_create(&(lab10_render_config_t){
               .max_points_per_update = 65,
           }) == NULL);
    failing_allocations = 0;
    assert(lab10_render_create(&(lab10_render_config_t){
               .alloc = fail_alloc,
               .free = fail_free,
           }) == NULL);
    assert(failing_allocations == 1);
}

int main(void)
{
    test_modes_and_equivalence();
    test_backlog_invalidation_and_cleanup();
    test_whole_frame_point_budget_and_output();
    test_error_paths();
    puts("LAB10_RENDER_NATIVE_TEST_PASS");
    return 0;
}
