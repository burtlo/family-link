#include "product_send_http_worker.hpp"

#include "product_mem.hpp"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

namespace
{

constexpr int kHttpBodyCap = 4096;
constexpr int kHttpWorkerStack = 16384;
constexpr int kUrlCap = 256;
constexpr int kAuthCap = 256;
constexpr int kIdCap = 48;
constexpr const char *kTag = "product_send_http";

struct HttpJob {
    char method[8];
    char path[160];
    const uint8_t *body;
    uint32_t body_len;
    int is_json;
    uint32_t seq;
    char client_message_id[kIdCap];
    char bearer[kAuthCap];
};

struct HttpExchange {
    int perform_ok;
    int http_status;
    int body_len;
    char body[kHttpBodyCap];
};

QueueHandle_t s_jobs = nullptr;
QueueHandle_t s_results = nullptr;
TaskHandle_t s_worker = nullptr;
char s_base_url[kUrlCap];
product_send_http::Result s_polled;

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

extern "C" esp_err_t product_send_http_event(esp_http_client_event_t *event)
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

esp_http_client_method_t method_of(const char *method)
{
    if (method != nullptr && strcmp(method, "PUT") == 0) {
        return HTTP_METHOD_PUT;
    }
    if (method != nullptr && strcmp(method, "POST") == 0) {
        return HTTP_METHOD_POST;
    }
    return HTTP_METHOD_GET;
}

void perform_exchange(const HttpJob &job, HttpExchange *out)
{
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    char url[kUrlCap];
    char auth[kAuthCap + 16];
    memset(out, 0, sizeof(*out));
    if (!join_url(job.path, url, (int)sizeof(url))) {
        return;
    }
    config.url = url;
    config.timeout_ms = 5000;
    config.method = method_of(job.method);
    config.event_handler = product_send_http_event;
    config.user_data = out;
    client = esp_http_client_init(&config);
    if (client == nullptr) {
        return;
    }
    snprintf(auth, sizeof(auth), "Bearer %s", job.bearer);
    esp_http_client_set_header(client, "Authorization", auth);
    memset(auth, 0, sizeof(auth));
    if (job.body != nullptr && job.body_len > 0) {
        esp_http_client_set_header(client, "Content-Type",
                                   job.is_json ? "application/json" : "application/octet-stream");
        esp_http_client_set_post_field(client, reinterpret_cast<const char *>(job.body), (int)job.body_len);
    }
    if (esp_http_client_perform(client) == ESP_OK) {
        out->perform_ok = 1;
        out->http_status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
}

void worker_task(void *arg)
{
    HttpJob job;
    static product_send_http::Result result;
    static HttpExchange exchange;
    (void)arg;
    while (true) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        memset(&result, 0, sizeof(result));
        result.seq = job.seq;
        strncpy(result.client_message_id, job.client_message_id, sizeof(result.client_message_id) - 1);
        product_mem::probe_heap("send_worker_begin", (int32_t)job.body_len);
        perform_exchange(job, &exchange);
        memset(job.bearer, 0, sizeof(job.bearer));
        product_mem::probe_heap("send_worker_end", exchange.perform_ok ? exchange.http_status : -1);
        result.perform_ok = exchange.perform_ok;
        result.http_status = exchange.http_status;
        result.body_len = exchange.body_len;
        if (exchange.body_len > 0 && exchange.body_len < (int)sizeof(result.body)) {
            memcpy(result.body, exchange.body, (size_t)exchange.body_len);
            result.body[exchange.body_len] = '\0';
        }
        if (s_results != nullptr) {
            xQueueSend(s_results, &result, portMAX_DELAY);
        }
    }
}

} /* namespace */

namespace product_send_http
{

bool worker_start()
{
    if (s_worker != nullptr) {
        return true;
    }
    strncpy(s_base_url, CONFIG_FAMILY_P1_BASE_URL, sizeof(s_base_url) - 1);
    s_jobs = xQueueCreate(1, sizeof(HttpJob));
    s_results = xQueueCreate(1, sizeof(Result));
    if (s_jobs == nullptr || s_results == nullptr) {
        ESP_LOGE(kTag, "queue alloc failed");
        worker_stop();
        return false;
    }
    if (xTaskCreateWithCaps(worker_task, "send_http", kHttpWorkerStack, nullptr, 5, &s_worker,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "task create failed");
        worker_stop();
        return false;
    }
    ESP_LOGI(kTag, "send HTTP worker ready");
    product_mem::probe_heap("send_http_worker_ready", 0);
    return true;
}

void worker_stop()
{
    if (s_worker != nullptr) {
        vTaskDeleteWithCaps(s_worker);
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
}

bool submit(const char *method, const char *path, const uint8_t *body, uint32_t body_len, int is_json,
            const char *client_message_id, uint32_t seq, const char *bearer)
{
    HttpJob job;
    bool queued;
    if (s_jobs == nullptr || method == nullptr || path == nullptr || client_message_id == nullptr ||
        bearer == nullptr || bearer[0] == '\0') {
        return false;
    }
    memset(&job, 0, sizeof(job));
    strncpy(job.method, method, sizeof(job.method) - 1);
    strncpy(job.path, path, sizeof(job.path) - 1);
    job.body = body;
    job.body_len = body_len;
    job.is_json = is_json;
    job.seq = seq;
    strncpy(job.client_message_id, client_message_id, sizeof(job.client_message_id) - 1);
    strncpy(job.bearer, bearer, sizeof(job.bearer) - 1);
    queued = xQueueSend(s_jobs, &job, 0) == pdTRUE;
    memset(job.bearer, 0, sizeof(job.bearer));
    return queued;
}

const Result *poll()
{
    if (s_results == nullptr) {
        return nullptr;
    }
    if (xQueueReceive(s_results, &s_polled, 0) != pdTRUE) {
        return nullptr;
    }
    return &s_polled;
}

} /* namespace product_send_http */
