#include "lab02/trace_emit.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* The production sink is deliberately a single write of a complete row.
 * Host transport tests provide a strong replacement to model a stalled
 * console without changing the firmware path. */
__attribute__((weak)) void lab02_trace_write_line(const char *line)
{
    (void)fputs(line, stdout);
}

typedef struct {
    int64_t t_us;
    int64_t submitted_us;
    int64_t started_us;
    int64_t terminal_us;
    int64_t elapsed_us;
    int64_t server_completed_ns;
    uint32_t generation;
    uint32_t lifecycle;
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
    int32_t focus_index;
    int32_t start_x;
    int32_t target_x;
    int32_t page_index;
    char event[LAB02_TRACE_EVENT_CAP];
    char message_id[LAB02_TRACE_MESSAGE_ID_CAP];
    char extra[LAB02_TRACE_EXTRA_CAP];
    char state[LAB02_TRACE_TEXT_CAP];
    char outcome[LAB02_TRACE_TEXT_CAP];
    char reason[LAB02_TRACE_REASON_CAP];
    char before[LAB02_TRACE_TEXT_CAP];
    char after[LAB02_TRACE_TEXT_CAP];
    char op_kind[LAB02_TRACE_TEXT_CAP];
    char page_receipt_id[LAB02_TRACE_RECEIPT_CAP];
    char run_id[LAB02_TRACE_RUN_ID_CAP];
    char focus_msg_before[LAB02_TRACE_MESSAGE_ID_CAP];
    char focus_msg_after[LAB02_TRACE_MESSAGE_ID_CAP];
    bool publication_after_apply;
} lab02_trace_record_t;

_Static_assert(sizeof(lab02_trace_record_t) <= 1024u,
               "trace records must remain bounded");

enum {
    TRACE_STATE_STOPPED = 0u,
    TRACE_STATE_STARTING = 1u,
    TRACE_STATE_RUNNING = 2u,
    TRACE_STATE_STOPPING = 3u,
};

static StaticQueue_t s_queue_control;
static uint8_t s_queue_storage[LAB02_TRACE_QUEUE_CAPACITY * sizeof(lab02_trace_record_t)];
static QueueHandle_t s_queue;
static StaticTask_t s_task_control;
static StackType_t s_task_stack[LAB02_TRACE_TASK_STACK_WORDS];
static _Atomic(TaskHandle_t) s_task;
static atomic_uint s_init_state;
static atomic_uint s_task_state;
static atomic_uint s_lifecycle;
static atomic_uint s_admitted;
static atomic_uint s_emitted;
static atomic_uint s_dropped;
static atomic_uint s_oversized;
static atomic_uint s_queue_full;
static atomic_uint s_stale_discarded;
static atomic_uint s_depth_mask;
static atomic_uint s_start_failures;
static atomic_uint s_loss_marker_dropped;
static atomic_uint s_loss_marker_oversized;
static atomic_uint s_loss_marker_queue_full;
static atomic_bool s_loss_marker_pending;
/* Both the FreeRTOS drain task and the public drain entry point can be
 * active at once.  A single non-blocking claim keeps complete rows and loss
 * markers serialized without making a producer or a competing drain wait. */
static atomic_bool s_drain_owner;

static void reset_lifecycle_counters(void)
{
    atomic_store(&s_admitted, 0u);
    atomic_store(&s_emitted, 0u);
    atomic_store(&s_depth_mask, 0u);
    atomic_store(&s_loss_marker_pending, true);
}

void lab02_trace_init(void)
{
    unsigned expected = 0u;
    if (atomic_compare_exchange_strong(&s_init_state, &expected, 1u)) {
        s_queue = xQueueCreateStatic(LAB02_TRACE_QUEUE_CAPACITY,
                                     sizeof(lab02_trace_record_t),
                                     s_queue_storage, &s_queue_control);
        atomic_store(&s_lifecycle, 1u);
        atomic_store(&s_task_state, TRACE_STATE_STOPPED);
        reset_lifecycle_counters();
        atomic_store(&s_init_state, 2u);
        return;
    }
    while (atomic_load(&s_init_state) != 2u) {
        vTaskDelay(1u);
    }
}

