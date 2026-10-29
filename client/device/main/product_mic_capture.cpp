#include "product_mic.hpp"

#include "product_mem.hpp"
#include "product_record.hpp"

#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

namespace
{

constexpr const char *kTag = "product_mic";
constexpr int kMicTaskStack = 6144;
/* pcm_s16le, 16000 Hz, one channel (message-delivery COM-OPEN-002). */
constexpr int kSampleRate = 16000;
constexpr size_t kReadBytes = 640;  /* 20 ms */
constexpr size_t kPushBytes = 2048; /* 64 ms per storage append */
constexpr float kInputGainDb = 42.0f;

TaskHandle_t s_task = nullptr;
volatile int s_armed = 0;
volatile int s_failed = 0;
esp_codec_dev_handle_t s_mic = nullptr;
uint8_t *s_chunk = nullptr;

bool open_mic()
{
    esp_codec_dev_sample_info_t fs = {};
    if (s_mic == nullptr) {
        return false;
    }
    fs.sample_rate = kSampleRate;
    fs.channel = 1;
    fs.bits_per_sample = 16;
    if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) {
        return false;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, kInputGainDb);
    return true;
}

void fail(const char *reason)
{
    ESP_LOGW(kTag, "mic_failed reason=%s", reason);
    s_armed = 0;
    s_failed = 1;
}

void mic_task(void *arg)
{
    bool open = false;
    size_t fill = 0;
    (void)arg;
    for (;;) {
        if (s_armed == 0) {
            if (open) {
                (void)esp_codec_dev_close(s_mic);
                open = false;
            }
            fill = 0;
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            continue;
        }
        if (!open) {
            if (!open_mic()) {
                fail("open");
                continue;
            }
            open = true;
            fill = 0;
            product_mem::probe_heap("mic_capture_open", 1);
        }
        if (esp_codec_dev_read(s_mic, s_chunk + fill, (int)kReadBytes) != ESP_CODEC_DEV_OK) {
            fail("read");
            continue;
        }
        fill += kReadBytes;
        if (fill + kReadBytes > kPushBytes) {
            /* Refused outside interactive recording (wipe, finish, cancel). */
            (void)product_record::push_pcm(s_chunk, fill);
            fill = 0;
        }
    }
}

bool start_task()
{
    if (s_task != nullptr) {
        return true;
    }
    if (s_chunk == nullptr) {
        s_chunk = static_cast<uint8_t *>(heap_caps_malloc(kPushBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (s_chunk == nullptr) {
            return false;
        }
    }
    return xTaskCreateWithCaps(mic_task, "product_mic", kMicTaskStack, nullptr, 5, &s_task,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) == pdPASS;
}

} /* namespace */

namespace product_mic
{

bool init()
{
    if (s_mic != nullptr) {
        return true;
    }
    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == nullptr) {
        ESP_LOGE(kTag, "mic_init_failed");
        return false;
    }
    ESP_LOGI(kTag, "mic_init_ok");
    return true;
}

void set_armed(bool armed)
{
    if (!armed) {
        s_armed = 0;
        return;
    }
    if (s_armed != 0) {
        return;
    }
    if (!start_task()) {
        fail("task");
        return;
    }
    s_armed = 1;
    xTaskNotifyGive(s_task);
}

bool take_failure()
{
    if (s_failed == 0) {
        return false;
    }
    s_failed = 0;
    return true;
}

} /* namespace product_mic */
