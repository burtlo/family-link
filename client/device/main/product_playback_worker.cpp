#include "product_playback_worker.hpp"

#include "bsp/esp-box-3.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace product_ui
{
namespace playback_worker
{
namespace
{
constexpr const char *kTag = "product_playback";
constexpr int kRing = 4096;
constexpr int kSampleRate = 16000;

TaskHandle_t audio_task;
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
bool audio_started;

int ringUsed()
{
    return (ring_head - ring_tail + kRing) % kRing;
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
    if (audio_started)
        return true;
    audio_quit = false;
    if (xTaskCreate(audioRun, "play_pcm", 4096, nullptr, 8, &audio_task) != pdPASS)
        return false;
    audio_started = true;
    ESP_LOGI(kTag, "playback audio ready");
    return true;
}
} // namespace

PlaybackTransport transport()
{
    PlaybackTransport out;
    out.start = workerStart;
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