static bool copy_bounded(char *dst, size_t dst_size, const char *src, bool token)
{
    if (!src) {
        dst[0] = '\0';
        return true;
    }
    for (size_t i = 0; i + 1u < dst_size; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\0') {
            dst[i] = '\0';
            return true;
        }
        if (c == '\r' || c == '\n' || (token && (c == ' ' || c == '\t'))) {
            dst[i] = '_';
        } else {
            dst[i] = (char)c;
        }
    }
    dst[dst_size - 1u] = '\0';
    return src[dst_size - 1u] == '\0';
}

static bool copy_field(char *dst, size_t dst_size, const char *src, bool token,
                       uint64_t *fields, uint64_t field)
{
    if (!(*fields & field)) {
        dst[0] = '\0';
        return true;
    }
    bool valid = copy_bounded(dst, dst_size, src, token);
    if (!valid) {
        *fields &= ~field;
    }
    return valid;
}

static void account_drop(bool oversized)
{
    atomic_fetch_add(&s_dropped, 1u);
    if (oversized) {
        atomic_fetch_add(&s_oversized, 1u);
    } else {
        atomic_fetch_add(&s_queue_full, 1u);
    }
}

void lab02_trace_emit_payload(const char *event, uint32_t generation, int focus_index,
                              const char *message_id,
                              const lab02_trace_payload_t *payload)
{
    if (atomic_load(&s_init_state) != 2u || !s_queue) {
        account_drop(false);
        return;
    }

    lab02_trace_record_t record = {
        .t_us = esp_timer_get_time(),
        .generation = generation,
        .focus_index = focus_index,
        .lifecycle = atomic_load(&s_lifecycle),
    };
    bool valid = copy_bounded(record.event, sizeof(record.event), event, true);
    valid = copy_bounded(record.message_id, sizeof(record.message_id), message_id, true) && valid;
    if (payload) {
        record.fields = payload->fields;
        record.session_id = payload->session_id;
        record.revision = payload->revision;
        record.snap_id = payload->snap_id;
        record.fetch_snap_id = payload->fetch_snap_id;
        record.attempt = payload->attempt;
        record.op_id = payload->op_id;
        record.applied_revision = payload->applied_revision;
        record.bind_generation = payload->bind_generation;
        record.card_slot = payload->card_slot;
        record.gesture_id = payload->gesture_id;
        record.focus_before = payload->focus_before;
        record.focus_after = payload->focus_after;
        record.queue_depth = payload->queue_depth;
        record.queue_high_water = payload->queue_high_water;
        record.overflow = payload->overflow;
        record.internal_free_bytes = payload->internal_free_bytes;
        record.internal_min_free_bytes = payload->internal_min_free_bytes;
        record.internal_largest_free_bytes = payload->internal_largest_free_bytes;
        record.dma_free_bytes = payload->dma_free_bytes;
        record.dma_largest_free_bytes = payload->dma_largest_free_bytes;
        record.psram_free_bytes = payload->psram_free_bytes;
        record.psram_largest_free_bytes = payload->psram_largest_free_bytes;
        record.lvgl_mem_used_bytes = payload->lvgl_mem_used_bytes;
        record.lvgl_mem_high_water_bytes = payload->lvgl_mem_high_water_bytes;
        record.lvgl_biggest_free_bytes = payload->lvgl_biggest_free_bytes;
        record.live_card_roots = payload->live_card_roots;
        record.live_card_nodes = payload->live_card_nodes;
        record.active_fetches = payload->active_fetches;
        record.admitted_fetches = payload->admitted_fetches;
        record.fetch_queue_depth = payload->fetch_queue_depth;
        record.terminal_pending = payload->terminal_pending;
        record.start_x = payload->start_x;
        record.target_x = payload->target_x;
        record.page_index = payload->page_index;
        record.submitted_us = payload->submitted_us;
        record.started_us = payload->started_us;
        record.terminal_us = payload->terminal_us;
        record.elapsed_us = payload->elapsed_us;
        record.server_completed_ns = payload->server_completed_ns;
        valid = copy_field(record.state, sizeof(record.state), payload->state, true,
                           &record.fields, LAB02_TRACE_FIELD_STATE) && valid;
        valid = copy_field(record.outcome, sizeof(record.outcome), payload->outcome, true,
                           &record.fields, LAB02_TRACE_FIELD_OUTCOME) && valid;
        valid = copy_field(record.reason, sizeof(record.reason), payload->reason, true,
                           &record.fields, LAB02_TRACE_FIELD_REASON) && valid;
        valid = copy_field(record.before, sizeof(record.before), payload->before, true,
                           &record.fields, LAB02_TRACE_FIELD_BEFORE) && valid;
        valid = copy_field(record.after, sizeof(record.after), payload->after, true,
                           &record.fields, LAB02_TRACE_FIELD_AFTER) && valid;
        valid = copy_field(record.op_kind, sizeof(record.op_kind), payload->op_kind, true,
                           &record.fields, LAB02_TRACE_FIELD_OP_KIND) && valid;
        valid = copy_field(record.page_receipt_id, sizeof(record.page_receipt_id),
                           payload->page_receipt_id, true, &record.fields,
                           LAB02_TRACE_FIELD_PAGE_RECEIPT_ID) && valid;
        valid = copy_field(record.run_id, sizeof(record.run_id), payload->run_id, true,
                           &record.fields, LAB02_TRACE_FIELD_RUN_ID) && valid;
        valid = copy_field(record.focus_msg_before, sizeof(record.focus_msg_before),
                           payload->focus_msg_before, true, &record.fields,
                           LAB02_TRACE_FIELD_FOCUS_MSG_BEFORE) && valid;
        valid = copy_field(record.focus_msg_after, sizeof(record.focus_msg_after),
                           payload->focus_msg_after, true, &record.fields,
                           LAB02_TRACE_FIELD_FOCUS_MSG_AFTER) && valid;
        record.publication_after_apply = payload->publication_after_apply;
        valid = copy_field(record.extra, sizeof(record.extra), payload->extra, false,
                           &record.fields, LAB02_TRACE_FIELD_LEGACY_EXTRA) && valid;
    }
    if (!valid) {
        account_drop(true);
        return;
    }

    /* Queue occupancy is observed before admission. It is a truthful sample,
     * not a claim about the later drain order or a high-water reduction. */
    record.fields |= LAB02_TRACE_FIELD_QUEUE_DEPTH;
    record.queue_depth = (uint32_t)uxQueueMessagesWaiting(s_queue);
    /* A zero-tick send is the only producer-side synchronization with storage. */
    if (xQueueSend(s_queue, &record, 0) != pdTRUE) {
        account_drop(false);
        return;
    }
    atomic_fetch_add(&s_admitted, 1u);
    unsigned depth = (unsigned)uxQueueMessagesWaiting(s_queue);
    /* A single atomic bit set records this observed depth.  Do not use an
     * atomic-max retry loop here: producer admission must have a fixed bound
     * even when another producer is updating the same statistic. */
    atomic_fetch_or(&s_depth_mask, 1u << depth);
}

