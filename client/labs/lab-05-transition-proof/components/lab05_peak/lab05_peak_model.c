#include "lab05_peak_model.h"

#if defined(CONFIG_LAB05_PEAK) && (CONFIG_LAB05_PEAK)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int depth_enter(lab05_peak_session *session)
{
    if (session->depth >= LAB05_PEAK_MAX_STACK_OR_CALL_DEPTH) {
        return -1;
    }
    session->depth++;
    if (session->depth > session->max_depth_seen) {
        session->max_depth_seen = session->depth;
    }
    return 0;
}

static void depth_leave(lab05_peak_session *session)
{
    if (session->depth > 0) {
        session->depth--;
    }
}

static void copy_reason(char *dst, const char *src)
{
    size_t i;
    if (src == NULL) {
        src = "";
    }
    for (i = 0; i + 1u < LAB05_PEAK_REASON_LEN && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void note_live_images(lab05_peak_session *session)
{
    if (session->live_images > session->max_live_images) {
        session->max_live_images = session->live_images;
    }
}

static int handle_matches(const lab05_peak_session *session, lab05_peak_handle handle)
{
    const lab05_peak_image_slot *slot;
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        return 0;
    }
    slot = &session->images[handle.slot];
    if (!slot->live || slot->pixels == NULL) {
        return 0;
    }
    if (slot->generation != handle.generation) {
        return 0;
    }
    return 1;
}

static int find_op_index(const lab05_peak_session *session, uint64_t operation_id)
{
    size_t i;
    for (i = 0; i < session->op_count; i++) {
        if (session->ops[i].operation_id == operation_id) {
            return (int)i;
        }
    }
    return -1;
}

static int record_terminal(lab05_peak_session *session, uint64_t operation_id,
                           lab05_peak_scenario scenario, lab05_peak_control control,
                           lab05_peak_op_kind kind, lab05_peak_terminal terminal,
                           const char *reason)
{
    int existing;
    lab05_peak_operation *op;
    if (operation_id == 0 || terminal == LAB05_PEAK_TERMINAL_NONE) {
        return -1;
    }
    existing = find_op_index(session, operation_id);
    if (existing >= 0) {
        session->duplicate_terminal_rejects++;
        return -1;
    }
    if (session->op_count >= LAB05_PEAK_MAX_OPERATIONS) {
        return -1;
    }
    op = &session->ops[session->op_count];
    memset(op, 0, sizeof(*op));
    op->operation_id = operation_id;
    op->scenario = scenario;
    op->control = control;
    op->kind = kind;
    op->terminal = terminal;
    op->terminal_count = 1;
    copy_reason(op->reason, reason);
    op->live_image_count = session->live_images;
    op->temp_image_bytes = session->pixel_bytes;
    session->op_count++;
    return 0;
}

static void queue_init(lab05_peak_queue *queue, const char *name)
{
    memset(queue, 0, sizeof(*queue));
    queue->capacity = LAB05_PEAK_QUEUE_CAPACITY;
    snprintf(queue->name, sizeof(queue->name), "%s", name);
}

static void write_summary(lab05_peak_summary *out, unsigned index)
{
    memset(out, 0, sizeof(*out));
    out->summary_id = index + 1u;
    snprintf(out->label, sizeof(out->label), "summary-%02u", index + 1u);
}

static int alloc_image_internal(lab05_peak_session *session, unsigned pattern,
                                lab05_peak_handle *out)
{
    size_t slot_index;
    lab05_peak_image_slot *slot;
    uint8_t *pixels;
    unsigned nodes;
    if (out == NULL) {
        return -1;
    }
    out->valid = 0;
    if (session->inject_alloc_fail) {
        session->inject_alloc_fail = 0;
        return -1;
    }
    if (session->live_images >= LAB05_PEAK_IMAGE_SLOTS) {
        return -1;
    }
    if (session->pixel_bytes > LAB05_PEAK_MAXIMUM_INPUT_SIZE - LAB05_PEAK_IMAGE_BYTES) {
        return -1;
    }
    nodes = session->page_count + session->live_images + 1u + session->live_roots;
    if (nodes > LAB05_PEAK_MAX_LIVE_NODES) {
        return -1;
    }
    for (slot_index = 0; slot_index < LAB05_PEAK_IMAGE_SLOTS; slot_index++) {
        if (!session->images[slot_index].live && session->images[slot_index].pixels == NULL) {
            break;
        }
    }
    if (slot_index >= LAB05_PEAK_IMAGE_SLOTS) {
        return -1;
    }
    pixels = (uint8_t *)malloc(LAB05_PEAK_IMAGE_BYTES);
    if (pixels == NULL) {
        return -1;
    }
    lab05_peak_fill_image(pixels, LAB05_PEAK_IMAGE_BYTES, pattern);
    slot = &session->images[slot_index];
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    slot->pixels = pixels;
    slot->live = 1;
    slot->reads = 0;
    slot->writes = 0;
    session->live_images++;
    session->pixel_bytes += LAB05_PEAK_IMAGE_BYTES;
    note_live_images(session);
    out->slot = (uint32_t)slot_index;
    out->generation = slot->generation;
    out->valid = 1;
    return 0;
}

static int delete_image_internal(lab05_peak_session *session, lab05_peak_handle handle)
{
    lab05_peak_image_slot *slot;
    uint8_t *pixels;
    if (!handle_matches(session, handle)) {
        return -1;
    }
    slot = &session->images[handle.slot];
    pixels = slot->pixels;
    slot->live = 0;
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }
    if (slot->view_refs > 0) {
        /* Canvas still holds this pointer. Keep the bytes until release. */
        slot->free_deferred = 1;
        if (session->live_images > 0) {
            session->live_images--;
        }
        if (session->has_incoming && session->incoming.slot == handle.slot &&
            session->incoming.generation == handle.generation) {
            session->has_incoming = 0;
            session->incoming.valid = 0;
        }
        return 0;
    }
    slot->pixels = NULL;
    free(pixels);
    if (session->live_images > 0) {
        session->live_images--;
    }
    if (session->pixel_bytes >= LAB05_PEAK_IMAGE_BYTES) {
        session->pixel_bytes -= LAB05_PEAK_IMAGE_BYTES;
    }
    if (session->has_incoming && session->incoming.slot == handle.slot &&
        session->incoming.generation == handle.generation) {
        session->has_incoming = 0;
        session->incoming.valid = 0;
    }
    return 0;
}

