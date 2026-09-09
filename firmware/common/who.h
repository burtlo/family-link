#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#include <stddef.h>
#include <string.h>

/* Identity slots stamped by `make flash WHO=` into the built .bin.
 * Same compile for Mazi and Arlo. Keep these 32-byte tags in sync with
 * scripts/flash.py (WHO_TAGS). */
extern const char WHO_DEVICE_ID[32];
extern const char WHO_DEVICE_TOKEN[32];
extern const char WHO_DEVICE_NAME[32];
extern const char WHO_PEER_NAME[32];

#undef DEMO_DEVICE_ID
#define DEMO_DEVICE_ID WHO_DEVICE_ID
#undef DEMO_DEVICE_TOKEN
#define DEMO_DEVICE_TOKEN WHO_DEVICE_TOKEN
#undef DEMO_DEVICE_NAME
#define DEMO_DEVICE_NAME WHO_DEVICE_NAME
#undef DEMO_PEER_NAME
#define DEMO_PEER_NAME WHO_PEER_NAME

static inline void who_str(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = 0;
}