void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra)
{
    lab02_trace_payload_t payload = {
        .fields = extra ? LAB02_TRACE_FIELD_LEGACY_EXTRA : 0u,
        .extra = extra,
    };
    lab02_trace_emit_payload(event, generation, focus_index, message_id, &payload);
}

void lab02_trace_emit_resource_sample(const lab02_resource_sample_t *sample)
{
    if (!sample) return;
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID |
                  LAB02_TRACE_FIELD_STATE |
                  LAB02_TRACE_FIELD_INTERNAL_FREE_BYTES |
                  LAB02_TRACE_FIELD_INTERNAL_MIN_FREE_BYTES |
                  LAB02_TRACE_FIELD_INTERNAL_LARGEST_FREE_BYTES |
                  LAB02_TRACE_FIELD_DMA_FREE_BYTES |
                  LAB02_TRACE_FIELD_DMA_LARGEST_FREE_BYTES |
                  LAB02_TRACE_FIELD_PSRAM_FREE_BYTES |
                  LAB02_TRACE_FIELD_PSRAM_LARGEST_FREE_BYTES |
                  LAB02_TRACE_FIELD_LVGL_MEM_USED_BYTES |
                  LAB02_TRACE_FIELD_LVGL_MEM_HIGH_WATER_BYTES |
                  LAB02_TRACE_FIELD_LVGL_BIGGEST_FREE_BYTES |
                  LAB02_TRACE_FIELD_LIVE_CARD_ROOTS |
                  LAB02_TRACE_FIELD_LIVE_CARD_NODES |
                  LAB02_TRACE_FIELD_ACTIVE_FETCHES |
                  LAB02_TRACE_FIELD_ADMITTED_FETCHES |
                  LAB02_TRACE_FIELD_FETCH_QUEUE_DEPTH |
                  LAB02_TRACE_FIELD_TERMINAL_PENDING,
        .session_id = sample->session_id,
        .internal_free_bytes = sample->internal_free_bytes,
        .internal_min_free_bytes = sample->internal_min_free_bytes,
        .internal_largest_free_bytes = sample->internal_largest_free_bytes,
        .dma_free_bytes = sample->dma_free_bytes,
        .dma_largest_free_bytes = sample->dma_largest_free_bytes,
        .psram_free_bytes = sample->psram_free_bytes,
        .psram_largest_free_bytes = sample->psram_largest_free_bytes,
        .lvgl_mem_used_bytes = sample->lvgl_mem_used_bytes,
        .lvgl_mem_high_water_bytes = sample->lvgl_mem_high_water_bytes,
        .lvgl_biggest_free_bytes = sample->lvgl_biggest_free_bytes,
        .live_card_roots = sample->live_card_roots,
        .live_card_nodes = sample->live_card_nodes,
        .active_fetches = sample->active_fetches,
        .admitted_fetches = sample->admitted_fetches,
        .fetch_queue_depth = sample->fetch_queue_depth,
        .terminal_pending = sample->terminal_pending,
        .state = sample->phase,
    };
    lab02_trace_emit_payload("resource_sample", 0, 0, NULL, &payload);
}

