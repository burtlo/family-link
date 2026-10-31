#include "product_inbox_http_worker.hpp"

#include "product_inbox.hpp"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"

#include "fl_product_inbox_p1_wire.hpp"
#include "fl_product_inbox_wire.hpp"
#include "fl_product_login_session.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>

namespace
{

constexpr int kWorkerStack = 16384;
constexpr int kQueueDepth = 4;
constexpr int kUrlCap = 256;
constexpr int kAuthCap = 80;
constexpr int kInboxCap = 262144;
constexpr int kMetadataCap = 131072;
constexpr int kMediaSlice = 16384;
constexpr int kLastViewedCap = 1024;
constexpr const char *kTag = "product_inbox_http";

struct HashRun {
    int active;
    uint32_t operation_id;
    char message_id[48];
    uint32_t filled;
    mbedtls_sha256_context ctx;
};

struct HttpExchange {
    int perform_ok;
    int http_status;
    int body_len;
    int cap;
    int slice_capped;
    int oversize;
    int discard_body;
    int write_error;
    uint8_t *data;
    FILE *file;
    HashRun *hash;
};

QueueHandle_t s_jobs = nullptr;
QueueHandle_t s_results = nullptr;
TaskHandle_t s_worker = nullptr;
char s_base_url[kUrlCap];
product_inbox_http::Result s_polled;
HashRun s_audio_hash;
HashRun s_sketch_hash;

void *alloc_buf(size_t bytes)
{
    void *block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (block == nullptr) {
        block = malloc(bytes);
    }
    return block;
}

void wipe(void *buffer, size_t bytes)
{
    if (buffer != nullptr && bytes > 0) {
        memset(buffer, 0, bytes);
    }
}

void hash_clear(HashRun *run)
{
    if (run == nullptr) {
        return;
    }
    if (run->active) {
        mbedtls_sha256_free(&run->ctx);
    }
    memset(run, 0, sizeof(*run));
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

extern "C" esp_err_t product_inbox_http_event(esp_http_client_event_t *event)
{
    HttpExchange *out;
    int status;
    int room;
    int count;
    if (event == nullptr || event->user_data == nullptr) {
        return ESP_OK;
    }
    out = static_cast<HttpExchange *>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr || event->data_len <= 0) {
        return ESP_OK;
    }
    status = esp_http_client_get_status_code(event->client);
    if (status > 0) {
        out->http_status = status;
    }
    if (out->http_status == 401 || out->http_status == 403) {
        out->discard_body = 1;
        return ESP_OK;
    }
    if (out->file != nullptr) {
        int room;
        int count;
        if (out->cap > 0 && out->body_len >= out->cap) {
            out->slice_capped = 1;
            return ESP_FAIL;
        }
        room = out->cap > 0 ? out->cap - out->body_len : event->data_len;
        count = event->data_len < room ? event->data_len : room;
        if (count > 0) {
            if (fwrite(event->data, 1, (size_t)count, out->file) != (size_t)count) {
                out->write_error = 1;
                return ESP_FAIL;
            }
            if (out->hash != nullptr && out->hash->active &&
                mbedtls_sha256_update(&out->hash->ctx, static_cast<const uint8_t *>(event->data), (size_t)count) != 0) {
                out->write_error = 1;
                return ESP_FAIL;
            }
            out->body_len += count;
        }
        if (event->data_len > count) {
            out->slice_capped = 1;
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    if (out->cap <= 0) {
        return ESP_OK;
    }
    if (out->data == nullptr) {
        out->data = static_cast<uint8_t *>(alloc_buf((size_t)out->cap + 1));
        if (out->data == nullptr) {
            return ESP_FAIL;
        }
        memset(out->data, 0, (size_t)out->cap + 1);
    }
    if (out->body_len >= out->cap) {
        out->slice_capped = 1;
        if (out->cap >= kInboxCap) {
            out->oversize = 1;
        }
        return ESP_FAIL;
    }
    room = out->cap - out->body_len;
    count = event->data_len < room ? event->data_len : room;
    if (count > 0) {
        memcpy(out->data + out->body_len, event->data, (size_t)count);
        out->body_len += count;
    }
    if (event->data_len > count || out->body_len >= out->cap) {
        out->slice_capped = 1;
        if (out->cap >= kInboxCap && (event->data_len > count || out->body_len > kInboxCap)) {
            out->oversize = 1;
        }
        if (out->cap >= kInboxCap && event->data_len > count) {
            out->oversize = 1;
        }
        return ESP_FAIL;
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

void perform_exchange(const product_inbox_http::Job &job, HttpExchange *out)
{
    esp_http_client_config_t config = {};
    esp_http_client_handle_t client;
    char url[kUrlCap];
    char auth[kAuthCap];
    const int stream_file = out->file != nullptr;
    FILE *saved_file = out->file;
    HashRun *saved_hash = out->hash;
    const int saved_cap = out->cap;
    memset(out, 0, sizeof(*out));
    if (!join_url(job.path, url, (int)sizeof(url))) {
        if (stream_file) {
            out->file = saved_file;
            out->hash = saved_hash;
        }
        return;
    }
    if (stream_file) {
        out->file = saved_file;
        out->hash = saved_hash;
        out->cap = saved_cap;
    } else if (job.kind == product_inbox_http::kMetadataJob) {
        out->cap = kMetadataCap;
    } else if (job.kind == fl_product_inbox::kInboxWireInbox) {
        out->cap = kInboxCap;
    } else if (job.kind == fl_product_inbox::kInboxWireLastViewed) {
        out->cap = kLastViewedCap;
    } else {
        out->cap = kMediaSlice;
    }
    config.url = url;
    config.timeout_ms = job.timeout_ms > 0 ? job.timeout_ms : fl_product_login::kRequestTimeoutMs;
    config.method = method_of(job.method);
    config.event_handler = product_inbox_http_event;
    config.user_data = out;
    if (strncmp(url, "https://", 8) == 0) {
        config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(kTag, "http init failed cap=%d psram_big=%u int_big=%u", out->cap,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        return;
    }
    snprintf(auth, sizeof(auth), "Bearer %s", job.bearer);
    esp_http_client_set_header(client, "Authorization", auth);
    wipe(auth, sizeof(auth));
    if (job.range_header[0] != '\0') {
        esp_http_client_set_header(client, "Range", job.range_header);
    }
    if (job.body_len > 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, job.body, job.body_len);
    }
    if (esp_http_client_perform(client) == ESP_OK) {
        out->perform_ok = 1;
        out->http_status = esp_http_client_get_status_code(client);
    } else if (out->slice_capped && !out->oversize && out->body_len > 0 &&
               (out->http_status == 200 || out->http_status == 206)) {
        /* Open-ended Range is capped so this task can return to inbox and last-viewed. */
        out->perform_ok = 1;
    } else if (out->http_status == 401 || out->http_status == 403) {
        out->perform_ok = 1;
    } else {
        int status = esp_http_client_get_status_code(client);
        if (status > 0) {
            out->http_status = status;
        }
        ESP_LOGE(kTag, "http perform failed cap=%d psram_big=%u int_big=%u", out->cap,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    esp_http_client_cleanup(client);
}

void hex_encode(const uint8_t digest[32], char out[65])
{
    static const char *kDigits = "0123456789abcdef";
    int i;
    for (i = 0; i < 32; ++i) {
        out[i * 2] = kDigits[digest[i] >> 4];
        out[i * 2 + 1] = kDigits[digest[i] & 0x0f];
    }
    out[64] = 0;
}

void note_media_hash(const product_inbox_http::Job &job, const uint8_t *data, int len, int *full_asset,
                     int *hash_matches)
{
    HashRun *run = job.kind == fl_product_inbox::kInboxWireSketch ? &s_sketch_hash : &s_audio_hash;
    uint8_t digest[32];
    char encoded[65];
    *full_asset = 0;
    *hash_matches = 1;
    if (job.asset_bytes == 0 || data == nullptr || len <= 0) {
        return;
    }
    if (!run->active || run->operation_id != job.operation_id || run->filled != job.range_start ||
        strcmp(run->message_id, job.message_id) != 0) {
        hash_clear(run);
        if (job.range_start != 0) {
            if ((uint64_t)job.range_start + (uint64_t)len >= job.asset_bytes) {
                *full_asset = 1;
            }
            return;
        }
        mbedtls_sha256_init(&run->ctx);
        if (mbedtls_sha256_starts(&run->ctx, 0) != 0) {
            mbedtls_sha256_free(&run->ctx);
            return;
        }
        run->active = 1;
        run->operation_id = job.operation_id;
        strncpy(run->message_id, job.message_id, sizeof(run->message_id) - 1);
        run->filled = 0;
    }
    if (mbedtls_sha256_update(&run->ctx, data, (size_t)len) != 0) {
        hash_clear(run);
        return;
    }
    run->filled += (uint32_t)len;
    if (run->filled < job.asset_bytes) {
        return;
    }
    *full_asset = 1;
    if (mbedtls_sha256_finish(&run->ctx, digest) != 0) {
        hash_clear(run);
        return;
    }
    hex_encode(digest, encoded);
    hash_clear(run);
    if (job.sha256[0] != '\0' && strcmp(encoded, job.sha256) != 0) {
        *hash_matches = 0;
    }
}

void finish_inbox(const product_inbox_http::Job &job, HttpExchange *exchange, product_inbox_http::Result *result)
{
    fl_product_inbox::InboxParseResult *parsed;
    result->terminal = 1;
    if (exchange->oversize) {
        result->perform_ok = 1;
        if (result->http_status == 0) {
            result->http_status = 200;
        }
        return;
    }
    if (!exchange->perform_ok || exchange->http_status != 200 || exchange->data == nullptr ||
        exchange->body_len <= 0) {
        return;
    }
    if (job.kind == product_inbox_http::kMetadataJob) {
        fl_product_inbox::InboxMetadataHeader header{};
        const char *body = reinterpret_cast<const char *>(exchange->data);
        if (!fl_product_inbox::parse_inbox_metadata(body, exchange->body_len, exchange->body_len, &header)) return;
        const size_t bytes = sizeof(header) + sizeof(fl_product_inbox::InboxMessage) * header.message_count;
        auto *block = static_cast<fl_product_inbox::InboxMetadataHeader *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!block) { result->perform_ok = 0; return; }
        struct Fill { fl_product_inbox::InboxMessage *messages; int count; } fill{
            reinterpret_cast<fl_product_inbox::InboxMessage *>(block + 1), 0};
        auto append = [](void *context, const fl_product_inbox::InboxMessage &message) {
            auto *target = static_cast<Fill *>(context);
            target->messages[target->count++] = message;
            return true;
        };
        if (!fl_product_inbox::parse_inbox_metadata(body, exchange->body_len, exchange->body_len,
                                                    block, append, &fill)) { free(block); return; }
        // Validate uniqueness on the worker. Presentation applies metadata in
        // linear time and never builds a large automatic index/snapshot.
        auto **ids = static_cast<const char **>(heap_caps_malloc(
            sizeof(char *) * std::max(header.message_count, 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!ids) { free(block); result->perform_ok = 0; return; }
        for (int i = 0; i < header.message_count; ++i) ids[i] = fill.messages[i].message_id;
        std::sort(ids, ids + header.message_count,
                  [](const char *a, const char *b) { return strcmp(a, b) < 0; });
        bool duplicate = false;
        for (int i = 1; i < header.message_count; ++i) duplicate |= strcmp(ids[i - 1], ids[i]) == 0;
        free(ids);
        if (duplicate) { free(block); return; }
        result->parsed = block;
        return;
    }
    parsed = static_cast<fl_product_inbox::InboxParseResult *>(alloc_buf(sizeof(*parsed)));
    if (parsed == nullptr) {
        result->perform_ok = 0;
        result->http_status = 0;
        return;
    }
    if (!fl_product_inbox::parse_inbox_snapshot(reinterpret_cast<const char *>(exchange->data),
                                                exchange->body_len, exchange->body_len, parsed) ||
        parsed->overflow || parsed->oversize || !parsed->parse_ok) {
        free(parsed);
        return;
    }
    result->parsed = parsed;
}

void finish_media(const product_inbox_http::Job &job, HttpExchange *exchange, product_inbox_http::Result *result)
{
    result->terminal = 1;
    result->hash_matches = 1;
    if (!exchange->perform_ok || exchange->discard_body || exchange->http_status == 401 ||
        exchange->http_status == 403 || exchange->data == nullptr || exchange->body_len <= 0) {
        exchange->body_len = 0;
        return;
    }
    if (exchange->http_status != 200 && exchange->http_status != 206) {
        exchange->body_len = 0;
        return;
    }
    note_media_hash(job, exchange->data, exchange->body_len, &result->full_asset, &result->hash_matches);
    result->data = exchange->data;
    result->data_len = exchange->body_len;
    exchange->data = nullptr;
}

void ensure_parent_dirs(const char *file_path)
{
    char copy[96];
    char *slash;
    if (file_path == nullptr || file_path[0] == '\0') {
        return;
    }
    strncpy(copy, file_path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    for (slash = copy + 1; *slash != '\0'; ++slash) {
        if (*slash == '/') {
            *slash = '\0';
            mkdir(copy, 0755);
            *slash = '/';
        }
    }
}

bool begin_playback_hash(const product_inbox_http::Job &job, HashRun **out)
{
    HashRun *run = job.asset == 1 ? &s_sketch_hash : &s_audio_hash;
    *out = run;
    if (!run->active || run->operation_id != job.operation_id || run->filled != job.range_start ||
        strcmp(run->message_id, job.message_id) != 0) {
        hash_clear(run);
        if (job.range_start != 0) {
            return false;
        }
        mbedtls_sha256_init(&run->ctx);
        if (mbedtls_sha256_starts(&run->ctx, 0) != 0) {
            mbedtls_sha256_free(&run->ctx);
            return false;
        }
        run->active = 1;
        run->operation_id = job.operation_id;
        strncpy(run->message_id, job.message_id, sizeof(run->message_id) - 1);
        run->filled = 0;
    }
    return true;
}

void perform_playback(product_inbox_http::Job &job, product_inbox_http::Result *result)
{
    HashRun *run = nullptr;
    FILE *file;
    HttpExchange exchange;
    uint32_t remain;
    int expected;
    uint8_t digest[32];
    char encoded[65];
    result->hash_matches = 1;
    if (job.file_path[0] == '\0' || job.asset_bytes == 0 || job.range_start >= job.asset_bytes) {
        return;
    }
    if (!begin_playback_hash(job, &run)) {
        return;
    }
    ensure_parent_dirs(job.file_path);
    remain = job.asset_bytes - job.range_start;
    expected = remain > (uint32_t)kMediaSlice ? kMediaSlice : (int)remain;
    file = fopen(job.file_path, job.range_start == 0 ? "wb" : "r+b");
    if (file == nullptr) {
        hash_clear(run);
        return;
    }
    if (job.range_start != 0 && fseek(file, (long)job.range_start, SEEK_SET) != 0) {
        fclose(file);
        hash_clear(run);
        return;
    }
    memset(&exchange, 0, sizeof(exchange));
    exchange.file = file;
    exchange.hash = run;
    exchange.cap = expected;
    perform_exchange(job, &exchange);
    fflush(file);
    fclose(file);
    result->perform_ok = exchange.perform_ok;
    result->http_status = exchange.http_status;
    result->bytes_written = exchange.body_len > 0 ? (uint32_t)exchange.body_len : 0;
    if (exchange.write_error || !exchange.perform_ok || exchange.discard_body || exchange.http_status == 401 ||
        exchange.http_status == 403 || exchange.body_len <= 0 ||
        (exchange.http_status != 200 && exchange.http_status != 206)) {
        hash_clear(run);
        result->bytes_written = 0;
        return;
    }
    run->filled += (uint32_t)exchange.body_len;
    if (run->filled < job.asset_bytes) {
        return;
    }
    result->full_asset = 1;
    if (mbedtls_sha256_finish(&run->ctx, digest) != 0) {
        hash_clear(run);
        result->hash_matches = 0;
        remove(job.file_path);
        return;
    }
    hex_encode(digest, encoded);
    hash_clear(run);
    if (job.sha256[0] != '\0' && strcmp(encoded, job.sha256) != 0) {
        result->hash_matches = 0;
        remove(job.file_path);
    }
}

void perform_maintenance(const product_inbox_http::Job &job, product_inbox_http::Result *result)
{
    if (job.file_path[0] != '\0') {
        remove(job.file_path);
    }
    result->perform_ok = 1;
    result->http_status = 200;
    result->terminal = 1;
}

void perform_job(product_inbox_http::Job &job, product_inbox_http::Result *result)
{
    memset(result, 0, sizeof(*result));
    result->kind = job.kind;
    result->generation = job.generation;
    result->operation_id = job.operation_id;
    result->hash_matches = 1;
    result->terminal = 1;
    strncpy(result->message_id, job.message_id, sizeof(result->message_id) - 1);
    if (job.kind == product_inbox_http::kPlaybackMediaMaintenanceJob) {
        perform_maintenance(job, result);
        wipe(job.bearer, sizeof(job.bearer));
        ESP_LOGI(kTag, "kind=%d maintenance", result->kind);
        return;
    }
    if (job.kind == product_inbox_http::kPlaybackMediaJob) {
        perform_playback(job, result);
        wipe(job.bearer, sizeof(job.bearer));
        ESP_LOGI(kTag, "kind=%d http=%d perform=%d bytes=%u hash=%d full=%d", result->kind, result->http_status,
                 result->perform_ok, (unsigned)result->bytes_written, result->hash_matches, result->full_asset);
        return;
    }
    if (job.kind == fl_product_inbox::kInboxWireAudio || job.kind == fl_product_inbox::kInboxWireSketch) {
        fl_product_inbox::CarouselSession *session = product_inbox::session();
        int cancel = session == nullptr ||
                     fl_product_inbox::InboxWirePlanner::media_job_cancelled(*session, job.kind);
        if (cancel) {
            wipe(job.bearer, sizeof(job.bearer));
            ESP_LOGI(kTag, "kind=%d cancelled", result->kind);
            return;
        }
    }
    HttpExchange exchange;
    perform_exchange(job, &exchange);
    wipe(job.bearer, sizeof(job.bearer));
    result->perform_ok = exchange.perform_ok;
    result->http_status = exchange.http_status;
    if (job.kind == fl_product_inbox::kInboxWireInbox || job.kind == product_inbox_http::kMetadataJob) {
        finish_inbox(job, &exchange, result);
    } else if (job.kind == fl_product_inbox::kInboxWireAudio || job.kind == fl_product_inbox::kInboxWireSketch) {
        finish_media(job, &exchange, result);
    } else if (exchange.data != nullptr && exchange.body_len > 0 && !exchange.discard_body) {
        result->data = exchange.data;
        result->data_len = exchange.body_len;
        exchange.data = nullptr;
    }
    if (exchange.data != nullptr) {
        free(exchange.data);
        exchange.data = nullptr;
    }
    ESP_LOGI(kTag, "kind=%d http=%d perform=%d bytes=%d parsed=%d", result->kind, result->http_status,
             result->perform_ok, exchange.body_len, result->parsed != nullptr ? 1 : 0);
}

void worker_task(void *arg)
{
    product_inbox_http::Job job;
    static product_inbox_http::Result result;
    (void)arg;
    while (true) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        perform_job(job, &result);
        if (s_results != nullptr) {
            if (xQueueSend(s_results, &result, portMAX_DELAY) != pdTRUE) {
                if (result.data != nullptr) {
                    free(result.data);
                }
                if (result.parsed != nullptr) {
                    free(result.parsed);
                }
            }
        } else {
            if (result.data != nullptr) {
                free(result.data);
            }
            if (result.parsed != nullptr) {
                free(result.parsed);
            }
        }
    }
}

} /* namespace */

namespace product_inbox_http
{

bool worker_start()
{
    if (s_worker != nullptr) {
        return true;
    }
    strncpy(s_base_url, CONFIG_FAMILY_P1_BASE_URL, sizeof(s_base_url) - 1);
    s_base_url[sizeof(s_base_url) - 1] = '\0';
    s_jobs = xQueueCreate(kQueueDepth, sizeof(Job));
    s_results = xQueueCreate(kQueueDepth, sizeof(Result));
    if (s_jobs == nullptr || s_results == nullptr) {
        ESP_LOGE(kTag, "queue alloc failed");
        worker_stop();
        return false;
    }
    if (xTaskCreateWithCaps(worker_task, "inbox_http", kWorkerStack, nullptr, 5, &s_worker,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "task create failed");
        worker_stop();
        return false;
    }
    ESP_LOGI(kTag, "inbox HTTP worker ready");
    return true;
}

void worker_stop()
{
    if (s_worker != nullptr) {
        vTaskDeleteWithCaps(s_worker);
        s_worker = nullptr;
    }
    if (s_jobs != nullptr) {
        product_inbox_http::Job dropped;
        while (xQueueReceive(s_jobs, &dropped, 0) == pdTRUE) {
            wipe(dropped.bearer, sizeof(dropped.bearer));
        }
        vQueueDelete(s_jobs);
        s_jobs = nullptr;
    }
    if (s_results != nullptr) {
        product_inbox_http::Result dropped;
        while (xQueueReceive(s_results, &dropped, 0) == pdTRUE) {
            if (dropped.data != nullptr) {
                free(dropped.data);
            }
            if (dropped.parsed != nullptr) {
                free(dropped.parsed);
            }
        }
        vQueueDelete(s_results);
        s_results = nullptr;
    }
    hash_clear(&s_audio_hash);
    hash_clear(&s_sketch_hash);
}

bool submit(const Job *job)
{
    if (s_jobs == nullptr || job == nullptr) {
        return false;
    }
    return xQueueSend(s_jobs, job, 0) == pdTRUE;
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

} /* namespace product_inbox_http */
