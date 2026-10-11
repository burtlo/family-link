#pragma once

#include "lab02/lab02_session.h"
#include "lab02/inbox_model.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int page_index;
    uint32_t op_id;
    uint32_t session_id;
    int64_t submitted_us;
} lab02_fetch_req_t;

bool lab02_session_http_take_fetch_request(lab02_fetch_req_t *out);
void lab02_session_http_mark_stage(lab02_load_stage_t stage, uint32_t session_id);
uint32_t lab02_session_generation(void);
uint16_t lab02_session_materialized_count(void);

#ifdef __cplusplus
}
#endif
