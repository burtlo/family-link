#pragma once

#include "sdkconfig.h"

#include <cstdint>

namespace product_mem
{

/* Phase-tagged memory samples for desk repros (CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC).
 * probe reads the LVGL pool and must run on the LVGL task.
 * probe_heap is safe from any task. Both compile out when off. */
#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
void probe(const char *phase, int32_t detail);
void probe_heap(const char *phase, int32_t detail);
#else
inline void probe(const char *phase, int32_t detail)
{
    (void)phase;
    (void)detail;
}
inline void probe_heap(const char *phase, int32_t detail)
{
    (void)phase;
    (void)detail;
}
#endif

} /* namespace product_mem */
