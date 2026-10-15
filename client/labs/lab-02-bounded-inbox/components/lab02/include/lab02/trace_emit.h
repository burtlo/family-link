#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAB02_TRACE_QUEUE_CAPACITY 8u
#define LAB02_TRACE_EVENT_CAP 32u
#define LAB02_TRACE_MESSAGE_ID_CAP 24u
#define LAB02_TRACE_EXTRA_CAP 96u
#define LAB02_TRACE_TEXT_CAP 48u
#define LAB02_TRACE_REASON_CAP 64u
#define LAB02_TRACE_RECEIPT_CAP 33u
#define LAB02_TRACE_RUN_ID_CAP 33u
#define LAB02_TRACE_LINE_CAP 1024u
/* ESP-IDF passes this to xTaskCreateStatic in StackType_t units; Xtensa
 * StackType_t is byte-sized, so this reserves 4096 bytes on the target. */
#define LAB02_TRACE_TASK_STACK_WORDS 4096u

enum {
    LAB02_TRACE_FIELD_SESSION_ID = 1u << 0,
    LAB02_TRACE_FIELD_REVISION = 1u << 1,
    LAB02_TRACE_FIELD_SNAP_ID = 1u << 2,
    LAB02_TRACE_FIELD_FETCH_SNAP_ID = 1u << 3,
    LAB02_TRACE_FIELD_ATTEMPT = 1u << 4,
    LAB02_TRACE_FIELD_OP_ID = 1u << 5,
    LAB02_TRACE_FIELD_APPLIED_REVISION = 1u << 6,
    LAB02_TRACE_FIELD_START_X = 1u << 7,
    LAB02_TRACE_FIELD_TARGET_X = 1u << 8,
    LAB02_TRACE_FIELD_PAGE_INDEX = 1u << 9,
    LAB02_TRACE_FIELD_SUBMITTED_US = 1u << 10,
    LAB02_TRACE_FIELD_STARTED_US = 1u << 11,
    LAB02_TRACE_FIELD_TERMINAL_US = 1u << 12,
    LAB02_TRACE_FIELD_ELAPSED_US = 1u << 13,
    LAB02_TRACE_FIELD_SERVER_COMPLETED_NS = 1u << 14,
    LAB02_TRACE_FIELD_STATE = 1u << 15,
    LAB02_TRACE_FIELD_OUTCOME = 1u << 16,
    LAB02_TRACE_FIELD_REASON = 1u << 17,
    LAB02_TRACE_FIELD_BEFORE = 1u << 18,
    LAB02_TRACE_FIELD_AFTER = 1u << 19,
    LAB02_TRACE_FIELD_OP_KIND = 1u << 20,
    LAB02_TRACE_FIELD_PAGE_RECEIPT_ID = 1u << 21,
    LAB02_TRACE_FIELD_RUN_ID = 1u << 22,
    LAB02_TRACE_FIELD_LEGACY_EXTRA = 1ull << 23,
    LAB02_TRACE_FIELD_BIND_GENERATION = 1ull << 24,
    LAB02_TRACE_FIELD_CARD_SLOT = 1ull << 25,
    LAB02_TRACE_FIELD_GESTURE_ID = 1ull << 26,
    LAB02_TRACE_FIELD_FOCUS_BEFORE = 1ull << 27,
    LAB02_TRACE_FIELD_FOCUS_AFTER = 1ull << 28,
    LAB02_TRACE_FIELD_FOCUS_MSG_BEFORE = 1ull << 29,
    LAB02_TRACE_FIELD_FOCUS_MSG_AFTER = 1ull << 30,
    LAB02_TRACE_FIELD_PUBLICATION_AFTER_APPLY = 1ull << 31,
    LAB02_TRACE_FIELD_QUEUE_DEPTH = 1ull << 32,
    LAB02_TRACE_FIELD_QUEUE_HIGH_WATER = 1ull << 33,
    LAB02_TRACE_FIELD_OVERFLOW = 1ull << 34,
    LAB02_TRACE_FIELD_INTERNAL_FREE_BYTES = 1ull << 35,
    LAB02_TRACE_FIELD_INTERNAL_MIN_FREE_BYTES = 1ull << 36,
    LAB02_TRACE_FIELD_INTERNAL_LARGEST_FREE_BYTES = 1ull << 37,
    LAB02_TRACE_FIELD_DMA_FREE_BYTES = 1ull << 38,
    LAB02_TRACE_FIELD_DMA_LARGEST_FREE_BYTES = 1ull << 39,
    LAB02_TRACE_FIELD_PSRAM_FREE_BYTES = 1ull << 40,
    LAB02_TRACE_FIELD_PSRAM_LARGEST_FREE_BYTES = 1ull << 41,
    LAB02_TRACE_FIELD_LVGL_MEM_USED_BYTES = 1ull << 42,
    LAB02_TRACE_FIELD_LVGL_MEM_HIGH_WATER_BYTES = 1ull << 43,
    LAB02_TRACE_FIELD_LVGL_BIGGEST_FREE_BYTES = 1ull << 44,
    LAB02_TRACE_FIELD_LIVE_CARD_ROOTS = 1ull << 45,
    LAB02_TRACE_FIELD_LIVE_CARD_NODES = 1ull << 46,
    LAB02_TRACE_FIELD_ACTIVE_FETCHES = 1ull << 47,
    LAB02_TRACE_FIELD_ADMITTED_FETCHES = 1ull << 48,
    LAB02_TRACE_FIELD_FETCH_QUEUE_DEPTH = 1ull << 49,
    LAB02_TRACE_FIELD_TERMINAL_PENDING = 1ull << 50,
};

