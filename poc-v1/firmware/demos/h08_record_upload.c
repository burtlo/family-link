/*
 * h08 — Hold the red circle, record, POST /v1/messages multipart audio WAV.
 * -- PASS h08 on HTTP 200. Twin on the Mac can fetch the blob.
 *
 * Do not use the top mute key as PTT. On ESP-BOX-3 it is a latch wired
 * through logic gates (BSP_MUTE_STATUS / GPIO1): when it is down the mics
 * are hardware-muted, so every clip is silence.
 *
 * Desk test 2026-08-23: upload works; WAV starts with a hardware click.
 * Trim the first tens of ms here after ADC open, or on the server at store.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"

#include "board.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h08";

#define SAMPLE_RATE 16000
#define CHUNK       640
#define MAX_SEC     10
#define PCM_CAP     (SAMPLE_RATE * 2 * MAX_SEC)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_pcm;
static volatile bool s_held;
static SemaphoreHandle_t s_down;
static bool s_passed;

static bool mute_latched(void)
{
    /* Active-low. 0 = mute engaged = analog mics dead. */
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void ptt_down(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = true;
    if (s_down) {
        xSemaphoreGive(s_down);
    }
}

static void ptt_up(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = false;
}

static void wav_header(uint8_t *p, uint32_t pcm_bytes)
{
    uint32_t riff = 36 + pcm_bytes;
    uint32_t fmt = 16;
    uint16_t audio = 1, ch = 1, bps = 16;
    uint32_t rate = SAMPLE_RATE;
    uint32_t byte_rate = rate * ch * bps / 8;
    uint16_t block = ch * bps / 8;
    memcpy(p, "RIFF", 4);
    memcpy(p + 4, &riff, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    memcpy(p + 16, &fmt, 4);
    memcpy(p + 20, &audio, 2);
    memcpy(p + 22, &ch, 2);
    memcpy(p + 24, &rate, 4);
    memcpy(p + 28, &byte_rate, 4);
    memcpy(p + 32, &block, 2);
    memcpy(p + 34, &bps, 2);
    memcpy(p + 36, "data", 4);
    memcpy(p + 40, &pcm_bytes, 4);
}

static int16_t pcm_peak(const uint8_t *p, size_t nbytes)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)p;
    size_t n = nbytes / 2;
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

static size_t record_while_held(void)
{
    size_t filled = 0;
    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
    while (s_held && filled + CHUNK <= PCM_CAP) {
        if (esp_codec_dev_read(s_mic, s_pcm + 44 + filled, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        filled += CHUNK;
    }
    (void)esp_codec_dev_close(s_mic);
    return filled;
}

static int post_wav(const uint8_t *wav, int wav_len, char *resp, int resp_cap)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/v1/messages", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    static const char *bnd = "----FamilyLinkBound";
    char pre[256];
    int pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; filename=\"clip.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n",
                           bnd, bnd);
    char post[80];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    int total = pre_len + wav_len + post_len;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(client, "Content-Type", ctype);

    if (esp_http_client_open(client, total) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    esp_http_client_write(client, pre, pre_len);
    esp_http_client_write(client, (const char *)wav, wav_len);
    esp_http_client_write(client, post, post_len);
    int clen = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int n = 0;
    if (resp && resp_cap > 1 && clen != ESP_FAIL) {
        n = esp_http_client_read(client, resp, resp_cap - 1);
        if (n < 0) {
            n = 0;
        }
        resp[n] = 0;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h08 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h08", "wifi");
        return;
    }

    /* Same I2S duplex bring-up as h05: speaker init starts the shared bus. */
    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        demo_fail("h08", "codec");
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 0);
    s_pcm = heap_caps_malloc(44 + PCM_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_pcm == NULL) {
        s_pcm = heap_caps_malloc(44 + PCM_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_pcm == NULL) {
        demo_fail("h08", "OOM");
        return;
    }
    ESP_LOGI(TAG, "pcm %u bytes @ %p", (unsigned)(44 + PCM_CAP), (void *)s_pcm);

    s_down = xSemaphoreCreateBinary();
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    /* MAIN is the red circle; needs display/touch already up. */
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        demo_fail("h08", "red circle");
        return;
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, ptt_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, ptt_up, NULL);

    board_status_set("unmute (LED off)\nhold red circle");
    ESP_LOGI(TAG, "PTT is the red circle under the LCD. Top mute latches and kills the mics.");

    while (1) {
        xSemaphoreTake(s_down, portMAX_DELAY);
        if (!s_held) {
            continue;
        }
        if (mute_latched()) {
            board_status_set("unmute first\nred LED must be off");
            ESP_LOGW(TAG, "mute latched; mics are hardware-muted");
            while (s_held) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            continue;
        }
        board_status_set("recording…");
        size_t n = record_while_held();
        while (s_held) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (n < SAMPLE_RATE * 2 / 5) {
            board_status_set("too short, hold longer");
            continue;
        }
        wav_header(s_pcm, (uint32_t)n);
        int wav_len = 44 + (int)n;
        int16_t peak = pcm_peak(s_pcm + 44, n);
        ESP_LOGI(TAG, "recorded %u bytes peak=%d", (unsigned)n, (int)peak);
        if (peak < 64) {
            board_status_set("recorded silence\nhold red circle, talk");
            ESP_LOGW(TAG, "near-silent clip (peak=%d)", (int)peak);
            continue;
        }
        board_status_set("uploading…");
        char resp[256];
        int status = post_wav(s_pcm, wav_len, resp, sizeof(resp));
        ESP_LOGI(TAG, "POST status=%d body=%s bytes=%d", status, resp, wav_len);
        if (status == 200) {
            if (!s_passed) {
                s_passed = true;
                demo_pass("h08");
            }
            board_status_set("uploaded  hold red\ncircle for another");
            continue;
        }
        board_status_set("upload failed");
        demo_fail("h08", "POST");
        return;
    }
}