static int push_queue(lab05_peak_session *session, lab05_peak_queue *queue,
                      uint64_t operation_id)
{
    if (operation_id == 0) {
        return -1;
    }
    if (queue->count >= queue->capacity) {
        int first = (queue->overflow_count == 0);
        queue->overflow_count++;
        if (first) {
            record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_QUEUE_OVERFLOW,
                            LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_OTHER,
                            LAB05_PEAK_TERMINAL_FAILURE, "overflow");
        }
        return -1;
    }
    queue->items[queue->count].operation_id = operation_id;
    queue->count++;
    return 0;
}

static int apply_page_internal(lab05_peak_session *session, unsigned record_count)
{
    unsigned i;
    unsigned nodes;
    if (record_count != LAB05_PEAK_PAGE_RECORDS) {
        return -1;
    }
    if (session->page_count == LAB05_PEAK_PAGE_RECORDS &&
        session->page[0].summary_id == 1u &&
        session->page[LAB05_PEAK_PAGE_RECORDS - 1u].summary_id == LAB05_PEAK_PAGE_RECORDS) {
        return 0;
    }
    nodes = LAB05_PEAK_PAGE_RECORDS + session->live_images + session->live_roots;
    if (nodes > LAB05_PEAK_MAX_LIVE_NODES) {
        return -1;
    }
    for (i = 0; i < LAB05_PEAK_PAGE_RECORDS; i++) {
        write_summary(&session->page[i], i);
    }
    session->page_count = LAB05_PEAK_PAGE_RECORDS;
    return 0;
}

static int drain_internal(lab05_peak_session *session, lab05_peak_scenario scenario,
                          lab05_peak_control control)
{
    size_t i;
    int began_audio = 0;
    int began_page = 0;
    if (session->audio_stop.count > 0) {
        if (session->running_operations >= LAB05_PEAK_MAX_CONCURRENT_OPERATIONS) {
            return -1;
        }
        session->running_operations++;
        began_audio = 1;
    }
    if (session->page_apply.count > 0) {
        if (session->running_operations >= LAB05_PEAK_MAX_CONCURRENT_OPERATIONS) {
            if (began_audio) {
                session->running_operations--;
            }
            return -1;
        }
        session->running_operations++;
        began_page = 1;
    }
    for (i = 0; i < session->audio_stop.count; i++) {
        session->audio_running = 0;
        if (record_terminal(session, session->audio_stop.items[i].operation_id, scenario,
                            control, LAB05_PEAK_OP_WORKER, LAB05_PEAK_TERMINAL_SUCCESS,
                            "done") != 0) {
            if (began_audio) {
                session->running_operations--;
            }
            if (began_page) {
                session->running_operations--;
            }
            return -1;
        }
    }
    session->audio_stop.count = 0;
    for (i = 0; i < session->page_apply.count; i++) {
        if (apply_page_internal(session, LAB05_PEAK_PAGE_RECORDS) != 0) {
            record_terminal(session, session->page_apply.items[i].operation_id, scenario,
                            control, LAB05_PEAK_OP_WORKER, LAB05_PEAK_TERMINAL_FAILURE,
                            "page");
        } else if (record_terminal(session, session->page_apply.items[i].operation_id, scenario,
                                   control, LAB05_PEAK_OP_WORKER, LAB05_PEAK_TERMINAL_SUCCESS,
                                   "done") != 0) {
            if (began_audio) {
                session->running_operations--;
            }
            if (began_page) {
                session->running_operations--;
            }
            return -1;
        }
    }
    session->page_apply.count = 0;
    if (began_audio && session->running_operations > 0) {
        session->running_operations--;
    }
    if (began_page && session->running_operations > 0) {
        session->running_operations--;
    }
    return 0;
}

