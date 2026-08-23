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

/**
 * Same as http_bearer_do, but https:// when use_tls is non-zero.
 * TLS skip-verify (LAN / trycloudflare demo). Production should pin a CA.
 */
int http_bearer_do_ex(const char *host, int port, const char *method, const char *path,
                      const char *token, const char *json, http_buf_t *out, int timeout_ms,
                      int use_tls);

#ifdef __cplusplus
}
#endif
