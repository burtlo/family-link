/*
 * h05 — Hold mute: record ES7210 into PSRAM. Release: play on ES8311.
 * Feasibility killer: if this is noise or stalled I2S, the product is dead.
 *
 * I2S path (BOX-3, one shared duplex I2S, BSP-owned — do not hand-wire):
 *   Dual mics → ES7210 ADC  --I2S DIN GPIO16-->  ESP32-S3
 *   ESP32-S3  --I2S DOUT GPIO15-->  ES8311 DAC → PA GPIO46 → 1 W speaker
 *   Clocks: MCLK GPIO2, BCLK/SCLK GPIO17, WS/LCLK GPIO45
 *   Codec control: I2C SDA GPIO8 / SCL GPIO18
 *
 * bsp_audio_codec_*_init() calls bsp_audio_init() for that I2S bus.
 * Mic is opened only while mute is held; closed before playback so PA
 * (tied to ES8311 open/close) is on only while we write PCM. No wake word.
 * No Wi-Fi.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "board.h"
#include "pass.h"

static const char *TAG = "h05";

#define SAMPLE_RATE     16000
#define CHANNELS        1
#define BITS            16
#define MAX_SECONDS     10
#define MIN_PASS_MS     300
#define CHUNK_BYTES     640  /* 20 ms of 16 kHz s16le mono */
#define SPK_VOLUME      70
#define MIC_GAIN_DB     42.0f

#define BYTES_PER_MS    ((SAMPLE_RATE * CHANNELS * (BITS / 8)) / 1000)
#define PCM_CAP         (SAMPLE_RATE * CHANNELS * (BITS / 8) * MAX_SECONDS)
#define MIN_PASS_BYTES  (BYTES_PER_MS * MIN_PASS_MS)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = CHANNELS,
    .bits_per_sample = BITS,
};

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_pcm;
static volatile bool s_held;
static volatile bool s_passed;
static volatile bool s_failed;
static SemaphoreHandle_t s_down_sem;

static void mute_down_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    s_held = true;
    if (s_down_sem != NULL) {
        xSemaphoreGive(s_down_sem);
    }
    ESP_LOGI(TAG, "DOWN t=%u ms (record)", (unsigned)esp_log_timestamp());
}

static void mute_up_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    s_held = false;
    ESP_LOGI(TAG, "UP t=%u ms (stop record, then play)", (unsigned)esp_log_timestamp());
}

static void fail_once(const char *reason)
{
    if (s_failed) {
        return;
    }
    s_failed = true;
    ESP_LOGE(TAG, "%s", reason);
    demo_fail("h05", reason);
}

static size_t record_while_held(void)
{
    size_t filled = 0;

    esp_err_t err = esp_codec_dev_open(s_mic, &s_fs);
    if (err != ESP_OK) {
        fail_once("mic open failed");
        return 0;
    }
    (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);

    while (s_held && filled + CHUNK_BYTES <= PCM_CAP) {
        int r = esp_codec_dev_read(s_mic, s_pcm + filled, CHUNK_BYTES);
        if (r != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_read %d after %u bytes", r, (unsigned)filled);
            break;
        }
        filled += CHUNK_BYTES;
    }

    /* Mic off as soon as the hold ends or the 10 s cap is hit. */
    (void)esp_codec_dev_close(s_mic);
    ESP_LOGI(TAG, "recorded %u bytes (%u ms) 16 kHz s16le mono",
             (unsigned)filled, (unsigned)(filled / BYTES_PER_MS));
    return filled;
}

static bool play_buffer(size_t nbytes)
{
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);

    esp_err_t err = esp_codec_dev_open(s_spk, &s_fs);
    if (err != ESP_OK) {
        fail_once("speaker open failed");
        return false;
    }
    /* PA GPIO46 is asserted by ES8311 while this handle is open. */

    size_t off = 0;
    while (off < nbytes) {
        int n = (int)((nbytes - off) > 2048u ? 2048u : (nbytes - off));
        int w = esp_codec_dev_write(s_spk, s_pcm + off, n);
        if (w != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(s_spk);
            fail_once("speaker write failed");
            return false;
        }
        off += (size_t)n;
    }

    (void)esp_codec_dev_close(s_spk); /* PA off */
    ESP_LOGI(TAG, "played %u bytes", (unsigned)nbytes);
    return true;
}

static bool init_buttons(void)
{
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MUTE] == NULL) {
        fail_once("mute button init failed");
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s (mute handle ok)", esp_err_to_name(err));
    }
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, mute_down_cb, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, mute_up_cb, NULL);
    return true;
}

void app_main(void)
{
    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        fail_once("ES8311 speaker init failed");
        return;
    }
    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == NULL) {
        fail_once("ES7210 mic init failed");
        return;
    }

    s_pcm = heap_caps_malloc(PCM_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pcm == NULL) {
        ESP_LOGW(TAG, "PSRAM alloc failed, falling back to SRAM");
        s_pcm = heap_caps_malloc(PCM_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_pcm == NULL) {
        fail_once("pcm buffer alloc failed");
        return;
    }
    ESP_LOGI(TAG, "pcm buffer %u bytes @ %p", (unsigned)PCM_CAP, (void *)s_pcm);

    s_down_sem = xSemaphoreCreateBinary();
    if (s_down_sem == NULL) {
        fail_once("semaphore alloc failed");
        return;
    }

    if (!init_buttons()) {
        return;
    }

    if (board_display_start() == ESP_OK) {
        board_status_set("hold mute to record\nrelease to play");
    }

    ESP_LOGI(TAG, "hold mute to record (cap %ds); release to play. No wake word.", MAX_SECONDS);

    while (!s_failed) {
        if (xSemaphoreTake(s_down_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!s_held || s_failed) {
            continue;
        }

        size_t n = record_while_held();
        while (s_held && !s_failed) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_failed) {
            break;
        }
        if (n < MIN_PASS_BYTES) {
            ESP_LOGW(TAG, "clip too short (%u bytes, need > %u / 0.3s); hold mute",
                     (unsigned)n, (unsigned)MIN_PASS_BYTES);
            continue;
        }
        if (!play_buffer(n)) {
            break;
        }
        if (!s_passed) {
            s_passed = true;
            demo_pass("h05");
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