static int begin_cover(lab05_peak_session *session, uint64_t operation_id)
{
    if (session->stage != LAB05_PEAK_STAGE_STEADY || session->animation_active) {
        return -1;
    }
    if (session->running_operations >= LAB05_PEAK_MAX_CONCURRENT_OPERATIONS) {
        return -1;
    }
    session->running_operations++;
    session->animation_holds_running = 1;
    session->animation_active = 1;
    session->animation_elapsed_ms = 0;
    session->animation_operation_id = operation_id;
    session->incoming_y = LAB05_PEAK_INCOMING_Y_START;
    session->stage = LAB05_PEAK_STAGE_COVER_DOWN;
    session->live_roots = 2;
    return 0;
}

static int terminal_slot_available(const lab05_peak_session *session, uint64_t operation_id)
{
    if (operation_id == 0 || find_op_index(session, operation_id) >= 0) {
        return 0;
    }
    if (session->op_count >= LAB05_PEAK_MAX_OPERATIONS) {
        return 0;
    }
    return 1;
}

static int finish_animation_running(lab05_peak_session *session)
{
    session->animation_active = 0;
    session->stage = LAB05_PEAK_STAGE_STEADY;
    session->live_roots = 1;
    if (session->animation_holds_running && session->running_operations > 0) {
        session->running_operations--;
    }
    session->animation_holds_running = 0;
    return 0;
}

void lab05_peak_fill_image(uint8_t *dst, size_t bytes, unsigned pattern)
{
    size_t i;
    if (dst == NULL || bytes != LAB05_PEAK_IMAGE_BYTES) {
        return;
    }
    for (i = 0; i < bytes; i += 2u) {
        unsigned pixel_index = (unsigned)(i / 2u);
        uint16_t pixel = (pattern == 0u)
                             ? (uint16_t)0x001Fu
                             : (uint16_t)((pixel_index & 0x3Fu) << 5);
        dst[i] = (uint8_t)(pixel & 0xFFu);
        dst[i + 1u] = (uint8_t)((pixel >> 8) & 0xFFu);
    }
}

void lab05_peak_set_peak_observer(lab05_peak_session *session, lab05_peak_peak_fn fn, void *ctx)
{
    if (session == NULL) {
        return;
    }
    session->peak_observer = fn;
    session->peak_observer_ctx = ctx;
}

static void notify_peak(lab05_peak_session *session, lab05_peak_scenario scenario,
                        uint64_t operation_id)
{
    lab05_peak_peak_view view;
    if (session->peak_observer == NULL) {
        return;
    }
    view.scenario = scenario;
    view.operation_id = operation_id;
    view.live_images = session->live_images;
    view.pixel_bytes = session->pixel_bytes;
    view.incoming_y = session->incoming_y;
    view.animation_elapsed_ms = session->animation_elapsed_ms;
    session->peak_observer(session, &view, session->peak_observer_ctx);
}

void lab05_peak_session_init(lab05_peak_session *session)
{
    lab05_peak_handle steady;
    size_t i;
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    queue_init(&session->audio_stop, "audio-stop");
    queue_init(&session->page_apply, "page-apply");
    for (i = 0; i < LAB05_PEAK_IMAGE_SLOTS; i++) {
        session->images[i].generation = 1;
    }
    session->stage = LAB05_PEAK_STAGE_STEADY;
    session->incoming_y = LAB05_PEAK_INCOMING_Y_END;
    session->live_roots = 1;
    session->audio_running = 1;
    session->next_operation_id = 1;
    if (alloc_image_internal(session, 0u, &steady) != 0) {
        return;
    }
    session->steady = steady;
}

const char *lab05_peak_terminal_name(lab05_peak_terminal terminal)
{
    switch (terminal) {
        case LAB05_PEAK_TERMINAL_SUCCESS:
            return "success";
        case LAB05_PEAK_TERMINAL_FAILURE:
            return "failure";
        case LAB05_PEAK_TERMINAL_CANCELLED:
            return "cancelled";
        case LAB05_PEAK_TERMINAL_STALE:
            return "stale";
        case LAB05_PEAK_TERMINAL_TIMED_OUT:
            return "timed_out";
        case LAB05_PEAK_TERMINAL_NONE:
        default:
            return "none";
    }
}

