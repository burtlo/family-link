#include "fl_product_send_sha256.hpp"

#include <string.h>

namespace fl_product_send {
namespace {

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

uint32_t load_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void store_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = load_be(block + (i * 4));
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    for (i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} /* namespace */

void sha256_stream_init(Sha256Stream *stream)
{
    if (stream == 0) {
        return;
    }
    memset(stream, 0, sizeof(*stream));
    stream->state[0] = 0x6a09e667u;
    stream->state[1] = 0xbb67ae85u;
    stream->state[2] = 0x3c6ef372u;
    stream->state[3] = 0xa54ff53au;
    stream->state[4] = 0x510e527fu;
    stream->state[5] = 0x9b05688cu;
    stream->state[6] = 0x1f83d9abu;
    stream->state[7] = 0x5be0cd19u;
}

void sha256_stream_update(Sha256Stream *stream, const uint8_t *data, size_t length)
{
    if (stream == 0 || data == 0 || length == 0) {
        return;
    }
    stream->total_len += (uint64_t)length;
    while (length > 0) {
        uint32_t room = 64u - stream->block_len;
        uint32_t n = length < (size_t)room ? (uint32_t)length : room;
        memcpy(stream->block + stream->block_len, data, n);
        stream->block_len += n;
        data += n;
        length -= n;
        if (stream->block_len == 64u) {
            compress(stream->state, stream->block);
            stream->block_len = 0;
        }
    }
}

void sha256_stream_final(Sha256Stream *stream, uint8_t out[32])
{
    uint64_t bits;
    uint32_t i;
    if (stream == 0 || out == 0) {
        return;
    }
    bits = stream->total_len * 8u;
    stream->block[stream->block_len] = 0x80u;
    stream->block_len += 1u;
    if (stream->block_len > 56u) {
        while (stream->block_len < 64u) {
            stream->block[stream->block_len] = 0;
            stream->block_len += 1u;
        }
        compress(stream->state, stream->block);
        stream->block_len = 0;
    }
    while (stream->block_len < 56u) {
        stream->block[stream->block_len] = 0;
        stream->block_len += 1u;
    }
    store_be(stream->block + 56, (uint32_t)(bits >> 32));
    store_be(stream->block + 60, (uint32_t)bits);
    compress(stream->state, stream->block);
    for (i = 0; i < 8u; i++) {
        store_be(out + (i * 4u), stream->state[i]);
    }
}

void sha256_bytes(const uint8_t *data, size_t length, uint8_t out[32])
{
    Sha256Stream stream;
    sha256_stream_init(&stream);
    if (data != 0 && length > 0) {
        sha256_stream_update(&stream, data, length);
    }
    sha256_stream_final(&stream, out);
}

void sha256_hex(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < 32; i++) {
        out[i * 2u] = hex[digest[i] >> 4];
        out[i * 2u + 1u] = hex[digest[i] & 0x0fu];
    }
    out[64] = '\0';
}

} /* namespace fl_product_send */
