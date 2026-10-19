#include "media_core.h"
#include <stdlib.h>
#include <string.h>
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
uint32_t l3_due(const uint8_t *data, unsigned index) { return u32(data + 8 + index * 10); }
bool l3_validate_sketch(const uint8_t *data, size_t len, uint16_t *count)
{
    if (!data || len < 8 || memcmp(data, "L3SK", 4) || u16(data + 4) != 1) return false;
    unsigned n = u16(data + 6);
    if (n > L3_MAX_SEGMENTS || len != 8 + n * 10) return false;
    uint32_t last = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t *p = data + 8 + i * 10;
        uint32_t due = u32(p);
        if (due < last || due >= 3000 || p[4] >= 120 || p[5] >= 120 || p[6] >= 120 || p[7] >= 120) return false;
        last = due;
    }
    *count = n;
    return true;
}
void l3_draw(uint16_t *pixels, const uint8_t *data, unsigned index)
{
    const uint8_t *p = data + 8 + index * 10;
    int x = p[4], y = p[5], x1 = p[6], y1 = p[7];
    int dx = abs(x1 - x), sx = x < x1 ? 1 : -1;
    int dy = -abs(y1 - y), sy = y < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        pixels[y * 120 + x] = u16(p + 8);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}
unsigned l3_render(uint16_t *pixels, const uint8_t *data, unsigned count,
                   unsigned *rendered, uint32_t position_ms)
{
    unsigned work = 0;
    while (*rendered < count && l3_due(data, *rendered) <= position_ms && work < L3_WORK_CAP) {
        l3_draw(pixels, data, (*rendered)++);
        work++;
    }
    return work;
}