const char *lab05_peak_scenario_name(lab05_peak_scenario scenario)
{
    switch (scenario) {
        case LAB05_PEAK_SCENARIO_OVERLAP:
            return "P-overlap";
        case LAB05_PEAK_SCENARIO_REPLACE:
            return "P-replace";
        case LAB05_PEAK_SCENARIO_ALLOC_FAIL:
            return "P-alloc-fail";
        case LAB05_PEAK_SCENARIO_STALE_CALLBACK:
            return "P-stale-callback";
        case LAB05_PEAK_SCENARIO_CANCEL:
            return "P-cancel";
        case LAB05_PEAK_SCENARIO_QUEUE_OVERFLOW:
            return "queue-overflow";
        default:
            return "unknown";
    }
}

const char *lab05_peak_control_name(lab05_peak_control control)
{
    if (control == LAB05_PEAK_CONTROL_REPLACE) {
        return "replace";
    }
    return "persistent";
}

const char *lab05_peak_op_kind_name(lab05_peak_op_kind kind)
{
    switch (kind) {
        case LAB05_PEAK_OP_REPETITION:
            return "repetition";
        case LAB05_PEAK_OP_WORKER:
            return "worker";
        case LAB05_PEAK_OP_OTHER:
            return "other";
        default:
            return "other";
    }
}

int lab05_peak_heap_metrics_are_instrumented(int memory_captured)
{
    return memory_captured ? 1 : 0;
}

int lab05_peak_retain_view(lab05_peak_session *session, lab05_peak_handle handle)
{
    lab05_peak_image_slot *slot;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!handle_matches(session, handle)) {
        depth_leave(session);
        return -1;
    }
    slot = &session->images[handle.slot];
    if (slot->view_refs >= 8) {
        depth_leave(session);
        return -1;
    }
    slot->view_refs++;
    depth_leave(session);
    return 0;
}

int lab05_peak_release_view(lab05_peak_session *session, lab05_peak_handle handle)
{
    lab05_peak_image_slot *slot;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        depth_leave(session);
        return -1;
    }
    slot = &session->images[handle.slot];
    if (slot->view_refs <= 0) {
        depth_leave(session);
        return -1;
    }
    if (!slot->free_deferred && slot->generation != handle.generation) {
        depth_leave(session);
        return -1;
    }
    slot->view_refs--;
    if (slot->view_refs == 0 && slot->free_deferred) {
        free(slot->pixels);
        slot->pixels = NULL;
        slot->free_deferred = 0;
        if (session->pixel_bytes >= LAB05_PEAK_IMAGE_BYTES) {
            session->pixel_bytes -= LAB05_PEAK_IMAGE_BYTES;
        }
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_scenario_is_experiment_input(lab05_peak_scenario scenario)
{
    return scenario == LAB05_PEAK_SCENARIO_ALLOC_FAIL ||
           scenario == LAB05_PEAK_SCENARIO_STALE_CALLBACK ||
           scenario == LAB05_PEAK_SCENARIO_CANCEL;
}

const char *lab05_peak_experiment_input_id(unsigned index)
{
    static const char *ids[] = {"P-alloc-fail", "P-stale-callback", "P-cancel"};
    if (index >= 3u) {
        return NULL;
    }
    return ids[index];
}

int lab05_peak_alloc_image(lab05_peak_session *session, unsigned pattern, lab05_peak_handle *out)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = alloc_image_internal(session, pattern, out);
    depth_leave(session);
    return rc;
}

int lab05_peak_delete_image(lab05_peak_session *session, lab05_peak_handle handle)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = delete_image_internal(session, handle);
    depth_leave(session);
    return rc;
}

int lab05_peak_read_image(lab05_peak_session *session, lab05_peak_handle handle, uint8_t *byte_out)
{
    lab05_peak_image_slot *slot;
    if (session == NULL || byte_out == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        depth_leave(session);
        return -1;
    }
    slot = &session->images[handle.slot];
    if (slot->generation != handle.generation || !slot->live || slot->pixels == NULL) {
        depth_leave(session);
        return -1;
    }
    *byte_out = slot->pixels[0];
    slot->reads++;
    depth_leave(session);
    return 0;
}

int lab05_peak_write_image(lab05_peak_session *session, lab05_peak_handle handle, uint8_t value)
{
    lab05_peak_image_slot *slot;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        depth_leave(session);
        return -1;
    }
    slot = &session->images[handle.slot];
    if (slot->generation != handle.generation || !slot->live || slot->pixels == NULL) {
        depth_leave(session);
        return -1;
    }
    slot->pixels[0] = value;
    slot->writes++;
    depth_leave(session);
    return 0;
}

