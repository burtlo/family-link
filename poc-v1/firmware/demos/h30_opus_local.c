/*
 * h30 — Local Opus encode/decode soak (no Wi-Fi).
 *
 * Tap red circle: start/stop mic -> Opus encode (+ decode for metrics; muted).
 * After stop (or 180 s cap), replays stored packets (fault test if >=10 s).
 * Packets are length-prefixed in PSRAM (~3 min @ 24 kbps). Boot = 16/24 kbps.
 * Top mute latch must be off. Full 180 s logs -- SOAK PASS h30 on serial.
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
#include "esp_system.h"
#include "esp_timer.h"

#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "board.h"
#include "fl_opus.h"
#include "pass.h"

static const char *TAG = "h30";

#define SAMPLE_RATE        FL_OPUS_SAMPLE_RATE_HZ
#define CHUNK_BYTES        FL_OPUS_FRAME_PCM_BYTES
#define WRITE_CHUNK        2048
#define SPK_VOLUME         90
#define MIC_GAIN_DB        42.0f
#define MAX_SECONDS        180
#define PASS_SECONDS       10
#define METRICS_MS         10000
#define CLICK_MS           80
#define DRAIN_MS           80
#define LIVE_SPK_MONITOR   0 /* 1 = live loopback (screeches on BOX-3); 0 = play on release only */
#define ENC_STORE_CAP      (600 * 1024)
#define LATENCY_SAMPLES    64
#define PACKET_HDR_BYTES   2
#define AUDIO_TASK_STACK   32768

#define BYTES_PER_MS       ((SAMPLE_RATE * 2) / 1000)

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    uint64_t enc_us_sum;
    uint64_t dec_us_sum;
    uint32_t enc_us_max;
    uint32_t dec_us_max;
    uint32_t enc_us_hist[LATENCY_SAMPLES];
    uint32_t dec_us_hist[LATENCY_SAMPLES];
    uint32_t hist_i;
    uint32_t frames;
    uint32_t dropped;
    uint32_t enc_bytes;
    uint32_t underruns;
    uint32_t plc_frames;
    uint32_t corrupt_frames;
} h30_metrics_t;

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static fl_opus_codec_t s_codec;
static volatile bool s_stop_click;
static volatile bool s_audio_busy;
static volatile bool s_passed;
static volatile bool s_failed;
static volatile fl_opus_profile_t s_profile = FL_OPUS_PROFILE_VOIP_16K;
static SemaphoreHandle_t s_circle_start;
static SemaphoreHandle_t s_audio_go;
static SemaphoreHandle_t s_audio_done;
static volatile bool s_profile_toggle;
static StackType_t *s_audio_stack;
static StaticTask_t *s_audio_tcb;
static int16_t s_pcm_in[FL_OPUS_FRAME_SAMPLES];
static int16_t s_pcm_out[FL_OPUS_FRAME_SAMPLES];
static uint8_t s_pkt_buf[FL_OPUS_MAX_PACKET_BYTES];
static uint32_t s_metrics_sort[LATENCY_SAMPLES];
static uint8_t *s_enc_store;
static size_t s_enc_store_len;
static h30_metrics_t s_m;
static int64_t s_session_start_ms;
static int64_t s_last_metrics_ms;

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void fail_once(const char *reason)
{
    if (s_failed) {
        return;
    }
    s_failed = true;
    ESP_LOGE(TAG, "%s", reason);
    demo_fail("h30", reason);
}

static void on_circle_up(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    if (!s_audio_busy) {
        if (s_circle_start != NULL) {
            xSemaphoreGive(s_circle_start);
        }
        return;
    }
    s_stop_click = true;
    ESP_LOGI(TAG, "stop tap t=%u ms", (unsigned)esp_log_timestamp());
}

static void on_boot(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;
    if (!s_audio_busy) {
        s_profile_toggle = true;
    }
}

static void apply_profile_toggle(void)
{
    if (!s_profile_toggle || s_audio_busy) {
        return;
    }
    s_profile_toggle = false;
    fl_opus_profile_t next =
        (s_profile == FL_OPUS_PROFILE_VOIP_16K) ? FL_OPUS_PROFILE_APP_24K
                                                : FL_OPUS_PROFILE_VOIP_16K;
    if (fl_opus_set_profile(&s_codec, next) == 0) {
        s_profile = next;
        ESP_LOGI(TAG, "profile -> %s kbps",
                 next == FL_OPUS_PROFILE_VOIP_16K ? "16" : "24");
        board_status_set(next == FL_OPUS_PROFILE_VOIP_16K ? "16 kbps VOIP\ntap red=rec"
                                                          : "24 kbps AUDIO\ntap red=rec");
    }
}

