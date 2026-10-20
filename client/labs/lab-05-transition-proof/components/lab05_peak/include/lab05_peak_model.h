#ifndef LAB05_PEAK_MODEL_H
#define LAB05_PEAK_MODEL_H

#include <stddef.h>
#include <stdint.h>

/* Firmware sees CONFIG_LAB05_PEAK only after sdkconfig.h. Host tests pass
 * the symbol on the compiler command line and do not have that header.
 */
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Production peak model. Host tests compile this header with
 * -DCONFIG_LAB05_PEAK=1. Firmware compiles it only when the Kconfig
 * symbol is on, and CMake omits the .c file otherwise.
 */
#if defined(CONFIG_LAB05_PEAK) && (CONFIG_LAB05_PEAK)

#define LAB05_PEAK_CANVAS_WIDTH 320
#define LAB05_PEAK_CANVAS_HEIGHT 240
#define LAB05_PEAK_IMAGE_SIDE 120
#define LAB05_PEAK_IMAGE_BYTES (120u * 120u * 2u)
#define LAB05_PEAK_IMAGE_PAIR_BYTES (LAB05_PEAK_IMAGE_BYTES * 2u)
#define LAB05_PEAK_PAGE_RECORDS 16u
#define LAB05_PEAK_QUEUE_CAPACITY 8u
#define LAB05_PEAK_AUDIO_STOP_QUEUE_CAPACITY 8u
#define LAB05_PEAK_PAGE_APPLY_QUEUE_CAPACITY 8u
#define LAB05_PEAK_COVER_MS 400u
#define LAB05_PEAK_INCOMING_Y_START 240
#define LAB05_PEAK_INCOMING_Y_END 0
#define LAB05_PEAK_OVERLAP_REPETITIONS 20u
#define LAB05_PEAK_STEADY_LIVE_IMAGES 1u
#define LAB05_PEAK_IMAGE_SLOTS 2u
#define LAB05_PEAK_MAX_OPERATIONS 80u
#define LAB05_PEAK_REASON_LEN 16u
#define LAB05_PEAK_LABEL_LEN 16u
#define LAB05_PEAK_QUEUE_NAME_LEN 16u

/* Provisional lab probe, not a product budget. */
#define LAB05_PEAK_MAX_LIVE_NODES 20u
#define LAB05_PEAK_MAX_TRANSIENT_BYTES 28800u
#define LAB05_PEAK_MAX_PERSISTENT_BYTES 28800u
#define LAB05_PEAK_MAX_CONCURRENT_OPERATIONS 3u
#define LAB05_PEAK_MAX_STACK_OR_CALL_DEPTH 6u
#define LAB05_PEAK_MAXIMUM_INPUT_SIZE 57600u

typedef enum {
    LAB05_PEAK_TERMINAL_NONE = 0,
    LAB05_PEAK_TERMINAL_SUCCESS,
    LAB05_PEAK_TERMINAL_FAILURE,
    LAB05_PEAK_TERMINAL_CANCELLED,
    LAB05_PEAK_TERMINAL_STALE,
    LAB05_PEAK_TERMINAL_TIMED_OUT
} lab05_peak_terminal;

typedef enum {
    LAB05_PEAK_CONTROL_PERSISTENT = 0,
    LAB05_PEAK_CONTROL_REPLACE
} lab05_peak_control;

typedef enum {
    LAB05_PEAK_SCENARIO_OVERLAP = 0,
    LAB05_PEAK_SCENARIO_REPLACE,
    LAB05_PEAK_SCENARIO_ALLOC_FAIL,
    LAB05_PEAK_SCENARIO_STALE_CALLBACK,
    LAB05_PEAK_SCENARIO_CANCEL,
    LAB05_PEAK_SCENARIO_QUEUE_OVERFLOW
} lab05_peak_scenario;

typedef enum {
    LAB05_PEAK_OP_REPETITION = 0,
    LAB05_PEAK_OP_WORKER,
    LAB05_PEAK_OP_OTHER
} lab05_peak_op_kind;

