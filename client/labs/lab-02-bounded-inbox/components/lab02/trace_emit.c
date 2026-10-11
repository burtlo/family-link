#include "lab02/trace_emit.h"

#include "esp_timer.h"
#include <stdio.h>

void lab02_trace_emit(const char *event, uint32_t generation, int focus_index,
                      const char *message_id, const char *extra)
{
    int64_t t_us = esp_timer_get_time();
    printf("LAB_TRACE t_us=%lld event=%s gen=%u focus=%d msg=%s",
           (long long)t_us, event ? event : "", (unsigned)generation, focus_index,
           message_id ? message_id : "-");
    if (extra && extra[0]) {
        printf(" %s", extra);
    }
    printf("\n");
}