static void metrics_reset(void)
{
    memset(&s_m, 0, sizeof(s_m));
    s_enc_store_len = 0;
    s_session_start_ms = esp_log_timestamp();
    s_last_metrics_ms = s_session_start_ms;
}

static uint32_t pctile_us(uint32_t *hist, uint32_t n, unsigned pct)
{
    uint32_t count = n > LATENCY_SAMPLES ? LATENCY_SAMPLES : n;
    if (count == 0) {
        return 0;
    }
    memcpy(s_metrics_sort, hist, count * sizeof(uint32_t));
    for (uint32_t i = 0; i + 1 < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (s_metrics_sort[j] < s_metrics_sort[i]) {
                uint32_t t = s_metrics_sort[i];
                s_metrics_sort[i] = s_metrics_sort[j];
                s_metrics_sort[j] = t;
            }
        }
    }
    uint32_t idx = (count * pct) / 100;
    if (idx >= count) {
        idx = count - 1;
    }
    return s_metrics_sort[idx];
}

static void metrics_log(bool force)
{
    int64_t now = esp_log_timestamp();
    if (!force && (now - s_last_metrics_ms) < METRICS_MS) {
        return;
    }
    s_last_metrics_ms = now;

    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);

    uint32_t enc_avg = s_m.frames ? (uint32_t)(s_m.enc_us_sum / s_m.frames) : 0;
    uint32_t dec_avg = s_m.frames ? (uint32_t)(s_m.dec_us_sum / s_m.frames) : 0;
    uint32_t enc_p95 = pctile_us(s_m.enc_us_hist, s_m.hist_i, 95);
    uint32_t dec_p95 = pctile_us(s_m.dec_us_hist, s_m.hist_i, 95);

    ESP_LOGI(TAG,
             "metrics t=%lldms prof=%s enc_bytes=%u frames=%u drop=%u underrun=%u "
             "enc_us avg=%u p95=%u max=%u dec_us avg=%u p95=%u max=%u "
             "heap_int=%u largest_int=%u heap_psram=%u largest_psram=%u stack_hwm=%u",
             (long long)(now - s_session_start_ms),
             s_profile == FL_OPUS_PROFILE_VOIP_16K ? "16k" : "24k", (unsigned)s_m.enc_bytes,
             (unsigned)s_m.frames, (unsigned)s_m.dropped, (unsigned)s_m.underruns,
             (unsigned)enc_avg, (unsigned)enc_p95, (unsigned)s_m.enc_us_max, (unsigned)dec_avg,
             (unsigned)dec_p95, (unsigned)s_m.dec_us_max, (unsigned)free_int,
             (unsigned)largest_int, (unsigned)free_psram, (unsigned)largest_psram,
             (unsigned)hwm);
}

static void metrics_note_encode(uint32_t us, int plen)
{
    s_m.enc_us_sum += us;
    if (us > s_m.enc_us_max) {
        s_m.enc_us_max = us;
    }
    if (s_m.hist_i < LATENCY_SAMPLES) {
        s_m.enc_us_hist[s_m.hist_i] = us;
    }
    if (plen > 0) {
        s_m.enc_bytes += (uint32_t)plen;
    }
}

static void metrics_note_decode(uint32_t us)
{
    s_m.dec_us_sum += us;
    if (us > s_m.dec_us_max) {
        s_m.dec_us_max = us;
    }
    if (s_m.hist_i < LATENCY_SAMPLES) {
        s_m.dec_us_hist[s_m.hist_i] = us;
    }
    s_m.hist_i++;
    s_m.frames++;
}

static bool store_packet(const uint8_t *pkt, int plen)
{
    if (plen <= 0 || plen > FL_OPUS_MAX_PACKET_BYTES) {
        return false;
    }
    size_t need = (size_t)PACKET_HDR_BYTES + (size_t)plen;
    if (s_enc_store_len + need > ENC_STORE_CAP) {
        s_m.dropped++;
        return false;
    }
    s_enc_store[s_enc_store_len++] = (uint8_t)(plen & 0xff);
    s_enc_store[s_enc_store_len++] = (uint8_t)((plen >> 8) & 0xff);
    memcpy(s_enc_store + s_enc_store_len, pkt, (size_t)plen);
    s_enc_store_len += (size_t)plen;
    return true;
}

static bool write_pcm(const int16_t *pcm, size_t nsamples)
{
    const uint8_t *p = (const uint8_t *)pcm;
    size_t nbytes = nsamples * sizeof(int16_t);
    while (nbytes > 0) {
        int n = (int)(nbytes > WRITE_CHUNK ? WRITE_CHUNK : nbytes);
        if (esp_codec_dev_write(s_spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            s_m.underruns++;
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
    int left = SAMPLE_RATE * DRAIN_MS / 1000 * 2;
    while (left > 0) {
        int n = left > (int)sizeof(z) ? (int)sizeof(z) : left;
        if (esp_codec_dev_write(s_spk, z, n) != ESP_CODEC_DEV_OK) {
            break;
        }
        left -= n;
    }
}

static bool init_buttons(void)
{
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        fail_once("red circle init failed");
        return false;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, on_circle_up, NULL);
    if (btns[BSP_BUTTON_CONFIG] != NULL) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot, NULL);
    }
    return true;
}

