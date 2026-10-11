#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t session_id;
    uint32_t bind_count;
    uint32_t unbind_count;
    uint32_t stale_bind_drops;
    uint32_t snap_count;
} lab02_instrumentation_t;

lab02_instrumentation_t *lab02_instrumentation_get(void);
void lab02_instrumentation_reset(void);
uint32_t lab02_instrumentation_next_generation(void);

#ifdef __cplusplus
}
#endif
