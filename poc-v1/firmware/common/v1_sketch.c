#include "v1_sketch.h"

#include <stdlib.h>
#include <string.h>

#define V1_SKETCH_HDR_LEN ((int)sizeof(v1_sketch_hdr_t))
#define V1_SKETCH_PT_LEN  ((int)sizeof(v1_sketch_pt_t))

static int16_t clamp_i16(int v, int lo, int hi)
{
    if (v < lo) {
        return (int16_t)lo;
    }
    if (v > hi) {
        return (int16_t)hi;
    }
    return (int16_t)v;
}

static void stamp(v1_sketch_ink_t *ink, int x, int y, uint16_t c)
{
    if (!ink || !ink->fb || ink->w <= 0 || ink->h <= 0) {
        return;
    }
    for (int dy = -ink->brush; dy <= ink->brush; dy++) {
        for (int dx = -ink->brush; dx <= ink->brush; dx++) {
            if (dx * dx + dy * dy > ink->brush * ink->brush) {
                continue;
            }
            int px = x + dx;
            int py = y + dy;
            if ((unsigned)px >= (unsigned)ink->w || (unsigned)py >= (unsigned)ink->h) {
                continue;
            }
            ink->fb[py * ink->w + px] = c;
        }
    }
}

static void line_to(v1_sketch_ink_t *ink, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        stamp(ink, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void v1_sketch_ink_init(v1_sketch_ink_t *ink, uint16_t *fb, int w, int h, int brush)
{
    if (!ink) {
        return;
    }
    ink->fb = fb;
    ink->w = (int16_t)w;
    ink->h = (int16_t)h;
    ink->brush = (int16_t)(brush > 0 ? brush : 1);
    ink->pen_x = -1;
    ink->pen_y = -1;
}

void v1_sketch_ink_clear(v1_sketch_ink_t *ink, uint16_t color)
{
    if (!ink || !ink->fb || ink->w <= 0 || ink->h <= 0) {
        return;
    }
    size_t px = (size_t)ink->w * (size_t)ink->h;
    for (size_t i = 0; i < px; i++) {
        ink->fb[i] = color;
    }
    ink->pen_x = -1;
    ink->pen_y = -1;
}

void v1_sketch_ink_point(v1_sketch_ink_t *ink, uint8_t phase, int16_t x, int16_t y, uint16_t color)
{
    if (!ink || !ink->fb || ink->w <= 0 || ink->h <= 0) {
        return;
    }
    x = clamp_i16(x, 0, ink->w - 1);
    y = clamp_i16(y, 0, ink->h - 1);
    if (phase == V1_SKETCH_PHASE_DOWN || ink->pen_x < 0) {
        stamp(ink, x, y, color);
    } else {
        line_to(ink, ink->pen_x, ink->pen_y, x, y, color);
    }
    ink->pen_x = x;
    ink->pen_y = y;
    if (phase == V1_SKETCH_PHASE_UP) {
        ink->pen_x = -1;
        ink->pen_y = -1;
    }
}

bool v1_sketch_capture_point(v1_sketch_pt_t *pts, int max_points, int *count, uint8_t phase,
                             int16_t x, int16_t y, int16_t max_x, int16_t max_y,
                             int64_t now_us, int64_t t0_us)
{
    if (!pts || !count || *count >= max_points) {
        return false;
    }
    if (max_x < 0 || max_y < 0) {
        return false;
    }
    int64_t t_ms = (now_us - t0_us) / 1000;
    if (t_ms < 0) {
        t_ms = 0;
    }
    if (t_ms > V1_SKETCH_MAX_MS) {
        t_ms = V1_SKETCH_MAX_MS;
    }
    if (t_ms > 0xFFFF) {
        t_ms = 0xFFFF;
    }
    v1_sketch_pt_t *p = &pts[*count];
    p->t_ms = (uint16_t)t_ms;
    p->phase = phase;
    p->pad = 0;
    p->x = (uint16_t)clamp_i16(x, 0, max_x);
    p->y = (uint16_t)clamp_i16(y, 0, max_y);
    (*count)++;
    return true;
}

size_t v1_sketch_pack(uint8_t *dst, size_t dst_cap, const v1_sketch_pt_t *pts, size_t n)
{
    if (!dst || !pts || n == 0 || n > V1_SKETCH_MAX_POINTS) {
        return 0;
    }
    size_t need = (size_t)V1_SKETCH_HDR_LEN + n * (size_t)V1_SKETCH_PT_LEN;
    if (dst_cap < need) {
        return 0;
    }
    v1_sketch_hdr_t h;
    memcpy(h.magic, V1_SKETCH_MAGIC, 4);
    h.ver = V1_SKETCH_VER;
    h.pad = 0;
    h.n = (uint16_t)n;
    memcpy(dst, &h, sizeof(h));
    memcpy(dst + sizeof(h), pts, n * sizeof(v1_sketch_pt_t));
    return need;
}

bool v1_sketch_blob_points(const uint8_t *blob, size_t len, const v1_sketch_pt_t **pts,
                           uint16_t *n_out)
{
    if (!v1_sketch_validate_blob(blob, len)) {
        return false;
    }
    const v1_sketch_hdr_t *h = (const v1_sketch_hdr_t *)blob;
    if (pts) {
        *pts = (const v1_sketch_pt_t *)(blob + sizeof(v1_sketch_hdr_t));
    }
    if (n_out) {
        *n_out = h->n;
    }
    return true;
}

bool v1_sketch_validate_blob(const uint8_t *blob, size_t len)
{
    if (!blob || len < sizeof(v1_sketch_hdr_t)) {
        return false;
    }
    const v1_sketch_hdr_t *h = (const v1_sketch_hdr_t *)blob;
    if (memcmp(h->magic, V1_SKETCH_MAGIC, 4) != 0 || h->ver != V1_SKETCH_VER) {
        return false;
    }
    size_t n = h->n;
    if (n == 0 || n > V1_SKETCH_MAX_POINTS) {
        return false;
    }
    size_t need = sizeof(v1_sketch_hdr_t) + n * sizeof(v1_sketch_pt_t);
    if (len < need) {
        return false;
    }
    uint16_t last_t = 0;
    const v1_sketch_pt_t *pts = (const v1_sketch_pt_t *)(blob + sizeof(v1_sketch_hdr_t));
    for (size_t i = 0; i < n; i++) {
        if (pts[i].phase > V1_SKETCH_PHASE_UP) {
            return false;
        }
        if (i > 0 && pts[i].t_ms < last_t) {
            return false;
        }
        last_t = pts[i].t_ms;
    }
    return true;
}
