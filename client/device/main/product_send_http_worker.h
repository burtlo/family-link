#ifndef PRODUCT_SEND_HTTP_WORKER_H
#define PRODUCT_SEND_HTTP_WORKER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct product_send_http_result {
    int perform_ok;
    int http_status;
    int body_len;
    uint32_t seq;
    char client_message_id[48];
    char body[4096];
} product_send_http_result_t;

bool product_send_http_worker_start(void);
void product_send_http_worker_stop(void);
/* body stays valid until the matching result is applied. One exchange at a time. */
bool product_send_http_submit(const char *method, const char *path, const uint8_t *body, uint32_t body_len,
                              int is_json, const char *client_message_id, uint32_t seq);
/* LVGL thread. Null when no result is waiting. The pointer is valid until the next poll. */
const product_send_http_result_t *product_send_http_poll(void);

#ifdef __cplusplus
}
#endif

#endif