static int append_field(char *line, size_t cap, int used, const char *key, const char *value)
{
    if (used < 0 || (size_t)used >= cap) {
        return -1;
    }
    int n = snprintf(line + used, cap - (size_t)used, " %s=%s", key, value);
    return n < 0 || (size_t)n >= cap - (size_t)used ? -1 : used + n;
}

static int append_u32(char *line, size_t cap, int used, const char *key, uint32_t value)
{
    if (used < 0 || (size_t)used >= cap) {
        return -1;
    }
    int n = snprintf(line + used, cap - (size_t)used, " %s=%u", key, (unsigned)value);
    return n < 0 || (size_t)n >= cap - (size_t)used ? -1 : used + n;
}

static int append_i32(char *line, size_t cap, int used, const char *key, int32_t value)
{
    if (used < 0 || (size_t)used >= cap) {
        return -1;
    }
    int n = snprintf(line + used, cap - (size_t)used, " %s=%ld", key, (long)value);
    return n < 0 || (size_t)n >= cap - (size_t)used ? -1 : used + n;
}

static int append_i64(char *line, size_t cap, int used, const char *key, int64_t value)
{
    if (used < 0 || (size_t)used >= cap) {
        return -1;
    }
    int n = snprintf(line + used, cap - (size_t)used, " %s=%lld", key, (long long)value);
    return n < 0 || (size_t)n >= cap - (size_t)used ? -1 : used + n;
}

