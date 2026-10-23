#include "product_login_http_worker.h"
#include "product_send_fw.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "fl_product_login_p1_wire.hpp"
#include "fl_product_login_session.hpp"

#include <stdio.h>
#include <string.h>

namespace {

constexpr int kHttpBodyCap = 4096;
constexpr int kHttpWorkerStack = 16384;
constexpr int kHttpQueueDepth = 2;
constexpr int kAuthCap = 256;
constexpr int kUrlCap = 256;
constexpr int kPinJsonCap = 320;
constexpr const char *kTag = "product_login_http";

enum JobKind {
    kJobRoster = 1,
    kJobPin = 2,
    kJobHealth = 3,
    kJobRefresh = 4,
    kJobInvalidate = 5,
};

struct HttpJob {
    JobKind kind;
    uint64_t operation_id;
    uint32_t generation;
    char session_id[16];
    char user_id[fl_product_login::kMaxId];
    char pin[8];
    char bearer[fl_product_login::kSessionTokenCap];
    int invalidate_attempt;
    int timeout_ms;
    int muted;
    char reason[16];
    char boot_id[40];
};

struct HttpExchange {
    int perform_ok;
    int http_status;
    int body_len;
    char body[kHttpBodyCap];
};

struct HttpResult {
    JobKind kind;
    uint64_t operation_id;
    uint32_t generation;
    char session_id[16];
    char user_id[fl_product_login::kMaxId];
    int invalidate_attempt;
    HttpExchange exchange;
};

fl_product_login::SessionWriter *s_session = nullptr;
QueueHandle_t s_jobs = nullptr;
QueueHandle_t s_results = nullptr;
TaskHandle_t s_worker = nullptr;
char s_base_url[kUrlCap];
char s_bearer[kAuthCap];
uint64_t s_active_roster_id = 0;
uint64_t s_active_pin_id = 0;
bool s_active_health = false;
uint64_t s_health_skip_id = 0;
uint64_t s_active_refresh_id = 0;
bool s_active_invalidate = false;

void wipe_buffer(char *buffer, int cap)
{
    if (buffer != nullptr && cap > 0) {
        memset(buffer, 0, (size_t)cap);
    }
}

bool load_desk_token()
{
    if (CONFIG_FAMILY_DESK_TOKEN[0] != '\0') {
        strncpy(s_bearer, CONFIG_FAMILY_DESK_TOKEN, sizeof(s_bearer) - 1);
        return s_bearer[0] != '\0';
    }
    nvs_handle_t handle;
    size_t length = sizeof(s_bearer);
    if (nvs_open("family_link", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    if (nvs_get_str(handle, "desk_token", s_bearer, &length) != ESP_OK) {
        nvs_close(handle);
        return false;
    }
    nvs_close(handle);
    return s_bearer[0] != '\0';
}

bool join_url(const char *suffix, char *out, int cap)
{
    int base_len;
    int suffix_len;
    if (suffix == nullptr || out == nullptr || cap < 16) {
        return false;
    }
    strncpy(out, s_base_url, (size_t)cap - 1);
    out[cap - 1] = '\0';
    base_len = (int)strlen(out);
    suffix_len = (int)strlen(suffix);
    if (base_len > 0 && out[base_len - 1] == '/' && suffix[0] == '/') {
        suffix += 1;
        suffix_len -= 1;
    } else if (base_len > 0 && out[base_len - 1] != '/' && suffix[0] != '/') {
        if (base_len + 1 >= cap) {
            return false;
        }
        out[base_len] = '/';
        base_len += 1;
        out[base_len] = '\0';
    }
    if (base_len + suffix_len >= cap) {
        return false;
    }
    strcat(out, suffix);
    return true;
}

extern "C" esp_err_t product_login_http_event(esp_http_client_event_t *event)
{
    HttpExchange *out;
    int room;
    int count;
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    out = static_cast<HttpExchange *>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }
    room = kHttpBodyCap - 1 - out->body_len;
    count = event->data_len < room ? event->data_len : room;
    if (count > 0) {
        memcpy(out->body + out->body_len, event->data, (size_t)count);
        out->body_len += count;
        out->body[out->body_len] = '\0';
    }
    return ESP_OK;
}

void perform_exchange(const char *url, esp_http_client_method_t method, const char *body, int body_len,
                      const char *bearer_override, int timeout_ms, HttpExchange *out)
{
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    char auth[kAuthCap + 16];
    memset(out, 0, sizeof(*out));
    config.url = url;
    config.timeout_ms = timeout_ms > 0 ? timeout_ms : fl_product_login::kRequestTimeoutMs;
    config.method = method;
    config.event_handler = product_login_http_event;
    config.user_data = out;
    client = esp_http_client_init(&config);
    if (client == nullptr) {
        return;
    }
    snprintf(auth, sizeof(auth), "Bearer %s",
             bearer_override != nullptr && bearer_override[0] != '\0' ? bearer_override : s_bearer);
    esp_http_client_set_header(client, "Authorization", auth);
    wipe_buffer(auth, (int)sizeof(auth));
    if (body != nullptr && body_len > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, body_len);
    }
    if (esp_http_client_perform(client) == ESP_OK) {
        out->perform_ok = 1;
        out->http_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
}

bool post_job(const HttpJob &job)
{
    if (s_jobs == nullptr) {
        return false;
    }
    return xQueueSend(s_jobs, &job, 0) == pdTRUE;
}

void handle_roster_result(const HttpResult &result)
{
    fl_product_login::RosterResult classified;
    if (s_session == nullptr) {
        return;
    }
    classified = fl_product_login::deliver_classified_roster(
        s_session, result.operation_id, result.exchange.perform_ok, result.exchange.http_status,
        result.exchange.body, result.exchange.body_len, s_session->now_ms());
    ESP_LOGI(kTag, "roster op=%llu http=%d perform=%d parse=%d valid=%d users=%d",
             (unsigned long long)result.operation_id, result.exchange.http_status, result.exchange.perform_ok,
             classified.parse_ok, classified.valid, classified.user_count);
}

void handle_pin_result(const HttpResult &result)
{
    if (s_session == nullptr) {
        return;
    }
    fl_product_login::deliver_classified_pin(s_session, result.operation_id, result.generation, result.session_id,
                                             result.user_id, result.exchange.perform_ok, result.exchange.http_status,
                                             result.exchange.body, result.exchange.body_len, s_session->now_ms());
}

void handle_health_result(const HttpResult &result)
{
    if (s_session == nullptr) {
        return;
    }
    s_session->deliver_health_completion(result.exchange.perform_ok, result.exchange.http_status,
                                       result.exchange.body, result.exchange.body_len, s_session->now_ms());
}

void handle_refresh_result(const HttpResult &result)
{
    fl_product_login::RefreshResult refresh;
    if (s_session == nullptr) {
        return;
    }
    memset(&refresh, 0, sizeof(refresh));
    refresh.operation_id = result.operation_id;
    refresh.perform_ok = result.exchange.perform_ok;
    refresh.http_status = result.exchange.http_status;
    refresh.parse_ok =
        fl_product_login::classify_refresh_http(result.exchange.perform_ok, result.exchange.http_status,
                                                result.exchange.body, result.exchange.body_len);
    s_session->deliver_refresh_completion(refresh, s_session->now_ms());
    product_send_set_refresh_failed(refresh.perform_ok == 1 && refresh.http_status == 200 && refresh.parse_ok == 1
                                        ? 0
                                        : 1);
}

void handle_invalidate_result(const HttpResult &result)
{
    if (s_session == nullptr) {
        return;
    }
    s_session->deliver_invalidate_completion(
        result.operation_id, result.invalidate_attempt, result.exchange.http_status, result.exchange.perform_ok,
        s_session->now_ms());
}

void worker_task(void *arg)
{
    HttpJob job;
    static HttpResult result;
    char url[kUrlCap];
    char json[kPinJsonCap];
    char operation_text[40];
    (void)arg;
    while (true) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        memset(&result, 0, sizeof(result));
        result.kind = job.kind;
        result.operation_id = job.operation_id;
        result.generation = job.generation;
        strncpy(result.session_id, job.session_id, sizeof(result.session_id) - 1);
        strncpy(result.user_id, job.user_id, sizeof(result.user_id) - 1);
        switch (job.kind) {
        case kJobRoster:
            if (join_url("v2/hangout", url, (int)sizeof(url))) {
                perform_exchange(url, HTTP_METHOD_GET, nullptr, 0, nullptr, job.timeout_ms, &result.exchange);
            }
            break;
        case kJobHealth:
            if (join_url("v2/endpoint/health", url, (int)sizeof(url))) {
                const char *reason = job.reason[0] ? job.reason : "periodic";
                snprintf(json, sizeof(json),
                         "{\"schema\":\"fl.endpoint-health/2\",\"reason\":\"%s\",\"boot_id\":\"%s\","
                         "\"muted\":%s}",
                         reason, job.boot_id[0] ? job.boot_id : "boot", job.muted ? "true" : "false");
                perform_exchange(url, HTTP_METHOD_POST, json, (int)strlen(json), nullptr, job.timeout_ms,
                                 &result.exchange);
                wipe_buffer(json, (int)sizeof(json));
            }
            break;
        case kJobPin:
            if (join_url("v2/session/login", url, (int)sizeof(url))) {
                fl_product_login::format_p1_operation_id(job.operation_id, operation_text,
                                                       (int)sizeof(operation_text));
                snprintf(json, sizeof(json),
                         "{\"schema\":\"fl.session-login/2\",\"operation_id\":\"%s\",\"user_id\":\"%s\","
                         "\"pin\":\"%s\"}",
                         operation_text, job.user_id, job.pin);
                perform_exchange(url, HTTP_METHOD_POST, json, (int)strlen(json), nullptr, job.timeout_ms,
                                 &result.exchange);
                wipe_buffer(json, (int)sizeof(json));
                wipe_buffer(job.pin, (int)sizeof(job.pin));
            }
            break;
        case kJobRefresh:
            if (join_url("v2/session/refresh", url, (int)sizeof(url))) {
                perform_exchange(url, HTTP_METHOD_POST, "{}", 2, job.bearer, job.timeout_ms, &result.exchange);
            }
            break;
        case kJobInvalidate:
            result.invalidate_attempt = job.invalidate_attempt;
            if (join_url("v2/session/invalidate", url, (int)sizeof(url))) {
                fl_product_login::format_invalidate_body(job.operation_id, job.bearer[0] != '\0' ? job.bearer : nullptr,
                                                         json, (int)sizeof(json));
                perform_exchange(url, HTTP_METHOD_POST, json, (int)strlen(json), nullptr, job.timeout_ms,
                                 &result.exchange);
                wipe_buffer(json, (int)sizeof(json));
            }
            break;
        default:
            break;
        }
        if (s_results != nullptr) {
            xQueueSend(s_results, &result, portMAX_DELAY);
        }
    }
}

} /* namespace */

