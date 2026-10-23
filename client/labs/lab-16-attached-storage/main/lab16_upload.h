#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdmmc_cmd.h"

#define LAB16_UPLOAD_EVT_STARTED (1U << 0)
#define LAB16_UPLOAD_EVT_DONE (1U << 1)

esp_err_t lab16_upload_pcm_from_sd(const sdmmc_card_t *card, uint64_t start_lba, uint32_t stream_seconds,
                                   int session_id, SemaphoreHandle_t sd_mu, EventGroupHandle_t progress_events,
                                   int *uploaded_out, int *failed_out);

typedef struct {
    const sdmmc_card_t *card;
    SemaphoreHandle_t sd_mu;
    uint64_t start_lba;
    uint32_t stream_seconds;
    int session_id;
    EventGroupHandle_t events;
    esp_err_t result;
    int uploaded;
    int failed;
    TaskHandle_t task;
} lab16_upload_job_t;

esp_err_t lab16_upload_job_start(lab16_upload_job_t *job);
esp_err_t lab16_upload_job_wait(lab16_upload_job_t *job, uint32_t wait_bits, TickType_t timeout_ticks);
