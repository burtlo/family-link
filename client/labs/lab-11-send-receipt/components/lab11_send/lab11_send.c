#include "lab11_send.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool same_writer(const lab11_session_t *session, uint32_t writer)
{
    return session != NULL && writer == session->writer_id;
}

static void set_phase_text(lab11_session_t *session, const char *text)
{
    if (text == NULL || strstr(text, "Sent") != NULL) {
        text = "open";
    }
    snprintf(session->phase_text, sizeof(session->phase_text), "%s", text);
}

static bool json_uint(const char *json, const char *key, uint32_t *out)
{
    char pattern[48];
    const char *found;
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    found = strstr(json, pattern);
    if (found == NULL) {
        return false;
    }
    *out = (uint32_t)strtoul(found + strlen(pattern), NULL, 10);
    return true;
}

static bool json_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[48];
    const char *found;
    size_t i = 0;
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    found = strstr(json, pattern);
    if (found == NULL || out_len == 0) {
        return false;
    }
    found += strlen(pattern);
    while (found[i] != '\0' && found[i] != '"' && i + 1u < out_len) {
        out[i] = found[i];
        i++;
    }
    out[i] = '\0';
    return found[i] == '"';
}

bool lab11_parse_response(const char *json, lab11_parsed_t *out)
{
    if (json == NULL || out == NULL || json[0] == '\0') {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!json_string(json, "result", out->result, sizeof(out->result))) {
        return false;
    }
    json_uint(json, "sequence", &out->sequence);
    json_uint(json, "durable_parts", &out->durable_parts);
    json_uint(json, "accepted", &out->accepted);
    json_uint(json, "message_count", &out->message_count);
    json_uint(json, "complete_posts", &out->complete_posts);
    json_uint(json, "recipient_count", &out->recipient_count);
    json_string(json, "receipt_id", out->receipt_id, sizeof(out->receipt_id));
    json_string(json, "push_result", out->push_result, sizeof(out->push_result));
    json_string(json, "status", out->status, sizeof(out->status));
    json_string(json, "resumed_sequences", out->resumed, sizeof(out->resumed));
    out->ok = true;
    return true;
}

const char *lab11_terminal_name(lab11_terminal_t terminal)
{
    switch (terminal) {
    case LAB11_TERM_SUCCESS:
        return "success";
    case LAB11_TERM_FAILURE:
        return "failure";
    case LAB11_TERM_CANCELLED:
        return "cancelled";
    case LAB11_TERM_STALE:
        return "stale";
    case LAB11_TERM_TIMED_OUT:
        return "timed_out";
    case LAB11_TERM_NONE:
    default:
        return "none";
    }
}

lab11_condition_t lab11_condition_from_name(const char *name)
{
    if (name != NULL && strcmp(name, "accept-on-recipient-publish") == 0) {
        return LAB11_COND_PUBLISH;
    }
    return LAB11_COND_ARCHIVE;
}

const char *lab11_expected_result(const char *scenario_id)
{
    if (scenario_id == NULL) {
        return "none";
    }
    if (strcmp(scenario_id, "D-one") == 0) {
        return "success only when server accepted is 1 under the active condition";
    }
    if (strcmp(scenario_id, "D-drop-ack") == 0) {
        return "retry keeps the durable part count at unique sequences";
    }
    if (strcmp(scenario_id, "D-drop-receipt") == 0) {
        return "lookup returns the same receipt and accepted stays 1";
    }
    if (strcmp(scenario_id, "D-full") == 0) {
        return "one storage failure and send buffers released";
    }
    if (strcmp(scenario_id, "D-cancel") == 0) {
        return "four operations; local cancel is not a remote rollback";
    }
    if (strcmp(scenario_id, "D-many") == 0) {
        return "exploratory record of three recipients";
    }
    if (strcmp(scenario_id, "D-invalid-recipient") == 0) {
        return "exploratory invalid recipient; not a product outcome";
    }
    if (strcmp(scenario_id, "D-offline") == 0) {
        return "exploratory offline work copied from the fixture";
    }
    if (strcmp(scenario_id, "D-push-fail") == 0) {
        return "exploratory push result kept separate from accept";
    }
    if (strcmp(scenario_id, "D-restart") == 0) {
        return "exploratory status and resumed sequences; recovery stays open";
    }
    return "unknown scenario";
}

size_t lab11_format_boot(char *out, size_t out_len, bool display_ok, const char *build,
                         const char *mode, int harness)
{
    if (out == NULL || out_len == 0) {
        return 0;
    }
    if (!display_ok) {
        return (size_t)snprintf(out, out_len, "LAB11_INIT_FAILED status=display_start_failed");
    }
    return (size_t)snprintf(out, out_len,
                            "LAB11_READY build=%s mode=%s fixture=lab11-send/v1 harness=%d",
                            build != NULL ? build : "missing",
                            mode != NULL ? mode : "manual",
                            harness ? 1 : 0);
}

size_t lab11_format_harness(char *out, size_t out_len, bool ok)
{
    if (out == NULL || out_len == 0) {
        return 0;
    }
    return (size_t)snprintf(out, out_len, "LAB11_HARNESS_DONE status=%s", ok ? "complete" : "failure");
}

void lab11_session_init(lab11_session_t *session, uint32_t writer_id)
{
    if (session == NULL) {
        return;
    }
    memset(session, 0, sizeof(*session));
    session->writer_id = writer_id;
    session->scene_generation = 1;
    session->station_up = true;
    session->chunk_bytes = LAB11_CHUNK_1024;
    session->remote_rollback = false;
    atomic_init(&session->cancel_requested, 0);
    snprintf(session->displayed_condition, sizeof(session->displayed_condition), "accept-on-archive-commit");
    snprintf(session->phase_text, sizeof(session->phase_text), "idle");
    snprintf(session->progress_text, sizeof(session->progress_text), "idle");
    snprintf(session->work, sizeof(session->work), "none");
    snprintf(session->push_result, sizeof(session->push_result), "none");
    snprintf(session->restart_status, sizeof(session->restart_status), "none");
    snprintf(session->resumed_sequences, sizeof(session->resumed_sequences), "none");
}

bool lab11_set_chunk_probe(lab11_session_t *session, uint32_t writer, uint32_t chunk_bytes)
{
    if (!same_writer(session, writer) || session->open) {
        return false;
    }
    if (chunk_bytes != LAB11_CHUNK_1024 && chunk_bytes != LAB11_CHUNK_4096) {
        return false;
    }
    session->chunk_bytes = chunk_bytes;
    return true;
}