int lab05_peak_queue_callback(lab05_peak_session *session, lab05_peak_handle handle,
                              uint64_t operation_id, uint8_t write_value)
{
    if (session == NULL || operation_id == 0 || depth_enter(session) != 0) {
        return -1;
    }
    if (session->callback.pending) {
        depth_leave(session);
        return -1;
    }
    if (!handle_matches(session, handle)) {
        depth_leave(session);
        return -1;
    }
    session->callback.pending = 1;
    session->callback.delivered = 0;
    session->callback.handle = handle;
    session->callback.operation_id = operation_id;
    session->callback.write_value = write_value;
    depth_leave(session);
    return 0;
}

int lab05_peak_deliver_callback(lab05_peak_session *session)
{
    lab05_peak_handle handle;
    lab05_peak_image_slot *slot;
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!session->callback.pending) {
        depth_leave(session);
        return -1;
    }
    handle = session->callback.handle;
    session->callback.pending = 0;
    session->callback.delivered = 1;
    if (!handle.valid || handle.slot >= LAB05_PEAK_IMAGE_SLOTS) {
        rc = record_terminal(session, session->callback.operation_id,
                             LAB05_PEAK_SCENARIO_STALE_CALLBACK, LAB05_PEAK_CONTROL_PERSISTENT,
                             LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_STALE, "stale");
        depth_leave(session);
        return rc;
    }
    slot = &session->images[handle.slot];
    if (slot->generation != handle.generation) {
        rc = record_terminal(session, session->callback.operation_id,
                             LAB05_PEAK_SCENARIO_STALE_CALLBACK, LAB05_PEAK_CONTROL_PERSISTENT,
                             LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_STALE, "stale");
        depth_leave(session);
        return rc;
    }
    if (!slot->live || slot->pixels == NULL) {
        rc = record_terminal(session, session->callback.operation_id,
                             LAB05_PEAK_SCENARIO_STALE_CALLBACK, LAB05_PEAK_CONTROL_PERSISTENT,
                             LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_STALE, "stale");
        depth_leave(session);
        return rc;
    }
    slot->pixels[0] = session->callback.write_value;
    slot->writes++;
    rc = record_terminal(session, session->callback.operation_id,
                         LAB05_PEAK_SCENARIO_STALE_CALLBACK, LAB05_PEAK_CONTROL_PERSISTENT,
                         LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_SUCCESS, "done");
    depth_leave(session);
    return rc;
}

int lab05_peak_set_terminal(lab05_peak_session *session, uint64_t operation_id,
                            lab05_peak_scenario scenario, lab05_peak_control control,
                            lab05_peak_op_kind kind, lab05_peak_terminal terminal,
                            const char *reason)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = record_terminal(session, operation_id, scenario, control, kind, terminal, reason);
    depth_leave(session);
    return rc;
}

const lab05_peak_operation *lab05_peak_find_operation(const lab05_peak_session *session,
                                                      uint64_t operation_id)
{
    int index;
    if (session == NULL) {
        return NULL;
    }
    index = find_op_index(session, operation_id);
    if (index < 0) {
        return NULL;
    }
    return &session->ops[index];
}

int lab05_peak_session_apply_transition(lab05_peak_session *session, lab05_peak_trigger trigger,
                                        uint64_t operation_id)
{
    int rc = -1;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (trigger == LAB05_PEAK_TRIGGER_COVER_DOWN) {
        rc = begin_cover(session, operation_id);
    } else if (trigger == LAB05_PEAK_TRIGGER_COMPLETE) {
        if (session->animation_active &&
            session->animation_elapsed_ms >= LAB05_PEAK_COVER_MS &&
            session->stage == LAB05_PEAK_STAGE_COVER_DOWN) {
            if (!terminal_slot_available(session, session->animation_operation_id)) {
                rc = -1;
            } else {
                session->incoming_y = LAB05_PEAK_INCOMING_Y_END;
                finish_animation_running(session);
                /* Completed cover is steady. Clear elapsed so a later
                 * scenario does not observe a cover that already finished.
                 * Cancel keeps its mid-cover elapsed as the stop position.
                 */
                session->animation_elapsed_ms = 0;
                rc = record_terminal(session, session->animation_operation_id,
                                     LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_CONTROL_PERSISTENT,
                                     LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_SUCCESS, "ok");
                if (rc != 0 && session->has_incoming) {
                    (void)delete_image_internal(session, session->incoming);
                }
            }
        }
    } else if (trigger == LAB05_PEAK_TRIGGER_CANCEL) {
        if (session->animation_active &&
            session->animation_elapsed_ms < LAB05_PEAK_COVER_MS &&
            session->stage == LAB05_PEAK_STAGE_COVER_DOWN) {
            if (!terminal_slot_available(session, session->animation_operation_id)) {
                rc = -1;
            } else {
                if (session->has_incoming) {
                    delete_image_internal(session, session->incoming);
                }
                finish_animation_running(session);
                rc = record_terminal(session, session->animation_operation_id,
                                     LAB05_PEAK_SCENARIO_CANCEL, LAB05_PEAK_CONTROL_PERSISTENT,
                                     LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_CANCELLED,
                                     "cancel");
            }
            (void)operation_id;
        }
    }
    depth_leave(session);
    return rc;
}

