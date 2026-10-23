#include "lab16_upload.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

enum {
    SAMPLE_RATE = 16000,
    CHUNK_PCM_BYTES = SAMPLE_RATE * 2,
    WAV_HEADER_BYTES = 44,
    SECTOR_BYTES = 512,
    MAX_ATTEMPTS = 6,
};

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

static uint8_t s_read_buf[64 * SECTOR_BYTES] DMA_ATTR __attribute__((aligned(4)));

static esp_err_t sd_read_locked(const sdmmc_card_t *card, SemaphoreHandle_t sd_mu, void *dst, size_t lba,
                                size_t sectors)
{
    if (sd_mu != NULL) {
        xSemaphoreTake(sd_mu, portMAX_DELAY);
    }
    esp_err_t err = sdmmc_read_sectors(card, dst, lba, sectors);
    if (sd_mu != NULL) {
        xSemaphoreGive(sd_mu);
    }
    return err;
}

static esp_err_t read_pcm_chunk(const sdmmc_card_t *card, SemaphoreHandle_t sd_mu, uint64_t pcm_start_lba,
                                uint32_t seq, uint8_t *pcm_out)
{
    const uint64_t byte_off = (uint64_t)seq * (uint64_t)CHUNK_PCM_BYTES;
    const uint64_t lba = pcm_start_lba + byte_off / SECTOR_BYTES;
    const size_t offset = (size_t)(byte_off % SECTOR_BYTES);
    const size_t need = offset + CHUNK_PCM_BYTES;
    const size_t sectors = (need + SECTOR_BYTES - 1) / SECTOR_BYTES;
    esp_err_t err = sd_read_locked(card, sd_mu, s_read_buf, (size_t)lba, sectors);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(pcm_out, ((uint8_t *)s_read_buf) + offset, CHUNK_PCM_BYTES);
    return ESP_OK;
}

static int post_chunk(const uint8_t *wav, int wav_len, int session_id, int seq)
{
    char url[160];
    snprintf(url, sizeof(url), "http://%s:%d/v1/diary", CONFIG_LAB16_SERVER_HOST, CONFIG_LAB16_SERVER_PORT);
    static const char *bnd = "----FamilyLinkLab16";
    char pre[320];
    int pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"session\"\r\n\r\n"
                           "%d\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"seq\"\r\n\r\n"
                           "%d\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; filename=\"chunk.wav\"\r\n"
                           "Content-Type: audio/wav\r\n\r\n",
                           bnd, session_id, bnd, seq, bnd);
    char post[80];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    int total = pre_len + wav_len + post_len;

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", CONFIG_LAB16_DEVICE_TOKEN);
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
    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

esp_err_t lab16_upload_pcm_from_sd(const sdmmc_card_t *card, uint64_t start_lba, uint32_t stream_seconds,
                                   int session_id, SemaphoreHandle_t sd_mu, EventGroupHandle_t progress_events,
                                   int *uploaded_out, int *failed_out)
{
    const uint64_t pcm_start_lba = start_lba + 1;
    const int total_chunks = (int)stream_seconds;
    int uploaded = 0;
    int failed = 0;

    uint8_t *wav = heap_caps_malloc(WAV_HEADER_BYTES + CHUNK_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *pcm = heap_caps_malloc(CHUNK_PCM_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wav == NULL || pcm == NULL) {
        free(wav);
        free(pcm);
        return ESP_ERR_NO_MEM;
    }

    printf("LAB16_UPLOAD session=%d status=starting chunks=%d host=%s:%d start_lba=%" PRIu64 "\n", session_id,
           total_chunks, CONFIG_LAB16_SERVER_HOST, CONFIG_LAB16_SERVER_PORT, start_lba);
    fflush(stdout);
    if (progress_events != NULL) {
        xEventGroupSetBits(progress_events, LAB16_UPLOAD_EVT_STARTED);
    }

    for (int seq = 0; seq < total_chunks; seq++) {
        esp_err_t err = read_pcm_chunk(card, sd_mu, pcm_start_lba, (uint32_t)seq, pcm);
        if (err != ESP_OK) {
            failed++;
            printf("LAB16_UPLOAD session=%d chunk seq=%d status=read_failed error=%d\n", session_id, seq,
                   (int)err);
            continue;
        }
        wav_header(wav, CHUNK_PCM_BYTES);
        memcpy(wav + WAV_HEADER_BYTES, pcm, CHUNK_PCM_BYTES);
        int status = -1;
        for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
            status = post_chunk(wav, WAV_HEADER_BYTES + CHUNK_PCM_BYTES, session_id, seq);
            if (status == 200) {
                uploaded++;
                if (seq % 30 == 0) {
                    printf("LAB16_UPLOAD session=%d progress seq=%d uploaded=%d\n", session_id, seq, uploaded);
                    fflush(stdout);
                }
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        if (status != 200) {
            failed++;
            printf("LAB16_UPLOAD session=%d chunk seq=%d status=http_%d\n", session_id, seq, status);
        }
    }

    free(wav);
    free(pcm);
    if (uploaded_out) {
        *uploaded_out = uploaded;
    }
    if (failed_out) {
        *failed_out = failed;
    }
    if (uploaded == total_chunks && failed == 0) {
        printf("LAB16_UPLOAD session=%d status=complete uploaded=%d failed=0\n", session_id, uploaded);
        return ESP_OK;
    }
    printf("LAB16_UPLOAD session=%d status=failed uploaded=%d failed=%d expected=%d\n", session_id, uploaded,
           failed, total_chunks);
    return ESP_FAIL;
}

static void upload_task(void *arg)
{
    lab16_upload_job_t *job = (lab16_upload_job_t *)arg;
    job->result = lab16_upload_pcm_from_sd(job->card, job->start_lba, job->stream_seconds, job->session_id,
                                           job->sd_mu, job->events, &job->uploaded, &job->failed);
    if (job->events != NULL) {
        xEventGroupSetBits(job->events, LAB16_UPLOAD_EVT_DONE);
    }
    job->task = NULL;
    vTaskDelete(NULL);
}

esp_err_t lab16_upload_job_start(lab16_upload_job_t *job)
{
    if (job == NULL || job->card == NULL || job->events == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    job->result = ESP_ERR_INVALID_STATE;
    job->uploaded = 0;
    job->failed = 0;
    BaseType_t ok = xTaskCreate(upload_task, "lab16_up", 12288, job, 5, &job->task);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t lab16_upload_job_wait(lab16_upload_job_t *job, uint32_t wait_bits, TickType_t timeout_ticks)
{
    if (job == NULL || job->events == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    EventBits_t got = xEventGroupWaitBits(job->events, wait_bits, pdFALSE, pdTRUE, timeout_ticks);
    if ((got & wait_bits) != wait_bits) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