bool lab11_set_active_scenario(lab11_session_t *session, uint32_t writer, const char *scenario_id)
{
    if (!same_writer(session, writer) || scenario_id == NULL) {
        return false;
    }
    snprintf(session->active_scenario, sizeof(session->active_scenario), "%s", scenario_id);
    snprintf(session->expected_text, sizeof(session->expected_text), "%s", lab11_expected_result(scenario_id));
    session->property_updates += 1u;
    session->progress_level = 0;
    return true;
}

bool lab11_set_progress(lab11_session_t *session, uint32_t writer, const char *text, lab11_level_t level)
{
    uint32_t generation;
    if (!same_writer(session, writer) || text == NULL) {
        return false;
    }
    generation = session->scene_generation;
    if (level != LAB11_LEVEL_PROPERTY) {
        return false;
    }
    snprintf(session->progress_text, sizeof(session->progress_text), "%s", text);
    session->property_updates += 1u;
    session->progress_level = 0;
    if (session->scene_generation != generation) {
        return false;
    }
    return true;
}

bool lab11_request_cancel(lab11_session_t *session, uint32_t operation_id)
{
    if (session == NULL) {
        return false;
    }
    if (session->open && session->operation_id != operation_id && session->terminal_count == 0) {
        return false;
    }
    atomic_store(&session->cancel_requested, 1);
    return true;
}

static bool remember_terminal(lab11_session_t *session)
{
    lab11_result_t *row;
    size_t i;
    if (session->result_count >= 16) {
        return false;
    }
    for (i = 0; i < session->result_count; i++) {
        if (session->results[i].operation_id == session->operation_id) {
            return false;
        }
    }
    row = &session->results[session->result_count++];
    row->operation_id = session->operation_id;
    row->terminal = session->terminal;
    row->terminal_count = session->terminal_count;
    row->accepted = session->server_accepted;
    snprintf(row->reason, sizeof(row->reason), "%s", session->reason);
    return true;
}

static bool condition_met(const lab11_session_t *session)
{
    if (session->server_accepted != 1u || !session->receipt_parsed || session->message_count != 1u) {
        return false;
    }
    if (session->condition == LAB11_COND_ARCHIVE) {
        return true;
    }
    if (session->condition == LAB11_COND_PUBLISH) {
        return session->published;
    }
    return false;
}

static void release_send_buffers(lab11_session_t *session)
{
    free(session->staging);
    free(session->response_buf);
    session->staging = NULL;
    session->response_buf = NULL;
    session->live_buffers = 0;
    session->sketch_len = 0;
    session->pcm_len = 0;
    if (session->borrow.held && session->capture != NULL) {
        lab11_capture_release_borrow(session->capture, &session->borrow);
    }
}

static bool terminalize(lab11_session_t *session, lab11_terminal_t terminal, const char *reason)
{
    if (session->terminal_count != 0) {
        return false;
    }
    session->terminal = terminal;
    session->terminal_count = 1;
    snprintf(session->reason, sizeof(session->reason), "%s", reason != NULL ? reason : "none");
    session->phase = LAB11_PHASE_TERMINAL;
    session->step = LAB11_STEP_STOP;
    session->open = false;
    if (terminal == LAB11_TERM_SUCCESS && condition_met(session)) {
        session->success_visible = true;
        set_phase_text(session, "condition met");
    } else {
        session->success_visible = false;
        set_phase_text(session, session->reason);
    }
    release_send_buffers(session);
    return remember_terminal(session);
}

static void clear_operation(lab11_session_t *session)
{
    session->open = false;
    session->operation_id = 0;
    session->client_message_id[0] = '\0';
    session->part_total = 0;
    memset(session->sequence_done, 0, sizeof(session->sequence_done));
    session->durable_unique = 0;
    session->server_durable = 0;
    session->server_accepted = 0;
    session->message_count = 0;
    session->complete_posts = 0;
    session->lookup_count = 0;
    session->receipt_parsed = false;
    session->receipt_id[0] = '\0';
    session->published = false;
    snprintf(session->push_result, sizeof(session->push_result), "none");
    session->identity_conflict = false;
    session->retry_same_sequence = 0;
    session->lookup_same = false;
    session->remote_rollback = false;
    session->success_visible = false;
    session->phase = LAB11_PHASE_IDLE;
    session->step = LAB11_STEP_PARTS;
    session->terminal = LAB11_TERM_NONE;
    session->reason[0] = '\0';
    session->terminal_count = 0;
    session->cancel_phase[0] = '\0';
    session->cancel_during_lookup = false;
    session->corrupt_next = false;
    session->reconcile_ok = false;
    session->queue_count = 0;
    session->sketch_len = 0;
    session->pcm_len = 0;
    session->follow_push = false;
    atomic_store(&session->cancel_requested, 0);
    snprintf(session->work, sizeof(session->work), "none");
}

bool lab11_op_begin(lab11_session_t *session, lab11_capture_t *capture, uint32_t writer,
                    uint32_t operation_id, const char *client_message_id,
                    lab11_condition_t condition, uint32_t chunk_bytes, bool copies_body)
{
    size_t i;
    if (!same_writer(session, writer) || capture == NULL || capture->bytes == NULL || client_message_id == NULL) {
        return false;
    }
    if (session->open && session->terminal_count == 0) {
        return false;
    }
    for (i = 0; i < session->result_count; i++) {
        if (session->results[i].operation_id == operation_id) {
            return false;
        }
    }
    if (chunk_bytes != LAB11_CHUNK_1024 && chunk_bytes != LAB11_CHUNK_4096) {
        return false;
    }
    clear_operation(session);
    if (!lab11_capture_borrow(capture, operation_id, &session->borrow)) {
        return false;
    }
    session->capture = capture;
    session->staging = (uint8_t *)malloc(chunk_bytes);
    session->response_buf = (uint8_t *)malloc(LAB11_RESPONSE_CAP);
    if (session->staging == NULL || session->response_buf == NULL) {
        release_send_buffers(session);
        terminalize(session, LAB11_TERM_FAILURE, "allocation");
        return false;
    }
    session->open = true;
    session->operation_id = operation_id;
    snprintf(session->client_message_id, sizeof(session->client_message_id), "%s", client_message_id);
    session->condition = condition;
    snprintf(session->condition_name, sizeof(session->condition_name), "%s",
             condition == LAB11_COND_PUBLISH ? "accept-on-recipient-publish" : "accept-on-archive-commit");
    session->chunk_bytes = chunk_bytes;
    session->copies_body = copies_body;
    memcpy(session->frozen_sha, capture->content_sha256, LAB11_SHA256_LEN);
    session->frozen_len = capture->length;
    session->payload_byte0 = capture->bytes[0];
    session->frozen_recipient_count = capture->info.recipient_count;
    for (i = 0; i < capture->info.recipient_count && i < LAB11_MAX_RECIPIENTS; i++) {
        snprintf(session->frozen_recipients[i], LAB11_RECIPIENT_LEN, "%s", capture->info.recipients[i]);
    }
    session->sketch_len = capture->info.sketch_len;
    session->pcm_len = capture->info.pcm_len;
    session->phase = LAB11_PHASE_BEFORE_BYTE;
    session->step = LAB11_STEP_PARTS;
    session->retained_input_bytes = capture->length;
    session->staging_bytes = chunk_bytes;
    session->transport_bytes = copies_body ? chunk_bytes : 0u;
    session->transport_view_bytes = chunk_bytes;
    session->retry_extra_bytes = 0;
    session->response_bytes = LAB11_RESPONSE_CAP;
    session->full_payload_duplicate_bytes = 0;
    session->peak_calculated_bytes = session->retained_input_bytes + session->staging_bytes
        + session->transport_bytes + session->retry_extra_bytes + session->response_bytes;
    session->live_buffers = 2;
    set_phase_text(session, "before first byte");
    return true;
}