extern "C" bool product_login_http_worker_start(fl_product_login::SessionWriter *session)
{
    if (session == nullptr || s_worker != nullptr) {
        return false;
    }
    strncpy(s_base_url, CONFIG_FAMILY_P1_BASE_URL, sizeof(s_base_url) - 1);
    if (!load_desk_token()) {
        ESP_LOGW(kTag, "desk token missing; HTTP poller idle");
    }
    s_session = session;
    s_jobs = xQueueCreate(kHttpQueueDepth, sizeof(HttpJob));
    s_results = xQueueCreate(kHttpQueueDepth, sizeof(HttpResult));
    if (s_jobs == nullptr || s_results == nullptr) {
        ESP_LOGE(kTag, "queue alloc failed jobs=%p results=%p free=%lu", (void *)s_jobs, (void *)s_results,
                 (unsigned long)esp_get_free_heap_size());
        product_login_http_worker_stop();
        return false;
    }
    if (xTaskCreate(worker_task, "login_http", kHttpWorkerStack, nullptr, 5, &s_worker) != pdPASS) {
        ESP_LOGE(kTag, "task create failed free=%lu", (unsigned long)esp_get_free_heap_size());
        product_login_http_worker_stop();
        return false;
    }
    ESP_LOGI(kTag, "HTTP worker ready base=%s", s_base_url);
    return true;
}