/* Pointers are borrowed only for the duration of lab02_trace_emit_payload(). */
typedef struct {
    uint64_t fields;
    uint32_t session_id;
    uint32_t revision;
    uint32_t snap_id;
    uint32_t fetch_snap_id;
    uint32_t attempt;
    uint32_t op_id;
    uint32_t applied_revision;
    uint32_t bind_generation;
    uint32_t card_slot;
    uint32_t gesture_id;
    int32_t focus_before;
    int32_t focus_after;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t overflow;
    uint32_t internal_free_bytes;
    uint32_t internal_min_free_bytes;
    uint32_t internal_largest_free_bytes;
    uint32_t dma_free_bytes;
    uint32_t dma_largest_free_bytes;
    uint32_t psram_free_bytes;
    uint32_t psram_largest_free_bytes;
    uint32_t lvgl_mem_used_bytes;
    uint32_t lvgl_mem_high_water_bytes;
    uint32_t lvgl_biggest_free_bytes;
    uint32_t live_card_roots;
    uint32_t live_card_nodes;
    uint32_t active_fetches;
    uint32_t admitted_fetches;
    uint32_t fetch_queue_depth;
    uint32_t terminal_pending;
    int32_t start_x;
    int32_t target_x;
    int32_t page_index;
    int64_t submitted_us;
    int64_t started_us;
    int64_t terminal_us;
    int64_t elapsed_us;
    int64_t server_completed_ns;
    const char *state;
    const char *outcome;
    const char *reason;
    const char *before;
    const char *after;
    const char *op_kind;
    const char *page_receipt_id;
    const char *run_id;
    const char *focus_msg_before;
    const char *focus_msg_after;
    bool publication_after_apply;
    const char *extra;
} lab02_trace_payload_t;

typedef struct {
    const char *phase;
    uint32_t session_id;
    uint32_t internal_free_bytes;
    uint32_t internal_min_free_bytes;
    uint32_t internal_largest_free_bytes;
    uint32_t dma_free_bytes;
    uint32_t dma_largest_free_bytes;
    uint32_t psram_free_bytes;
    uint32_t psram_largest_free_bytes;
    uint32_t lvgl_mem_used_bytes;
    uint32_t lvgl_mem_high_water_bytes;
    uint32_t lvgl_biggest_free_bytes;
    uint32_t live_card_roots;
    uint32_t live_card_nodes;
    uint32_t active_fetches;
    uint32_t admitted_fetches;
    uint32_t fetch_queue_depth;
    uint32_t terminal_pending;
} lab02_resource_sample_t;

typedef struct {
    uint32_t admitted;
    uint32_t emitted;
    uint32_t dropped;
    uint32_t oversized;
    uint32_t queue_full;
    uint32_t stale_discarded;
    uint32_t high_water;
    uint32_t start_failures;
} lab02_trace_stats_t;

/* Must be called once before producers start. No allocation is performed. */
void lab02_trace_init(void);

/* Starts/stops the non-presentation drain task. Lifecycle calls are not hot-path calls. */
bool lab02_trace_start(void);
void lab02_trace_stop(void);

/* Marks a lifecycle boundary without waiting for or interrupting a drain. */
void lab02_trace_reset(void);

/* Test/lifecycle hook: drain admitted records in the calling non-owner context. */
void lab02_trace_drain(void);

void lab02_trace_stats(lab02_trace_stats_t *out);

/* Producer API: bounded copy/admission only; it never formats or writes output. */
void lab02_trace_emit_payload(const char *event, uint32_t generation, int focus_index,
                              const char *message_id,
                              const lab02_trace_payload_t *payload);

/* Compatibility API for already-structured constant reason text. */
void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra);

/* Bounded, typed resource row. Sampling is owned by the session/presentation
 * pump; this function only copies into the existing bounded trace queue. */
void lab02_trace_emit_resource_sample(const lab02_resource_sample_t *sample);

#ifdef __cplusplus
}
#endif