bool lab11_admit_part(lab11_session_t *session, uint32_t writer, const lab11_part_desc_t *desc)
{
    if (!same_writer(session, writer) || desc == NULL || !session->open) {
        return false;
    }
    if (session->queue_count >= LAB11_PART_QUEUE_CAPACITY) {
        session->overflow_count += 1u;
        if (session->terminal_count == 0) {
            terminalize(session, LAB11_TERM_FAILURE, "overflow");
        }
        return false;
    }
    session->queue[session->queue_count++] = *desc;
    return true;
}

bool lab11_plan_parts(lab11_session_t *session, uint32_t writer)
{
    size_t offset = 0;
    uint32_t sequence = 0;
    if (!same_writer(session, writer) || !session->open || session->capture == NULL) {
        return false;
    }
    while (offset < session->frozen_len) {
        lab11_part_desc_t desc;
        uint32_t length = (uint32_t)(session->frozen_len - offset);
        if (length > session->chunk_bytes) {
            length = session->chunk_bytes;
        }
        desc.sequence = sequence;
        desc.offset = (uint32_t)offset;
        desc.length = length;
        if (!lab11_admit_part(session, writer, &desc)) {
            return false;
        }
        offset += length;
        sequence++;
        if (sequence >= 16u) {
            return false;
        }
    }
    session->part_total = sequence;
    return sequence > 0;
}

static int call_transport(lab11_session_t *session, lab11_transport_t *transport,
                          const lab11_request_t *request, lab11_response_t *response)
{
    int rc;
    session->exchange_count += 1u;
    if (transport == NULL || transport->exchange == NULL) {
        return -1;
    }
    memset(response, 0, sizeof(*response));
    rc = transport->exchange(transport->ctx, request, response);
    if (rc != 0 || response->transport_error) {
        return -1;
    }
    return 0;
}

static void fill_identity(lab11_session_t *session, lab11_request_t *request)
{
    memset(request, 0, sizeof(*request));
    snprintf(request->client_message_id, sizeof(request->client_message_id), "%s", session->client_message_id);
    request->operation_id = session->operation_id;
    request->chunk_bytes = session->chunk_bytes;
    snprintf(request->condition, sizeof(request->condition), "%s", session->condition_name);
    request->recipient_count = (uint32_t)session->frozen_recipient_count;
}

bool lab11_post_control(lab11_session_t *session, lab11_transport_t *transport, const char *json)
{
    lab11_request_t request;
    lab11_response_t response;
    lab11_parsed_t parsed;
    if (session == NULL || json == NULL) {
        return false;
    }
    memset(&request, 0, sizeof(request));
    snprintf(request.method, sizeof(request.method), "POST");
    snprintf(request.path, sizeof(request.path), "/v1/control");
    request.body = (const uint8_t *)json;
    request.body_len = (uint32_t)strlen(json);
    if (call_transport(session, transport, &request, &response) != 0 || response.lost) {
        return false;
    }
    return lab11_parse_response(response.body, &parsed) && strcmp(parsed.result, "ok") == 0;
}