extern "C" void product_login_http_worker_stop(void)
{
    if (s_worker != nullptr) {
        vTaskDelete(s_worker);
        s_worker = nullptr;
    }
    if (s_jobs != nullptr) {
        vQueueDelete(s_jobs);
        s_jobs = nullptr;
    }
    if (s_results != nullptr) {
        vQueueDelete(s_results);
        s_results = nullptr;
    }
    s_session = nullptr;
    s_active_roster_id = 0;
    s_active_pin_id = 0;
    s_active_health = false;
    s_health_skip_id = 0;
    s_active_refresh_id = 0;
    s_active_invalidate = false;
}

extern "C" void product_login_http_service(fl_product_login::SessionWriter *session, int64_t now_ms)
{
    /* LVGL task stack is 7168 bytes. HttpResult carries a 4KB body; a local here
     * overflows that stack inside the roster log and reboots on the first hangout. */
    static HttpResult result;
    HttpJob job;
    char user[fl_product_login::kMaxId];
    char pin[8];
    char key[68];
    if (session == nullptr || s_worker == nullptr || s_bearer[0] == '\0') {
        return;
    }
    session->advance_to(now_ms);
    while (s_results != nullptr && xQueueReceive(s_results, &result, 0) == pdTRUE) {
        switch (result.kind) {
        case kJobRoster:
            handle_roster_result(result);
            if (result.operation_id == s_active_roster_id) {
                s_active_roster_id = 0;
            }
            break;
        case kJobPin:
            handle_pin_result(result);
            if (result.operation_id == s_active_pin_id) {
                s_active_pin_id = 0;
            }
            break;
        case kJobHealth:
            handle_health_result(result);
            s_active_health = false;
            if (s_session != nullptr && s_session->health_ack_pending() &&
                result.operation_id == s_session->health_operation_id()) {
                /* This beat failed. Do not queue it; the next scheduled beat is fresh. */
                s_health_skip_id = result.operation_id;
            }
            break;
        case kJobRefresh:
            handle_refresh_result(result);
            if (result.operation_id == s_active_refresh_id) {
                s_active_refresh_id = 0;
            }
            break;
        case kJobInvalidate:
            handle_invalidate_result(result);
            s_active_invalidate = false;
            break;
        default:
            break;
        }
    }
    if (session->roster_inflight() && session->roster_operation_id() != s_active_roster_id) {
        memset(&job, 0, sizeof(job));
        job.kind = kJobRoster;
        job.operation_id = session->roster_operation_id();
        job.timeout_ms = session->roster_request_timeout_ms();
        if (post_job(job)) {
            s_active_roster_id = job.operation_id;
        }
    }
    if (session->pin_transport_ready() && session->pin_operation_id() != s_active_pin_id &&
        session->copy_pin_request(user, (int)sizeof(user), pin, (int)sizeof(pin), key, (int)sizeof(key))) {
        memset(&job, 0, sizeof(job));
        job.kind = kJobPin;
        job.timeout_ms = fl_product_login::kPinAttemptTimeoutMs;
        job.operation_id = session->pin_operation_id();
        job.generation = session->pin_key_generation();
        strncpy(job.session_id, session->session_id(), sizeof(job.session_id) - 1);
        strncpy(job.user_id, user, sizeof(job.user_id) - 1);
        strncpy(job.pin, pin, sizeof(job.pin) - 1);
        wipe_buffer(pin, (int)sizeof(pin));
        wipe_buffer(key, (int)sizeof(key));
        if (post_job(job)) {
            s_active_pin_id = job.operation_id;
        }
    }
    if (session->health_ack_pending() && !s_active_health &&
        session->health_operation_id() != s_health_skip_id) {
        const char *reason = session->health_reason();
        memset(&job, 0, sizeof(job));
        job.kind = kJobHealth;
        job.operation_id = session->health_operation_id();
        job.timeout_ms = fl_product_login::kRequestTimeoutMs;
        job.muted = session->stage() == fl_product_login::kStageMuted ? 1 : 0;
        strncpy(job.session_id, session->session_id(), sizeof(job.session_id) - 1);
        strncpy(job.reason, reason != nullptr ? reason : "periodic", sizeof(job.reason) - 1);
        strncpy(job.boot_id, session->boot_id(), sizeof(job.boot_id) - 1);
        if (post_job(job)) {
            s_active_health = true;
        }
    }
    if (session->refresh_inflight() && session->refresh_operation_id() != s_active_refresh_id &&
        session->credential_active()) {
        memset(&job, 0, sizeof(job));
        job.kind = kJobRefresh;
        job.operation_id = session->refresh_operation_id();
        strncpy(job.bearer, session->credential_token(), sizeof(job.bearer) - 1);
        if (post_job(job)) {
            s_active_refresh_id = job.operation_id;
        }
    }
    if (!s_active_invalidate) {
        fl_product_login::InvalidateTicket ticket = session->take_invalidate_job(now_ms);
        if (ticket.active) {
            memset(&job, 0, sizeof(job));
            job.kind = kJobInvalidate;
            job.operation_id = ticket.pin_operation_id;
            job.invalidate_attempt = ticket.attempt;
            strncpy(job.bearer, ticket.session_token, sizeof(job.bearer) - 1);
            if (post_job(job)) {
                s_active_invalidate = true;
            }
        }
    }
}
