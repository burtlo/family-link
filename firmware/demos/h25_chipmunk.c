/*
 * h25 — Hold the red circle, record a short memo, release: play it back
 * as a chipmunk (pitch up + faster). Island demo: no Wi-Fi.
 *
 * Same I2S path as h05. Do not use the top mute latch as PTT — when it
 * is down the mics are hardware-muted (see h08).
 *
 * Chipmunk: linear-interpolate 16 kHz s16le at 5/3 (~+9 semitones).
 * Drop the first ~80 ms so the button/codec click is not part of the take.
 *
 * Speaker stays open for the whole demo (h18): close/reopen was leaving
 * GPIO46 PA off. Mute the DAC while recording so the cone does not loop
 * into the mics.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "board.h"
#include "pass.h"

static const char *TAG = "h25";

#define SAMPLE_RATE     16000
#define CHANNELS        1
#define BITS            16
#define MAX_SECONDS     8
#define MIN_PASS_MS     300
#define CLICK_MS        80
#define DRAIN_MS        80
#define CHUNK_BYTES     640  /* 20 ms of 16 kHz s16le mono */
#define WRITE_CHUNK     2048
#define SPK_VOLUME      90
#define MIC_GAIN_DB     42.0f

/* 5/3 ≈ 1.67×: higher pitch and shorter clip, still a few words. */
#define PITCH_NUM       5
#define PITCH_DEN       3

#define BYTES_PER_MS    ((SAMPLE_RATE * CHANNELS * (BITS / 8)) / 1000)
#define PCM_CAP         (SAMPLE_RATE * CHANNELS * (BITS / 8) * MAX_SECONDS)
#define PLAY_CAP        ((PCM_CAP / sizeof(int16_t)) * PITCH_DEN / PITCH_NUM * sizeof(int16_t))
#define MIN_PASS_BYTES  (BYTES_PER_MS * MIN_PASS_MS)
#define CLICK_BYTES     (BYTES_PER_MS * CLICK_MS)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = CHANNELS,
    .bits_per_sample = BITS,
};

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_pcm;
static uint8_t *s_play;
static volatile bool s_held;
static volatile bool s_passed;
static volatile bool s_failed;
static SemaphoreHandle_t s_down_sem;

