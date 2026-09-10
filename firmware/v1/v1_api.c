#include "v1_api.h"

#include "v1_connect.h"
#include "v1_timing.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "who.h"

#include <string.h>

#ifndef DEMO_SERVER_TLS
#define DEMO_SERVER_TLS 0
#endif
#ifndef DEMO_TLS_SKIP_VERIFY
#define DEMO_TLS_SKIP_VERIFY 1
#endif

#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "v1_api";

static uint8_t s_json[V1_JSON_CAP];
static esp_websocket_client_handle_t s_ws;
static void (*s_inbox_cb)(const char *user_id);

static msg_t *s_msgs;
static int *s_msg_n;
static int *s_focus;
static int s_msg_max;

void v1_api_init(void)
{
    s_json[0] = 0;
}

uint8_t *v1_api_json_buf(void)
{
    return s_json;
}

size_t v1_api_json_cap(void)
{
    return sizeof(s_json);
}

void v1_api_inbox_bind(msg_t *msgs, int *msg_n, int *focus, int msg_max)
{
    s_msgs = msgs;
    s_msg_n = msg_n;
    s_focus = focus;
    s_msg_max = msg_max;
}

msg_t *v1_api_msgs(void)
{
    return s_msgs;
}

int v1_api_msg_count(void)
{
    return s_msg_n ? *s_msg_n : 0;
}

int v1_api_focus(void)
{
    return s_focus ? *s_focus : 0;
}

void v1_api_set_focus(int focus)
{
    if (s_focus) {
        *s_focus = focus;
    }
}

void v1_api_format_url(char *url, size_t cap, const char *path)
{
    snprintf(url, cap, "%s://%s:%d%s",
#if DEMO_SERVER_TLS
             "https",
#else
             "http",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT, path ? path : "/");
}

void v1_api_apply_tls(void *cfg_void)
{
    esp_http_client_config_t *cfg = cfg_void;
#if DEMO_SERVER_TLS
    cfg->transport_type = HTTP_TRANSPORT_OVER_SSL;
#if DEMO_TLS_SKIP_VERIFY
    cfg->crt_bundle_attach = NULL;
    cfg->skip_cert_common_name_check = true;
#else
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
#else
    (void)cfg;
#endif
}

int v1_api_http_json_timeout(const char *method, const char *path, const char *json,
                             const char *user_id, http_buf_t *body, int timeout_ms)
{
    char url[160];
    v1_api_format_url(url, sizeof(url), path);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .buffer_size = 2048,
    };
    v1_api_apply_tls(&cfg);
    if (strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    } else if (strcmp(method, "PUT") == 0) {
        cfg.method = HTTP_METHOD_PUT;
    }

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(c, "Authorization", auth);
    if (user_id && user_id[0]) {
        esp_http_client_set_header(c, "X-User-Id", user_id);
    }
    if (json) {
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, json, (int)strlen(json));
    }

    if (body && body->buf) {
        body->len = 0;
        int total = 0;
        if (esp_http_client_open(c, json ? (int)strlen(json) : 0) != ESP_OK) {
            esp_http_client_cleanup(c);
            return -1;
        }
        if (json) {
            esp_http_client_write(c, json, (int)strlen(json));
        }
        (void)esp_http_client_fetch_headers(c);
        int64_t read_t0 = esp_timer_get_time();
        while (total < body->cap - 1) {
            if ((esp_timer_get_time() - read_t0) / 1000 > timeout_ms) {
                esp_http_client_close(c);
                esp_http_client_cleanup(c);
                return -1;
            }
            int n = esp_http_client_read(c, (char *)body->buf + total, body->cap - 1 - total);
            if (n <= 0) {
                break;
            }
            total += n;
        }
        body->len = total;
        body->buf[total] = 0;
        int st = esp_http_client_get_status_code(c);
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
        return st;
    }

    esp_err_t err = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    return err == ESP_OK ? st : -1;
}

int v1_api_http_json(const char *method, const char *path, const char *json,
                     const char *user_id, http_buf_t *body)
{
    return v1_api_http_json_timeout(method, path, json, user_id, body, 15000);
}

void v1_api_parse_inbox(const char *js)
{
    if (!s_msgs || !s_msg_n || !s_focus) {
        return;
    }
    *s_msg_n = 0;
    cJSON *root = cJSON_Parse(js);
    if (!root) {
        return;
    }
    cJSON *arr = cJSON_GetObjectItem(root, "messages");
    cJSON *view = cJSON_GetObjectItem(root, "last_viewed_seq");
    if (cJSON_IsArray(arr)) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n && *s_msg_n < s_msg_max; i++) {
            cJSON *it = cJSON_GetArrayItem(arr, i);
            cJSON *seq = cJSON_GetObjectItem(it, "seq");
            cJSON *label = cJSON_GetObjectItem(it, "from_label");
            cJSON *read = cJSON_GetObjectItem(it, "read");
            cJSON *pos = cJSON_GetObjectItem(it, "position_ms");
            cJSON *dur = cJSON_GetObjectItem(it, "duration_ms");
            if (!cJSON_IsNumber(seq)) {
                continue;
            }
            msg_t *m = &s_msgs[(*s_msg_n)++];
            m->seq = seq->valueint;
            m->from_label[0] = 0;
            if (cJSON_IsString(label)) {
                strncpy(m->from_label, label->valuestring, sizeof(m->from_label) - 1);
            }
            m->read = cJSON_IsTrue(read);
            m->position_ms = cJSON_IsNumber(pos) ? pos->valueint : 0;
            m->duration_ms = cJSON_IsNumber(dur) ? dur->valueint : 0;
        }
    }
    *s_focus = *s_msg_n > 0 ? 0 : 0;
    if (cJSON_IsNumber(view)) {
        for (int i = 0; i < *s_msg_n; i++) {
            if (s_msgs[i].seq == view->valueint) {
                *s_focus = i;
                break;
            }
        }
    }
    cJSON_Delete(root);
}

