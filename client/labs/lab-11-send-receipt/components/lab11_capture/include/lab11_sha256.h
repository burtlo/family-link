#ifndef LAB11_SHA256_H
#define LAB11_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAB11_SHA256_LEN 32

void lab11_sha256(const uint8_t *data, size_t length, uint8_t out[LAB11_SHA256_LEN]);
void lab11_sha256_hex(const uint8_t digest[LAB11_SHA256_LEN], char out[65]);

#ifdef __cplusplus
}
#endif
#endif
