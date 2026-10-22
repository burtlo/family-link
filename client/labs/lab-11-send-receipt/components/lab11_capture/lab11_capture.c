#include "lab11_capture.h"

#include "lab11_sha256.h"

#include <stdlib.h>
#include <string.h>

void lab11_capture_synthesize(uint8_t *dst, size_t length)
{
    size_t i;
    if (dst == NULL) {
        return;
    }
    for (i = 0; i < length; i++) {
        dst[i] = (uint8_t)((i * 17u + 3u) & 0xffu);
    }
}

bool lab11_capture_create(lab11_capture_t *capture, size_t length, const lab11_capture_info_t *info)
{
    uint8_t *bytes;
    if (capture == NULL || info == NULL || length == 0 || length > LAB11_PAYLOAD_LEN) {
        return false;
    }
    bytes = (uint8_t *)malloc(length);
    if (bytes == NULL) {
        return false;
    }
    memset(capture, 0, sizeof(*capture));
    lab11_capture_synthesize(bytes, length);
    capture->bytes = bytes;
    capture->length = length;
    capture->info = *info;
    capture->generation = 1;
    lab11_sha256(bytes, length, capture->content_sha256);
    return true;
}

bool lab11_capture_borrow(lab11_capture_t *capture, uint32_t operation_id, lab11_borrow_t *out)
{
    if (capture == NULL || out == NULL || capture->bytes == NULL || capture->released) {
        return false;
    }
    capture->borrows += 1u;
    out->generation = capture->generation;
    out->operation_id = operation_id;
    out->held = true;
    return true;
}

void lab11_capture_release_borrow(lab11_capture_t *capture, lab11_borrow_t *borrow)
{
    if (capture == NULL || borrow == NULL || !borrow->held) {
        return;
    }
    if (capture->borrows > 0) {
        capture->borrows -= 1u;
    }
    borrow->held = false;
}

bool lab11_capture_free(lab11_capture_t *capture)
{
    if (capture == NULL) {
        return false;
    }
    if (capture->borrows != 0) {
        return false;
    }
    free(capture->bytes);
    capture->bytes = NULL;
    capture->length = 0;
    capture->released = true;
    return true;
}