static void write_record(const lab02_trace_record_t *record)
{
    char line[LAB02_TRACE_LINE_CAP];
    int used = snprintf(line, sizeof(line),
                        "LAB_TRACE t_us=%lld event=%s gen=%u focus=%ld msg=%s "
                        "trace_schema=lab-trace/v1",
                        (long long)record->t_us, record->event,
                        (unsigned)record->generation, (long)record->focus_index,
                        record->message_id[0] ? record->message_id : "-");
    const uint64_t fields = record->fields;
    if (fields & LAB02_TRACE_FIELD_SESSION_ID) used = append_u32(line, sizeof(line), used, "session_id", record->session_id);
    if (fields & LAB02_TRACE_FIELD_REVISION) used = append_u32(line, sizeof(line), used, "revision", record->revision);
    if (fields & LAB02_TRACE_FIELD_SNAP_ID) used = append_u32(line, sizeof(line), used, "snap_id", record->snap_id);
    if (fields & LAB02_TRACE_FIELD_FETCH_SNAP_ID) used = append_u32(line, sizeof(line), used, "fetch_snap_id", record->fetch_snap_id);
    if (fields & LAB02_TRACE_FIELD_ATTEMPT) used = append_u32(line, sizeof(line), used, "attempt", record->attempt);
    if (fields & LAB02_TRACE_FIELD_OP_ID) used = append_u32(line, sizeof(line), used, "op_id", record->op_id);
    if (fields & LAB02_TRACE_FIELD_APPLIED_REVISION) used = append_u32(line, sizeof(line), used, "applied_revision", record->applied_revision);
    if (fields & LAB02_TRACE_FIELD_BIND_GENERATION) used = append_u32(line, sizeof(line), used, "bind_generation", record->bind_generation);
    if (fields & LAB02_TRACE_FIELD_CARD_SLOT) used = append_u32(line, sizeof(line), used, "card_slot", record->card_slot);
    if (fields & LAB02_TRACE_FIELD_GESTURE_ID) used = append_u32(line, sizeof(line), used, "gesture_id", record->gesture_id);
    if (fields & LAB02_TRACE_FIELD_FOCUS_BEFORE) used = append_i32(line, sizeof(line), used, "focus_before", record->focus_before);
    if (fields & LAB02_TRACE_FIELD_FOCUS_AFTER) used = append_i32(line, sizeof(line), used, "focus_after", record->focus_after);
    if (fields & LAB02_TRACE_FIELD_START_X) used = append_i32(line, sizeof(line), used, "start_x", record->start_x);
    if (fields & LAB02_TRACE_FIELD_TARGET_X) used = append_i32(line, sizeof(line), used, "target_x", record->target_x);
    if (fields & LAB02_TRACE_FIELD_PAGE_INDEX) used = append_i32(line, sizeof(line), used, "page_index", record->page_index);
    if (fields & LAB02_TRACE_FIELD_SUBMITTED_US) used = append_i64(line, sizeof(line), used, "submitted_us", record->submitted_us);
    if (fields & LAB02_TRACE_FIELD_STARTED_US) used = append_i64(line, sizeof(line), used, "started_us", record->started_us);
    if (fields & LAB02_TRACE_FIELD_TERMINAL_US) used = append_i64(line, sizeof(line), used, "terminal_us", record->terminal_us);
    if (fields & LAB02_TRACE_FIELD_ELAPSED_US) used = append_i64(line, sizeof(line), used, "elapsed_us", record->elapsed_us);
    if (fields & LAB02_TRACE_FIELD_SERVER_COMPLETED_NS) used = append_i64(line, sizeof(line), used, "server_completed_ns", record->server_completed_ns);
    if (fields & LAB02_TRACE_FIELD_STATE) used = append_field(line, sizeof(line), used, "state", record->state);
    if (fields & LAB02_TRACE_FIELD_OUTCOME) used = append_field(line, sizeof(line), used, "outcome", record->outcome);
    if (fields & LAB02_TRACE_FIELD_REASON) used = append_field(line, sizeof(line), used, "reason", record->reason);
    if (fields & LAB02_TRACE_FIELD_BEFORE) used = append_field(line, sizeof(line), used, "before", record->before);
    if (fields & LAB02_TRACE_FIELD_AFTER) used = append_field(line, sizeof(line), used, "after", record->after);
    if (fields & LAB02_TRACE_FIELD_OP_KIND) used = append_field(line, sizeof(line), used, "op_kind", record->op_kind);
    if (fields & LAB02_TRACE_FIELD_PAGE_RECEIPT_ID) used = append_field(line, sizeof(line), used, "page_receipt_id", record->page_receipt_id);
    if (fields & LAB02_TRACE_FIELD_RUN_ID) used = append_field(line, sizeof(line), used, "run_id", record->run_id);
    if (fields & LAB02_TRACE_FIELD_FOCUS_MSG_BEFORE) used = append_field(line, sizeof(line), used, "focus_msg_before", record->focus_msg_before);
    if (fields & LAB02_TRACE_FIELD_FOCUS_MSG_AFTER) used = append_field(line, sizeof(line), used, "focus_msg_after", record->focus_msg_after);
    if (fields & LAB02_TRACE_FIELD_PUBLICATION_AFTER_APPLY) used = append_field(line, sizeof(line), used,
                                                                                "publication_after_apply",
                                                                                record->publication_after_apply ? "true" : "false");
    if (fields & LAB02_TRACE_FIELD_QUEUE_DEPTH) used = append_u32(line, sizeof(line), used, "queue_depth", record->queue_depth);
    if (fields & LAB02_TRACE_FIELD_QUEUE_HIGH_WATER) used = append_u32(line, sizeof(line), used, "queue_high_water", record->queue_high_water);
    if (fields & LAB02_TRACE_FIELD_OVERFLOW) used = append_u32(line, sizeof(line), used, "overflow", record->overflow);
    if (fields & LAB02_TRACE_FIELD_INTERNAL_FREE_BYTES) used = append_u32(line, sizeof(line), used, "internal_free_bytes", record->internal_free_bytes);
    if (fields & LAB02_TRACE_FIELD_INTERNAL_MIN_FREE_BYTES) used = append_u32(line, sizeof(line), used, "internal_min_free_bytes", record->internal_min_free_bytes);
    if (fields & LAB02_TRACE_FIELD_INTERNAL_LARGEST_FREE_BYTES) used = append_u32(line, sizeof(line), used, "internal_largest_free_bytes", record->internal_largest_free_bytes);
    if (fields & LAB02_TRACE_FIELD_DMA_FREE_BYTES) used = append_u32(line, sizeof(line), used, "dma_free_bytes", record->dma_free_bytes);
    if (fields & LAB02_TRACE_FIELD_DMA_LARGEST_FREE_BYTES) used = append_u32(line, sizeof(line), used, "dma_largest_free_bytes", record->dma_largest_free_bytes);
    if (fields & LAB02_TRACE_FIELD_PSRAM_FREE_BYTES) used = append_u32(line, sizeof(line), used, "psram_free_bytes", record->psram_free_bytes);
    if (fields & LAB02_TRACE_FIELD_PSRAM_LARGEST_FREE_BYTES) used = append_u32(line, sizeof(line), used, "psram_largest_free_bytes", record->psram_largest_free_bytes);
    if (fields & LAB02_TRACE_FIELD_LVGL_MEM_USED_BYTES) used = append_u32(line, sizeof(line), used, "lvgl_mem_used_bytes", record->lvgl_mem_used_bytes);
    if (fields & LAB02_TRACE_FIELD_LVGL_MEM_HIGH_WATER_BYTES) used = append_u32(line, sizeof(line), used, "lvgl_mem_high_water_bytes", record->lvgl_mem_high_water_bytes);
    if (fields & LAB02_TRACE_FIELD_LVGL_BIGGEST_FREE_BYTES) used = append_u32(line, sizeof(line), used, "lvgl_biggest_free_bytes", record->lvgl_biggest_free_bytes);
    if (fields & LAB02_TRACE_FIELD_LIVE_CARD_ROOTS) used = append_u32(line, sizeof(line), used, "live_card_roots", record->live_card_roots);
    if (fields & LAB02_TRACE_FIELD_LIVE_CARD_NODES) used = append_u32(line, sizeof(line), used, "live_card_nodes", record->live_card_nodes);
    if (fields & LAB02_TRACE_FIELD_ACTIVE_FETCHES) used = append_u32(line, sizeof(line), used, "active_fetches", record->active_fetches);
    if (fields & LAB02_TRACE_FIELD_ADMITTED_FETCHES) used = append_u32(line, sizeof(line), used, "admitted_fetches", record->admitted_fetches);
    if (fields & LAB02_TRACE_FIELD_FETCH_QUEUE_DEPTH) used = append_u32(line, sizeof(line), used, "fetch_queue_depth", record->fetch_queue_depth);
    if (fields & LAB02_TRACE_FIELD_TERMINAL_PENDING) used = append_u32(line, sizeof(line), used, "terminal_pending", record->terminal_pending);
    if (used >= 0 && record->extra[0]) {
        size_t remaining = sizeof(line) - (size_t)used;
        int n = snprintf(line + used, remaining, " %s", record->extra);
        if (n < 0 || (size_t)n >= remaining) used = -1;
        else used += n;
    }
    if (used < 0) {
        return;
    }
    (void)snprintf(line + used, sizeof(line) - (size_t)used, "\n");
    lab02_trace_write_line(line);
    atomic_fetch_add(&s_emitted, 1u);
}

