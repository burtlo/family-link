#include "lab10_render.h"

#include <stdlib.h>
#include <string.h>

enum {
    LAB10_RENDER_JOB_NONE = 0,
    LAB10_RENDER_JOB_LINE,
};

typedef struct {
    int x;
    int y;
    int x1;
    int y1;
    int dx;
    int sx;
    int dy;
    int sy;
    int err;
    uint8_t color_index;
    uint32_t stroke_id;
    bool active;
} lab10_render_line_job_t;

struct lab10_render {
    lab10_render_config_t config;
    lab10_render_hooks_t hooks;
    uint8_t *pixels;
    uint8_t *work_pixels;
    lab10_capture_session_id_t session_id;
    bool initialized;
    bool frame_initialized;
    bool pending_point_valid;
    lab10_capture_point_t pending_point;
    lab10_render_line_job_t job;
    lab10_capture_point_t previous_point;
    bool has_previous_point;
    size_t source_cursor;
    size_t target_point_count;
    size_t frame_source_cursor;
    size_t frame_clear_cursor;
    size_t frame_publication_cursor;
    lab10_render_frame_phase_t frame_phase;
    lab10_render_metrics_t metrics;
};

static void *default_alloc(size_t size, void *context)
{
    (void)context;
    return malloc(size);
}

static void default_free(void *memory, void *context)
{
    (void)context;
    free(memory);
}

static bool mode_valid(lab10_render_mode_t mode)
{
    return mode <= LAB10_RENDER_MODE_WHOLE_FRAME;
}

static bool role_valid(lab10_render_role_t role)
{
    return role <= LAB10_RENDER_ROLE_CANDIDATE;
}

static bool session_valid(const lab10_render_t *renderer,
                          lab10_capture_session_id_t session_id)
{
    return renderer != NULL && renderer->initialized &&
           session_id != LAB10_CAPTURE_INVALID_SESSION_ID &&
           renderer->session_id == session_id;
}

static uint16_t effective_raster_budget(const lab10_render_t *renderer)
{
    if (renderer->config.mode == LAB10_RENDER_MODE_WHOLE_FRAME) {
        return renderer->config.max_raster_ops_per_update;
    }
    return renderer->config.max_raster_ops_per_update <
                   renderer->config.max_publication_ops_per_update
               ? renderer->config.max_raster_ops_per_update
               : renderer->config.max_publication_ops_per_update;
}

static uint16_t effective_point_budget(const lab10_render_t *renderer)
{
    if (renderer->config.mode == LAB10_RENDER_MODE_DIRECT_POINT ||
        renderer->config.mode == LAB10_RENDER_MODE_SEGMENT) {
        return 1;
    }
    return renderer->config.max_points_per_update;
}

static void clear_pixels(uint8_t *pixels)
{
    if (pixels != NULL) {
        memset(pixels, LAB10_COLOR_BACKGROUND,
               LAB10_RENDER_LOGICAL_PIXEL_COUNT * sizeof(*pixels));
    }
}

static void reset_pipeline(lab10_render_t *renderer)
{
    renderer->pending_point_valid = false;
    renderer->pending_point = (lab10_capture_point_t){0};
    renderer->job = (lab10_render_line_job_t){0};
    renderer->has_previous_point = false;
    renderer->previous_point = (lab10_capture_point_t){0};
    renderer->source_cursor = 0;
    renderer->target_point_count = 0;
    renderer->frame_source_cursor = 0;
    renderer->frame_clear_cursor = 0;
    renderer->frame_publication_cursor = 0;
    renderer->frame_initialized = false;
    renderer->frame_phase = LAB10_RENDER_FRAME_IDLE;
    clear_pixels(renderer->pixels);
    clear_pixels(renderer->work_pixels);
}

