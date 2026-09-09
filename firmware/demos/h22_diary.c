/*
 * h22 — Diary journal: record while unmuted, POST ~1 s WAV chunks, stop
 * when muted. The host stamps UTC received_at on each chunk.
 *
 * Mute down → not recording. Mute up → new session, mic stays open until
 * mute (one click per session, not per chunk).
 *
 * Host (leave running):
 *   python demos/server/h22_diary/server.py --host 0.0.0.0 --port 8080
 *
 * -- PASS h22 after the first HTTP 200 upload.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"

#include "board.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h22";

#define SAMPLE_RATE 16000
#define CHUNK       640
#define CHUNK_SEC   1
#define PCM_BYTES   (SAMPLE_RATE * 2 * CHUNK_SEC)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static uint8_t *s_wav;
static volatile bool s_open;
static int s_session;
static bool s_passed;

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void paint(int seq, const char *note)
{
    char line[96];
    if (!s_open) {
        snprintf(line, sizeof(line), "diary muted\nnot recording");
    } else if (note && note[0]) {
        snprintf(line, sizeof(line), "diary session %d\n%s", s_session, note);
    } else {
        snprintf(line, sizeof(line), "diary session %d\nchunk %d", s_session, seq);
    }
    board_status_set(line);
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

static int post_chunk(const uint8_t *wav, int wav_len, int session, int seq, char *resp, int resp_cap)
{
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/v1/diary", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    static const char *bnd = "----FamilyLinkDiary";
    char pre[320];
    int pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"session\"\r\n\r\n"
                           "%d\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"seq\"\r\n\r\n"
                           "%d\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; filename=\"chunk.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n",
                           bnd, session, bnd, seq, bnd);
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
    if (resp && resp_cap > 1 && clen != ESP_FAIL) {
        int n = esp_http_client_read(client, resp, resp_cap - 1);
        if (n < 0) {
            n = 0;
        }
        resp[n] = 0;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

static size_t record_chunk(void)
{
    size_t filled = 0;
    while (s_open && filled + CHUNK <= PCM_BYTES) {
        if (esp_codec_dev_read(s_mic, s_wav + 44 + filled, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        filled += CHUNK;
    }
    return filled;
}

static void diary_task(void *arg)
{
    (void)arg;
    char resp[192];
    bool mic_open = false;
    int seq = 0;

    while (1) {
        if (!s_open) {
            if (mic_open) {
                (void)esp_codec_dev_close(s_mic);
                mic_open = false;
            }
            paint(0, NULL);
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        if (!mic_open) {
            if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
                paint(0, "mic fail");
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            (void)esp_codec_dev_set_in_mute(s_mic, false);
            (void)esp_codec_dev_set_in_gain(s_mic, 42.0f);
            mic_open = true;
            seq = 0;
            paint(seq, "recording…");
        }

        size_t n = record_chunk();
        if (!s_open) {
            continue;
        }
        if (n < SAMPLE_RATE * 2 / 5) {
            paint(seq, "chunk short");
            continue;
        }
        wav_header(s_wav, (uint32_t)n);
        paint(seq, "uploading…");
        int status = post_chunk(s_wav, (int)(44 + n), s_session, seq, resp, sizeof(resp));
        ESP_LOGI(TAG, "chunk session=%d seq=%d bytes=%u status=%d %s", s_session, seq,
                 (unsigned)(44 + n), status, resp);
        if (status == 200) {
            if (!s_passed) {
                s_passed = true;
                demo_pass("h22");
            }
            seq++;
            paint(seq, NULL);
        } else {
            paint(seq, "upload fail");
            vTaskDelay(pdMS_TO_TICKS(400));
        }
    }
}

static void mute_task(void *arg)
{
    (void)arg;
    bool last = mute_latched();
    s_open = !last;
    if (s_open) {
        s_session = 1;
    }
    while (1) {
        bool muted = mute_latched();
        if (muted == last) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }
        last = muted;
        if (muted) {
            s_open = false;
            ESP_LOGI(TAG, "muted — diary paused");
        } else {
            s_session++;
            s_open = true;
            ESP_LOGI(TAG, "open — diary session %d", s_session);
        }
        paint(0, NULL);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h22 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h22", "wifi");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        demo_fail("h22", "codec");
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 0);

    s_wav = heap_caps_malloc(44 + PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_wav == NULL) {
        s_wav = heap_caps_malloc(44 + PCM_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_wav == NULL) {
        demo_fail("h22", "OOM");
        return;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    (void)btns;

    xTaskCreate(diary_task, "diary", 6144, NULL, 5, NULL);
    xTaskCreate(mute_task, "mute", 2048, NULL, 4, NULL);
    paint(0, NULL);
    ESP_LOGI(TAG, "who id=%s name=%s  unmute to record diary chunks to %s:%d",
             DEMO_DEVICE_ID, DEMO_DEVICE_NAME, DEMO_SERVER_HOST, DEMO_SERVER_PORT);
}