int lab05_peak_animation_advance(lab05_peak_session *session, unsigned elapsed_ms)
{
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (!session->animation_active) {
        depth_leave(session);
        return -1;
    }
    if (elapsed_ms > LAB05_PEAK_COVER_MS) {
        elapsed_ms = LAB05_PEAK_COVER_MS;
    }
    session->animation_elapsed_ms = elapsed_ms;
    session->incoming_y = LAB05_PEAK_INCOMING_Y_START -
                          (int)((LAB05_PEAK_INCOMING_Y_START * (int)elapsed_ms) / (int)LAB05_PEAK_COVER_MS);
    depth_leave(session);
    return 0;
}

int lab05_peak_queue_push_audio(lab05_peak_session *session, uint64_t operation_id)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = push_queue(session, &session->audio_stop, operation_id);
    depth_leave(session);
    return rc;
}

int lab05_peak_queue_push_page(lab05_peak_session *session, uint64_t operation_id)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = push_queue(session, &session->page_apply, operation_id);
    depth_leave(session);
    return rc;
}

int lab05_peak_drain_workers(lab05_peak_session *session, lab05_peak_scenario scenario,
                             lab05_peak_control control)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = drain_internal(session, scenario, control);
    depth_leave(session);
    return rc;
}

int lab05_peak_on_worker_result(lab05_peak_session *session, uint64_t operation_id,
                                lab05_peak_scenario scenario, lab05_peak_control control,
                                lab05_peak_terminal terminal, const char *reason,
                                int replace_stage, int clear_partial)
{
    int rc = 0;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (replace_stage || clear_partial) {
        session->writer_rejects++;
    }
    if (operation_id != 0) {
        rc = record_terminal(session, operation_id, scenario, control, LAB05_PEAK_OP_WORKER,
                             terminal, reason);
    }
    depth_leave(session);
    return rc;
}

int lab05_peak_apply_summary_page(lab05_peak_session *session, unsigned record_count)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    rc = apply_page_internal(session, record_count);
    depth_leave(session);
    return rc;
}

int lab05_peak_submit_pixels(lab05_peak_session *session, uint64_t operation_id, size_t bytes)
{
    int rc;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (bytes > LAB05_PEAK_MAXIMUM_INPUT_SIZE) {
        rc = record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_OVERLAP,
                             LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_OTHER,
                             LAB05_PEAK_TERMINAL_FAILURE, "input_too_large");
        depth_leave(session);
        return rc == 0 ? -1 : rc;
    }
    rc = record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_OVERLAP,
                         LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_OTHER,
                         LAB05_PEAK_TERMINAL_SUCCESS, "ok");
    depth_leave(session);
    return rc;
}

int lab05_peak_try_begin_operation(lab05_peak_session *session)
{
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (session->running_operations >= LAB05_PEAK_MAX_CONCURRENT_OPERATIONS) {
        depth_leave(session);
        return -1;
    }
    session->running_operations++;
    depth_leave(session);
    return 0;
}

void lab05_peak_end_operation(lab05_peak_session *session)
{
    if (session == NULL || depth_enter(session) != 0) {
        return;
    }
    if (session->running_operations > 0) {
        session->running_operations--;
    }
    depth_leave(session);
}

