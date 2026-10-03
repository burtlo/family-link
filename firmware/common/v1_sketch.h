#ifndef V1_SKETCH_H
#define V1_SKETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define V1_SKETCH_MAGIC "FLSK"
#define V1_SKETCH_VER 1
#define V1_SKETCH_MAX_POINTS 2048
#define V1_SKETCH_MAX_MS 30000

enum {
    V1_SKETCH_PHASE_DOWN = 0,
    V1_SKETCH_PHASE_MOVE = 1,
    V1_SKETCH_PHASE_UP = 2,
};

typedef struct __attribute__((packed)) {
    char magic[4];
    uint8_t ver;
    uint8_t pad;
    uint16_t n;
} v1_sketch_hdr_t;

typedef struct __attribute__((packed)) {
    uint16_t t_ms;
    uint8_t phase;
    uint8_t pad;
    uint16_t x;
    uint16_t y;
} v1_sketch_pt_t;

typedef struct {
    uint16_t *fb;
    int16_t w;
    int16_t h;
    int16_t brush;
    int16_t pen_x;
    int16_t pen_y;
} v1_sketch_ink_t;

void v1_sketch_ink_init(v1_sketch_ink_t *ink, uint16_t *fb, int w, int h, int brush);
void v1_sketch_ink_clear(v1_sketch_ink_t *ink, uint16_t color);
void v1_sketch_ink_point(v1_sketch_ink_t *ink, uint8_t phase, int16_t x, int16_t y, uint16_t color);

bool v1_sketch_capture_point(v1_sketch_pt_t *pts, int max_points, int *count, uint8_t phase,
                             int16_t x, int16_t y, int16_t max_x, int16_t max_y,
                             int64_t now_us, int64_t t0_us);

size_t v1_sketch_pack(uint8_t *dst, size_t dst_cap, const v1_sketch_pt_t *pts, size_t n);
bool v1_sketch_validate_blob(const uint8_t *blob, size_t len);
bool v1_sketch_blob_points(const uint8_t *blob, size_t len, const v1_sketch_pt_t **pts,
                           uint16_t *n_out);

#endif /* V1_SKETCH_H */
