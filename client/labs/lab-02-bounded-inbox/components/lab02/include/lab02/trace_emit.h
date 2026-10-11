#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra);

#ifdef __cplusplus
}
#endif
