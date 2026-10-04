/*
 * h31 — Opus chunk upload + Ogg playback (index.json + HTTP Range).
 *
 * Hold red circle: mic -> Opus -> 2 s PUT chunks (length-prefixed packets).
 * Release: POST /complete. Short Boot plays last upload; long Boot toggles 16/24
 * kbps for the next hold (never while recording or playing).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "mbedtls/sha256.h"

#include "bsp/esp-bsp.h"
#include "board.h"
#include "fl_opus.h"
#include "pass.h"
#include "wifi_sta.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

static const char *TAG = "h31";

#define SAMPLE_RATE        FL_OPUS_SAMPLE_RATE_HZ
#define CHUNK_PCM_BYTES    FL_OPUS_FRAME_PCM_BYTES
#define CHUNK_MS           2000
#define MAX_SECONDS        180
#define PASS_HOLD_MS       2500
#define CHUNK_BUF_CAP      (48 * 1024)
#define PLAY_BUF_CAP       (16 * 1024)
#define PACKET_HDR         2
#define SPK_VOLUME         90
#define MIC_GAIN_DB        42.0f
#define REC_TASK_STACK     28672
#define UPLOAD_TASK_STACK  20480
#define PLAY_TASK_STACK    12288

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} body_buf_t;

static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static fl_opus_codec_t s_codec;
static volatile bool s_held;
static volatile bool s_play_req;
static volatile bool s_profile_toggle;
static volatile bool s_play_running;
static SemaphoreHandle_t s_down_sem;
static SemaphoreHandle_t s_rec_done;
static SemaphoreHandle_t s_upload_go;
static SemaphoreHandle_t s_upload_done;
static volatile bool s_rec_task_running;
static bool s_spk_open;
static int16_t s_pcm_frame[FL_OPUS_FRAME_SAMPLES];
static uint8_t s_pkt_frame[FL_OPUS_MAX_PACKET_BYTES];
static SemaphoreHandle_t s_record_start;
static StackType_t *s_rec_stack;
static StackType_t *s_upload_stack;
static StaticTask_t *s_rec_tcb;
static StaticTask_t *s_upload_tcb;
static char s_ui_pending[48];
static volatile bool s_ui_dirty;

typedef enum {
    UPLOAD_CMD_CREATE = 0,
    UPLOAD_CMD_FLUSH,
    UPLOAD_CMD_COMPLETE,
} upload_cmd_t;

static volatile upload_cmd_t s_upload_cmd;
static volatile bool s_upload_ok;
static volatile fl_opus_profile_t s_profile = FL_OPUS_PROFILE_VOIP_16K;

static uint8_t *s_chunk_buf;
static size_t s_chunk_len;
static uint32_t s_chunk_start_ms;
static uint32_t s_record_ms;
static int s_seq;
static char s_message_id[48];
static bool s_passed;
static bool s_failed;

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void ui_request(const char *line)
{
    if (line == NULL) {
        return;
    }
    strncpy(s_ui_pending, line, sizeof(s_ui_pending) - 1);
    s_ui_pending[sizeof(s_ui_pending) - 1] = 0;
    s_ui_dirty = true;
}

static void ui_flush(void)
{
    if (!s_ui_dirty) {
        return;
    }
    s_ui_dirty = false;
    board_status_set(s_ui_pending);
}

static esp_err_t on_http_evt(esp_http_client_event_t *evt)
{
    body_buf_t *b = evt->user_data;
    if (b == NULL || b->buf == NULL) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int n = evt->data_len;
        if ((size_t)b->len + (size_t)n > b->cap) {
            n = (int)(b->cap - (size_t)b->len);
        }
        if (n > 0) {
            memcpy(b->buf + b->len, evt->data, n);
            b->len += (size_t)n;
        }
    }
    return ESP_OK;
}

static void sha256_hex(const uint8_t *data, size_t len, char *out_hex, size_t out_cap)
{
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32 && (size_t)(i * 2 + 1) < out_cap; i++) {
        snprintf(out_hex + i * 2, 3, "%02x", hash[i]);
    }
    out_hex[64] = 0;
}

static int http_request(const char *method, const char *path, const uint8_t *body, int body_len,
                        const char *content_type, body_buf_t *resp, int timeout_ms)
{
    char url[192];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_evt,
        .user_data = resp,
        .timeout_ms = timeout_ms > 0 ? timeout_ms : 15000,
        .buffer_size = 4096,
    };
    if (method && strcmp(method, "POST") == 0) {
        cfg.method = HTTP_METHOD_POST;
    } else if (method && strcmp(method, "PUT") == 0) {
        cfg.method = HTTP_METHOD_PUT;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    if (content_type) {
        esp_http_client_set_header(client, "Content-Type", content_type);
    }

    int status = -1;
    if (body != NULL && body_len > 0 && (strcmp(method, "PUT") == 0)) {
        if (esp_http_client_open(client, body_len) == ESP_OK) {
            esp_http_client_write(client, (const char *)body, body_len);
            esp_http_client_fetch_headers(client);
            status = esp_http_client_get_status_code(client);
            if (resp && resp->buf) {
                int n = esp_http_client_read(client, (char *)resp->buf, resp->cap - 1);
                if (n > 0) {
                    resp->len = (size_t)n;
                    resp->buf[n] = 0;
                }
            }
            esp_http_client_close(client);
        }
    } else if (body != NULL && body_len > 0) {
        esp_http_client_set_post_field(client, (const char *)body, body_len);
        if (esp_http_client_perform(client) == ESP_OK) {
            status = esp_http_client_get_status_code(client);
        }
    } else {
        if (esp_http_client_perform(client) == ESP_OK) {
            status = esp_http_client_get_status_code(client);
        }
    }
    esp_http_client_cleanup(client);
    return status;
}

static bool create_message(void)
{
    char json[320];
    snprintf(json, sizeof(json),
             "{\"protocol\":\"family-message/1\",\"to_user_id\":\"box-b\","
             "\"audio\":{\"codec\":\"opus\",\"sample_rate_hz\":16000,\"channels\":1,"
             "\"target_chunk_ms\":%d}}",
             CHUNK_MS);
    uint8_t resp[512];
    body_buf_t out = {.buf = resp, .len = 0, .cap = sizeof(resp) - 1};
    int st = http_request("POST", "/v1/messages", (const uint8_t *)json, (int)strlen(json),
                          "application/json", &out, 12000);
    if (st != 201 && st != 200) {
        ESP_LOGE(TAG, "create status=%d (401=token mismatch vs devices/hangout yaml)", st);
        if (st == 401) {
            board_status_set("401 token\nfix secrets");
        }
        return false;
    }
    cJSON *root = cJSON_Parse((const char *)resp);
    if (root == NULL) {
        return false;
    }
    cJSON *mid = cJSON_GetObjectItem(root, "message_id");
    if (!cJSON_IsString(mid)) {
        cJSON_Delete(root);
        return false;
    }
    strncpy(s_message_id, mid->valuestring, sizeof(s_message_id) - 1);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "message_id=%s", s_message_id);
    return true;
}

static bool flush_chunk(void)
{
    if (s_chunk_len == 0 || s_message_id[0] == 0) {
        return true;
    }
    char path[96];
    snprintf(path, sizeof(path), "/v1/messages/%s/audio/%d", s_message_id, s_seq);
    char sha[68];
    sha256_hex(s_chunk_buf, s_chunk_len, sha, sizeof(sha));
    char url[192];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "%u", (unsigned)s_chunk_start_ms);
    esp_http_client_set_header(client, "X-Chunk-Start-Ms", hdr);
    snprintf(hdr, sizeof(hdr), "%d", CHUNK_MS);
    esp_http_client_set_header(client, "X-Chunk-Duration-Ms", hdr);
    snprintf(hdr, sizeof(hdr), "%u", (unsigned)s_chunk_len);
    esp_http_client_set_header(client, "X-Chunk-Bytes", hdr);
    esp_http_client_set_header(client, "X-Chunk-SHA256", sha);

    int st = -1;
    if (esp_http_client_open(client, (int)s_chunk_len) == ESP_OK) {
        esp_http_client_write(client, (const char *)s_chunk_buf, (int)s_chunk_len);
        esp_http_client_fetch_headers(client);
        st = esp_http_client_get_status_code(client);
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    if (st != 201 && st != 200) {
        ESP_LOGE(TAG, "PUT seq=%d status=%d", s_seq, st);
        return false;
    }
    ESP_LOGI(TAG, "chunk %d bytes=%u start=%u", s_seq, (unsigned)s_chunk_len,
             (unsigned)s_chunk_start_ms);
    s_seq++;
    s_chunk_len = 0;
    s_chunk_start_ms = s_record_ms;
    return true;
}

static bool append_packet(const uint8_t *pkt, int plen)
{
    if (plen <= 0 || plen > FL_OPUS_MAX_PACKET_BYTES) {
        return false;
    }
    size_t need = PACKET_HDR + (size_t)plen;
    if (s_chunk_len + need > CHUNK_BUF_CAP) {
        return false;
    }
    s_chunk_buf[s_chunk_len++] = (uint8_t)(plen & 0xff);
    s_chunk_buf[s_chunk_len++] = (uint8_t)((plen >> 8) & 0xff);
    memcpy(s_chunk_buf + s_chunk_len, pkt, (size_t)plen);
    s_chunk_len += (size_t)plen;
    return true;
}

static bool complete_message(void)
{
    if (s_chunk_len > 0) {
        if (!flush_chunk()) {
            return false;
        }
    }
    if (s_seq <= 0) {
        return false;
    }
    char json[160];
    snprintf(json, sizeof(json),
             "{\"audio_chunks\":%d,\"duration_ms\":%u,\"closed_reason\":\"button\"}", s_seq,
             (unsigned)s_record_ms);
    char path[80];
    snprintf(path, sizeof(path), "/v1/messages/%s/complete", s_message_id);
    int st = http_request("POST", path, (const uint8_t *)json, (int)strlen(json),
                          "application/json", NULL, 20000);
    if (st != 201 && st != 200) {
        ESP_LOGE(TAG, "complete status=%d", st);
        return false;
    }
    return true;
}

static bool upload_run(upload_cmd_t cmd)
{
    if (s_upload_go == NULL || s_upload_done == NULL) {
        return false;
    }
    while (xSemaphoreTake(s_upload_done, 0) == pdTRUE) {
    }
    s_upload_cmd = cmd;
    s_upload_ok = false;
    if (xSemaphoreGive(s_upload_go) != pdTRUE) {
        return false;
    }
    if (xSemaphoreTake(s_upload_done, pdMS_TO_TICKS(60000)) != pdTRUE) {
        return false;
    }
    return s_upload_ok;
}

static void upload_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_upload_go, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        bool ok = false;
        switch (s_upload_cmd) {
        case UPLOAD_CMD_CREATE:
            ok = create_message();
            break;
        case UPLOAD_CMD_FLUSH:
            ok = flush_chunk();
            break;
        case UPLOAD_CMD_COMPLETE:
            ok = complete_message();
            break;
        default:
            break;
        }
        s_upload_ok = ok;
        xSemaphoreGive(s_upload_done);
    }
}

static bool speaker_open_once(void)
{
    if (s_spk_open) {
        return true;
    }
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        return false;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    (void)esp_codec_dev_set_out_mute(s_spk, true);
    uint8_t z[CHUNK_PCM_BYTES];
    memset(z, 0, sizeof(z));
    (void)esp_codec_dev_write(s_spk, z, sizeof(z));
    vTaskDelay(pdMS_TO_TICKS(50));
    s_spk_open = true;
    return true;
}

static bool record_session(void)
{
    s_seq = 0;
    s_record_ms = 0;
    s_chunk_len = 0;
    s_chunk_start_ms = 0;
    s_message_id[0] = 0;
    if (!upload_run(UPLOAD_CMD_CREATE)) {
        return false;
    }

    if (!speaker_open_once()) {
        return false;
    }
    (void)esp_codec_dev_set_out_mute(s_spk, true);
    if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
        (void)esp_codec_dev_set_out_mute(s_spk, false);
        return false;
    }
    (void)esp_codec_dev_set_in_mute(s_mic, false);
    (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);

    int64_t t0 = esp_timer_get_time();
    while (s_held && s_record_ms < (uint32_t)MAX_SECONDS * 1000u && !s_failed) {
        if (esp_codec_dev_read(s_mic, s_pcm_frame, CHUNK_PCM_BYTES) != ESP_CODEC_DEV_OK) {
            break;
        }
        int plen = fl_opus_encode_frame(&s_codec, s_pcm_frame, s_pkt_frame, sizeof(s_pkt_frame));
        if (plen < 0) {
            break;
        }
        (void)append_packet(s_pkt_frame, plen);
        s_record_ms += 20;
        if (s_record_ms - s_chunk_start_ms >= (uint32_t)CHUNK_MS) {
            if (!upload_run(UPLOAD_CMD_FLUSH)) {
                break;
            }
        }
        /* Feed IDLE / LVGL / task WDT while encoding (tight loop otherwise runs seconds). */
        vTaskDelay(1);
    }
    (void)esp_codec_dev_close(s_mic);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    uint32_t held_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    ESP_LOGI(TAG, "record held=%u ms seq=%d stack_hw=%u", (unsigned)held_ms, s_seq,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    if (!upload_run(UPLOAD_CMD_COMPLETE)) {
        return false;
    }
    return held_ms >= PASS_HOLD_MS && s_seq >= 1;
}

