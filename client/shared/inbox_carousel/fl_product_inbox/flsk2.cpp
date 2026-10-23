#include "fl_product_inbox_flsk2.hpp"
#include "fl_product_inbox_types.hpp"

#include <string.h>

namespace fl_product_inbox {
namespace {

uint16_t read_u16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
uint32_t read_u32(const uint8_t *p)
{
    return read_u16(p) | ((uint32_t)read_u16(p + 2) << 16);
}

} /* namespace */

bool flsk2_validate(const uint8_t *data, size_t len, uint16_t *point_count)
{
    uint16_t count;
    uint32_t last_ms = 0;
    size_t i;
    if (data == 0 || len == 0 || (len % 8) != 0) {
        return false;
    }
    count = (uint16_t)(len / 8);
    for (i = 0; i < count; ++i) {
        const uint8_t *p = data + i * 8;
        uint8_t x = p[0];
        uint8_t y = p[1];
        uint32_t t_ms = read_u32(p + 4);
        if (x >= 120 || y >= 120 || t_ms < last_ms) {
            return false;
        }
        last_ms = t_ms;
    }
    if (point_count != 0) {
        *point_count = count;
    }
    return true;
}

void flsk2_paint_prefix(uint16_t *pixels, size_t pixel_count, const uint8_t *data, size_t len,
                        uint32_t position_ms, uint16_t *rendered)
{
    uint16_t count;
    uint16_t idx;
    if (pixels == 0 || pixel_count < kSketchPixels || data == 0 || rendered == 0) {
        return;
    }
    if (!flsk2_validate(data, len, &count)) {
        return;
    }
    for (idx = *rendered; idx < count; ++idx) {
        const uint8_t *p = data + idx * 8;
        uint32_t t_ms = read_u32(p + 4);
        if (t_ms > position_ms) {
            break;
        }
        pixels[(size_t)p[1] * 120 + p[0]] = read_u16(p + 2);
        *rendered = idx + 1;
    }
}

} /* namespace fl_product_inbox */
