#ifndef FL_PRODUCT_INBOX_FLSK2_HPP
#define FL_PRODUCT_INBOX_FLSK2_HPP

#include <stddef.h>
#include <stdint.h>

namespace fl_product_inbox
{

bool flsk2_validate(const uint8_t *data, size_t len, uint16_t *point_count);
void flsk2_paint_prefix(uint16_t *pixels, size_t pixel_count, const uint8_t *data, size_t len,
                        uint32_t position_ms, uint16_t *rendered);

} /* namespace fl_product_inbox */

#endif