static void write_loss_marker_if_needed(void)
{
    const unsigned dropped = atomic_load(&s_dropped);
    const unsigned oversized = atomic_load(&s_oversized);
    const unsigned queue_full = atomic_load(&s_queue_full);
    const bool pending = atomic_load(&s_loss_marker_pending);
    if (!pending && dropped == atomic_load(&s_loss_marker_dropped) &&
        oversized == atomic_load(&s_loss_marker_oversized) &&
        queue_full == atomic_load(&s_loss_marker_queue_full)) {
        return;
    }

    char line[160];
    const char *status = dropped != 0u ? "OBSERVED" : "NONE";
    int written = snprintf(line, sizeof(line),
                           "LAB_TRACE_LOSS status=%s dropped_events=%u "
                           "oversized_events=%u queue_full=%u malformed_rows=0\n",
                           status, dropped, oversized, queue_full);
    if (written < 0 || (size_t)written >= sizeof(line)) {
        return;
    }
    lab02_trace_write_line(line);
    atomic_store(&s_loss_marker_dropped, dropped);
    atomic_store(&s_loss_marker_oversized, oversized);
    atomic_store(&s_loss_marker_queue_full, queue_full);
    atomic_store(&s_loss_marker_pending, false);
}

static bool try_claim_drain(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_drain_owner, &expected, true);
}

