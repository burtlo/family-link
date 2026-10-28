#ifndef FL_PRODUCT_SEND_SHA256_HPP
#define FL_PRODUCT_SEND_SHA256_HPP

#include <stddef.h>
#include <stdint.h>

namespace fl_product_send
{

/* Incremental hasher. Callers feed stored bytes in part-sized steps. */
struct Sha256Stream {
    uint32_t state[8];
    uint8_t block[64];
    uint32_t block_len;
    uint64_t total_len;
};

void sha256_stream_init(Sha256Stream *stream);
void sha256_stream_update(Sha256Stream *stream, const uint8_t *data, size_t length);
void sha256_stream_final(Sha256Stream *stream, uint8_t out[32]);
void sha256_bytes(const uint8_t *data, size_t length, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);

} /* namespace fl_product_send */

#endif
