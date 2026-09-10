#ifndef V1_API_H
#define V1_API_H

#include "http_bearer.h"
#include "v1_types.h"

#include "esp_websocket_client.h"

void v1_api_init(void);

void v1_api_format_url(char *url, size_t cap, const char *path);
void v1_api_apply_tls(void *cfg);

int v1_api_http_json_timeout(const char *method, const char *path, const char *json,
                             const char *user_id, http_buf_t *body, int timeout_ms);
int v1_api_http_json(const char *method, const char *path, const char *json,
                     const char *user_id, http_buf_t *body);

uint8_t *v1_api_json_buf(void);
size_t v1_api_json_cap(void);

void v1_api_inbox_bind(msg_t *msgs, int *msg_n, int *focus, int msg_max);
msg_t *v1_api_msgs(void);
int v1_api_msg_count(void);
int v1_api_focus(void);
void v1_api_set_focus(int focus);
void v1_api_parse_inbox(const char *js);
bool v1_api_reload_inbox(const char *session_user);

bool v1_api_login_user(const char *user_id, const char *pin, char *session_user,
                       size_t session_cap, int *http_status, bool *pin_reset);

void v1_api_ws_start(void);
esp_websocket_client_handle_t v1_api_ws_handle(void);
void v1_api_ws_on_inbox(void (*cb)(const char *user_id));

#endif /* V1_API_H */
