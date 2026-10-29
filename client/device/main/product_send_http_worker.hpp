#pragma once

#include <cstdint>

namespace product_send_http
{

struct Result {
    int perform_ok;
    int http_status;
    int body_len;
    uint32_t seq;
    char client_message_id[48];
    char body[4096];
};

bool worker_start();
void worker_stop();
/* body stays valid until the matching result is applied. One exchange at a time.
 * bearer is the signed-in session credential (login COM-019); empty refuses the job. */
bool submit(const char *method, const char *path, const uint8_t *body, uint32_t body_len, int is_json,
            const char *client_message_id, uint32_t seq, const char *bearer);
/* LVGL thread. Null when no result is waiting. The pointer is valid until the next poll. */
const Result *poll();

} /* namespace product_send_http */
