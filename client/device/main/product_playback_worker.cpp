#include "product_playback_worker.hpp"

#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace playback_worker
{
namespace
{
constexpr const char *kTag = "product_playback";
constexpr int kQueueDepth = 2;
constexpr int kSlice = 16384;
constexpr int kRing = 4096;
constexpr int kSampleRate = 16000;

QueueHandle_t jobs;
QueueHandle_t results;
TaskHandle_t http_task;
TaskHandle_t audio_task;
char base_url[256];
PlaybackFetchResult polled;
bool http_started;
bool quiesce_requested;
bool quiescent;
bool http_busy;

esp_codec_dev_handle_t speaker;
bool codec_open;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t ring[kRing];
int ring_head;
int ring_tail;
int base_ms;
int written_samples;
bool output_running;
bool output_fault;
bool audio_quit;

int ringUsed()
{
    return (ring_head - ring_tail + kRing) % kRing;
}

void *alloc(size_t bytes)
{
    void *block = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!block)
        block = std::malloc(bytes);
    return block;
}

bool joinUrl(const char *suffix, char *out, int cap)
{
    if (!suffix || !out || cap < 16)
        return false;
    std::snprintf(out, (size_t)cap, "%s", base_url);
    const int base_len = (int)std::strlen(out);
    if (base_len > 0 && out[base_len - 1] == '/' && suffix[0] == '/')
        ++suffix;
    else if (base_len > 0 && out[base_len - 1] != '/' && suffix[0] != '/') {
        if (base_len + 1 >= cap)
            return false;
        out[base_len] = '/';
        out[base_len + 1] = 0;
    }
    if ((int)std::strlen(out) + (int)std::strlen(suffix) >= cap)
        return false;
    std::strcat(out, suffix);
    return true;
}

struct Exchange {
    int perform_ok;
    int http_status;
    int body_len;
    int cap;
    int slice_capped;
    int discard;
    uint8_t *data;
};

esp_err_t onHttp(esp_http_client_event_t *event)
{
    if (!event || !event->user_data || event->event_id != HTTP_EVENT_ON_DATA || !event->data || event->data_len <= 0)
        return ESP_OK;
    auto *out = static_cast<Exchange *>(event->user_data);
    const int status = esp_http_client_get_status_code(event->client);
    if (status > 0)
        out->http_status = status;
    if (out->http_status == 401 || out->http_status == 403) {
        out->discard = 1;
        return ESP_OK;
    }
    if (!out->data) {
        out->data = static_cast<uint8_t *>(alloc((size_t)out->cap));
        if (!out->data)
            return ESP_FAIL;
    }
    if (out->body_len >= out->cap) {
        out->slice_capped = 1;
        return ESP_FAIL;
    }
    const int room = out->cap - out->body_len;
    const int count = event->data_len < room ? event->data_len : room;
    std::memcpy(out->data + out->body_len, event->data, (size_t)count);
    out->body_len += count;
    if (event->data_len > count) {
        out->slice_capped = 1;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void perform(const PlaybackFetchJob &job, PlaybackFetchResult *result)
{
    *result = {};
    result->operation = job.operation;
    result->generation = job.generation;
    result->epoch = job.epoch;
    result->kind = job.kind;
    result->terminal = 1;
    std::snprintf(result->message_id, sizeof(result->message_id), "%s", job.message_id);
    if (quiesce_requested)
        return;
    Exchange exchange{};
    exchange.cap = kSlice;
    char url[256];
    if (!joinUrl(job.path, url, (int)sizeof(url)))
        return;
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = job.timeout_ms > 0 ? job.timeout_ms : 6000;
    config.method = HTTP_METHOD_GET;
    config.event_handler = onHttp;
    config.user_data = &exchange;
    if (std::strncmp(url, "https://", 8) == 0)
        config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
        return;
    char auth[80];
    std::snprintf(auth, sizeof(auth), "Bearer %s", job.bearer);
    esp_http_client_set_header(client, "Authorization", auth);
    std::memset(auth, 0, sizeof(auth));
    if (job.range_header[0])
        esp_http_client_set_header(client, "Range", job.range_header);
    const esp_err_t performed = esp_http_client_perform(client);
    if (performed == ESP_OK) {
        exchange.perform_ok = 1;
        exchange.http_status = esp_http_client_get_status_code(client);
    } else if (exchange.slice_capped && exchange.body_len > 0 &&
               (exchange.http_status == 200 || exchange.http_status == 206)) {
        exchange.perform_ok = 1;
    } else if (exchange.http_status == 401 || exchange.http_status == 403) {
        exchange.perform_ok = 1;
        exchange.body_len = 0;
    }
    esp_http_client_cleanup(client);
    result->perform_ok = exchange.perform_ok;
    result->http_status = exchange.http_status;
    if (!exchange.discard && exchange.body_len > 0) {
        result->data = exchange.data;
        result->data_len = exchange.body_len;
        exchange.data = nullptr;
    }
    std::free(exchange.data);
}

void httpRun(void *)
{
    PlaybackFetchJob job;
    while (!audio_quit) {
        if (xQueueReceive(jobs, &job, pdMS_TO_TICKS(50)) != pdTRUE) {
            if (quiesce_requested)
                quiescent = true;
            continue;
        }
        http_busy = true;
        quiescent = false;
        PlaybackFetchResult result;
        perform(job, &result);
        std::memset(job.bearer, 0, sizeof(job.bearer));
        if (xQueueSend(results, &result, portMAX_DELAY) != pdTRUE && result.data)
            std::free(result.data);
        http_busy = false;
        if (quiesce_requested && uxQueueMessagesWaiting(jobs) == 0)
            quiescent = true;
    }
    quiescent = true;
    vTaskDelete(nullptr);
}

bool ensureCodec()
{
    if (codec_open)
        return true;
    if (!speaker)
        speaker = bsp_audio_codec_speaker_init();
    if (!speaker)
        return false;
    esp_codec_dev_sample_info_t format = {};
    format.sample_rate = kSampleRate;
    format.channel = 1;
    format.bits_per_sample = 16;
    if (esp_codec_dev_open(speaker, &format) != ESP_CODEC_DEV_OK)
        return false;
    codec_open = true;
    return true;
}

void applyVolume(int codec, bool silent)
{
    if (!speaker)
        return;
    (void)esp_codec_dev_set_out_vol(speaker, silent ? 0 : codec);
    (void)esp_codec_dev_set_out_mute(speaker, silent);
}

void audioRun(void *)
{
    uint8_t chunk[640];
    while (!audio_quit) {
        bool running;
        int count = 0;
        portENTER_CRITICAL(&mux);
        running = output_running && !output_fault;
        if (running) {
            const int used = ringUsed();
            count = used < (int)sizeof(chunk) ? used : (int)sizeof(chunk);
            for (int i = 0; i < count; ++i) {
                chunk[i] = ring[ring_tail];
                ring_tail = (ring_tail + 1) % kRing;
            }
        }
        portEXIT_CRITICAL(&mux);
        if (!running || count <= 0) {
            // The tick is 10 ms. A 5 ms delay truncates to 0, so this priority-8
            // task yields without blocking and starves the idle watchdog.
            vTaskDelay(1);
            continue;
        }
        if (esp_codec_dev_write(speaker, chunk, count) != ESP_CODEC_DEV_OK) {
            portENTER_CRITICAL(&mux);
            output_fault = true;
            portEXIT_CRITICAL(&mux);
            ESP_LOGE(kTag, "speaker_write_failed");
            continue;
        }
        portENTER_CRITICAL(&mux);
        written_samples += count / 2;
        portEXIT_CRITICAL(&mux);
    }
    vTaskDelete(nullptr);
}

bool audioStart(void *, int position_ms, int codec_volume, bool silent)
{
    if (!ensureCodec())
        return false;
    applyVolume(codec_volume, silent);
    portENTER_CRITICAL(&mux);
    ring_head = ring_tail = 0;
    base_ms = position_ms;
    written_samples = 0;
    output_fault = false;
    output_running = true;
    portEXIT_CRITICAL(&mux);
    return true;
}

void audioPause(void *)
{
    portENTER_CRITICAL(&mux);
    output_running = false;
    portEXIT_CRITICAL(&mux);
}

void audioStop(void *)
{
    portENTER_CRITICAL(&mux);
    output_running = false;
    ring_head = ring_tail = 0;
    portEXIT_CRITICAL(&mux);
}

int audioPlayed(void *)
{
    portENTER_CRITICAL(&mux);
    const int ms = base_ms + written_samples / (kSampleRate / 1000);
    portEXIT_CRITICAL(&mux);
    return ms;
}

int audioAccept(void *, const uint8_t *pcm, int bytes)
{
    if (!pcm || bytes <= 0)
        return 0;
    portENTER_CRITICAL(&mux);
    if (output_fault) {
        portEXIT_CRITICAL(&mux);
        return -1;
    }
    if (!output_running) {
        portEXIT_CRITICAL(&mux);
        return 0;
    }
    const int space = kRing - 1 - ringUsed();
    if (bytes > space) {
        portEXIT_CRITICAL(&mux);
        return 0;
    }
    for (int i = 0; i < bytes; ++i) {
        ring[ring_head] = pcm[i];
        ring_head = (ring_head + 1) % kRing;
    }
    portEXIT_CRITICAL(&mux);
    return bytes;
}

void audioVolume(void *, int codec_volume, bool silent)
{
    applyVolume(codec_volume, silent);
}

bool workerStart()
{
    if (http_started)
        return true;
    std::snprintf(base_url, sizeof(base_url), "%s", CONFIG_FAMILY_P1_BASE_URL);
    jobs = xQueueCreate(kQueueDepth, sizeof(PlaybackFetchJob));
    results = xQueueCreate(kQueueDepth, sizeof(PlaybackFetchResult));
    if (!jobs || !results)
        return false;
    quiesce_requested = false;
    quiescent = false;
    audio_quit = false;
    if (xTaskCreateWithCaps(httpRun, "play_http", 12288, nullptr, 5, &http_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
        pdPASS)
        return false;
    if (xTaskCreate(audioRun, "play_pcm", 4096, nullptr, 8, &audio_task) != pdPASS)
        return false;
    http_started = true;
    ESP_LOGI(kTag, "playback worker ready");
    return true;
}

bool workerSubmit(const PlaybackFetchJob *job)
{
    if (!jobs || !job || quiesce_requested)
        return false;
    return xQueueSend(jobs, job, 0) == pdTRUE;
}

const PlaybackFetchResult *workerPoll()
{
    if (!results)
        return nullptr;
    if (xQueueReceive(results, &polled, 0) != pdTRUE)
        return nullptr;
    return &polled;
}

void workerRelease(const PlaybackFetchResult *result)
{
    if (result && result->data)
        std::free(result->data);
}

void workerQuiesce()
{
    quiesce_requested = true;
}

bool workerQuiescent()
{
    return quiescent && !http_busy;
}
} // namespace

PlaybackTransport transport()
{
    PlaybackTransport out;
    out.start = workerStart;
    out.submit = workerSubmit;
    out.poll = workerPoll;
    out.release = workerRelease;
    out.request_quiesce = workerQuiesce;
    out.quiescent = workerQuiescent;
    return out;
}

AudioEndpoint audio()
{
    AudioEndpoint out;
    out.start = audioStart;
    out.pause = audioPause;
    out.stop = audioStop;
    out.played_ms = audioPlayed;
    out.accept = audioAccept;
    out.set_volume = audioVolume;
    return out;
}
} // namespace playback_worker
} // namespace product_ui
