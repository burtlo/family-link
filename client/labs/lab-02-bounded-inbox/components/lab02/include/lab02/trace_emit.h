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
    LAB02_TRACE_FIELD_LEGACY_EXTRA = 1u << 23,
};

/* Pointers are borrowed only for the duration of lab02_trace_emit_payload(). */
typedef struct {
    uint32_t fields;
    uint32_t session_id;
    uint32_t revision;
    uint32_t snap_id;
    uint32_t fetch_snap_id;
    uint32_t attempt;
    uint32_t op_id;
    uint32_t applied_revision;
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
    const char *extra;
} lab02_trace_payload_t;

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

#ifdef __cplusplus
}
#endif
