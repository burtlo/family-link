#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Destination buffer for GET/POST. Binary-safe; NUL-terminated when len < cap. */
typedef struct {
    uint8_t *buf;
    int cap;
    int len;
} http_buf_t;

/**
 * HTTP/1.1 to http://host:port with Bearer token.
 * method is "GET", "POST", or "PUT". json may be NULL.
 * Returns status code, or -1 on transport error.
 */
int http_bearer_do(const char *host, int port, const char *method, const char *path,
                   const char *token, const char *json, http_buf_t *out, int timeout_ms);

#ifdef __cplusplus
}
#endif