static void reset_metrics(lab10_render_t *renderer)
{
    const lab10_render_mode_t mode = renderer->config.mode;
    const lab10_render_role_t role = renderer->config.role;
    const lab10_render_lock_state_t lock_state = renderer->metrics.lock_state;
    renderer->metrics = (lab10_render_metrics_t){
        .mode = mode,
        .role = role,
        .frame_phase = LAB10_RENDER_FRAME_IDLE,
        .lock_state = lock_state,
        .configured_raster_budget =
            renderer->config.max_raster_ops_per_update,
        .configured_publication_budget =
            renderer->config.max_publication_ops_per_update,
        .configured_point_budget = effective_point_budget(renderer),
        .effective_raster_budget = effective_raster_budget(renderer),
        .live_framebuffer_bytes =
            LAB10_RENDER_LOGICAL_PIXEL_COUNT * sizeof(*renderer->pixels) * 2u,
    };
}

static void mark_dirty(uint8_t x, uint8_t y, uint8_t *min_x, uint8_t *min_y,
                       uint8_t *max_x, uint8_t *max_y, bool *has_dirty)
{
    if (!*has_dirty) {
        *min_x = x;
        *max_x = x;
        *min_y = y;
        *max_y = y;
        *has_dirty = true;
        return;
    }
    if (x < *min_x) {
        *min_x = x;
    }
    if (x > *max_x) {
        *max_x = x;
    }
    if (y < *min_y) {
        *min_y = y;
    }
    if (y > *max_y) {
        *max_y = y;
    }
}

static void emit_raster(lab10_render_t *renderer, uint8_t x, uint8_t y,
                        uint8_t color_index, uint32_t stroke_id,
                        bool visible)
{
    lab10_render_pixel_event_t event = {
        .logical_x = x,
        .logical_y = y,
        .color_index = color_index,
        .stroke_id = stroke_id,
        .frame_generation = renderer->metrics.frame_generation,
        .visible = visible,
    };
    if (renderer->hooks.on_raster_pixel != NULL) {
        renderer->hooks.on_raster_pixel(&event, renderer->hooks.context);
    }
}

static void emit_publication(lab10_render_t *renderer, uint8_t x, uint8_t y,
                             uint8_t color_index, uint32_t stroke_id,
                             uint8_t *min_x, uint8_t *min_y, uint8_t *max_x,
                             uint8_t *max_y, bool *has_dirty)
{
    ++renderer->metrics.publication_ops_last_update;
    ++renderer->metrics.published_pixel_count;
    if (renderer->metrics.publication_ops_last_update >
        renderer->metrics.publication_ops_high_water) {
        renderer->metrics.publication_ops_high_water =
            renderer->metrics.publication_ops_last_update;
    }
    mark_dirty(x, y, min_x, min_y, max_x, max_y, has_dirty);
    if (renderer->hooks.on_publish_pixel != NULL) {
        renderer->hooks.on_publish_pixel(
            &(lab10_render_pixel_event_t){
                .logical_x = x,
                .logical_y = y,
                .color_index = color_index,
                .stroke_id = stroke_id,
                .frame_generation = renderer->metrics.frame_generation,
                .visible = true,
            },
            renderer->hooks.context);
    }
}

static void init_line_job(lab10_render_line_job_t *job,
                          const lab10_capture_point_t *previous,
                          const lab10_capture_point_t *point)
{
    job->x = previous->logical_x;
    job->y = previous->logical_y;
    job->x1 = point->logical_x;
    job->y1 = point->logical_y;
    job->dx = abs(job->x1 - job->x);
    job->sx = job->x < job->x1 ? 1 : -1;
    job->dy = -abs(job->y1 - job->y);
    job->sy = job->y < job->y1 ? 1 : -1;
    job->err = job->dx + job->dy;
    job->color_index = point->color_index;
    job->stroke_id = point->stroke_id;
    job->active = true;
}

static void advance_line_job(lab10_render_line_job_t *job)
{
    if (job->x == job->x1 && job->y == job->y1) {
        job->active = false;
        return;
    }
    const int twice_error = 2 * job->err;
    if (twice_error >= job->dy) {
        job->err += job->dy;
        job->x += job->sx;
    }
    if (twice_error <= job->dx) {
        job->err += job->dx;
        job->y += job->sy;
    }
}

