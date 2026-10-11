#include "lab02/instrumentation.h"

#include "esp_timer.h"
#include <string.h>

static lab02_instrumentation_t s_inst;
static uint32_t s_generation;

lab02_instrumentation_t *lab02_instrumentation_get(void)
{
    if (s_inst.session_id == 0) {
        s_inst.session_id = (uint32_t)esp_timer_get_time();
    }
    return &s_inst;
}

void lab02_instrumentation_reset(void)
{
    uint32_t sid = s_inst.session_id ? s_inst.session_id : (uint32_t)esp_timer_get_time();
    memset(&s_inst, 0, sizeof(s_inst));
    s_inst.session_id = sid;
    s_generation = 0;
}

uint32_t lab02_instrumentation_next_generation(void)
{
    return ++s_generation;
}
