#pragma once

#include "lab02/lab02_session.h"
#include "lab02/inbox_model.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Frozen at owner admission. Cancellation is owner delivery-ledger state. */
typedef struct {
    int page_index;
    uint32_t op_id;
    uint32_t session_id;
    uint32_t revision;
    uint16_t page_size;
    int page_offset;
    uint32_t attempt;
    int64_t submitted_us;
    /** Response-read budget after headers; not a whole-attempt deadline. */
    int64_t response_read_timeout_us;
} lab02_fetch_req_t;

/** One startup availability report; applied only by the session pump. */
void lab02_session_http_worker_started(bool available, uint32_t session_id);

bool lab02_session_http_initial_request(lab02_fetch_req_t *out);
/** Lifecycle broker only: consumes one immutable admitted owner restart. */
bool lab02_session_http_take_initial_restart(lab02_fetch_req_t *out);
bool lab02_session_http_take_fetch_request(lab02_fetch_req_t *out);
void lab02_session_http_mark_stage(lab02_load_stage_t stage, uint32_t session_id);
uint32_t lab02_session_generation(void);
uint16_t lab02_session_materialized_count(void);

#ifdef __cplusplus
}
#endif