static bool point_can_segment(const lab10_render_t *renderer,
                              const lab10_capture_point_t *point)
{
    return renderer->config.mode != LAB10_RENDER_MODE_DIRECT_POINT &&
           renderer->has_previous_point &&
           point->boundary == LAB10_CAPTURE_POINT_BOUNDARY_CONTINUE &&
           point->stroke_id == renderer->previous_point.stroke_id &&
           point->color_index == renderer->previous_point.color_index;
}

static bool prepare_job(lab10_render_t *renderer,
                        const lab10_capture_t *capture,
                        lab10_capture_session_id_t session_id,
                        size_t source_limit)
{
    if (renderer->pending_point_valid || renderer->source_cursor >= source_limit) {
        return renderer->pending_point_valid;
    }
    if (!lab10_capture_point_at(capture, session_id, renderer->source_cursor,
                                &renderer->pending_point)) {
        return false;
    }
    renderer->pending_point_valid = true;
    if (point_can_segment(renderer, &renderer->pending_point)) {
        init_line_job(&renderer->job, &renderer->previous_point,
                      &renderer->pending_point);
    } else {
        init_line_job(&renderer->job, &renderer->pending_point,
                      &renderer->pending_point);
    }
    return true;
}

static void finish_job(lab10_render_t *renderer, size_t *points_processed)
{
    renderer->previous_point = renderer->pending_point;
    renderer->has_previous_point = true;
    renderer->pending_point_valid = false;
    renderer->job = (lab10_render_line_job_t){0};
    ++renderer->source_cursor;
    ++*points_processed;
}

static void finish_frame_job(lab10_render_t *renderer, size_t *points_processed)
{
    renderer->previous_point = renderer->pending_point;
    renderer->has_previous_point = true;
    renderer->pending_point_valid = false;
    renderer->job = (lab10_render_line_job_t){0};
    ++renderer->frame_source_cursor;
    ++*points_processed;
}

static void raster_incremental_job(lab10_render_t *renderer,
                                   size_t *points_processed,
                                   uint16_t *raster_ops,
                                   uint16_t raster_budget, uint8_t *min_x,
                                   uint8_t *min_y, uint8_t *max_x,
                                   uint8_t *max_y, bool *has_dirty)
{
    while (renderer->job.active && *raster_ops < raster_budget) {
        const uint8_t x = (uint8_t)renderer->job.x;
        const uint8_t y = (uint8_t)renderer->job.y;
        renderer->pixels[(size_t)y * LAB10_LOGICAL_WIDTH + x] =
            renderer->job.color_index;
        ++*raster_ops;
        ++renderer->metrics.raster_pixel_count;
        emit_raster(renderer, x, y, renderer->job.color_index,
                    renderer->job.stroke_id, true);
        emit_publication(renderer, x, y, renderer->job.color_index,
                         renderer->job.stroke_id, min_x, min_y, max_x, max_y,
                         has_dirty);
        advance_line_job(&renderer->job);
    }
    if (!renderer->job.active) {
        finish_job(renderer, points_processed);
    }
}

static void raster_whole_job(lab10_render_t *renderer,
                             size_t *points_processed, uint16_t *raster_ops,
                             uint16_t raster_budget)
{
    while (renderer->job.active && *raster_ops < raster_budget) {
        const uint8_t x = (uint8_t)renderer->job.x;
        const uint8_t y = (uint8_t)renderer->job.y;
        renderer->work_pixels[(size_t)y * LAB10_LOGICAL_WIDTH + x] =
            renderer->job.color_index;
        ++*raster_ops;
        ++renderer->metrics.raster_pixel_count;
        emit_raster(renderer, x, y, renderer->job.color_index,
                    renderer->job.stroke_id, false);
        advance_line_job(&renderer->job);
    }
    if (!renderer->job.active) {
        finish_frame_job(renderer, points_processed);
    }
}