static int http_get_range(const char *path, size_t range_start, body_buf_t *out)
{
    char url[220];
    snprintf(url, sizeof(url), "http://%s:%d%s", DEMO_SERVER_HOST, DEMO_SERVER_PORT, path);
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_evt,
        .user_data = out,
        .timeout_ms = 20000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth);
    char range[48];
    snprintf(range, sizeof(range), "bytes=%u-", (unsigned)range_start);
    esp_http_client_set_header(client, "Range", range);
    int status = -1;
    if (esp_http_client_perform(client) == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    }
    esp_http_client_cleanup(client);
    return status;
}

static bool decode_ogg_page_payload(const uint8_t *body, size_t body_len, int16_t *pcm_out)
{
    if (body_len >= 8 && (memcmp(body, "OpusHead", 8) == 0 || memcmp(body, "OpusTags", 8) == 0)) {
        return true;
    }
    if (body_len == 0) {
        return true;
    }
    if (fl_opus_decode_frame(&s_codec, body, (int)body_len, pcm_out) < 0) {
        return false;
    }
    return true;
}

static bool play_ogg_stream(size_t start_offset)
{
    if (s_message_id[0] == 0) {
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "/v1/messages/%s/audio", s_message_id);
    uint8_t *buf = heap_caps_malloc(PLAY_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = heap_caps_malloc(PLAY_BUF_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (buf == NULL) {
        return false;
    }
    body_buf_t body = {.buf = buf, .len = 0, .cap = PLAY_BUF_CAP - 1};
    int st = http_get_range(path, start_offset, &body);
    if (st != 200 && st != 206) {
        free(buf);
        ESP_LOGE(TAG, "play GET status=%d", st);
        return false;
    }

    if (!speaker_open_once()) {
        free(buf);
        return false;
    }
    (void)esp_codec_dev_set_out_mute(s_spk, false);

    size_t pos = 0;
    while (pos + 27 <= body.len) {
        if (memcmp(buf + pos, "OggS", 4) != 0) {
            pos++;
            continue;
        }
        uint8_t seg_count = buf[pos + 26];
        size_t hdr_len = 27 + (size_t)seg_count;
        if (pos + hdr_len > body.len) {
            break;
        }
        size_t body_len = 0;
        for (size_t i = 0; i < seg_count; i++) {
            body_len += buf[pos + 27 + i];
        }
        if (pos + hdr_len + body_len > body.len) {
            break;
        }
        const uint8_t *page_body = buf + pos + hdr_len;
        int16_t pcm[FL_OPUS_FRAME_SAMPLES];
        if (!decode_ogg_page_payload(page_body, body_len, pcm)) {
            free(buf);
            return false;
        }
        if (body_len >= 8 && memcmp(page_body, "OpusHead", 8) != 0 &&
            memcmp(page_body, "OpusTags", 8) != 0 && body_len > 0) {
            (void)esp_codec_dev_write(s_spk, pcm, CHUNK_PCM_BYTES);
        }
        pos += hdr_len + body_len;
    }
    free(buf);
    return true;
}

static void ptt_down(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = true;
    if (s_down_sem) {
        xSemaphoreGive(s_down_sem);
    }
}

static void ptt_up(void *b, void *u)
{
    (void)b;
    (void)u;
    s_held = false;
}

static void status_idle_line(void)
{
    if (s_message_id[0] != 0) {
        board_status_set(s_profile == FL_OPUS_PROFILE_VOIP_16K ? "PASS\nBoot=play"
                                                               : "PASS 24k\nBoot=play");
    } else {
        board_status_set(s_profile == FL_OPUS_PROFILE_VOIP_16K
                             ? "16 kbps\nhold red\nlong Boot=24k"
                             : "24 kbps\nhold red\nlong Boot=16k");
    }
}

static void apply_profile_toggle(void)
{
    if (!s_profile_toggle || s_held || s_rec_task_running || s_play_running) {
        return;
    }
    s_profile_toggle = false;
    fl_opus_profile_t next =
        (s_profile == FL_OPUS_PROFILE_VOIP_16K) ? FL_OPUS_PROFILE_APP_24K
                                                : FL_OPUS_PROFILE_VOIP_16K;
    if (fl_opus_set_profile(&s_codec, next) == 0) {
        s_profile = next;
        ESP_LOGI(TAG, "profile %s (next record)", next == FL_OPUS_PROFILE_VOIP_16K ? "16k" : "24k");
        status_idle_line();
    }
}

static void on_boot_short(void *b, void *u)
{
    (void)b;
    (void)u;
    if (s_message_id[0] != 0 && !s_rec_task_running && !s_play_running && !s_held) {
        s_play_req = true;
    }
}

static void on_boot_long(void *b, void *u)
{
    (void)b;
    (void)u;
    if (!s_rec_task_running && !s_play_running && !s_held) {
        s_profile_toggle = true;
    }
}

static void play_task(void *arg);

static bool start_playback_task(void)
{
    if (s_play_running || s_message_id[0] == 0) {
        return false;
    }
    if (xTaskCreate(play_task, "h31_play", PLAY_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "play task create failed");
        return false;
    }
    return true;
}

static void record_worker(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_record_start, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_rec_task_running = true;
        board_backlight_set(45);
        board_status_set("recording…\n2s chunks");
        bool ok = record_session();
        board_backlight_set(80);
        if (ok) {
            if (!s_passed) {
                s_passed = true;
                demo_pass("h31");
            }
            status_idle_line();
        } else {
            board_status_set("retry hold");
        }
        s_rec_task_running = false;
        if (s_rec_done != NULL) {
            xSemaphoreGive(s_rec_done);
        }
    }
}

static void play_task(void *arg)
{
    (void)arg;
    s_play_running = true;
    board_status_set("playing…");
    if (play_ogg_stream(0)) {
        ESP_LOGI(TAG, "playback done");
        status_idle_line();
    } else {
        board_status_set("play failed");
    }
    s_play_running = false;
    vTaskDelete(NULL);
}

static bool start_record_task(void)
{
    if (s_rec_task_running || s_record_start == NULL) {
        return false;
    }
    while (xSemaphoreTake(s_rec_done, 0) == pdTRUE) {
    }
    if (xSemaphoreGive(s_record_start) != pdTRUE) {
        return false;
    }
    return true;
}

static bool spawn_psram_task(TaskFunction_t fn, const char *name, uint32_t stack_bytes,
                             UBaseType_t prio, StaticTask_t **tcb_slot, StackType_t **stack_slot)
{
    if (*tcb_slot != NULL && *stack_slot != NULL) {
        return xTaskCreateStatic(fn, name, stack_bytes, NULL, prio, *stack_slot, *tcb_slot) != NULL;
    }
    StaticTask_t *tcb =
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t *stack =
        heap_caps_malloc(stack_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tcb == NULL || stack == NULL) {
        if (tcb) {
            free(tcb);
        }
        if (stack) {
            free(stack);
        }
        return xTaskCreate(fn, name, stack_bytes, NULL, prio, NULL) == pdPASS;
    }
    *tcb_slot = tcb;
    *stack_slot = stack;
    return xTaskCreateStatic(fn, name, stack_bytes, NULL, prio, stack, tcb) != NULL;
}

static bool init_buttons(void)
{
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) != ESP_OK) {
        return false;
    }
    if (btns[BSP_BUTTON_MAIN] == NULL) {
        return false;
    }
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, ptt_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, ptt_up, NULL);
    if (btns[BSP_BUTTON_CONFIG] != NULL) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot_short,
                               NULL);
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_LONG_PRESS_START, NULL, on_boot_long,
                               NULL);
    }
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "reset reason %d (1=POR 4=panic 6=int_wdt 7=task_wdt)", (int)esp_reset_reason());

    if (board_display_start() == ESP_OK) {
        board_status_set("h31 wifi…");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h31", "wifi");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_spk == NULL || s_mic == NULL) {
        demo_fail("h31", "codec");
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 0);

    if (fl_opus_init(&s_codec, s_profile) != 0) {
        demo_fail("h31", "opus");
        return;
    }

    s_chunk_buf = heap_caps_malloc(CHUNK_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_chunk_buf == NULL) {
        s_chunk_buf = malloc(CHUNK_BUF_CAP);
    }
    if (s_chunk_buf == NULL) {
        demo_fail("h31", "chunk buf");
        return;
    }

    s_down_sem = xSemaphoreCreateBinary();
    s_rec_done = xSemaphoreCreateBinary();
    s_record_start = xSemaphoreCreateBinary();
    s_upload_go = xSemaphoreCreateBinary();
    s_upload_done = xSemaphoreCreateBinary();
    if (s_down_sem == NULL || s_rec_done == NULL || s_record_start == NULL || s_upload_go == NULL ||
        s_upload_done == NULL || !init_buttons()) {
        demo_fail("h31", "buttons");
        return;
    }
    if (!spawn_psram_task(upload_task, "h31_up", UPLOAD_TASK_STACK, 4, &s_upload_tcb,
                          &s_upload_stack)) {
        demo_fail("h31", "upload task");
        return;
    }
    if (!spawn_psram_task(record_worker, "h31_rec", REC_TASK_STACK, 5, &s_rec_tcb, &s_rec_stack)) {
        demo_fail("h31", "record task");
        return;
    }
    if (!speaker_open_once()) {
        ESP_LOGW(TAG, "speaker open failed");
    }

    status_idle_line();
    ESP_LOGI(TAG, "hold red=record; short Boot=play; long Boot=16/24k");

    while (!s_failed) {
        ui_flush();
        apply_profile_toggle();
        if (s_play_req && !s_held && !s_rec_task_running && !s_play_running) {
            s_play_req = false;
            (void)start_playback_task();
        }
        if (xSemaphoreTake(s_down_sem, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (!s_held) {
            continue;
        }
        if (mute_latched()) {
            board_status_set("unmute first");
            while (s_held && !s_failed) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }
        board_status_set("recording…");
        if (!start_record_task()) {
            board_status_set("busy");
            continue;
        }
        while (xSemaphoreTake(s_rec_done, pdMS_TO_TICKS(50)) != pdTRUE) {
            ui_flush();
        }
        ui_flush();
        while (s_held && !s_failed) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
