#ifndef FL_PRODUCT_RECORD_FLSK2_HPP
#define FL_PRODUCT_RECORD_FLSK2_HPP

#include <stddef.h>
#include <stdint.h>

namespace fl_product_record
{

struct CapturePointView {
    uint64_t timestamp_us;
    uint8_t logical_x;
    uint8_t logical_y;
    uint8_t color_index;
};

uint16_t rail_color_rgb565(uint8_t color_index);
size_t encode_flsk2(const CapturePointView *points, size_t point_count, uint8_t *out, size_t out_cap,
                    uint32_t start_ms);
bool validate_flsk2(const uint8_t *data, size_t len);

} /* namespace fl_product_record */

#endif