static void release_drain(void)
{
    atomic_store(&s_drain_owner, false);
}

static void write_record_if_current(const lab02_trace_record_t *record,
                                    bool task_context)
{
    if ((task_context && atomic_load(&s_task_state) != TRACE_STATE_RUNNING) ||
        record->lifecycle != atomic_load(&s_lifecycle)) {
        atomic_fetch_add(&s_stale_discarded, 1u);
        return;
    }
    write_record(record);
}

void lab02_trace_drain(void)
{
    if (atomic_load(&s_init_state) != 2u || !s_queue) {
        return;
    }
    if (!try_claim_drain()) {
        return;
    }
    lab02_trace_record_t record;
    while (xQueueReceive(s_queue, &record, 0) == pdTRUE) {
        write_record_if_current(&record, false);
    }
    write_loss_marker_if_needed();
    release_drain();
}

static void trace_task(void *arg)
{
    (void)arg;
    lab02_trace_record_t record;
    while (atomic_load(&s_task_state) == TRACE_STATE_STARTING) {
        vTaskDelay(1u);
    }
    while (atomic_load(&s_task_state) == TRACE_STATE_RUNNING) {
        if (!try_claim_drain()) {
            vTaskDelay(1u);
            continue;
        }
        if (xQueueReceive(s_queue, &record, pdMS_TO_TICKS(50)) != pdTRUE) {
            write_loss_marker_if_needed();
        } else {
            write_record_if_current(&record, true);
        }
        write_loss_marker_if_needed();
        release_drain();
    }
    atomic_store(&s_task, NULL);
    atomic_store(&s_task_state, TRACE_STATE_STOPPED);
    vTaskDelete(NULL);
}