int lab05_peak_run_overlap(lab05_peak_session *session, unsigned repetitions)
{
    unsigned i;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (repetitions > LAB05_PEAK_OVERLAP_REPETITIONS) {
        depth_leave(session);
        return -1;
    }
    for (i = 0; i < repetitions; i++) {
        uint64_t rep_id = session->next_operation_id++;
        uint64_t audio_id = session->next_operation_id++;
        uint64_t page_id = session->next_operation_id++;
        lab05_peak_handle incoming;
        if (lab05_peak_session_apply_transition(session, LAB05_PEAK_TRIGGER_COVER_DOWN, rep_id) != 0) {
            depth_leave(session);
            return -1;
        }
        if (alloc_image_internal(session, 1u, &incoming) != 0) {
            finish_animation_running(session);
            (void)record_terminal(session, rep_id, LAB05_PEAK_SCENARIO_OVERLAP,
                                  LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                                  LAB05_PEAK_TERMINAL_FAILURE, "alloc");
            depth_leave(session);
            return -1;
        }
        session->has_incoming = 1;
        session->incoming = incoming;
        if (lab05_peak_animation_advance(session, LAB05_PEAK_COVER_MS) != 0) {
            depth_leave(session);
            return -1;
        }
        if (push_queue(session, &session->audio_stop, audio_id) != 0 ||
            push_queue(session, &session->page_apply, page_id) != 0) {
            depth_leave(session);
            return -1;
        }
        notify_peak(session, LAB05_PEAK_SCENARIO_OVERLAP, rep_id);
        if (drain_internal(session, LAB05_PEAK_SCENARIO_OVERLAP, LAB05_PEAK_CONTROL_PERSISTENT) != 0) {
            depth_leave(session);
            return -1;
        }
        if (lab05_peak_session_apply_transition(session, LAB05_PEAK_TRIGGER_COMPLETE, rep_id) != 0) {
            depth_leave(session);
            return -1;
        }
        if (session->has_incoming) {
            if (delete_image_internal(session, session->incoming) != 0) {
                depth_leave(session);
                return -1;
            }
        }
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_overlap_completeness(const lab05_peak_session *session, unsigned expected,
                                    unsigned *missing, unsigned *duplicate_ids, unsigned *extra)
{
    unsigned count = 0;
    unsigned dup = 0;
    size_t i;
    size_t j;
    if (missing != NULL) {
        *missing = 0;
    }
    if (duplicate_ids != NULL) {
        *duplicate_ids = 0;
    }
    if (extra != NULL) {
        *extra = 0;
    }
    if (session == NULL) {
        return -1;
    }
    for (i = 0; i < session->op_count; i++) {
        const lab05_peak_operation *op = &session->ops[i];
        if (op->scenario != LAB05_PEAK_SCENARIO_OVERLAP || op->kind != LAB05_PEAK_OP_REPETITION) {
            continue;
        }
        count++;
        if (op->terminal != LAB05_PEAK_TERMINAL_SUCCESS || op->terminal_count != 1u) {
            return -1;
        }
        for (j = 0; j < i; j++) {
            if (session->ops[j].operation_id == op->operation_id) {
                dup++;
            }
        }
    }
    if (session->duplicate_terminal_rejects > 0) {
        dup += session->duplicate_terminal_rejects;
    }
    if (missing != NULL) {
        *missing = (count < expected) ? (expected - count) : 0u;
    }
    if (extra != NULL) {
        *extra = (count > expected) ? (count - expected) : 0u;
    }
    if (duplicate_ids != NULL) {
        *duplicate_ids = dup;
    }
    if (count != expected || dup != 0) {
        return -1;
    }
    return 0;
}

int lab05_peak_run_replace(lab05_peak_session *session, uint64_t operation_id, int inject_alloc_fail,
                           lab05_peak_handle *old_out)
{
    lab05_peak_handle old_handle;
    lab05_peak_handle incoming;
    uint64_t audio_id;
    uint64_t page_id;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    old_handle = session->steady;
    if (old_out != NULL) {
        *old_out = old_handle;
    }
    if (delete_image_internal(session, old_handle) != 0) {
        depth_leave(session);
        return -1;
    }
    if (inject_alloc_fail) {
        session->inject_alloc_fail = 1;
    }
    if (alloc_image_internal(session, 1u, &incoming) != 0) {
        notify_peak(session, LAB05_PEAK_SCENARIO_REPLACE, operation_id);
        record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_REPLACE, LAB05_PEAK_CONTROL_REPLACE,
                        LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_FAILURE, "alloc");
        depth_leave(session);
        return 0;
    }
    session->steady = incoming;
    notify_peak(session, LAB05_PEAK_SCENARIO_REPLACE, operation_id);
    audio_id = session->next_operation_id++;
    page_id = session->next_operation_id++;
    if (push_queue(session, &session->audio_stop, audio_id) != 0 ||
        push_queue(session, &session->page_apply, page_id) != 0 ||
        drain_internal(session, LAB05_PEAK_SCENARIO_REPLACE, LAB05_PEAK_CONTROL_REPLACE) != 0) {
        depth_leave(session);
        return -1;
    }
    if (record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_REPLACE, LAB05_PEAK_CONTROL_REPLACE,
                        LAB05_PEAK_OP_REPETITION, LAB05_PEAK_TERMINAL_SUCCESS, "ok") != 0) {
        depth_leave(session);
        return -1;
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_run_alloc_fail(lab05_peak_session *session, uint64_t operation_id,
                              lab05_peak_handle *old_out)
{
    lab05_peak_handle old_handle;
    lab05_peak_handle incoming;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    old_handle = session->steady;
    if (old_out != NULL) {
        *old_out = old_handle;
    }
    if (delete_image_internal(session, old_handle) != 0) {
        depth_leave(session);
        return -1;
    }
    session->inject_alloc_fail = 1;
    if (alloc_image_internal(session, 1u, &incoming) == 0) {
        depth_leave(session);
        return -1;
    }
    notify_peak(session, LAB05_PEAK_SCENARIO_ALLOC_FAIL, operation_id);
    if (record_terminal(session, operation_id, LAB05_PEAK_SCENARIO_ALLOC_FAIL,
                        LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                        LAB05_PEAK_TERMINAL_FAILURE, "alloc") != 0) {
        depth_leave(session);
        return -1;
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_run_stale_callback(lab05_peak_session *session, uint64_t operation_id,
                                  lab05_peak_handle *old_out)
{
    lab05_peak_handle handle;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    handle = session->steady;
    if (old_out != NULL) {
        *old_out = handle;
    }
    if (lab05_peak_queue_callback(session, handle, operation_id, 0x5Au) != 0) {
        depth_leave(session);
        return -1;
    }
    if (delete_image_internal(session, handle) != 0) {
        depth_leave(session);
        return -1;
    }
    notify_peak(session, LAB05_PEAK_SCENARIO_STALE_CALLBACK, operation_id);
    if (lab05_peak_deliver_callback(session) != 0) {
        depth_leave(session);
        return -1;
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_run_cancel(lab05_peak_session *session, uint64_t animation_id, uint64_t audio_id,
                          uint64_t page_id)
{
    lab05_peak_handle incoming;
    if (session == NULL || depth_enter(session) != 0) {
        return -1;
    }
    if (lab05_peak_session_apply_transition(session, LAB05_PEAK_TRIGGER_COVER_DOWN, animation_id) != 0) {
        depth_leave(session);
        return -1;
    }
    if (alloc_image_internal(session, 1u, &incoming) != 0) {
        finish_animation_running(session);
        (void)record_terminal(session, animation_id, LAB05_PEAK_SCENARIO_CANCEL,
                              LAB05_PEAK_CONTROL_PERSISTENT, LAB05_PEAK_OP_REPETITION,
                              LAB05_PEAK_TERMINAL_FAILURE, "alloc");
        depth_leave(session);
        return -1;
    }
    session->has_incoming = 1;
    session->incoming = incoming;
    if (lab05_peak_animation_advance(session, LAB05_PEAK_COVER_MS / 2u) != 0) {
        depth_leave(session);
        return -1;
    }
    if (push_queue(session, &session->audio_stop, audio_id) != 0 ||
        push_queue(session, &session->page_apply, page_id) != 0) {
        depth_leave(session);
        return -1;
    }
    notify_peak(session, LAB05_PEAK_SCENARIO_CANCEL, animation_id);
    if (drain_internal(session, LAB05_PEAK_SCENARIO_CANCEL, LAB05_PEAK_CONTROL_PERSISTENT) != 0) {
        depth_leave(session);
        return -1;
    }
    if (lab05_peak_session_apply_transition(session, LAB05_PEAK_TRIGGER_CANCEL, animation_id) != 0) {
        depth_leave(session);
        return -1;
    }
    depth_leave(session);
    return 0;
}

int lab05_peak_format_trace(const lab05_peak_session *session, uint64_t operation_id, char *buf,
                            size_t cap)
{
    const lab05_peak_operation *op;
    const char *class_name = "none";
    int wrote;
    if (session == NULL || buf == NULL || cap < 64u) {
        return -1;
    }
    op = lab05_peak_find_operation(session, operation_id);
    if (op == NULL) {
        return -1;
    }
    if (op->scenario == LAB05_PEAK_SCENARIO_OVERLAP || op->scenario == LAB05_PEAK_SCENARIO_REPLACE) {
        class_name = "exploratory";
    } else if (lab05_peak_scenario_is_experiment_input(op->scenario)) {
        class_name = "decided";
    }
    wrote = snprintf(
        buf, cap,
        "LAB05_PEAK_TRACE schema=lab05-peak-trace/v1 scenario=%s kind=%s control=%s terminal=%s "
        "operation_id=%llu terminal_count=%u live_image_count=%u class=%s reason=%s "
        "temp_image_bytes=%u lvgl_high_water_bytes=unmeasured lvgl_largest_free_bytes=unmeasured "
        "internal_largest_free_bytes=unmeasured psram_largest_free_bytes=unmeasured "
        "anim_frame_us=unmeasured metrics_provenance=unmeasured\n",
        lab05_peak_scenario_name(op->scenario), lab05_peak_op_kind_name(op->kind),
        lab05_peak_control_name(op->control),
        lab05_peak_terminal_name(op->terminal), (unsigned long long)op->operation_id,
        op->terminal_count, op->live_image_count, class_name, op->reason, op->temp_image_bytes);
    if (wrote < 0 || (size_t)wrote >= cap) {
        return -1;
    }
    return 0;
}

#endif /* CONFIG_LAB05_PEAK */