static void emit_invalidation(lab10_render_t *renderer, uint8_t min_x,
                              uint8_t min_y, uint8_t max_x, uint8_t max_y,
                              bool has_dirty, bool whole_frame)
{
    if (!has_dirty) {
        return;
    }
    ++renderer->metrics.invalidation_count;
    if (min_x == 0 && min_y == 0 && max_x == LAB10_LOGICAL_WIDTH - 1 &&
        max_y == LAB10_LOGICAL_HEIGHT - 1) {
        ++renderer->metrics.full_screen_invalidation_count;
    }
    if (renderer->hooks.on_invalidation != NULL) {
        renderer->hooks.on_invalidation(
            &(lab10_render_invalidation_t){
                .min_x = min_x,
                .min_y = min_y,
                .max_x = max_x,
                .max_y = max_y,
                .frame_generation = renderer->metrics.frame_generation,
                .whole_frame = whole_frame,
            },
            renderer->hooks.context);
    }
}

static void start_whole_frame(lab10_render_t *renderer, size_t point_count)
{
    renderer->target_point_count = point_count;
    renderer->frame_clear_cursor = 0;
    renderer->frame_source_cursor = 0;
    renderer->frame_publication_cursor = 0;
    renderer->pending_point_valid = false;
    renderer->job = (lab10_render_line_job_t){0};
    renderer->has_previous_point = false;
    renderer->frame_phase = LAB10_RENDER_FRAME_CLEAR;
    renderer->frame_initialized = true;
    ++renderer->metrics.frame_generation;
    ++renderer->metrics.frame_started_count;
}

static void update_backlog(lab10_render_t *renderer, size_t available_count)
{
    size_t pending = 0;
    if (renderer->config.mode == LAB10_RENDER_MODE_WHOLE_FRAME) {
        if (renderer->target_point_count > renderer->frame_source_cursor) {
            pending += renderer->target_point_count - renderer->frame_source_cursor;
        }
        if (available_count > renderer->target_point_count) {
            pending += available_count - renderer->target_point_count;
        }
    } else if (available_count > renderer->source_cursor) {
        pending = available_count - renderer->source_cursor;
    }
    renderer->metrics.source_point_count = available_count;
    renderer->metrics.source_cursor = renderer->config.mode ==
                                              LAB10_RENDER_MODE_WHOLE_FRAME
                                          ? renderer->frame_source_cursor
                                          : renderer->source_cursor;
    renderer->metrics.pending_point_count = pending;
    if (pending > renderer->metrics.backlog_high_water) {
        renderer->metrics.backlog_high_water = pending;
    }
}

static void update_capture_queue_metrics(lab10_render_t *renderer,
                                         const lab10_capture_t *capture,
                                         lab10_capture_session_id_t session_id)
{
    lab10_capture_stats_t capture_stats;
    if (!lab10_capture_get_stats(capture, session_id, &capture_stats)) {
        return;
    }
    renderer->metrics.capture_queue_depth_current =
        capture_stats.queue_depth_current;
    renderer->metrics.capture_queue_depth_high_water =
        capture_stats.queue_depth_high_water;
}

