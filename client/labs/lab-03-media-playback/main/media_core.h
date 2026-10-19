#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define L3_SIDE 120
#define L3_FRAME_BYTES (120 * 120 * 2)
#define L3_MAX_SEGMENTS 4096
#define L3_SEGMENT_BYTES 10
#define L3_SKETCH_CAP (8 + L3_MAX_SEGMENTS * L3_SEGMENT_BYTES)
#define L3_AUDIO_CAP 96000
#define L3_WORK_CAP 64
bool l3_validate_sketch(const uint8_t *data, size_t len, uint16_t *count);
uint32_t l3_due(const uint8_t *data, unsigned index);
void l3_draw(uint16_t *pixels, const uint8_t *data, unsigned index);
unsigned l3_render(uint16_t *pixels, const uint8_t *data, unsigned count,
                   unsigned *rendered, uint32_t position_ms);
