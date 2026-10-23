#ifndef LAB11_CAPTURE_H
#define LAB11_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Synthetic frozen capture owned outside the send operation.
 * Send may borrow it. Only the capture owner frees the bytes. */

#define LAB11_PAYLOAD_LEN 5120u
#define LAB11_MAX_RECIPIENTS 3
#define LAB11_RECIPIENT_LEN 16

typedef struct {
    uint32_t accepted_point_count;
    uint32_t duration_cap_ms;
    uint16_t logical_w;
    uint16_t logical_h;
    uint16_t capture_queue_capacity;
    uint32_t sketch_len;
    uint32_t pcm_len;
    char recipients[LAB11_MAX_RECIPIENTS][LAB11_RECIPIENT_LEN];
    size_t recipient_count;
} lab11_capture_info_t;

typedef struct {
    uint32_t generation;
    uint8_t *bytes;
    size_t length;
    uint8_t content_sha256[32];
    lab11_capture_info_t info;
    uint32_t borrows;
    bool released;
} lab11_capture_t;

typedef struct {
    uint32_t generation;
    uint32_t operation_id;
    bool held;
} lab11_borrow_t;

void lab11_capture_synthesize(uint8_t *dst, size_t length);
bool lab11_capture_create(lab11_capture_t *capture, size_t length, const lab11_capture_info_t *info);
bool lab11_capture_borrow(lab11_capture_t *capture, uint32_t operation_id, lab11_borrow_t *out);
void lab11_capture_release_borrow(lab11_capture_t *capture, lab11_borrow_t *borrow);
/* Owner release. Refuses while a send borrow is held. */
bool lab11_capture_free(lab11_capture_t *capture);

#ifdef __cplusplus
}
#endif
#endif
