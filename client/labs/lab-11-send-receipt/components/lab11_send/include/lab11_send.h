#ifndef LAB11_SEND_H
#define LAB11_SEND_H

#include "lab11_capture.h"
#include "lab11_sha256.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAB11_PART_QUEUE_CAPACITY 8u
#define LAB11_CHUNK_1024 1024u
#define LAB11_CHUNK_4096 4096u
#define LAB11_RESPONSE_CAP 512u
#define LAB11_MSG_LEN 40
#define LAB11_WRITER_ID 11u

typedef enum {
    LAB11_COND_ARCHIVE = 1,
    LAB11_COND_PUBLISH = 2
} lab11_condition_t;

typedef enum {
    LAB11_PHASE_IDLE = 0,
    LAB11_PHASE_BEFORE_BYTE,
    LAB11_PHASE_UPLOADING,
    LAB11_PHASE_CHECKING,
    LAB11_PHASE_LOOKUP,
    LAB11_PHASE_TERMINAL
} lab11_phase_t;

typedef enum {
    LAB11_TERM_NONE = 0,
    LAB11_TERM_SUCCESS,
    LAB11_TERM_FAILURE,
    LAB11_TERM_CANCELLED,
    LAB11_TERM_STALE,
    LAB11_TERM_TIMED_OUT
} lab11_terminal_t;

typedef enum {
    LAB11_LEVEL_PROPERTY = 0,
    LAB11_LEVEL_SUBTREE = 1,
    LAB11_LEVEL_SCENE = 2
} lab11_level_t;

typedef enum {
    LAB11_STEP_PARTS = 0,
    LAB11_STEP_COMPLETE,
    LAB11_STEP_LOOKUP,
    LAB11_STEP_PUBLISH,
    LAB11_STEP_PUSH,
    LAB11_STEP_STATUS,
    LAB11_STEP_STOP
} lab11_step_t;

typedef enum {
    LAB11_EV_NONE = 0,
    LAB11_EV_PART,
    LAB11_EV_LOST,
    LAB11_EV_COMPLETE,
    LAB11_EV_LOOKUP,
    LAB11_EV_PUBLISH,
    LAB11_EV_PUSH,
    LAB11_EV_STATUS,
    LAB11_EV_CONFLICT,
    LAB11_EV_STORAGE,
    LAB11_EV_CANCEL,
    LAB11_EV_OFFLINE,
    LAB11_EV_HTTP_FAIL,
    LAB11_EV_INVALID
} lab11_event_kind_t;

typedef struct {
    uint32_t sequence;
    uint32_t offset;
    uint32_t length;
} lab11_part_desc_t;

typedef struct {
    char method[8];
    char path[96];
    const uint8_t *body;
    uint32_t body_len;
    char content_sha256_hex[65];
    char client_message_id[LAB11_MSG_LEN];
    uint32_t operation_id;
    uint32_t sequence;
    uint32_t offset;
    uint32_t chunk_bytes;
    char condition[40];
    uint32_t recipient_count;
} lab11_request_t;

typedef struct {
    int http_status;
    char body[LAB11_RESPONSE_CAP];
    size_t body_len;
    bool lost;
    bool transport_error;
} lab11_response_t;

typedef struct {
    int (*exchange)(void *ctx, const lab11_request_t *request, lab11_response_t *response);
    void *ctx;
    bool copies_body;
} lab11_transport_t;

typedef struct {
    bool ok;
    char result[32];
    uint32_t sequence;
    uint32_t durable_parts;
    uint32_t accepted;
    uint32_t message_count;
    uint32_t complete_posts;
    uint32_t recipient_count;
    char receipt_id[LAB11_MSG_LEN];
    char push_result[16];
    char status[16];
    char resumed[32];
} lab11_parsed_t;

typedef struct {
    lab11_event_kind_t kind;
    uint32_t operation_id;
    uint32_t sequence;
    bool cancel_during_lookup;
    lab11_parsed_t parsed;
} lab11_worker_event_t;

typedef struct {
    uint32_t operation_id;
    lab11_terminal_t terminal;
    char reason[24];
    uint8_t terminal_count;
    uint32_t accepted;
} lab11_result_t;