static uint32_t session_ms(void)
{
    return (uint32_t)(esp_log_timestamp() - s_session_start_ms);
}

static bool live_loop_frame(void)
{
    int r = esp_codec_dev_read(s_mic, s_pcm_in, CHUNK_BYTES);
    if (r != ESP_CODEC_DEV_OK) {
        s_m.dropped++;
        return false;
    }

    int64_t t0 = esp_timer_get_time();
    int plen = fl_opus_encode_frame(&s_codec, s_pcm_in, s_pkt_buf, sizeof(s_pkt_buf));
    uint32_t enc_us = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (plen < 0) {
        ESP_LOGE(TAG, "encode %d", plen);
        s_m.dropped++;
        return false;
    }
    metrics_note_encode(enc_us, plen);
    (void)store_packet(s_pkt_buf, plen);

    t0 = esp_timer_get_time();
    int dn = fl_opus_decode_frame(&s_codec, s_pkt_buf, plen, s_pcm_out);
    uint32_t dec_us = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    if (dn < 0) {
        s_m.dropped++;
        return false;
    }
    metrics_note_decode(dec_us);

#if LIVE_SPK_MONITOR
    if (!write_pcm(s_pcm_out, FL_OPUS_FRAME_SAMPLES)) {
        return false;
    }
#endif

    if (enc_us > 20000 || dec_us > 20000) {
        s_m.underruns++;
    }

    metrics_log(false);
    vTaskDelay(1);
    return true;
}

static bool record_encode_decode_session(void)
{
    (void)esp_codec_dev_set_out_mute(s_spk, true);

    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        (void)esp_codec_dev_set_out_mute(s_spk, false);
        fail_once("mic open failed");
        return false;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);
    /* Keep DAC muted while the mic is open — live loopback screeches on the desk cone. */
    (void)esp_codec_dev_set_out_mute(s_spk, true);

    metrics_reset();
    s_stop_click = false;

    uint32_t max_ms = (uint32_t)MAX_SECONDS * 1000u;
    while (session_ms() < max_ms && !s_failed && !s_stop_click) {
        if (!live_loop_frame()) {
            break;
        }
        if (session_ms() < (uint32_t)CLICK_MS) {
            continue;
        }
    }

    (void)esp_codec_dev_close(s_mic);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    metrics_log(true);
    ESP_LOGI(TAG, "session stack_hwm=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    return session_ms() >= (uint32_t)PASS_SECONDS * 1000u;
}

static size_t packet_count(void)
{
    size_t off = 0;
    size_t count = 0;
    while (off + PACKET_HDR_BYTES <= s_enc_store_len) {
        int plen = (int)s_enc_store[off] | ((int)s_enc_store[off + 1] << 8);
        off += PACKET_HDR_BYTES;
        if (plen <= 0 || plen > FL_OPUS_MAX_PACKET_BYTES || off + (size_t)plen > s_enc_store_len) {
            break;
        }
        count++;
        off += (size_t)plen;
    }
    return count;
}

static bool replay_with_faults(void)
{
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    size_t n_pkt = packet_count();
    if (n_pkt < 8) {
        ESP_LOGW(TAG, "skip fault replay: only %u packets", (unsigned)n_pkt);
        return true;
    }

    size_t miss_idx = n_pkt / 4;
    size_t bad_idx = miss_idx + 3;
    if (bad_idx >= n_pkt) {
        bad_idx = n_pkt - 1;
    }

    ESP_LOGI(TAG, "fault replay: %u packets, missing@%u corrupt@%u", (unsigned)n_pkt,
             (unsigned)miss_idx, (unsigned)bad_idx);

    size_t off = 0;
    size_t idx = 0;
    while (off + PACKET_HDR_BYTES <= s_enc_store_len && !s_failed) {
        int plen = (int)s_enc_store[off] | ((int)s_enc_store[off + 1] << 8);
        off += PACKET_HDR_BYTES;
        if (plen <= 0 || plen > FL_OPUS_MAX_PACKET_BYTES || off + (size_t)plen > s_enc_store_len) {
            break;
        }
        const uint8_t *pkt = s_enc_store + off;
        off += (size_t)plen;

        if (idx == miss_idx) {
            ESP_LOGW(TAG, "inject MISSING frame %u (PLC)", (unsigned)idx);
            s_m.plc_frames++;
            (void)fl_opus_decode_frame(&s_codec, NULL, 0, s_pcm_out);
        } else if (idx == bad_idx) {
            memcpy(s_pkt_buf, pkt, (size_t)plen);
            s_pkt_buf[0] ^= 0xff;
            ESP_LOGW(TAG, "inject CORRUPT frame %u", (unsigned)idx);
            s_m.corrupt_frames++;
            (void)fl_opus_decode_frame(&s_codec, s_pkt_buf, plen, s_pcm_out);
        } else {
            (void)fl_opus_decode_frame(&s_codec, pkt, plen, s_pcm_out);
        }

        if (!write_pcm(s_pcm_out, FL_OPUS_FRAME_SAMPLES)) {
            return false;
        }
        idx++;
    }

    speaker_drain();
    ESP_LOGI(TAG, "fault replay done plc=%u corrupt=%u", (unsigned)s_m.plc_frames,
             (unsigned)s_m.corrupt_frames);
    return true;
}