lab10_render_t *lab10_render_create(const lab10_render_config_t *config)
{
    lab10_render_alloc_fn alloc = config != NULL && config->alloc != NULL
                                      ? config->alloc
                                      : default_alloc;
    lab10_render_free_fn free_fn = config != NULL && config->free != NULL
                                       ? config->free
                                       : default_free;
    if (config != NULL && ((config->alloc == NULL) != (config->free == NULL))) {
        return NULL;
    }
    lab10_render_config_t resolved = config != NULL ? *config
                                                     : (lab10_render_config_t){0};
    if (!mode_valid(resolved.mode) || !role_valid(resolved.role)) {
        return NULL;
    }
    if (resolved.max_raster_ops_per_update == 0) {
        resolved.max_raster_ops_per_update =
            LAB10_RENDER_DEFAULT_RASTER_OPS_PER_UPDATE;
    }
    if (resolved.max_publication_ops_per_update == 0) {
        resolved.max_publication_ops_per_update =
            LAB10_RENDER_DEFAULT_PUBLICATION_OPS_PER_UPDATE;
    }
    if (resolved.max_points_per_update == 0) {
        resolved.max_points_per_update =
            LAB10_RENDER_DEFAULT_BATCH_POINTS_PER_UPDATE;
    }
    if (resolved.max_raster_ops_per_update > LAB10_RENDER_MAX_OPS_PER_UPDATE ||
        resolved.max_publication_ops_per_update > LAB10_RENDER_MAX_OPS_PER_UPDATE ||
        resolved.max_points_per_update > LAB10_RENDER_MAX_BATCH_POINTS_PER_UPDATE) {
        return NULL;
    }

    lab10_render_t *renderer = alloc(sizeof(*renderer),
                                     config != NULL ? config->allocator_context
                                                     : NULL);
    if (renderer == NULL) {
        return NULL;
    }
    memset(renderer, 0, sizeof(*renderer));
    renderer->config = resolved;
    renderer->config.alloc = alloc;
    renderer->config.free = free_fn;
    renderer->config.allocator_context =
        config != NULL ? config->allocator_context : NULL;
    renderer->pixels = alloc(LAB10_RENDER_LOGICAL_PIXEL_COUNT,
                             renderer->config.allocator_context);
    renderer->work_pixels = alloc(LAB10_RENDER_LOGICAL_PIXEL_COUNT,
                                  renderer->config.allocator_context);
    if (renderer->pixels == NULL || renderer->work_pixels == NULL) {
        if (renderer->pixels != NULL) {
            free_fn(renderer->pixels, renderer->config.allocator_context);
        }
        if (renderer->work_pixels != NULL) {
            free_fn(renderer->work_pixels, renderer->config.allocator_context);
        }
        free_fn(renderer, renderer->config.allocator_context);
        return NULL;
    }
    renderer->metrics.lock_state = LAB10_RENDER_LOCK_UNKNOWN;
    reset_metrics(renderer);
    reset_pipeline(renderer);
    return renderer;
}

void lab10_render_destroy(lab10_render_t *renderer)
{
    if (renderer == NULL) {
        return;
    }
    if (renderer->pixels != NULL) {
        renderer->config.free(renderer->pixels,
                              renderer->config.allocator_context);
    }
    if (renderer->work_pixels != NULL) {
        renderer->config.free(renderer->work_pixels,
                              renderer->config.allocator_context);
    }
    renderer->config.free(renderer, renderer->config.allocator_context);
}

bool lab10_render_set_hooks(lab10_render_t *renderer,
                            const lab10_render_hooks_t *hooks)
{
    if (renderer == NULL) {
        return false;
    }
    renderer->hooks = hooks != NULL ? *hooks : (lab10_render_hooks_t){0};
    return true;
}

bool lab10_render_set_mode(lab10_render_t *renderer, lab10_render_mode_t mode,
                           lab10_render_role_t role)
{
    if (renderer == NULL || !mode_valid(mode) || !role_valid(role)) {
        return false;
    }
    renderer->config.mode = mode;
    renderer->config.role = role;
    reset_pipeline(renderer);
    reset_metrics(renderer);
    renderer->metrics.lock_state = LAB10_RENDER_LOCK_UNKNOWN;
    return true;
}

bool lab10_render_begin(lab10_render_t *renderer,
                        lab10_capture_session_id_t session_id)
{
    if (renderer == NULL || session_id == LAB10_CAPTURE_INVALID_SESSION_ID) {
        return false;
    }
    renderer->session_id = session_id;
    renderer->initialized = true;
    reset_pipeline(renderer);
    reset_metrics(renderer);
    return true;
}

bool lab10_render_reset(lab10_render_t *renderer,
                        lab10_capture_session_id_t session_id)
{
    if (!session_valid(renderer, session_id)) {
        return false;
    }
    reset_pipeline(renderer);
    renderer->initialized = false;
    renderer->session_id = LAB10_CAPTURE_INVALID_SESSION_ID;
    ++renderer->metrics.cleanup_count;
    renderer->metrics.frame_phase = LAB10_RENDER_FRAME_IDLE;
    renderer->metrics.pending_point_count = 0;
    return true;
}

