#ifndef FL_PRODUCT_SEND_SHA256_HPP
#define FL_PRODUCT_SEND_SHA256_HPP

#include <stddef.h>
#include <stdint.h>

namespace fl_product_send {

void sha256_bytes(const uint8_t *data, size_t length, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);

} /* namespace fl_product_send */

#endif