typedef enum {
    LAB05_PEAK_STAGE_STEADY = 0,
    LAB05_PEAK_STAGE_COVER_DOWN
} lab05_peak_stage;

typedef enum {
    LAB05_PEAK_TRIGGER_COVER_DOWN = 1,
    LAB05_PEAK_TRIGGER_COMPLETE,
    LAB05_PEAK_TRIGGER_CANCEL
} lab05_peak_trigger;

typedef struct {
    uint32_t slot;
    uint32_t generation;
    int valid;
} lab05_peak_handle;

typedef struct {
    uint32_t generation;
    uint8_t *pixels;
    int live;
    int view_refs;
    int free_deferred;
    uint32_t reads;
    uint32_t writes;
} lab05_peak_image_slot;

typedef struct {
    uint32_t summary_id;
    char label[LAB05_PEAK_LABEL_LEN];
} lab05_peak_summary;

typedef struct {
    uint64_t operation_id;
} lab05_peak_command;

typedef struct {
    lab05_peak_command items[LAB05_PEAK_QUEUE_CAPACITY];
    size_t count;
    size_t capacity;
    size_t overflow_count;
    size_t dropped_count;
    char name[LAB05_PEAK_QUEUE_NAME_LEN];
} lab05_peak_queue;

typedef struct {
    uint64_t operation_id;
    lab05_peak_scenario scenario;
    lab05_peak_control control;
    lab05_peak_op_kind kind;
    lab05_peak_terminal terminal;
    unsigned terminal_count;
    char reason[LAB05_PEAK_REASON_LEN];
    unsigned live_image_count;
    unsigned temp_image_bytes;
} lab05_peak_operation;

typedef struct {
    int pending;
    int delivered;
    lab05_peak_handle handle;
    uint64_t operation_id;
    uint8_t write_value;
} lab05_peak_callback;

typedef struct lab05_peak_session lab05_peak_session;

/* Firmware observes the live-image peak inside a scenario function.
 * The callback must not call back into the session mutators.
 */
typedef struct {
    lab05_peak_scenario scenario;
    uint64_t operation_id;
    unsigned live_images;
    unsigned pixel_bytes;
    int incoming_y;
    unsigned animation_elapsed_ms;
} lab05_peak_peak_view;

typedef void (*lab05_peak_peak_fn)(lab05_peak_session *session,
                                   const lab05_peak_peak_view *view, void *ctx);

typedef struct lab05_peak_session {
    lab05_peak_stage stage;
    int incoming_y;
    unsigned animation_elapsed_ms;
    int animation_active;
    int animation_holds_running;
    uint64_t animation_operation_id;
    unsigned live_images;
    unsigned max_live_images;
    unsigned live_roots;
    unsigned page_count;
    unsigned pixel_bytes;
    unsigned running_operations;
    unsigned depth;
    unsigned max_depth_seen;
    int audio_running;
    int inject_alloc_fail;
    unsigned writer_rejects;
    unsigned duplicate_terminal_rejects;
    uint64_t next_operation_id;
    int has_incoming;
    lab05_peak_handle steady;
    lab05_peak_handle incoming;
    lab05_peak_image_slot images[LAB05_PEAK_IMAGE_SLOTS];
    lab05_peak_summary page[LAB05_PEAK_PAGE_RECORDS];
    lab05_peak_queue audio_stop;
    lab05_peak_queue page_apply;
    lab05_peak_callback callback;
    lab05_peak_operation ops[LAB05_PEAK_MAX_OPERATIONS];
    size_t op_count;
    lab05_peak_peak_fn peak_observer;
    void *peak_observer_ctx;
} lab05_peak_session;

void lab05_peak_session_init(lab05_peak_session *session);
void lab05_peak_set_peak_observer(lab05_peak_session *session, lab05_peak_peak_fn fn,
                                  void *ctx);

const char *lab05_peak_terminal_name(lab05_peak_terminal terminal);
const char *lab05_peak_scenario_name(lab05_peak_scenario scenario);
const char *lab05_peak_control_name(lab05_peak_control control);
const char *lab05_peak_op_kind_name(lab05_peak_op_kind kind);
int lab05_peak_scenario_is_experiment_input(lab05_peak_scenario scenario);
const char *lab05_peak_experiment_input_id(unsigned index);