typedef struct {
    uint32_t writer_id;
    uint32_t scene_generation;
    char progress_text[80];
    char phase_text[80];
    char expected_text[120];
    char active_scenario[32];
    char displayed_condition[48];
    uint32_t property_updates;
    int progress_level;
    atomic_int cancel_requested;
    uint32_t exchange_count;

    lab11_part_desc_t queue[LAB11_PART_QUEUE_CAPACITY];
    size_t queue_count;
    uint32_t overflow_count;

    lab11_capture_t *capture;
    lab11_borrow_t borrow;
    bool open;
    uint32_t operation_id;
    char client_message_id[LAB11_MSG_LEN];
    lab11_condition_t condition;
    char condition_name[40];
    uint32_t chunk_bytes;
    uint8_t frozen_sha[LAB11_SHA256_LEN];
    size_t frozen_len;
    char frozen_recipients[LAB11_MAX_RECIPIENTS][LAB11_RECIPIENT_LEN];
    size_t frozen_recipient_count;
    uint32_t sketch_len;
    uint32_t pcm_len;
    uint8_t payload_byte0;

    uint8_t *staging;
    uint8_t *response_buf;
    size_t part_total;
    bool sequence_done[16];
    uint32_t durable_unique;
    uint32_t server_durable;
    uint32_t server_accepted;
    uint32_t message_count;
    uint32_t complete_posts;
    uint32_t lookup_count;
    bool receipt_parsed;
    char receipt_id[LAB11_MSG_LEN];
    bool published;
    char push_result[16];
    bool identity_conflict;
    uint32_t retry_same_sequence;
    bool lookup_same;
    bool remote_rollback;
    uint32_t late_responses;
    bool discarded_explicit;
    char work[16];
    char restart_status[16];
    char resumed_sequences[32];
    bool success_visible;
    lab11_phase_t phase;
    lab11_step_t step;
    lab11_terminal_t terminal;
    char reason[24];
    uint8_t terminal_count;
    char cancel_phase[24];
    bool cancel_during_lookup;
    bool corrupt_next;
    bool station_up;
    bool copies_body;
    size_t retained_input_bytes;
    size_t staging_bytes;
    size_t transport_bytes;
    size_t transport_view_bytes;
    size_t retry_extra_bytes;
    size_t response_bytes;
    size_t full_payload_duplicate_bytes;
    size_t peak_calculated_bytes;
    uint32_t live_buffers;
    bool reconcile_ok;
    bool follow_push;
    uint32_t monotonic_us;
    lab11_result_t results[16];
    size_t result_count;
    char control_body[160];
} lab11_session_t;

typedef void (*lab11_trace_fn)(void *ctx, const char *line);

typedef struct {
    const char *active_condition;
    const char *offline_work;
    const char *restart_status;
    uint32_t resumed0;
    uint32_t resumed1;
    bool copies_body;
} lab11_script_input_t;

void lab11_session_init(lab11_session_t *session, uint32_t writer_id);
bool lab11_set_active_scenario(lab11_session_t *session, uint32_t writer, const char *scenario_id);
bool lab11_set_chunk_probe(lab11_session_t *session, uint32_t writer, uint32_t chunk_bytes);
bool lab11_set_progress(lab11_session_t *session, uint32_t writer, const char *text, lab11_level_t level);
bool lab11_request_cancel(lab11_session_t *session, uint32_t operation_id);
bool lab11_op_begin(lab11_session_t *session, lab11_capture_t *capture, uint32_t writer,
                    uint32_t operation_id, const char *client_message_id,
                    lab11_condition_t condition, uint32_t chunk_bytes, bool copies_body);
bool lab11_admit_part(lab11_session_t *session, uint32_t writer, const lab11_part_desc_t *desc);
bool lab11_plan_parts(lab11_session_t *session, uint32_t writer);
int lab11_worker_exchange(lab11_session_t *session, lab11_transport_t *transport, lab11_worker_event_t *out);
bool lab11_writer_apply(lab11_session_t *session, uint32_t writer, const lab11_worker_event_t *event);
bool lab11_try_park_checking(lab11_session_t *session);
bool lab11_checking_without_terminal(const lab11_session_t *session);
bool lab11_post_control(lab11_session_t *session, lab11_transport_t *transport, const char *json);
bool lab11_run_scripted(lab11_session_t *session, lab11_capture_t *capture, lab11_transport_t *transport,
                        const lab11_script_input_t *input, lab11_trace_fn trace, void *ctx);
const char *lab11_expected_result(const char *scenario_id);
const char *lab11_terminal_name(lab11_terminal_t terminal);
lab11_condition_t lab11_condition_from_name(const char *name);
size_t lab11_format_boot(char *out, size_t out_len, bool display_ok, const char *build,
                         const char *mode, int harness);
size_t lab11_format_harness(char *out, size_t out_len, bool ok);
bool lab11_parse_response(const char *json, lab11_parsed_t *out);

#ifdef __cplusplus
}
#endif
#endif
