#include "fl_product_record_flsk2.hpp"
#include "fl_product_record_types.hpp"

#include "fl_product_record_lab10.h"

#include <string.h>

namespace fl_product_record {
namespace {

uint16_t rgb565_from_index(uint8_t color_index)
{
    const uint32_t rgb = lab10_color_rgb(color_index);
    const uint16_t r = (uint16_t)((rgb >> 19) & 0x1f);
    const uint16_t g = (uint16_t)((rgb >> 10) & 0x3f);
    const uint16_t b = (uint16_t)((rgb >> 3) & 0x1f);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

} /* namespace */

uint16_t rail_color_rgb565(uint8_t color_index) { return rgb565_from_index(color_index); }

size_t encode_flsk2(const CapturePointView *points, size_t point_count, uint8_t *out, size_t out_cap,
                    uint32_t start_ms)
{
    size_t i;
    size_t written = 0;
    uint32_t last_ms = start_ms;
    if (points == 0 || out == 0) {
        return 0;
    }
    for (i = 0; i < point_count; ++i) {
        const CapturePointView *pt = &points[i];
        uint32_t t_ms;
        if (written + kFlsk2PointBytes > out_cap) {
            break;
        }
        if (pt->timestamp_us < points[0].timestamp_us) {
            continue;
        }
        t_ms = start_ms + (uint32_t)((pt->timestamp_us - points[0].timestamp_us) / 1000ULL);
        if (t_ms < last_ms) {
            t_ms = last_ms;
        }
        out[written++] = pt->logical_x;
        out[written++] = pt->logical_y;
        out[written++] = (uint8_t)(rgb565_from_index(pt->color_index) & 0xff);
        out[written++] = (uint8_t)((rgb565_from_index(pt->color_index) >> 8) & 0xff);
        out[written++] = (uint8_t)(t_ms & 0xff);
        out[written++] = (uint8_t)((t_ms >> 8) & 0xff);
        out[written++] = (uint8_t)((t_ms >> 16) & 0xff);
        out[written++] = (uint8_t)((t_ms >> 24) & 0xff);
        last_ms = t_ms;
    }
    return written;
}

bool validate_flsk2(const uint8_t *data, size_t len)
{
    uint32_t last_ms = 0;
    size_t i;
    if (data == 0 || len == 0 || (len % kFlsk2PointBytes) != 0) {
        return false;
    }
    for (i = 0; i < len / kFlsk2PointBytes; ++i) {
        const uint8_t *p = data + i * kFlsk2PointBytes;
        uint32_t t_ms = read_u32(p + 4);
        if (p[0] >= kLogicalWidth || p[1] >= kLogicalHeight || t_ms < last_ms) {
            return false;
        }
        last_ms = t_ms;
    }
    return true;
}

} /* namespace fl_product_record */