int lab11_worker_exchange(lab11_session_t *session, lab11_transport_t *transport, lab11_worker_event_t *out)
{
    lab11_request_t request;
    lab11_response_t response;
    lab11_part_desc_t desc;
    if (session == NULL || out == NULL || !session->open || session->terminal_count != 0) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->operation_id = session->operation_id;
    if (!session->station_up) {
        out->kind = LAB11_EV_OFFLINE;
        return 1;
    }
    if (atomic_load(&session->cancel_requested) && session->step == LAB11_STEP_PARTS) {
        out->kind = LAB11_EV_CANCEL;
        return 1;
    }
    if (atomic_load(&session->cancel_requested) && session->step == LAB11_STEP_LOOKUP) {
        session->cancel_during_lookup = true;
    }
    fill_identity(session, &request);
    if (session->step == LAB11_STEP_STATUS) {
        snprintf(request.method, sizeof(request.method), "GET");
        snprintf(request.path, sizeof(request.path), "/v1/status");
        if (call_transport(session, transport, &request, &response) != 0 || response.lost
            || !lab11_parse_response(response.body, &out->parsed)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        out->kind = LAB11_EV_STATUS;
        return 1;
    }
    if (session->step == LAB11_STEP_PARTS) {
        if (session->queue_count == 0) {
            session->step = LAB11_STEP_COMPLETE;
        } else {
            desc = session->queue[0];
            if (session->capture == NULL || session->staging == NULL || desc.length > session->chunk_bytes
                || desc.offset + desc.length > session->frozen_len) {
                out->kind = LAB11_EV_HTTP_FAIL;
                return 1;
            }
            memcpy(session->staging, session->capture->bytes + desc.offset, desc.length);
            if (session->corrupt_next && desc.length > 0) {
                session->staging[0] = (uint8_t)(session->staging[0] ^ 0xffu);
                session->corrupt_next = false;
            }
            lab11_sha256(session->staging, desc.length, session->response_buf);
            lab11_sha256_hex(session->response_buf, request.content_sha256_hex);
            snprintf(request.method, sizeof(request.method), "POST");
            snprintf(request.path, sizeof(request.path), "/v1/parts");
            request.body = session->staging;
            request.body_len = desc.length;
            request.sequence = desc.sequence;
            request.offset = desc.offset;
            out->sequence = desc.sequence;
            session->phase = LAB11_PHASE_UPLOADING;
            if (call_transport(session, transport, &request, &response) != 0) {
                out->kind = LAB11_EV_HTTP_FAIL;
                return 1;
            }
            if (response.lost || response.http_status == 204 || response.body_len == 0) {
                out->kind = LAB11_EV_LOST;
                return 1;
            }
            if (!lab11_parse_response(response.body, &out->parsed)) {
                out->kind = LAB11_EV_HTTP_FAIL;
                return 1;
            }
            if (response.http_status == 409 || strcmp(out->parsed.result, "identity_conflict") == 0) {
                out->kind = LAB11_EV_CONFLICT;
                return 1;
            }
            if (response.http_status == 507 || strcmp(out->parsed.result, "storage") == 0) {
                out->kind = LAB11_EV_STORAGE;
                return 1;
            }
            out->kind = LAB11_EV_PART;
            return 1;
        }
    }
    if (session->step == LAB11_STEP_COMPLETE) {
        int wrote;
        session->phase = LAB11_PHASE_CHECKING;
        wrote = snprintf(session->control_body, sizeof(session->control_body),
                         "{\"result\":\"complete\",\"client_message_id\":\"%s\",\"operation_id\":%u,"
                         "\"part_count\":%u,\"condition\":\"%s\"}",
                         session->client_message_id, (unsigned)session->operation_id,
                         (unsigned)session->part_total, session->condition_name);
        if (wrote < 0 || (size_t)wrote >= sizeof(session->control_body)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        snprintf(request.method, sizeof(request.method), "POST");
        snprintf(request.path, sizeof(request.path), "/v1/complete");
        request.body = (const uint8_t *)session->control_body;
        request.body_len = (uint32_t)strlen(session->control_body);
        session->complete_posts += 1u;
        if (call_transport(session, transport, &request, &response) != 0) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        if (response.lost || response.http_status == 204 || response.body_len == 0) {
            out->kind = LAB11_EV_LOST;
            return 1;
        }
        if (!lab11_parse_response(response.body, &out->parsed)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        if (strcmp(out->parsed.result, "invalid_recipient") == 0) {
            out->kind = LAB11_EV_INVALID;
            return 1;
        }
        if (strcmp(out->parsed.result, "storage") == 0) {
            out->kind = LAB11_EV_STORAGE;
            return 1;
        }
        out->kind = LAB11_EV_COMPLETE;
        return 1;
    }
    if (session->step == LAB11_STEP_LOOKUP) {
        session->phase = LAB11_PHASE_LOOKUP;
        snprintf(request.method, sizeof(request.method), "GET");
        snprintf(request.path, sizeof(request.path), "/v1/receipts/%s", session->client_message_id);
        session->lookup_count += 1u;
        if (call_transport(session, transport, &request, &response) != 0 || response.lost
            || !lab11_parse_response(response.body, &out->parsed)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        out->cancel_during_lookup = session->cancel_during_lookup;
        out->kind = LAB11_EV_LOOKUP;
        return 1;
    }
    if (session->step == LAB11_STEP_PUBLISH) {
        snprintf(session->control_body, sizeof(session->control_body),
                 "{\"client_message_id\":\"%s\",\"recipient_count\":%u}",
                 session->client_message_id, (unsigned)session->frozen_recipient_count);
        snprintf(request.method, sizeof(request.method), "POST");
        snprintf(request.path, sizeof(request.path), "/v1/publish");
        request.body = (const uint8_t *)session->control_body;
        request.body_len = (uint32_t)strlen(session->control_body);
        if (call_transport(session, transport, &request, &response) != 0 || response.lost
            || !lab11_parse_response(response.body, &out->parsed)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        if (strcmp(out->parsed.result, "invalid_recipient") == 0) {
            out->kind = LAB11_EV_INVALID;
            return 1;
        }
        out->kind = LAB11_EV_PUBLISH;
        return 1;
    }
    if (session->step == LAB11_STEP_PUSH) {
        snprintf(session->control_body, sizeof(session->control_body),
                 "{\"client_message_id\":\"%s\"}", session->client_message_id);
        snprintf(request.method, sizeof(request.method), "POST");
        snprintf(request.path, sizeof(request.path), "/v1/push");
        request.body = (const uint8_t *)session->control_body;
        request.body_len = (uint32_t)strlen(session->control_body);
        if (call_transport(session, transport, &request, &response) != 0 || response.lost
            || !lab11_parse_response(response.body, &out->parsed)) {
            out->kind = LAB11_EV_HTTP_FAIL;
            return 1;
        }
        out->kind = LAB11_EV_PUSH;
        return 1;
    }
    return 0;
}

static void pop_head(lab11_session_t *session)
{
    size_t i;
    if (session->queue_count == 0) {
        return;
    }
    for (i = 1; i < session->queue_count; i++) {
        session->queue[i - 1u] = session->queue[i];
    }
    session->queue_count -= 1u;
}

static bool note_late(lab11_session_t *session)
{
    session->late_responses += 1u;
    return true;
}

bool lab11_writer_apply(lab11_session_t *session, uint32_t writer, const lab11_worker_event_t *event)
{
    if (!same_writer(session, writer) || event == NULL) {
        return false;
    }
    if (session->terminal_count != 0) {
        return note_late(session);
    }
    if (event->operation_id != session->operation_id) {
        return false;
    }
    switch (event->kind) {
    case LAB11_EV_OFFLINE:
        if (strcmp(session->work, "none") == 0) {
            snprintf(session->work, sizeof(session->work), "discarded");
        }
        session->discarded_explicit = true;
        session->server_accepted = 0;
        return terminalize(session, LAB11_TERM_FAILURE, "offline");
    case LAB11_EV_CANCEL:
        session->server_accepted = 0;
        return terminalize(session, LAB11_TERM_CANCELLED, "cancelled");
    case LAB11_EV_HTTP_FAIL:
        return terminalize(session, LAB11_TERM_FAILURE, "transport");
    case LAB11_EV_STORAGE:
        session->server_accepted = 0;
        session->discarded_explicit = true;
        snprintf(session->work, sizeof(session->work), "discarded");
        return terminalize(session, LAB11_TERM_FAILURE, "storage");
    case LAB11_EV_INVALID:
        session->server_accepted = event->parsed.accepted;
        return terminalize(session, LAB11_TERM_FAILURE, "invalid_recipient");
    case LAB11_EV_LOST:
        if (session->step == LAB11_STEP_COMPLETE || session->phase == LAB11_PHASE_CHECKING) {
            session->step = LAB11_STEP_LOOKUP;
            session->phase = LAB11_PHASE_LOOKUP;
            session->success_visible = false;
            set_phase_text(session, "lookup");
            return true;
        }
        session->retry_same_sequence += 1u;
        session->phase = LAB11_PHASE_UPLOADING;
        return true;
    case LAB11_EV_CONFLICT:
        if (session->queue_count > 0 && session->queue[0].sequence == event->sequence) {
            pop_head(session);
        }
        session->identity_conflict = true;
        if (event->parsed.durable_parts != session->durable_unique && session->durable_unique != 0) {
            return terminalize(session, LAB11_TERM_FAILURE, "reconcile");
        }
        session->server_durable = event->parsed.durable_parts;
        return true;
    case LAB11_EV_PART:
        if (session->queue_count == 0 || session->queue[0].sequence != event->sequence) {
            return terminalize(session, LAB11_TERM_FAILURE, "sequence");
        }
        if (event->parsed.sequence != event->sequence || strcmp(event->parsed.result, "acked") != 0) {
            return terminalize(session, LAB11_TERM_FAILURE, "parse");
        }
        pop_head(session);
        if (event->sequence < 16u && !session->sequence_done[event->sequence]) {
            session->sequence_done[event->sequence] = true;
            session->durable_unique += 1u;
        }
        if (event->parsed.durable_parts != session->durable_unique) {
            return terminalize(session, LAB11_TERM_FAILURE, "reconcile");
        }
        session->server_durable = event->parsed.durable_parts;
        session->phase = LAB11_PHASE_UPLOADING;
        set_phase_text(session, "uploading");
        return true;
    case LAB11_EV_COMPLETE:
        session->server_durable = event->parsed.durable_parts;
        if (event->parsed.accepted == 1u && session->condition == LAB11_COND_ARCHIVE) {
            session->server_accepted = 1u;
            session->message_count = event->parsed.message_count;
            session->receipt_parsed = true;
            snprintf(session->receipt_id, sizeof(session->receipt_id), "%s", event->parsed.receipt_id);
            session->phase = LAB11_PHASE_CHECKING;
            if (!condition_met(session)) {
                session->success_visible = false;
                return terminalize(session, LAB11_TERM_FAILURE, "condition");
            }
            if (session->follow_push) {
                session->step = LAB11_STEP_PUSH;
                session->phase = LAB11_PHASE_UPLOADING;
                session->success_visible = false;
                set_phase_text(session, "awaiting push");
                return true;
            }
            return terminalize(session, LAB11_TERM_SUCCESS, "condition_met");
        }
        if (session->condition == LAB11_COND_PUBLISH && event->parsed.accepted == 0u) {
            session->success_visible = false;
            session->step = LAB11_STEP_PUBLISH;
            session->phase = LAB11_PHASE_UPLOADING;
            set_phase_text(session, "awaiting publish");
            return true;
        }
        return terminalize(session, LAB11_TERM_FAILURE, "parse");
    case LAB11_EV_LOOKUP:
        session->server_accepted = event->parsed.accepted;
        session->message_count = event->parsed.message_count;
        session->server_durable = event->parsed.durable_parts;
        if (event->parsed.accepted == 1u && strcmp(event->parsed.receipt_id, session->client_message_id) == 0) {
            session->lookup_same = true;
            session->receipt_parsed = true;
            snprintf(session->receipt_id, sizeof(session->receipt_id), "%s", event->parsed.receipt_id);
        }
        if (event->cancel_during_lookup || session->cancel_during_lookup) {
            return terminalize(session, LAB11_TERM_CANCELLED, "cancelled");
        }
        if (session->condition == LAB11_COND_PUBLISH) {
            session->step = LAB11_STEP_PUBLISH;
            session->success_visible = false;
            set_phase_text(session, "awaiting publish");
            return true;
        }
        if (!condition_met(session)) {
            return terminalize(session, LAB11_TERM_FAILURE, "condition");
        }
        return terminalize(session, LAB11_TERM_SUCCESS, "condition_met");
    case LAB11_EV_PUBLISH:
        session->published = true;
        session->server_accepted = event->parsed.accepted;
        session->message_count = event->parsed.message_count;
        if (event->parsed.accepted == 1u) {
            session->receipt_parsed = true;
            snprintf(session->receipt_id, sizeof(session->receipt_id), "%s", event->parsed.receipt_id);
        }
        if (!condition_met(session)) {
            session->success_visible = false;
            return terminalize(session, LAB11_TERM_FAILURE, "condition");
        }
        return terminalize(session, LAB11_TERM_SUCCESS, "condition_met");
    case LAB11_EV_PUSH:
        snprintf(session->push_result, sizeof(session->push_result), "%s",
                 event->parsed.push_result[0] ? event->parsed.push_result : "failed");
        if (session->terminal_count == 0 && session->server_accepted == 1u && condition_met(session)) {
            return terminalize(session, LAB11_TERM_SUCCESS, "condition_met");
        }
        return true;
    case LAB11_EV_STATUS:
        snprintf(session->restart_status, sizeof(session->restart_status), "%s", event->parsed.status);
        snprintf(session->resumed_sequences, sizeof(session->resumed_sequences), "%s", event->parsed.resumed);
        session->server_accepted = 0;
        session->success_visible = false;
        return terminalize(session, LAB11_TERM_SUCCESS, "status_recorded");
    case LAB11_EV_NONE:
    default:
        return false;
    }
}

bool lab11_try_park_checking(lab11_session_t *session)
{
    (void)session;
    return false;
}

bool lab11_checking_without_terminal(const lab11_session_t *session)
{
    if (session == NULL) {
        return false;
    }
    return session->phase == LAB11_PHASE_CHECKING && session->terminal_count == 0;
}

static bool step_once(lab11_session_t *session, lab11_transport_t *transport)
{
    lab11_worker_event_t event;
    uint32_t before;
    uint32_t generation;
    int rc;
    if (session->terminal_count != 0) {
        return false;
    }
    before = session->exchange_count;
    generation = session->scene_generation;
    if (!lab11_set_progress(session, session->writer_id, "uploading", LAB11_LEVEL_PROPERTY)) {
        return false;
    }
    if (session->exchange_count != before || session->scene_generation != generation) {
        return false;
    }
    rc = lab11_worker_exchange(session, transport, &event);
    if (rc <= 0) {
        return false;
    }
    if (!lab11_writer_apply(session, session->writer_id, &event)) {
        return false;
    }
    return !lab11_checking_without_terminal(session);
}

static bool until_terminal(lab11_session_t *session, lab11_transport_t *transport, bool conflict_once)
{
    bool conflicted = false;
    int guard;
    for (guard = 0; guard < 40 && session->terminal_count == 0; guard++) {
        if (conflict_once && !conflicted && session->step == LAB11_STEP_PARTS && session->queue_count == 0
            && session->part_total > 0 && session->durable_unique == session->part_total) {
            lab11_part_desc_t desc;
            desc.sequence = 0;
            desc.offset = 0;
            desc.length = session->chunk_bytes < session->frozen_len ? session->chunk_bytes : (uint32_t)session->frozen_len;
            session->corrupt_next = true;
            if (!lab11_admit_part(session, session->writer_id, &desc)) {
                return false;
            }
            conflicted = true;
        }
        if (!step_once(session, transport)) {
            return false;
        }
    }
    return session->terminal_count == 1 && !lab11_checking_without_terminal(session);
}

static bool reconcile_message(lab11_session_t *session, lab11_transport_t *transport)
{
    lab11_request_t request;
    lab11_response_t response;
    lab11_parsed_t parsed;
    memset(&request, 0, sizeof(request));
    snprintf(request.method, sizeof(request.method), "GET");
    snprintf(request.path, sizeof(request.path), "/v1/reconcile?message_id=%s", session->client_message_id);
    if (call_transport(session, transport, &request, &response) != 0 || response.lost) {
        return false;
    }
    if (!lab11_parse_response(response.body, &parsed)) {
        return false;
    }
    if (parsed.accepted != session->server_accepted || parsed.durable_parts != session->server_durable
        || parsed.complete_posts != session->complete_posts) {
        return false;
    }
    session->reconcile_ok = true;
    return true;
}

static void emit_line(lab11_session_t *session, lab11_trace_fn trace, void *ctx, const char *scenario,
                      const char *class_name)
{
    char line[1100];
    int discarded;
    if (trace == NULL) {
        return;
    }
    session->monotonic_us += 1u;
    discarded = strcmp(session->work, "discarded") == 0 ? 1 : 0;
    snprintf(line, sizeof(line),
             "LAB11_TRACE schema=lab11-trace/v1 event=scenario_terminal seq=%u monotonic_us=%u "
             "session_id=s-lab11 message_id=%s operation_id=%u generation=1 scenario=%s class=%s "
             "terminal=%s terminal_count=%u reason=%s accepted=%u durable_parts=%u unique_sequences=%u "
             "message_count=%u identity_conflict=%d live_buffers=%u sketch_len=%u pcm_len=%u "
             "condition=%s chunk_bytes=%u part_queue_capacity=%u scene_generation=%u progress_level=%d "
             "discarded=%d work=%s recipients=%u push_result=%s restart_status=%s resumed_sequences=%s "
             "phase=terminal queue_count=%u overflow_count=%u remote_rollback=%d mode=scripted "
             "lookup_same=%d complete_posts=%u cancel_phase=%s retry_same_sequence=%u "
             "capture_retained=%d full_payload_duplicate_bytes=%u",
             (unsigned)session->monotonic_us, (unsigned)session->monotonic_us, session->client_message_id,
             (unsigned)session->operation_id,
             scenario, class_name, lab11_terminal_name(session->terminal), (unsigned)session->terminal_count,
             session->reason[0] ? session->reason : "none", (unsigned)session->server_accepted,
             (unsigned)session->server_durable,
             (unsigned)session->durable_unique, (unsigned)session->message_count, session->identity_conflict ? 1 : 0,
             (unsigned)session->live_buffers, (unsigned)session->sketch_len, (unsigned)session->pcm_len,
             session->condition_name[0] ? session->condition_name : "none", (unsigned)session->chunk_bytes,
             (unsigned)LAB11_PART_QUEUE_CAPACITY, (unsigned)session->scene_generation, session->progress_level,
             discarded, session->work[0] ? session->work : "none",
             (unsigned)session->frozen_recipient_count,
             session->push_result[0] ? session->push_result : "none",
             session->restart_status[0] ? session->restart_status : "none",
             session->resumed_sequences[0] ? session->resumed_sequences : "none",
             (unsigned)session->queue_count, (unsigned)session->overflow_count, session->remote_rollback ? 1 : 0,
             session->lookup_same ? 1 : 0, (unsigned)session->complete_posts,
             session->cancel_phase[0] ? session->cancel_phase : "none", (unsigned)session->retry_same_sequence,
             (session->capture != NULL && session->capture->bytes != NULL) ? 1 : 0,
             (unsigned)session->full_payload_duplicate_bytes);
    trace(ctx, line);
}

static bool upload_case(lab11_session_t *session, lab11_capture_t *capture, lab11_transport_t *transport,
                        uint32_t operation_id, const char *message_id, lab11_condition_t condition,
                        uint32_t chunk, bool conflict_once, bool follow_push)
{
    if (!lab11_op_begin(session, capture, session->writer_id, operation_id, message_id, condition, chunk,
                        transport != NULL && transport->copies_body)) {
        return false;
    }
    session->follow_push = follow_push;
    if (!lab11_plan_parts(session, session->writer_id)) {
        return false;
    }
    if (session->full_payload_duplicate_bytes != 0 || session->staging_bytes != chunk
        || session->staging_bytes >= session->frozen_len || session->retry_extra_bytes != 0) {
        return false;
    }
    if (!until_terminal(session, transport, conflict_once)) {
        return false;
    }
    return reconcile_message(session, transport);
}

bool lab11_run_scripted(lab11_session_t *session, lab11_capture_t *capture, lab11_transport_t *transport,
                        const lab11_script_input_t *input, lab11_trace_fn trace, void *ctx)
{
    char saved_condition[48];
    char saved_recipient[LAB11_RECIPIENT_LEN];
    size_t saved_count;
    size_t peak1024 = 0;
    size_t stage1024 = 0;
    size_t trans1024 = 0;
    size_t peak4096 = 0;
    size_t stage4096 = 0;
    size_t trans4096 = 0;
    size_t retained = 0;
    uint8_t saved_byte;
    uint8_t saved_sha[LAB11_SHA256_LEN];
    bool ok = true;
    int i;
    if (session == NULL || capture == NULL || capture->bytes == NULL || transport == NULL || input == NULL) {
        return false;
    }
    if (input->copies_body) {
        transport->copies_body = true;
    }
    snprintf(session->displayed_condition, sizeof(session->displayed_condition), "%s",
             input->active_condition != NULL ? input->active_condition : "accept-on-archive-commit");
    snprintf(saved_condition, sizeof(saved_condition), "%s", session->displayed_condition);
    saved_byte = capture->bytes[0];
    memcpy(saved_sha, capture->content_sha256, sizeof(saved_sha));
    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }

    if (!upload_case(session, capture, transport, 1, "m-one-a", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, false)
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u || !session->success_visible
        || strstr(session->phase_text, "Sent") != NULL) {
        ok = false;
    }
    peak1024 = session->peak_calculated_bytes;
    stage1024 = session->staging_bytes;
    trans1024 = session->transport_bytes;
    retained = session->retained_input_bytes;
    snprintf(session->cancel_phase, sizeof(session->cancel_phase), "none");
    emit_line(session, trace, ctx, "D-one", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 2, "m-one-p", LAB11_COND_PUBLISH, LAB11_CHUNK_1024, false, false)
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u || !session->published
        || !session->success_visible) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-one", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 3, "m-one-b", LAB11_COND_ARCHIVE, LAB11_CHUNK_4096, false, false)
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u || !session->success_visible) {
        ok = false;
    }
    peak4096 = session->peak_calculated_bytes;
    stage4096 = session->staging_bytes;
    trans4096 = session->transport_bytes;
    emit_line(session, trace, ctx, "D-one", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"drop_next_acks\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 4, "m-ack", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, true, false)
        || !session->identity_conflict || session->retry_same_sequence < 1u
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u
        || capture->bytes[0] != saved_byte || memcmp(capture->content_sha256, saved_sha, sizeof(saved_sha)) != 0) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-drop-ack", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"drop_next_receipt\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 5, "m-rcpt", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, false)
        || !session->lookup_same || session->complete_posts != 1u || session->server_accepted != 1u
        || session->terminal != LAB11_TERM_SUCCESS || !session->reconcile_ok) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-drop-receipt", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"storage_full\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 6, "m-full", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, false)
        || session->terminal != LAB11_TERM_FAILURE || strcmp(session->reason, "storage") != 0
        || session->terminal_count != 1u || session->live_buffers != 0u || session->sketch_len != 0u
        || session->pcm_len != 0u || session->borrow.held) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-full", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!lab11_op_begin(session, capture, session->writer_id, 7, "m-c0", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)
        || !lab11_plan_parts(session, session->writer_id)) {
        return false;
    }
    {
        uint32_t before = session->exchange_count;
        if (!lab11_request_cancel(session, 7) || session->exchange_count != before || !step_once(session, transport)
            || session->terminal != LAB11_TERM_CANCELLED || session->server_accepted != 0u
            || session->exchange_count != before || !reconcile_message(session, transport)) {
            ok = false;
        }
    }
    snprintf(session->cancel_phase, sizeof(session->cancel_phase), "before_byte");
    emit_line(session, trace, ctx, "D-cancel", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!lab11_op_begin(session, capture, session->writer_id, 8, "m-c1", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)
        || !lab11_plan_parts(session, session->writer_id)) {
        return false;
    }
    for (i = 0; i < 20 && session->durable_unique < 1u && session->terminal_count == 0; i++) {
        if (!step_once(session, transport)) {
            return false;
        }
    }
    {
        uint32_t before = session->exchange_count;
        if (session->durable_unique < 1u || !lab11_request_cancel(session, 8) || session->exchange_count != before) {
            ok = false;
        }
        if (session->terminal_count == 0 && !step_once(session, transport)) {
            return false;
        }
        if (session->terminal != LAB11_TERM_CANCELLED || session->server_accepted != 0u
            || session->server_durable < 1u || session->remote_rollback || !reconcile_message(session, transport)) {
            ok = false;
        }
    }
    snprintf(session->cancel_phase, sizeof(session->cancel_phase), "mid_upload");
    emit_line(session, trace, ctx, "D-cancel", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 9, "m-c2", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, false)
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u) {
        ok = false;
    } else {
        uint32_t before = session->exchange_count;
        uint8_t count = session->terminal_count;
        if (!lab11_request_cancel(session, 9) || session->exchange_count != before || session->terminal_count != count
            || session->server_accepted != 1u || session->remote_rollback || !session->success_visible
            || !reconcile_message(session, transport)) {
            ok = false;
        }
    }
    snprintf(session->cancel_phase, sizeof(session->cancel_phase), "after_accept");
    emit_line(session, trace, ctx, "D-cancel", "decided");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"drop_next_receipt\":1}")) {
        return false;
    }
    if (!lab11_op_begin(session, capture, session->writer_id, 10, "m-c3", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)
        || !lab11_plan_parts(session, session->writer_id)) {
        return false;
    }
    for (i = 0; i < 30 && session->phase != LAB11_PHASE_LOOKUP && session->terminal_count == 0; i++) {
        if (!step_once(session, transport)) {
            return false;
        }
    }
    {
        uint32_t before = session->exchange_count;
        if (session->phase != LAB11_PHASE_LOOKUP || session->terminal_count != 0
            || !lab11_request_cancel(session, 10) || session->exchange_count != before
            || !step_once(session, transport)) {
            ok = false;
        } else if (session->terminal != LAB11_TERM_CANCELLED || session->server_accepted != 1u
                   || !session->lookup_same || session->complete_posts != 1u || session->remote_rollback
                   || session->terminal_count != 1u || !reconcile_message(session, transport)) {
            ok = false;
        }
    }
    snprintf(session->cancel_phase, sizeof(session->cancel_phase), "during_lookup");
    emit_line(session, trace, ctx, "D-cancel", "decided");

    saved_count = capture->info.recipient_count;
    snprintf(saved_recipient, sizeof(saved_recipient), "%s", capture->info.recipients[0]);
    snprintf(capture->info.recipients[0], LAB11_RECIPIENT_LEN, "kin-1");
    snprintf(capture->info.recipients[1], LAB11_RECIPIENT_LEN, "kin-2");
    snprintf(capture->info.recipients[2], LAB11_RECIPIENT_LEN, "kin-3");
    capture->info.recipient_count = 3;
    if (!lab11_post_control(session, transport, "{\"reset\":1}")) {
        return false;
    }
    if (!lab11_op_begin(session, capture, session->writer_id, 11, "m-many", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)) {
        return false;
    }
    snprintf(capture->info.recipients[0], LAB11_RECIPIENT_LEN, "changed");
    if (strcmp(session->frozen_recipients[0], "kin-1") != 0 || session->frozen_recipient_count != 3u) {
        ok = false;
    }
    snprintf(capture->info.recipients[0], LAB11_RECIPIENT_LEN, "%s", saved_recipient);
    capture->info.recipient_count = saved_count;
    if (!lab11_plan_parts(session, session->writer_id) || !until_terminal(session, transport, false)
        || !reconcile_message(session, transport)) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-many", "exploratory");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"invalid_recipient\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 12, "m-inv", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, false)
        || session->terminal != LAB11_TERM_FAILURE || session->server_accepted != 0u) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-invalid-recipient", "exploratory");

    if (!lab11_op_begin(session, capture, session->writer_id, 13, "m-off", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)) {
        return false;
    }
    snprintf(session->work, sizeof(session->work), "%s",
             input->offline_work != NULL ? input->offline_work : "discarded");
    session->station_up = false;
    {
        uint32_t before = session->exchange_count;
        if (!step_once(session, transport) || session->exchange_count != before
            || strcmp(session->work, input->offline_work != NULL ? input->offline_work : "discarded") != 0
            || !session->discarded_explicit || session->server_accepted != 0u) {
            ok = false;
        }
    }
    session->station_up = true;
    emit_line(session, trace, ctx, "D-offline", "exploratory");

    if (!lab11_post_control(session, transport, "{\"reset\":1,\"push_fail\":1}")) {
        return false;
    }
    if (!upload_case(session, capture, transport, 14, "m-push", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024, false, true)
        || session->terminal != LAB11_TERM_SUCCESS || session->server_accepted != 1u || session->terminal_count != 1u
        || strcmp(session->push_result, "failed") != 0) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-push-fail", "exploratory");

    if (!lab11_op_begin(session, capture, session->writer_id, 15, "m-restart", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)) {
        return false;
    }
    session->step = LAB11_STEP_STATUS;
    if (!step_once(session, transport) || session->server_accepted != 0u || session->success_visible
        || strcmp(session->restart_status, input->restart_status != NULL ? input->restart_status : "open") != 0
        || strcmp(session->resumed_sequences, "1,2") != 0) {
        ok = false;
    }
    emit_line(session, trace, ctx, "D-restart", "exploratory");

    if (!lab11_op_begin(session, capture, session->writer_id, 16, "m-over", LAB11_COND_ARCHIVE, LAB11_CHUNK_1024,
                        transport->copies_body)) {
        return false;
    }
    for (i = 0; i < 9; i++) {
        lab11_part_desc_t desc;
        desc.sequence = (uint32_t)i;
        desc.offset = 0;
        desc.length = 1;
        lab11_admit_part(session, session->writer_id, &desc);
    }
    if (session->terminal != LAB11_TERM_FAILURE || strcmp(session->reason, "overflow") != 0
        || session->queue_count != LAB11_PART_QUEUE_CAPACITY || session->overflow_count != 1u
        || session->terminal_count != 1u || session->live_buffers != 0u) {
        ok = false;
    } else if (trace != NULL) {
        char line[400];
        snprintf(line, sizeof(line),
                 "LAB11_TRACE schema=lab11-trace/v1 event=part_overflow seq=%u monotonic_us=%u "
                 "session_id=s-lab11 message_id=m-over operation_id=16 generation=1 "
                 "queue_count=%u overflow_count=%u dropped=0 terminal=failure terminal_count=1 "
                 "reason=overflow mode=scripted",
                 (unsigned)(session->monotonic_us + 1u), (unsigned)(session->monotonic_us + 1u),
                 (unsigned)session->queue_count, (unsigned)session->overflow_count);
        trace(ctx, line);
    }

    if (trace != NULL) {
        char line[400];
        snprintf(line, sizeof(line),
                 "LAB11_TRACE schema=lab11-trace/v1 event=resource_account seq=1 monotonic_us=1 "
                 "chunk_bytes=1024 retained_input_bytes=%u staging_bytes=%u transport_bytes=%u "
                 "transport_view_bytes=1024 retry_extra_bytes=0 response_bytes=512 "
                 "full_payload_duplicate_bytes=0 peak_calculated_bytes=%u provenance=calculated",
                 (unsigned)retained, (unsigned)stage1024, (unsigned)trans1024, (unsigned)peak1024);
        trace(ctx, line);
        snprintf(line, sizeof(line),
                 "LAB11_TRACE schema=lab11-trace/v1 event=resource_account seq=2 monotonic_us=2 "
                 "chunk_bytes=4096 retained_input_bytes=%u staging_bytes=%u transport_bytes=%u "
                 "transport_view_bytes=4096 retry_extra_bytes=0 response_bytes=512 "
                 "full_payload_duplicate_bytes=0 peak_calculated_bytes=%u provenance=calculated",
                 (unsigned)retained, (unsigned)stage4096, (unsigned)trans4096, (unsigned)peak4096);
        trace(ctx, line);
        trace(ctx, "LAB11_TRACE schema=lab11-trace/v1 event=resource_ledger seq=3 monotonic_us=3 "
                   "ledger=lvgl p50=unmeasured p95=unmeasured p99=unmeasured worst=unmeasured "
                   "largest_free_block_bytes=unmeasured provenance=unmeasured/blocked");
        trace(ctx, "LAB11_TRACE schema=lab11-trace/v1 event=resource_ledger seq=4 monotonic_us=4 "
                   "ledger=transient p50=unmeasured p95=unmeasured p99=unmeasured worst=unmeasured "
                   "largest_free_block_bytes=unmeasured provenance=unmeasured/blocked");
    }
    if (strcmp(session->displayed_condition, saved_condition) != 0 || session->scene_generation != 1u) {
        ok = false;
    }
    if (stage1024 != LAB11_CHUNK_1024 || stage4096 != LAB11_CHUNK_4096) {
        ok = false;
    }
    if (session->remote_rollback) {
        ok = false;
    }
    return ok;
}
