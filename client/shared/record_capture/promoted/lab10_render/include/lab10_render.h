#pragma once

/*
 * LAB-10 L10-04 bounded rendering and presentation contract.
 *
 * The renderer consumes the immutable point result owned by lab10_capture. It
 * never submits touch reports, owns the capture FIFO, or retains a capture
 * pointer after an update. Every update has independent point, raster, and
 * publication limits. The callbacks are intentionally small and synchronous;
 * the LVGL adapter uses them only from the UI task.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lab10_capture.h"

#define LAB10_RENDER_LOGICAL_PIXEL_COUNT \
    ((size_t)LAB10_LOGICAL_WIDTH * (size_t)LAB10_LOGICAL_HEIGHT)
#define LAB10_RENDER_DEFAULT_RASTER_OPS_PER_UPDATE 32u
#define LAB10_RENDER_DEFAULT_PUBLICATION_OPS_PER_UPDATE 32u
#define LAB10_RENDER_DEFAULT_BATCH_POINTS_PER_UPDATE 8u
#define LAB10_RENDER_MAX_OPS_PER_UPDATE 512u
#define LAB10_RENDER_MAX_BATCH_POINTS_PER_UPDATE 64u

typedef struct lab10_render lab10_render_t;

typedef enum {
    LAB10_RENDER_MODE_DIRECT_POINT = 0,
    LAB10_RENDER_MODE_SEGMENT,
    LAB10_RENDER_MODE_BOUNDED_BATCH,
    LAB10_RENDER_MODE_WHOLE_FRAME,
} lab10_render_mode_t;

typedef enum {
    LAB10_RENDER_ROLE_CONTROL = 0,
    LAB10_RENDER_ROLE_CANDIDATE,
} lab10_render_role_t;

typedef enum {
    LAB10_RENDER_FRAME_IDLE = 0,
    LAB10_RENDER_FRAME_CLEAR,
    LAB10_RENDER_FRAME_REPLAY,
    LAB10_RENDER_FRAME_PUBLISH,
} lab10_render_frame_phase_t;

typedef enum {
    LAB10_RENDER_LOCK_UNKNOWN = 0,
    LAB10_RENDER_LOCK_UI_TASK_OWNED,
    LAB10_RENDER_LOCK_ACQUIRED,
    LAB10_RENDER_LOCK_RELEASED,
    LAB10_RENDER_LOCK_FAILED,
} lab10_render_lock_state_t;

typedef void *(*lab10_render_alloc_fn)(size_t size, void *context);
typedef void (*lab10_render_free_fn)(void *memory, void *context);

typedef struct {
    lab10_render_mode_t mode;
    lab10_render_role_t role;
    uint16_t max_points_per_update;
    uint16_t max_raster_ops_per_update;
    uint16_t max_publication_ops_per_update;
    lab10_render_alloc_fn alloc;
    lab10_render_free_fn free;
    void *allocator_context;
} lab10_render_config_t;

typedef struct {
    uint8_t logical_x;
    uint8_t logical_y;
    uint8_t color_index;
    uint32_t stroke_id;
    uint32_t frame_generation;
    bool visible;
} lab10_render_pixel_event_t;

typedef struct {
    uint8_t min_x;
    uint8_t min_y;
    uint8_t max_x;
    uint8_t max_y;
    uint32_t frame_generation;
    bool whole_frame;
} lab10_render_invalidation_t;

typedef struct {
    bool valid;
    lab10_render_mode_t mode;
    lab10_render_role_t role;
    lab10_render_frame_phase_t frame_phase;
    lab10_render_lock_state_t lock_state;
    size_t source_point_count;
    size_t pending_point_count;
    size_t points_processed;
    uint16_t raster_ops;
    uint16_t publication_ops;
    uint16_t raster_budget;
    uint16_t publication_budget;
    uint16_t point_budget;
    uint32_t frame_generation;
    bool invalidated;
    bool frame_complete;
    bool presentation_pending;
} lab10_render_update_result_t;

typedef struct {
    void (*on_raster_pixel)(const lab10_render_pixel_event_t *event,
                            void *context);
    void (*on_publish_pixel)(const lab10_render_pixel_event_t *event,
                             void *context);
    void (*on_invalidation)(const lab10_render_invalidation_t *invalidation,
                            void *context);
    void (*on_update)(const lab10_render_update_result_t *result,
                      void *context);
    void (*on_lock_state)(lab10_render_lock_state_t state, void *context);
    void *context;
} lab10_render_hooks_t;

typedef struct {
    lab10_render_mode_t mode;
    lab10_render_role_t role;
    lab10_render_frame_phase_t frame_phase;
    lab10_render_lock_state_t lock_state;
    uint16_t configured_raster_budget;
    uint16_t configured_publication_budget;
    uint16_t configured_point_budget;
    uint16_t effective_raster_budget;
    uint32_t update_count;
    uint32_t invalid_update_count;
    uint32_t stale_session_count;
    uint32_t invalidation_count;
    uint32_t full_screen_invalidation_count;
    uint32_t lock_state_change_count;
    uint32_t frame_started_count;
    uint32_t frame_completed_count;
    uint32_t frame_generation;
    uint32_t capture_queue_depth_current;
    uint32_t capture_queue_depth_high_water;
    size_t source_point_count;
    size_t source_cursor;
    size_t pending_point_count;
    size_t backlog_high_water;
    uint16_t raster_ops_last_update;
    uint16_t raster_ops_high_water;
    uint16_t publication_ops_last_update;
    uint16_t publication_ops_high_water;
    size_t points_processed_last_update;
    size_t published_pixel_count;
    size_t raster_pixel_count;
    size_t live_framebuffer_bytes;
    uint32_t cleanup_count;
} lab10_render_metrics_t;

lab10_render_t *lab10_render_create(const lab10_render_config_t *config);
void lab10_render_destroy(lab10_render_t *renderer);

bool lab10_render_set_hooks(lab10_render_t *renderer,
                            const lab10_render_hooks_t *hooks);
bool lab10_render_set_mode(lab10_render_t *renderer, lab10_render_mode_t mode,
                           lab10_render_role_t role);

/* Start a new render generation for an existing capture session. */
bool lab10_render_begin(lab10_render_t *renderer,
                        lab10_capture_session_id_t session_id);

/* Drop renderer-owned state before the capture result is released. */
bool lab10_render_reset(lab10_render_t *renderer,
                        lab10_capture_session_id_t session_id);

/* This is a UI-task observation, not proof of LVGL lock timing. */
bool lab10_render_note_lock_state(lab10_render_t *renderer,
                                  lab10_capture_session_id_t session_id,
                                  lab10_render_lock_state_t state);

lab10_render_update_result_t lab10_render_update(
    lab10_render_t *renderer, const lab10_capture_t *capture,
    lab10_capture_session_id_t session_id);

bool lab10_render_get_metrics(const lab10_render_t *renderer,
                              lab10_capture_session_id_t session_id,
                              lab10_render_metrics_t *metrics);
bool lab10_render_pixel_at(const lab10_render_t *renderer,
                           lab10_capture_session_id_t session_id, uint8_t x,
                           uint8_t y, uint8_t *color_index);
const uint8_t *lab10_render_pixels(const lab10_render_t *renderer,
                                   lab10_capture_session_id_t session_id,
                                   size_t *count);

const char *lab10_render_mode_name(lab10_render_mode_t mode);
const char *lab10_render_role_name(lab10_render_role_t role);
const char *lab10_render_frame_phase_name(lab10_render_frame_phase_t phase);
const char *lab10_render_lock_state_name(lab10_render_lock_state_t state);