static void audio_worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_audio_go, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_audio_busy = true;
        board_backlight_set(45);
        board_status_set("recording…\ntap red=stop");
        bool ok = record_encode_decode_session();
        uint32_t max_ms = (uint32_t)MAX_SECONDS * 1000u;
        bool full_soak = session_ms() >= max_ms - 100u;
        board_backlight_set(80);
        if (!s_failed && full_soak) {
            ESP_LOGI(TAG, "-- SOAK PASS h30 %ds frames=%u enc_bytes=%u", MAX_SECONDS,
                     (unsigned)s_m.frames, (unsigned)s_m.enc_bytes);
            board_status_set("SOAK PASS\n3min");
        }
        if (!s_failed && ok && !full_soak) {
            board_status_set("fault replay...");
            if (!replay_with_faults()) {
                s_audio_busy = false;
                xSemaphoreGive(s_audio_done);
                continue;
            }
            if (!s_passed) {
                s_passed = true;
                demo_pass("h30");
            }
            board_status_set("PASS\ntap red=rec");
        } else if (!s_failed && ok && full_soak) {
            board_status_set("SOAK done\ntap red=rec");
        } else if (!s_failed) {
            board_status_set("need >=10s\ntap red=rec");
        }
        s_audio_busy = false;
        xSemaphoreGive(s_audio_done);
    }
}

static bool start_audio_worker(void)
{
    s_audio_tcb =
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_audio_stack = heap_caps_malloc(AUDIO_TASK_STACK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_audio_tcb != NULL && s_audio_stack != NULL) {
        if (xTaskCreateStatic(audio_worker, "h30_audio", AUDIO_TASK_STACK, NULL, 5, s_audio_stack,
                              s_audio_tcb) != NULL) {
            return true;
        }
    }
    ESP_LOGW(TAG, "PSRAM audio stack unavailable; using internal stack");
    return xTaskCreate(audio_worker, "h30_audio", AUDIO_TASK_STACK, NULL, 5, NULL) == pdPASS;
}

void app_main(void)
{
    ESP_LOGI(TAG, "reset reason %d (4=panic 7=task_wdt)", (int)esp_reset_reason());

    if (board_display_start() != ESP_OK) {
        fail_once("display init failed");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        fail_once("codec init failed");
        return;
    }
    if (!speaker_open_once()) {
        return;
    }

    if (fl_opus_init(&s_codec, s_profile) != 0) {
        fail_once("opus init failed");
        return;
    }

    s_enc_store = heap_caps_malloc(ENC_STORE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_enc_store == NULL) {
        s_enc_store = heap_caps_malloc(ENC_STORE_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_enc_store == NULL) {
        fail_once("enc store alloc failed");
        return;
    }

    s_circle_start = xSemaphoreCreateBinary();
    s_audio_go = xSemaphoreCreateBinary();
    s_audio_done = xSemaphoreCreateBinary();
    if (s_circle_start == NULL || s_audio_go == NULL || s_audio_done == NULL || !init_buttons()) {
        fail_once("buttons failed");
        return;
    }
    if (!start_audio_worker()) {
        fail_once("audio task failed");
        return;
    }

    board_status_set("16 kbps VOIP\ntap red=rec max 3m");
    ESP_LOGI(TAG, "tap red start/stop (max %ds); stop -> replay. Boot=16/24k.", MAX_SECONDS);

    while (!s_failed) {
        apply_profile_toggle();
        if (xSemaphoreTake(s_circle_start, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (s_audio_busy || s_failed) {
            continue;
        }
        if (mute_latched()) {
            board_status_set("unmute first\nred LED off");
            continue;
        }

        while (xSemaphoreTake(s_audio_done, 0) == pdTRUE) {
        }
        if (xSemaphoreGive(s_audio_go) != pdTRUE) {
            continue;
        }
        while (xSemaphoreTake(s_audio_done, pdMS_TO_TICKS(50)) != pdTRUE) {
            vTaskDelay(1);
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