bool lab10_render_note_lock_state(lab10_render_t *renderer,
                                  lab10_capture_session_id_t session_id,
                                  lab10_render_lock_state_t state)
{
    if (!session_valid(renderer, session_id) ||
        state > LAB10_RENDER_LOCK_FAILED) {
        return false;
    }
    if (renderer->metrics.lock_state != state) {
        ++renderer->metrics.lock_state_change_count;
    }
    renderer->metrics.lock_state = state;
    if (renderer->hooks.on_lock_state != NULL) {
        renderer->hooks.on_lock_state(state, renderer->hooks.context);
    }
    return true;
}

lab10_render_update_result_t lab10_render_update(
    lab10_render_t *renderer, const lab10_capture_t *capture,
    lab10_capture_session_id_t session_id)
{
    lab10_render_update_result_t result = {0};
    if (renderer == NULL || capture == NULL ||
        !session_valid(renderer, session_id)) {
        if (renderer != NULL && renderer->initialized &&
            session_id != renderer->session_id) {
            ++renderer->metrics.stale_session_count;
        } else if (renderer != NULL) {
            ++renderer->metrics.invalid_update_count;
        }
        return result;
    }

    lab10_capture_snapshot_t snapshot;
    if (!lab10_capture_snapshot(capture, session_id, &snapshot)) {
        ++renderer->metrics.invalid_update_count;
        return result;
    }
    const size_t available_count = snapshot.point_count;
    result.mode = renderer->config.mode;
    result.role = renderer->config.role;
    result.frame_phase = renderer->frame_phase;
    result.lock_state = renderer->metrics.lock_state;
    result.raster_budget = effective_raster_budget(renderer);
    result.publication_budget = renderer->config.max_publication_ops_per_update;
    result.point_budget = effective_point_budget(renderer);
    renderer->metrics.update_count++;
    renderer->metrics.raster_ops_last_update = 0;
    renderer->metrics.publication_ops_last_update = 0;
    renderer->metrics.points_processed_last_update = 0;
    update_capture_queue_metrics(renderer, capture, session_id);

    uint8_t min_x = 0;
    uint8_t min_y = 0;
    uint8_t max_x = 0;
    uint8_t max_y = 0;
    bool has_dirty = false;
    uint16_t raster_budget = effective_raster_budget(renderer);
    const uint16_t point_budget = effective_point_budget(renderer);
    size_t points_processed = 0;
    bool frame_completed_this_update = false;

    if (renderer->config.mode == LAB10_RENDER_MODE_WHOLE_FRAME) {
        if (renderer->frame_phase == LAB10_RENDER_FRAME_IDLE &&
            (!renderer->frame_initialized ||
             available_count != renderer->target_point_count)) {
            start_whole_frame(renderer, available_count);
        }
        while (raster_budget > 0) {
            if (renderer->frame_phase == LAB10_RENDER_FRAME_CLEAR) {
                while (renderer->frame_clear_cursor <
                           LAB10_RENDER_LOGICAL_PIXEL_COUNT &&
                       renderer->metrics.raster_ops_last_update < raster_budget) {
                    const size_t index = renderer->frame_clear_cursor++;
                    const uint8_t x = (uint8_t)(index % LAB10_LOGICAL_WIDTH);
                    const uint8_t y = (uint8_t)(index / LAB10_LOGICAL_WIDTH);
                    renderer->work_pixels[index] = LAB10_COLOR_BACKGROUND;
                    ++renderer->metrics.raster_ops_last_update;
                    ++renderer->metrics.raster_pixel_count;
                    emit_raster(renderer, x, y, LAB10_COLOR_BACKGROUND, 0,
                                false);
                }
                if (renderer->frame_clear_cursor <
                    LAB10_RENDER_LOGICAL_PIXEL_COUNT) {
                    break;
                }
                renderer->frame_phase = LAB10_RENDER_FRAME_REPLAY;
            } else if (renderer->frame_phase == LAB10_RENDER_FRAME_REPLAY) {
                if (!renderer->pending_point_valid &&
                    renderer->frame_source_cursor >= renderer->target_point_count) {
                    renderer->frame_phase = LAB10_RENDER_FRAME_PUBLISH;
                    renderer->frame_publication_cursor = 0;
                    continue;
                }
                if (points_processed >= point_budget) {
                    break;
                }
                if (!renderer->pending_point_valid &&
                    renderer->frame_source_cursor < renderer->target_point_count) {
                    if (!lab10_capture_point_at(
                            capture, session_id, renderer->frame_source_cursor,
                            &renderer->pending_point)) {
                        ++renderer->metrics.invalid_update_count;
                        break;
                    }
                    renderer->pending_point_valid = true;
                    if (point_can_segment(renderer, &renderer->pending_point)) {
                        init_line_job(&renderer->job, &renderer->previous_point,
                                      &renderer->pending_point);
                    } else {
                        init_line_job(&renderer->job, &renderer->pending_point,
                                      &renderer->pending_point);
                    }
                }
                const uint16_t before = renderer->metrics.raster_ops_last_update;
                raster_whole_job(renderer, &points_processed,
                                  &renderer->metrics.raster_ops_last_update,
                                  raster_budget);
                if (renderer->metrics.raster_ops_last_update == before &&
                    renderer->job.active) {
                    break;
                }
            } else if (renderer->frame_phase == LAB10_RENDER_FRAME_PUBLISH) {
                while (renderer->frame_publication_cursor <
                           LAB10_RENDER_LOGICAL_PIXEL_COUNT &&
                       renderer->metrics.publication_ops_last_update <
                           renderer->config.max_publication_ops_per_update) {
                    const size_t index = renderer->frame_publication_cursor++;
                    const uint8_t x = (uint8_t)(index % LAB10_LOGICAL_WIDTH);
                    const uint8_t y = (uint8_t)(index / LAB10_LOGICAL_WIDTH);
                    const uint8_t color = renderer->work_pixels[index];
                    renderer->pixels[index] = color;
                    emit_publication(renderer, x, y, color, 0, &min_x, &min_y,
                                     &max_x, &max_y, &has_dirty);
                }
                if (renderer->frame_publication_cursor ==
                    LAB10_RENDER_LOGICAL_PIXEL_COUNT) {
                    renderer->frame_phase = LAB10_RENDER_FRAME_IDLE;
                    ++renderer->metrics.frame_completed_count;
                    frame_completed_this_update = true;
                }
                break;
            } else {
                break;
            }
        }
    } else {
        while (points_processed < point_budget &&
               renderer->metrics.raster_ops_last_update < raster_budget) {
            if (!renderer->job.active &&
                !prepare_job(renderer, capture, session_id, available_count)) {
                break;
            }
            const size_t before = points_processed;
            raster_incremental_job(renderer, &points_processed,
                                   &renderer->metrics.raster_ops_last_update,
                                   raster_budget, &min_x, &min_y, &max_x, &max_y,
                                   &has_dirty);
            if (points_processed == before && renderer->job.active) {
                break;
            }
        }
    }

    renderer->metrics.raster_ops_high_water =
        renderer->metrics.raster_ops_last_update >
                renderer->metrics.raster_ops_high_water
            ? renderer->metrics.raster_ops_last_update
            : renderer->metrics.raster_ops_high_water;
    renderer->metrics.points_processed_last_update = points_processed;
    update_backlog(renderer, available_count);
    if (renderer->metrics.publication_ops_last_update > 0) {
        emit_invalidation(renderer, min_x, min_y, max_x, max_y,
                          has_dirty,
                          renderer->config.mode == LAB10_RENDER_MODE_WHOLE_FRAME);
    }
    renderer->metrics.frame_phase = renderer->frame_phase;

    result.frame_phase = renderer->frame_phase;
    result.source_point_count = available_count;
    result.pending_point_count = renderer->metrics.pending_point_count;
    result.points_processed = points_processed;
    result.raster_ops = renderer->metrics.raster_ops_last_update;
    result.publication_ops = renderer->metrics.publication_ops_last_update;
    result.frame_generation = renderer->metrics.frame_generation;
    result.invalidated = renderer->metrics.publication_ops_last_update > 0;
    result.frame_complete = renderer->config.mode == LAB10_RENDER_MODE_WHOLE_FRAME &&
                            frame_completed_this_update;
    result.presentation_pending = renderer->config.mode ==
                                      LAB10_RENDER_MODE_WHOLE_FRAME &&
                                  renderer->frame_phase !=
                                      LAB10_RENDER_FRAME_IDLE;
    result.valid = true;
    if (renderer->hooks.on_update != NULL) {
        renderer->hooks.on_update(&result, renderer->hooks.context);
    }
    return result;
}

