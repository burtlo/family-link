#include "lab02/resource_sample.h"

#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/trace_emit.h"

#include "esp_heap_caps.h"
#include "lvgl.h"

#include <string.h>

static const char *phase_name(lab02_load_stage_t stage)
{
    switch (stage) {
    case LAB02_LOAD_CONNECTING: return "connecting";
    case LAB02_LOAD_FETCHING: return "fetching";
    case LAB02_LOAD_READY: return "ready";
    case LAB02_LOAD_ERROR: return "error";
    case LAB02_LOAD_SHELL:
    default: return "shell";
    }
}

void lab02_resource_sample_emit(const lab02_carousel_t *carousel)
{
    if (!carousel) return;

    lab02_session_resource_counts_t counts = {0};
    (void)lab02_session_resource_counts(&counts);
    lab02_session_snapshot_t session = lab02_session_snapshot();
    lv_mem_monitor_t lvgl_mem = {0};
    lv_mem_monitor(&lvgl_mem);

    uint32_t live_roots = 0;
    uint32_t live_nodes = 0;
    lab02_card_pool_resource_counts(&carousel->pool, &live_roots, &live_nodes);

    const lab02_resource_sample_t sample = {
        .phase = phase_name(session.load_stage),
        .session_id = lab02_session_generation(),
        .internal_free_bytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .internal_min_free_bytes = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .internal_largest_free_bytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        .dma_free_bytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA),
        .dma_largest_free_bytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
        .psram_free_bytes = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .psram_largest_free_bytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .lvgl_mem_used_bytes = (uint32_t)(lvgl_mem.total_size - lvgl_mem.free_size),
        .lvgl_mem_high_water_bytes = (uint32_t)lvgl_mem.max_used,
        .lvgl_biggest_free_bytes = (uint32_t)lvgl_mem.free_biggest_size,
        .live_card_roots = live_roots,
        .live_card_nodes = live_nodes,
        .active_fetches = counts.active_fetches,
        .admitted_fetches = counts.admitted_fetches,
        .fetch_queue_depth = counts.fetch_queue_depth,
        .terminal_pending = counts.terminal_pending,
    };
    lab02_trace_emit_resource_sample(&sample);
}