static bool mute_latched(void)
{
    /* Active-low. 0 = mute engaged = analog mics dead. */
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void ptt_down(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    s_held = true;
    if (s_down_sem != NULL) {
        xSemaphoreGive(s_down_sem);
    }
    ESP_LOGI(TAG, "DOWN t=%u ms (record)", (unsigned)esp_log_timestamp());
}

static void ptt_up(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    s_held = false;
    ESP_LOGI(TAG, "UP t=%u ms (stop record, then chipmunk)", (unsigned)esp_log_timestamp());
}

static void fail_once(const char *reason)
{
    if (s_failed) {
        return;
    }
    s_failed = true;
    ESP_LOGE(TAG, "%s", reason);
    demo_fail("h25", reason);
}

static int16_t pcm_peak(const int16_t *s, size_t n)
{
    int16_t peak = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t a = s[i];
        if (a < 0) {
            a = (int16_t)(-a);
        }
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static bool write_pcm(const uint8_t *p, size_t nbytes)
{
    while (nbytes > 0) {
        int n = (int)(nbytes > WRITE_CHUNK ? WRITE_CHUNK : nbytes);
        if (esp_codec_dev_write(s_spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            fail_once("speaker write failed");
            return false;
        }
        p += (size_t)n;
        nbytes -= (size_t)n;
    }
    return true;
}

static bool speaker_open_once(void)
{
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        fail_once("speaker open failed");
        return false;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    /* PA GPIO46 needs a beat after enable; first I2S write after open can be a no-op. */
    vTaskDelay(pdMS_TO_TICKS(50));
    uint8_t z[CHUNK_BYTES];
    memset(z, 0, sizeof(z));
    (void)esp_codec_dev_write(s_spk, z, sizeof(z));
    return true;
}

static void speaker_drain(void)
{
    uint8_t z[CHUNK_BYTES];
    memset(z, 0, sizeof(z));
    int left = SAMPLE_RATE * DRAIN_MS / 1000 * (BITS / 8);
    while (left > 0) {
        int n = left > (int)sizeof(z) ? (int)sizeof(z) : left;
        if (esp_codec_dev_write(s_spk, z, n) != ESP_CODEC_DEV_OK) {
            break;
        }
        left -= n;
    }
}

static size_t record_while_held(void)
{
    size_t filled = 0;

    (void)esp_codec_dev_set_out_mute(s_spk, true);

    esp_err_t err = esp_codec_dev_open(s_mic, &s_fs);
    if (err != ESP_OK) {
        (void)esp_codec_dev_set_out_mute(s_spk, false);
        fail_once("mic open failed");
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);

    while (s_held && filled + CHUNK_BYTES <= PCM_CAP) {
        int r = esp_codec_dev_read(s_mic, s_pcm + filled, CHUNK_BYTES);
        if (r != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "esp_codec_dev_read %d after %u bytes", r, (unsigned)filled);
            break;
        }
        filled += CHUNK_BYTES;
    }

    (void)esp_codec_dev_close(s_mic);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    ESP_LOGI(TAG, "recorded %u bytes (%u ms) 16 kHz s16le mono",
             (unsigned)filled, (unsigned)(filled / BYTES_PER_MS));
    return filled;
}

/* Linear-interpolate in[] at PITCH_NUM/PITCH_DEN into a stable play buffer. */
static size_t chipmunk_fill(const int16_t *in, size_t in_n, int16_t *out, size_t out_cap)
{
    size_t out_n = (in_n * (size_t)PITCH_DEN) / (size_t)PITCH_NUM;
    if (out_n > out_cap) {
        out_n = out_cap;
    }
    for (size_t i = 0; i < out_n; i++) {
        uint32_t src = (uint32_t)i * (uint32_t)PITCH_NUM;
        size_t idx = (size_t)(src / (uint32_t)PITCH_DEN);
        uint32_t frac = src % (uint32_t)PITCH_DEN;
        if (idx >= in_n) {
            out[i] = 0;
            continue;
        }
        int32_t a = in[idx];
        int32_t b = (idx + 1 < in_n) ? in[idx + 1] : a;
        out[i] = (int16_t)((a * (int32_t)(PITCH_DEN - frac) + b * (int32_t)frac)
                           / (int32_t)PITCH_DEN);
    }
    return out_n;
}

static bool play_chipmunk(const int16_t *in, size_t in_n)
{
    size_t out_n = chipmunk_fill(in, in_n, (int16_t *)s_play, PLAY_CAP / sizeof(int16_t));
    if (out_n < 16) {
        ESP_LOGW(TAG, "chipmunk produced %u samples", (unsigned)out_n);
        return true;
    }

    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    /* Unmute after record: give PA a beat before the voice. */
    vTaskDelay(pdMS_TO_TICKS(40));

    if (!write_pcm(s_play, out_n * sizeof(int16_t))) {
        return false;
    }
    speaker_drain();
    ESP_LOGI(TAG, "played chipmunk %u in -> %u out samples (5/3) peak=%d",
             (unsigned)in_n, (unsigned)out_n, (int)pcm_peak((const int16_t *)s_play, out_n));
    return true;
}

static uint8_t *alloc_pcm(size_t nbytes)
{
    uint8_t *p = heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) {
        ESP_LOGW(TAG, "PSRAM alloc %u failed, falling back to SRAM", (unsigned)nbytes);
        p = heap_caps_malloc(nbytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return p;
}

static bool init_buttons(void)
{
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    /* MAIN is the red circle; needs display/touch already up. */
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        fail_once("red circle init failed");
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s (circle handle ok)", esp_err_to_name(err));
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, ptt_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, ptt_up, NULL);
    return true;
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        fail_once("display init failed");
        return;
    }

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
    if (!speaker_open_once()) {
        return;
    }

    s_pcm = alloc_pcm(PCM_CAP);
    s_play = alloc_pcm(PLAY_CAP);
    if (s_pcm == NULL || s_play == NULL) {
        fail_once("pcm buffer alloc failed");
        return;
    }
    ESP_LOGI(TAG, "pcm %u + play %u @ %p %p",
             (unsigned)PCM_CAP, (unsigned)PLAY_CAP, (void *)s_pcm, (void *)s_play);

    s_down_sem = xSemaphoreCreateBinary();
    if (s_down_sem == NULL) {
        fail_once("semaphore alloc failed");
        return;
    }

    if (!init_buttons()) {
        return;
    }

    board_status_set("unmute (LED off)\nhold red circle");
    ESP_LOGI(TAG, "hold red circle to record (cap %ds); release for chipmunk. No wake word.",
             MAX_SECONDS);

    while (!s_failed) {
        if (xSemaphoreTake(s_down_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!s_held || s_failed) {
            continue;
        }
        if (mute_latched()) {
            board_status_set("unmute first\nred LED must be off");
            ESP_LOGW(TAG, "mute latched; mics are hardware-muted");
            while (s_held && !s_failed) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }

        board_status_set("recording...");
        size_t n = record_while_held();
        while (s_held && !s_failed) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_failed) {
            break;
        }
        if (n < MIN_PASS_BYTES) {
            ESP_LOGW(TAG, "clip too short (%u bytes); hold red circle", (unsigned)n);
            board_status_set("too short, hold longer");
            continue;
        }

        size_t skip = (n > CLICK_BYTES + MIN_PASS_BYTES) ? CLICK_BYTES : 0;
        const int16_t *in = (const int16_t *)(s_pcm + skip);
        size_t in_n = (n - skip) / sizeof(int16_t);
        int16_t peak = pcm_peak(in, in_n);
        ESP_LOGI(TAG, "take peak=%d samples=%u", (int)peak, (unsigned)in_n);

        board_status_set("chipmunk...");
        if (!play_chipmunk(in, in_n)) {
            break;
        }
        if (!s_passed) {
            s_passed = true;
            demo_pass("h25");
        }
        board_status_set("hold red circle\nfor another");
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