bool lab10_render_get_metrics(const lab10_render_t *renderer,
                              lab10_capture_session_id_t session_id,
                              lab10_render_metrics_t *metrics)
{
    if (!session_valid(renderer, session_id) || metrics == NULL) {
        return false;
    }
    *metrics = renderer->metrics;
    return true;
}

bool lab10_render_pixel_at(const lab10_render_t *renderer,
                           lab10_capture_session_id_t session_id, uint8_t x,
                           uint8_t y, uint8_t *color_index)
{
    if (!session_valid(renderer, session_id) || color_index == NULL ||
        x >= LAB10_LOGICAL_WIDTH || y >= LAB10_LOGICAL_HEIGHT) {
        return false;
    }
    *color_index = renderer->pixels[(size_t)y * LAB10_LOGICAL_WIDTH + x];
    return true;
}

const uint8_t *lab10_render_pixels(const lab10_render_t *renderer,
                                   lab10_capture_session_id_t session_id,
                                   size_t *count)
{
    if (!session_valid(renderer, session_id)) {
        if (count != NULL) {
            *count = 0;
        }
        return NULL;
    }
    if (count != NULL) {
        *count = LAB10_RENDER_LOGICAL_PIXEL_COUNT;
    }
    return renderer->pixels;
}

const char *lab10_render_mode_name(lab10_render_mode_t mode)
{
    switch (mode) {
    case LAB10_RENDER_MODE_DIRECT_POINT:
        return "direct_point";
    case LAB10_RENDER_MODE_SEGMENT:
        return "segment";
    case LAB10_RENDER_MODE_BOUNDED_BATCH:
        return "bounded_batch";
    case LAB10_RENDER_MODE_WHOLE_FRAME:
        return "whole_frame";
    default:
        return "unknown";
    }
}

const char *lab10_render_role_name(lab10_render_role_t role)
{
    return role == LAB10_RENDER_ROLE_CONTROL ? "control" :
           role == LAB10_RENDER_ROLE_CANDIDATE ? "candidate" : "unknown";
}

const char *lab10_render_frame_phase_name(lab10_render_frame_phase_t phase)
{
    switch (phase) {
    case LAB10_RENDER_FRAME_IDLE:
        return "idle";
    case LAB10_RENDER_FRAME_CLEAR:
        return "clear";
    case LAB10_RENDER_FRAME_REPLAY:
        return "replay";
    case LAB10_RENDER_FRAME_PUBLISH:
        return "publish";
    default:
        return "unknown";
    }
}

const char *lab10_render_lock_state_name(lab10_render_lock_state_t state)
{
    switch (state) {
    case LAB10_RENDER_LOCK_UNKNOWN:
        return "unknown";
    case LAB10_RENDER_LOCK_UI_TASK_OWNED:
        return "ui_task_owned";
    case LAB10_RENDER_LOCK_ACQUIRED:
        return "acquired";
    case LAB10_RENDER_LOCK_RELEASED:
        return "released";
    case LAB10_RENDER_LOCK_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}