bool v1_api_reload_inbox(const char *session_user)
{
    http_buf_t b = { .buf = s_json, .cap = (int)sizeof(s_json) };
    if (v1_api_http_json("GET", "/v1/inbox", NULL, session_user, &b) != 200) {
        return false;
    }
    v1_api_parse_inbox((char *)s_json);
    return true;
}

bool v1_api_login_user(const char *user_id, const char *pin, char *session_user,
                       size_t session_cap, int *http_status, bool *pin_reset)
{
    char js[80];
    snprintf(js, sizeof(js), "{\"user_id\":\"%s\",\"pin\":\"%s\"}", user_id, pin);
    http_buf_t b = { .buf = s_json, .cap = (int)sizeof(s_json) };
    if (pin_reset) {
        *pin_reset = false;
    }
    int st = v1_api_http_json_timeout("POST", "/v1/session/login", js, NULL, &b,
                                      V1_CONNECT_PROBE_MS);
    if (http_status) {
        *http_status = st;
    }
    if (st != 200) {
        if (st < 0) {
            v1_connect_mark_offline();
        }
        return false;
    }
    v1_connect_mark_online();
    cJSON *root = cJSON_Parse((char *)s_json);
    cJSON *ok = root ? cJSON_GetObjectItem(root, "ok") : NULL;
    bool good = cJSON_IsTrue(ok);
    if (good && session_user && session_cap > 0) {
        strncpy(session_user, user_id, session_cap - 1);
        session_user[session_cap - 1] = 0;
        v1_api_parse_inbox((char *)s_json);
        cJSON *prof = cJSON_GetObjectItem(root, "profile");
        v1_connect_apply_profile(user_id, prof);
        cJSON *pr = cJSON_GetObjectItem(root, "pin_reset");
        if (pin_reset) {
            *pin_reset = cJSON_IsTrue(pr);
        }
    }
    cJSON_Delete(root);
    return good;
}

static void ws_hello(void)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    char hello[128];
    snprintf(hello, sizeof(hello), "{\"type\":\"hello\",\"token\":\"%s\"}", DEMO_DEVICE_TOKEN);
    esp_websocket_client_send_text(s_ws, hello, (int)strlen(hello), pdMS_TO_TICKS(1000));
}

static void on_ws_text(const char *s, int n)
{
    char tmp[256];
    if (n >= (int)sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    cJSON *j = cJSON_Parse(tmp);
    if (!j) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(j, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (strcmp(t, "inbox") == 0) {
        cJSON *uid = cJSON_GetObjectItem(j, "user_id");
        char user_id[16] = {0};
        if (cJSON_IsString(uid) && uid->valuestring) {
            strncpy(user_id, uid->valuestring, sizeof(user_id) - 1);
        }
        if (s_inbox_cb) {
            s_inbox_cb(user_id);
        }
        ESP_LOGI(TAG, "ws inbox user=%s", user_id);
    } else if (strcmp(t, "hello_ok") == 0) {
        ESP_LOGI(TAG, "ws hello_ok");
    }
    cJSON_Delete(j);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        v1_connect_mark_online();
        ws_hello();
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR) {
        v1_connect_mark_offline();
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev == NULL) {
        return;
    }
    if (ev->op_code == 0x01) {
        on_ws_text(ev->data_ptr, ev->data_len);
    }
}

void v1_api_ws_on_inbox(void (*cb)(const char *user_id))
{
    s_inbox_cb = cb;
}

void v1_api_ws_start(void)
{
    if (s_ws) {
        return;
    }
    char uri[128];
    snprintf(uri, sizeof(uri), "%s://%s:%d/v1/ws",
#if DEMO_SERVER_TLS
             "wss",
#else
             "ws",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 2048,
#if DEMO_SERVER_TLS
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
#if DEMO_TLS_SKIP_VERIFY
        .skip_cert_common_name_check = true,
        .crt_bundle_attach = NULL,
#else
        .crt_bundle_attach = esp_crt_bundle_attach,
#endif
#endif
    };
    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) {
        ESP_LOGW(TAG, "ws init failed");
        return;
    }
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGW(TAG, "ws start failed");
        return;
    }
    ESP_LOGI(TAG, "ws %s", uri);
}

esp_websocket_client_handle_t v1_api_ws_handle(void)
{
    return s_ws;
}
