#ifndef PRODUCT_MEM_PROBE_H
#define PRODUCT_MEM_PROBE_H

#include "sdkconfig.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phase-tagged memory samples for desk repros (CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC).
 * product_mem_probe reads the LVGL pool and must run on the LVGL task.
 * product_mem_probe_heap is safe from any task. Both compile out when off. */
#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
void product_mem_probe(const char *phase, int32_t detail);
void product_mem_probe_heap(const char *phase, int32_t detail);
#else
static inline void product_mem_probe(const char *phase, int32_t detail)
{
    (void)phase;
    (void)detail;
}
static inline void product_mem_probe_heap(const char *phase, int32_t detail)
{
    (void)phase;
    (void)detail;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