bool lab02_trace_start(void)
{
    lab02_trace_init();
    for (;;) {
        unsigned expected = TRACE_STATE_STOPPED;
        if (atomic_compare_exchange_strong(&s_task_state, &expected,
                                           TRACE_STATE_STARTING)) {
            break;
        }
        if (expected == TRACE_STATE_RUNNING) {
            return true;
        }
        if (expected == TRACE_STATE_STARTING) {
            vTaskDelay(1u);
            continue;
        }
        return false;
    }

    TaskHandle_t task = xTaskCreateStatic(trace_task, "lab02_trace", LAB02_TRACE_TASK_STACK_WORDS,
                                          NULL, 2, s_task_stack, &s_task_control);
    if (!task) {
        atomic_store(&s_task_state, TRACE_STATE_STOPPED);
        atomic_fetch_add(&s_start_failures, 1u);
        return false;
    }
    atomic_store(&s_task, task);
    atomic_store(&s_task_state, TRACE_STATE_RUNNING);
    return true;
}

void lab02_trace_stop(void)
{
    unsigned state = atomic_load(&s_task_state);
    while (state == TRACE_STATE_STARTING) {
        vTaskDelay(1u);
        state = atomic_load(&s_task_state);
    }
    if (state == TRACE_STATE_RUNNING) {
        unsigned expected = TRACE_STATE_RUNNING;
        (void)atomic_compare_exchange_strong(&s_task_state, &expected, TRACE_STATE_STOPPING);
    }
    while (atomic_load(&s_task) != NULL) {
        vTaskDelay(1u);
    }
    atomic_store(&s_task_state, TRACE_STATE_STOPPED);
}

void lab02_trace_reset(void)
{
    if (atomic_load(&s_init_state) != 2u || !s_queue) {
        lab02_trace_init();
        return;
    }

    /* Reset is deliberately a mark-and-defer operation.  The task and a
     * public drain may be inside the sink, so reset must not wait for either
     * one and must not drain/output from this call.  Queue storage, records,
     * and the task stack are static and remain valid until any in-flight
     * drain has returned.  The next drain reconciles old lifecycle records
     * as stale. */
    atomic_fetch_add(&s_lifecycle, 1u);
    unsigned state = atomic_load(&s_task_state);
    /* Do not race the STARTING -> RUNNING publication in lab02_trace_start().
     * A reset during startup still advances the lifecycle; the task can
     * start safely and will observe the new lifecycle when it drains. */
    if (state == TRACE_STATE_RUNNING) {
        unsigned expected = state;
        (void)atomic_compare_exchange_strong(&s_task_state, &expected,
                                              TRACE_STATE_STOPPING);
    }
    reset_lifecycle_counters();
}

void lab02_trace_stats(lab02_trace_stats_t *out)
{
    if (!out) {
        return;
    }
    out->admitted = atomic_load(&s_admitted);
    out->emitted = atomic_load(&s_emitted);
    out->dropped = atomic_load(&s_dropped);
    out->oversized = atomic_load(&s_oversized);
    out->queue_full = atomic_load(&s_queue_full);
    out->stale_discarded = atomic_load(&s_stale_discarded);
    unsigned depth_mask = atomic_load(&s_depth_mask);
    out->high_water = 0u;
    for (unsigned depth = LAB02_TRACE_QUEUE_CAPACITY; depth > 0u; --depth) {
        if (depth_mask & (1u << depth)) {
            out->high_water = depth;
            break;
        }
    }
    out->start_failures = atomic_load(&s_start_failures);
}
