#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#define LAB15_DURATION_CAP_MS 180000U
#define LAB15_SKETCH_BYTES_MAX 32768U
#define LAB15_CANVAS_BYTES_PER_BUFFER 115200U
#define LAB15_CANVAS_BUFFER_COUNT 2U
#define LAB15_CAPTURE_OPERATION_ID 1U

typedef struct {
    uint32_t live_nodes;
    size_t internal_free;
    size_t internal_largest_free;
    size_t internal_min_free;
    size_t psram_free;
    size_t psram_largest_free;
    size_t lvgl_used;
    size_t lvgl_largest_free;
    size_t dma_largest_free;
} lab15_heap_snapshot_t;

const char *lab15_arm_slug(void);

int64_t lab15_clock_ms(void);
void lab15_clock_advance_ms(int64_t delta_ms);

void lab15_sample_heap(lab15_heap_snapshot_t *out);

void lab15_trace_resource_sample(const char *phase, const lab15_heap_snapshot_t *snap);
void lab15_trace_allocation_attempt(const char *kind, size_t bytes, bool ok, const char *region);
void lab15_trace_terminal(uint64_t operation_id, const char *outcome, const char *reason);

bool lab15_montage_mount(lv_obj_t *screen, uint32_t *live_nodes_out);
void lab15_montage_unmount(void);

bool lab15_reserve_sketch(void **sketch_out, size_t *bytes_out);
bool lab15_reserve_canvas(void **canvas_out, size_t *bytes_out);
bool lab15_reserve_pcm(void **pcm_out, size_t *bytes_out);
void lab15_release_capture_buffers(void **sketch, void **canvas, void **pcm);

bool lab15_pcm_fill_deterministic(void *pcm, size_t bytes, int64_t end_ms);

void lab15_scripted_harness_run(void);