void lab05_peak_fill_image(uint8_t *dst, size_t bytes, unsigned pattern);

int lab05_peak_alloc_image(lab05_peak_session *session, unsigned pattern,
                           lab05_peak_handle *out);
int lab05_peak_delete_image(lab05_peak_session *session, lab05_peak_handle handle);
/* A canvas that still references pixels must retain the slot. Delete then
 * defers free until release. The device lock itself is not unit-tested.
 */
int lab05_peak_retain_view(lab05_peak_session *session, lab05_peak_handle handle);
int lab05_peak_release_view(lab05_peak_session *session, lab05_peak_handle handle);
/* Heap numbers are instrumented only after capture_memory has run. */
int lab05_peak_heap_metrics_are_instrumented(int memory_captured);
int lab05_peak_read_image(lab05_peak_session *session, lab05_peak_handle handle,
                          uint8_t *byte_out);
int lab05_peak_write_image(lab05_peak_session *session, lab05_peak_handle handle,
                           uint8_t value);

int lab05_peak_queue_callback(lab05_peak_session *session, lab05_peak_handle handle,
                              uint64_t operation_id, uint8_t write_value);
int lab05_peak_deliver_callback(lab05_peak_session *session);

int lab05_peak_set_terminal(lab05_peak_session *session, uint64_t operation_id,
                            lab05_peak_scenario scenario, lab05_peak_control control,
                            lab05_peak_op_kind kind, lab05_peak_terminal terminal,
                            const char *reason);
const lab05_peak_operation *lab05_peak_find_operation(const lab05_peak_session *session,
                                                      uint64_t operation_id);

int lab05_peak_session_apply_transition(lab05_peak_session *session,
                                        lab05_peak_trigger trigger,
                                        uint64_t operation_id);
int lab05_peak_animation_advance(lab05_peak_session *session, unsigned elapsed_ms);

int lab05_peak_queue_push_audio(lab05_peak_session *session, uint64_t operation_id);
int lab05_peak_queue_push_page(lab05_peak_session *session, uint64_t operation_id);
int lab05_peak_drain_workers(lab05_peak_session *session, lab05_peak_scenario scenario,
                             lab05_peak_control control);

int lab05_peak_on_worker_result(lab05_peak_session *session, uint64_t operation_id,
                                lab05_peak_scenario scenario, lab05_peak_control control,
                                lab05_peak_terminal terminal, const char *reason,
                                int replace_stage, int clear_partial);

int lab05_peak_apply_summary_page(lab05_peak_session *session, unsigned record_count);
int lab05_peak_submit_pixels(lab05_peak_session *session, uint64_t operation_id,
                             size_t bytes);
int lab05_peak_try_begin_operation(lab05_peak_session *session);
void lab05_peak_end_operation(lab05_peak_session *session);

int lab05_peak_run_overlap(lab05_peak_session *session, unsigned repetitions);
int lab05_peak_overlap_completeness(const lab05_peak_session *session, unsigned expected,
                                    unsigned *missing, unsigned *duplicate_ids,
                                    unsigned *extra);
int lab05_peak_run_replace(lab05_peak_session *session, uint64_t operation_id, int inject_alloc_fail,
                           lab05_peak_handle *old_out);
int lab05_peak_run_alloc_fail(lab05_peak_session *session, uint64_t operation_id,
                              lab05_peak_handle *old_out);
int lab05_peak_run_stale_callback(lab05_peak_session *session, uint64_t operation_id,
                                  lab05_peak_handle *old_out);
int lab05_peak_run_cancel(lab05_peak_session *session, uint64_t animation_id,
                          uint64_t audio_id, uint64_t page_id);

int lab05_peak_format_trace(const lab05_peak_session *session, uint64_t operation_id,
                            char *buf, size_t cap);

#endif /* CONFIG_LAB05_PEAK */

#ifdef __cplusplus
}
#endif

#endif /* LAB05_PEAK_MODEL_H */
