/* LAB-16/v2: SENSOR microSD discovery, LBA0 read, then mic PCM stream to raw sectors. */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_attr.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lab16_station.h"
#include "lab16_upload.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"

#ifndef CONFIG_LAB16_BUILD_ID
#define CONFIG_LAB16_BUILD_ID "lab16-fw-build-id-missing"
#endif
#ifndef CONFIG_LAB16_BOOT_SETTLE_MS
#define CONFIG_LAB16_BOOT_SETTLE_MS 500
#endif
#ifndef CONFIG_LAB16_STREAM_SECONDS
#define CONFIG_LAB16_STREAM_SECONDS 210
#endif
#ifndef CONFIG_LAB16_PCM_START_LBA
#define CONFIG_LAB16_PCM_START_LBA 2048
#endif

enum {
    POWER_GPIO = 43,
    SECTOR_BYTES = 512,
    SAMPLE_RATE = 16000,
    PCM_CHANNELS = 1,
    PCM_BITS = 16,
    MIC_CHUNK_BYTES = 640,
    SD_WRITE_BYTES = 4096,
    SD_WRITE_SECTORS = SD_WRITE_BYTES / SECTOR_BYTES,
    PROGRESS_INTERVAL_S = 30,
};

_Static_assert(BSP_SD_D0 == 9 && BSP_SD_D1 == 13 && BSP_SD_D2 == 42 && BSP_SD_D3 == 12 &&
                   BSP_SD_CMD == 14 && BSP_SD_CLK == 11 && BSP_SD_POWER == 43,
               "BOX-3 SENSOR SDMMC pin contract changed");

static uint8_t sector0[SECTOR_BYTES] DMA_ATTR __attribute__((aligned(4)));
static uint8_t sd_dma[SD_WRITE_BYTES] DMA_ATTR __attribute__((aligned(4)));
static uint8_t mic_chunk[MIC_CHUNK_BYTES];

typedef struct {
    esp_err_t err;
    uint32_t sectors;
    uint16_t sector_bytes;
    uint64_t capacity_bytes;
    uint8_t bus_width;
    int real_freq_khz;
    bool read_lba0_ok;
} lab16_sd_probe_t;

typedef struct {
    sdmmc_host_t host;
    sdmmc_card_t card;
    bool host_ready;
    bool card_ready;
} lab16_sd_session_t;

typedef struct {
    uint64_t cur_lba;
    size_t fill;
    uint64_t pcm_bytes;
    uint32_t driver_writes;
    esp_err_t err;
} lab16_sd_writer_t;

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint32_t pcm_bytes;
    uint32_t start_lba;
} lab16_pcm_header_t;

static void power_on(void)
{
    gpio_config_t power = {
        .pin_bit_mask = 1ULL << POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&power);
    gpio_set_level(POWER_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static void power_off(void)
{
    gpio_set_level(POWER_GPIO, 1);
}

static bool hardware_mute_engaged(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static lab16_sd_probe_t probe_lba0(lab16_sd_session_t *session)
{
    lab16_sd_probe_t out = {0};
    esp_err_t err = sdmmc_read_sectors(&session->card, sector0, 0, 1);
    out.read_lba0_ok = (err == ESP_OK);
    if (err != ESP_OK) {
        out.err = err;
        return out;
    }

    if (!session->card.is_mem || session->card.is_mmc || session->card.is_sdio ||
        session->card.csd.sector_size <= 0 || session->card.csd.capacity == 0) {
        out.err = ESP_ERR_NOT_SUPPORTED;
        return out;
    }

    out.sectors = (uint32_t)session->card.csd.capacity;
    out.sector_bytes = (uint16_t)session->card.csd.sector_size;
    out.capacity_bytes = (uint64_t)session->card.csd.capacity * (uint64_t)session->card.csd.sector_size;
    out.bus_width = (uint8_t)session->host.get_bus_width(session->host.slot);
    out.real_freq_khz = session->card.real_freq_khz;
    out.err = ESP_OK;
    return out;
}

static esp_err_t sd_session_open(lab16_sd_session_t *session)
{
    memset(session, 0, sizeof(*session));
    power_on();

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK) {
        return err;
    }
    session->host_ready = true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.command_timeout_ms = 1000;
    session->host = host;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = BSP_SD_CLK;
    slot.cmd = BSP_SD_CMD;
    slot.d0 = BSP_SD_D0;
    slot.d1 = BSP_SD_D1;
    slot.d2 = BSP_SD_D2;
    slot.d3 = BSP_SD_D3;
    slot.width = 4;
    slot.cd = SDMMC_SLOT_NO_CD;
    slot.wp = SDMMC_SLOT_NO_WP;
    slot.flags = 0;

    err = sdmmc_host_init_slot(session->host.slot, &slot);
    if (err != ESP_OK) {
        return err;
    }

    err = sdmmc_card_init(&session->host, &session->card);
    if (err != ESP_OK) {
        return err;
    }
    session->card_ready = true;
    return ESP_OK;
}

static void sd_session_close(lab16_sd_session_t *session)
{
    if (session->host_ready) {
        (void)sdmmc_host_deinit();
        session->host_ready = false;
    }
    power_off();
    session->card_ready = false;
}

static void writer_init(lab16_sd_writer_t *w, uint64_t start_lba)
{
    memset(w, 0, sizeof(*w));
    w->cur_lba = start_lba;
}

static esp_err_t writer_flush_full(lab16_sd_session_t *session, lab16_sd_writer_t *w, SemaphoreHandle_t sd_mu)
{
    if (w->fill == 0) {
        return ESP_OK;
    }
    if (w->fill != SD_WRITE_BYTES) {
        memset(sd_dma + w->fill, 0, SD_WRITE_BYTES - w->fill);
    }
    if (sd_mu != NULL) {
        xSemaphoreTake(sd_mu, portMAX_DELAY);
    }
    esp_err_t err = sdmmc_write_sectors(&session->card, sd_dma, (size_t)w->cur_lba, SD_WRITE_SECTORS);
    if (sd_mu != NULL) {
        xSemaphoreGive(sd_mu);
    }
    if (err != ESP_OK) {
        w->err = err;
        return err;
    }
    w->cur_lba += SD_WRITE_SECTORS;
    w->driver_writes++;
    w->fill = 0;
    return ESP_OK;
}

static esp_err_t writer_append(lab16_sd_session_t *session, lab16_sd_writer_t *w, const uint8_t *data, size_t len,
                               SemaphoreHandle_t sd_mu)
{
    while (len > 0) {
        size_t take = SD_WRITE_BYTES - w->fill;
        if (take > len) {
            take = len;
        }
        memcpy(sd_dma + w->fill, data, take);
        w->fill += take;
        data += take;
        len -= take;
        w->pcm_bytes += take;
        if (w->fill == SD_WRITE_BYTES) {
            esp_err_t err = writer_flush_full(session, w, sd_mu);
            if (err != ESP_OK) {
                return err;
            }
        }
    }
    return ESP_OK;
}

static esp_err_t writer_write_header_sector(lab16_sd_session_t *session, lab16_sd_writer_t *w,
                                            const lab16_pcm_header_t *hdr, SemaphoreHandle_t sd_mu)
{
    memset(sd_dma, 0, SECTOR_BYTES);
    memcpy(sd_dma, hdr, sizeof(*hdr));
    if (sd_mu != NULL) {
        xSemaphoreTake(sd_mu, portMAX_DELAY);
    }
    esp_err_t err = sdmmc_write_sectors(&session->card, sd_dma, (size_t)w->cur_lba, 1);
    if (sd_mu != NULL) {
        xSemaphoreGive(sd_mu);
    }
    if (err != ESP_OK) {
        w->err = err;
        return err;
    }
    w->cur_lba++;
    w->driver_writes++;
    return ESP_OK;
}

static void print_sd_line(const lab16_sd_probe_t *r)
{
    if (r->err == ESP_OK && r->read_lba0_ok) {
        printf("LAB16_SD status=detected sectors=%" PRIu32 " sector_bytes=%u capacity_bytes=%" PRIu64
               " bus_width=%u real_freq_khz=%d read_lba0=ok probe_writes=0\n",
               r->sectors, (unsigned)r->sector_bytes, r->capacity_bytes, (unsigned)r->bus_width,
               r->real_freq_khz);
    } else {
        printf("LAB16_SD status=failed error=%d read_lba0=%s probe_writes=0\n", (int)r->err,
               r->read_lba0_ok ? "ok" : "no");
    }
}

static uint64_t pcm_bytes_for_seconds(uint32_t stream_seconds)
{
    return (uint64_t)SAMPLE_RATE * (uint64_t)(PCM_BITS / 8) * (uint64_t)PCM_CHANNELS * (uint64_t)stream_seconds;
}

static uint64_t message_disk_sectors(uint32_t stream_seconds)
{
    const uint64_t target_pcm_bytes = pcm_bytes_for_seconds(stream_seconds);
    const uint64_t pcm_flushes = (target_pcm_bytes + SD_WRITE_BYTES - 1) / SD_WRITE_BYTES;
    return 1 + pcm_flushes * (uint64_t)SD_WRITE_SECTORS;
}

static esp_err_t stream_mic_to_sd(lab16_sd_session_t *session, const lab16_sd_probe_t *probe, uint64_t start_lba,
                                  uint32_t stream_seconds, int msg_index, SemaphoreHandle_t sd_mu,
                                  uint64_t *next_lba_out)
{
    const uint64_t target_pcm_bytes = pcm_bytes_for_seconds(stream_seconds);
    const uint64_t pcm_sectors = (target_pcm_bytes + SECTOR_BYTES - 1) / SECTOR_BYTES;
    const uint64_t end_lba = start_lba + 1 + pcm_sectors;

    if (end_lba > probe->sectors) {
        printf("LAB16_STREAM status=failed error=%d reason=capacity start_lba=%" PRIu64 " need_sectors=%" PRIu64
               "\n",
               (int)ESP_ERR_NO_MEM, start_lba, pcm_sectors + 1);
        return ESP_ERR_NO_MEM;
    }
    if (hardware_mute_engaged()) {
        printf("LAB16_STREAM status=failed error=%d reason=hardware_mute_engaged\n", (int)ESP_ERR_INVALID_STATE);
        return ESP_ERR_INVALID_STATE;
    }

    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    if (mic == NULL) {
        printf("LAB16_STREAM status=failed error=%d reason=mic_init\n", (int)ESP_FAIL);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = PCM_CHANNELS,
        .bits_per_sample = PCM_BITS,
    };

    esp_err_t err = esp_codec_dev_open(mic, &fs);
    if (err != ESP_OK) {
        printf("LAB16_STREAM status=failed error=%d reason=mic_open\n", (int)err);
        return err;
    }
    (void)esp_codec_dev_set_in_mute(mic, false);
    (void)esp_codec_dev_set_in_gain(mic, 42.0f);

    lab16_pcm_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "LAB16PCM", 8);
    hdr.version = 1;
    hdr.sample_rate = SAMPLE_RATE;
    hdr.channels = PCM_CHANNELS;
    hdr.bits = PCM_BITS;
    hdr.pcm_bytes = (uint32_t)target_pcm_bytes;
    hdr.start_lba = (uint32_t)start_lba;

    lab16_sd_writer_t writer;
    writer_init(&writer, start_lba);
    err = writer_write_header_sector(session, &writer, &hdr, sd_mu);
    if (err != ESP_OK) {
        (void)esp_codec_dev_close(mic);
        if (msg_index > 0) {
            printf("LAB16_STREAM msg=%d status=failed error=%d reason=header_write\n", msg_index, (int)err);
        } else {
            printf("LAB16_STREAM status=failed error=%d reason=header_write\n", (int)err);
        }
        return err;
    }

    if (msg_index > 0) {
        printf("LAB16_STREAM msg=%d status=recording seconds=%" PRIu32 " target_pcm_bytes=%" PRIu32
               " start_lba=%" PRIu32 " sample_rate=%d\n",
               msg_index, stream_seconds, (uint32_t)target_pcm_bytes, (uint32_t)start_lba, SAMPLE_RATE);
    } else {
        printf("LAB16_STREAM status=recording seconds=%" PRIu32 " target_pcm_bytes=%" PRIu32
               " start_lba=%" PRIu32 " sample_rate=%d\n",
               stream_seconds, (uint32_t)target_pcm_bytes, (uint32_t)start_lba, SAMPLE_RATE);
    }
    fflush(stdout);

    const int64_t start_us = esp_timer_get_time();
    const int64_t end_us = start_us + (int64_t)stream_seconds * 1000000LL;
    int last_progress_s = 0;

    while (writer.pcm_bytes < target_pcm_bytes) {
        int64_t now_us = esp_timer_get_time();
        if (now_us >= end_us) {
            break;
        }

        size_t want = MIC_CHUNK_BYTES;
        uint64_t remain = target_pcm_bytes - writer.pcm_bytes;
        if (want > remain) {
            want = (size_t)remain;
        }

        if (esp_codec_dev_read(mic, mic_chunk, want) != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            break;
        }

        err = writer_append(session, &writer, mic_chunk, want, sd_mu);
        if (err != ESP_OK) {
            break;
        }

        int elapsed_s = (int)((now_us - start_us) / 1000000LL);
        if (elapsed_s >= last_progress_s + PROGRESS_INTERVAL_S) {
            last_progress_s = elapsed_s - (elapsed_s % PROGRESS_INTERVAL_S);
            if (msg_index > 0) {
                printf("LAB16_STREAM msg=%d progress elapsed_s=%d pcm_bytes=%" PRIu64 "\n", msg_index, elapsed_s,
                       writer.pcm_bytes);
            } else {
                printf("LAB16_STREAM progress elapsed_s=%d pcm_bytes=%" PRIu64 "\n", elapsed_s, writer.pcm_bytes);
            }
            fflush(stdout);
        }
    }

    (void)esp_codec_dev_close(mic);

    if (err == ESP_OK && writer.pcm_bytes < target_pcm_bytes) {
        err = ESP_ERR_TIMEOUT;
    }
    if (err == ESP_OK) {
        err = writer_flush_full(session, &writer, sd_mu);
    }

    const int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000LL;
    if (err == ESP_OK) {
        if (msg_index > 0) {
            printf("LAB16_STREAM msg=%d status=complete elapsed_ms=%" PRId64 " pcm_bytes=%" PRIu64
                   " sectors_written=%" PRIu32 " driver_writes=%" PRIu32 " start_lba=%" PRIu32
                   " sample_rate=%d\n",
                   msg_index, elapsed_ms, writer.pcm_bytes, (uint32_t)(1 + pcm_sectors), writer.driver_writes,
                   (uint32_t)start_lba, SAMPLE_RATE);
        } else {
            printf("LAB16_STREAM status=complete elapsed_ms=%" PRId64 " pcm_bytes=%" PRIu64
                   " sectors_written=%" PRIu32 " driver_writes=%" PRIu32 " start_lba=%" PRIu32
                   " sample_rate=%d\n",
                   elapsed_ms, writer.pcm_bytes, (uint32_t)(1 + pcm_sectors), writer.driver_writes,
                   (uint32_t)start_lba, SAMPLE_RATE);
        }
    } else {
        if (msg_index > 0) {
            printf("LAB16_STREAM msg=%d status=failed error=%d elapsed_ms=%" PRId64 " pcm_bytes=%" PRIu64
                   " driver_writes=%" PRIu32 "\n",
                   msg_index, (int)err, elapsed_ms, writer.pcm_bytes, writer.driver_writes);
        } else {
            printf("LAB16_STREAM status=failed error=%d elapsed_ms=%" PRId64 " pcm_bytes=%" PRIu64
                   " driver_writes=%" PRIu32 "\n",
                   (int)err, elapsed_ms, writer.pcm_bytes, writer.driver_writes);
        }
    }
    fflush(stdout);
    if (err == ESP_OK && next_lba_out != NULL) {
        *next_lba_out = writer.cur_lba;
    }
    return err;
}

static int16_t pcm_peak_s16(const uint8_t *sector)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)sector;
    for (size_t i = 0; i < SECTOR_BYTES / sizeof(int16_t); i++) {
        int32_t v = s[i];
        if (v < 0) {
            v = -v;
        }
        if (v > peak) {
            peak = (int16_t)v;
        }
    }
    return peak;
}

static uint32_t pcm_rms_s16(const uint8_t *sector)
{
    uint64_t sumsq = 0;
    const int16_t *s = (const int16_t *)sector;
    size_t n = SECTOR_BYTES / sizeof(int16_t);
    for (size_t i = 0; i < n; i++) {
        int32_t v = s[i];
        sumsq += (uint64_t)(v * v);
    }
    return (uint32_t)(sumsq / n);
}

static esp_err_t validate_written_pcm(lab16_sd_session_t *session, uint64_t start_lba, uint64_t pcm_bytes)
{
    esp_err_t err = sdmmc_read_sectors(&session->card, sd_dma, (size_t)start_lba, 1);
    if (err != ESP_OK) {
        printf("LAB16_AUDIO status=failed error=%d reason=header_read\n", (int)err);
        return err;
    }
    const lab16_pcm_header_t *hdr = (const lab16_pcm_header_t *)sd_dma;
    if (memcmp(hdr->magic, "LAB16PCM", 8) != 0 || hdr->sample_rate != SAMPLE_RATE || hdr->pcm_bytes != pcm_bytes) {
        printf("LAB16_AUDIO status=failed error=%d reason=header_mismatch\n", (int)ESP_ERR_INVALID_CRC);
        return ESP_ERR_INVALID_CRC;
    }

    const uint64_t pcm_start_lba = start_lba + 1;
    const uint64_t mid_offset = pcm_bytes / 2;
    const uint64_t mid_lba = pcm_start_lba + (mid_offset / SECTOR_BYTES);
    const uint64_t last_lba = pcm_start_lba + ((pcm_bytes - SECTOR_BYTES) / SECTOR_BYTES);

    err = sdmmc_read_sectors(&session->card, sector0, (size_t)pcm_start_lba, 1);
    if (err != ESP_OK) {
        printf("LAB16_AUDIO status=failed error=%d reason=pcm_first_read\n", (int)err);
        return err;
    }
    int16_t peak_first = pcm_peak_s16(sector0);

    err = sdmmc_read_sectors(&session->card, sector0, (size_t)mid_lba, 1);
    if (err != ESP_OK) {
        printf("LAB16_AUDIO status=failed error=%d reason=pcm_mid_read\n", (int)err);
        return err;
    }
    int16_t peak_mid = pcm_peak_s16(sector0);
    uint32_t rms_mid = pcm_rms_s16(sector0);

    err = sdmmc_read_sectors(&session->card, sector0, (size_t)last_lba, 1);
    if (err != ESP_OK) {
        printf("LAB16_AUDIO status=failed error=%d reason=pcm_last_read\n", (int)err);
        return err;
    }
    int16_t peak_last = pcm_peak_s16(sector0);
    const int silent = (peak_first < 80 && peak_mid < 80 && peak_last < 80) ? 1 : 0;

    printf("LAB16_AUDIO status=ok header_magic=LAB16PCM peak_first=%d peak_mid=%d peak_last=%d rms_mid=%u silent=%d\n",
           (int)peak_first, (int)peak_mid, (int)peak_last, (unsigned)rms_mid, silent);
    fflush(stdout);
    return silent ? ESP_ERR_INVALID_STATE : ESP_OK;
}

#ifdef CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD
static bool run_overlap_harness(lab16_sd_session_t *session, const lab16_sd_probe_t *probe)
{
    const uint32_t stream_seconds = (uint32_t)CONFIG_LAB16_STREAM_SECONDS;
    const uint64_t lba_msg1 = (uint64_t)CONFIG_LAB16_PCM_START_LBA;
    uint64_t lba_msg2 = 0;
    const uint64_t pcm_bytes = pcm_bytes_for_seconds(stream_seconds);
    const uint64_t need_end = lba_msg1 + message_disk_sectors(stream_seconds) + message_disk_sectors(stream_seconds);

    if (need_end > probe->sectors) {
        printf("LAB16_OVERLAP status=failed reason=capacity need_end_lba=%" PRIu64 "\n", need_end);
        return false;
    }

    SemaphoreHandle_t sd_mu = xSemaphoreCreateMutex();
    if (sd_mu == NULL) {
        printf("LAB16_OVERLAP status=failed reason=sd_mutex\n");
        return false;
    }

    bool ok = false;
    if (stream_mic_to_sd(session, probe, lba_msg1, stream_seconds, 1, NULL, &lba_msg2) != ESP_OK) {
        goto done;
    }
    if (validate_written_pcm(session, lba_msg1, pcm_bytes) != ESP_OK) {
        goto done;
    }
    if (!lab16_station_join()) {
        printf("LAB16_UPLOAD status=failed reason=wifi_join\n");
        goto done;
    }

    EventGroupHandle_t events = xEventGroupCreate();
    if (events == NULL) {
        goto done;
    }

    lab16_upload_job_t job = {
        .card = &session->card,
        .sd_mu = sd_mu,
        .start_lba = lba_msg1,
        .stream_seconds = stream_seconds,
        .session_id = 1,
        .events = events,
    };
    if (lab16_upload_job_start(&job) != ESP_OK) {
        vEventGroupDelete(events);
        goto done;
    }
    if (lab16_upload_job_wait(&job, LAB16_UPLOAD_EVT_STARTED, pdMS_TO_TICKS(120000)) != ESP_OK) {
        printf("LAB16_OVERLAP status=failed reason=upload_start_timeout\n");
        vEventGroupDelete(events);
        goto done;
    }

    printf("LAB16_OVERLAP status=recording_msg2 upload_session=1_active start_lba=%" PRIu64 "\n", lba_msg2);
    fflush(stdout);
    if (stream_mic_to_sd(session, probe, lba_msg2, stream_seconds, 2, sd_mu, NULL) != ESP_OK) {
        vEventGroupDelete(events);
        goto done;
    }

    if (lab16_upload_job_wait(&job, LAB16_UPLOAD_EVT_DONE, pdMS_TO_TICKS(600000)) != ESP_OK ||
        job.result != ESP_OK) {
        printf("LAB16_OVERLAP status=failed reason=upload_msg1\n");
        vEventGroupDelete(events);
        goto done;
    }
    if (validate_written_pcm(session, lba_msg2, pcm_bytes) != ESP_OK) {
        vEventGroupDelete(events);
        goto done;
    }

    int uploaded2 = 0;
    int failed2 = 0;
    if (lab16_upload_pcm_from_sd(&session->card, lba_msg2, stream_seconds, 2, sd_mu, NULL, &uploaded2,
                                 &failed2) != ESP_OK) {
        vEventGroupDelete(events);
        goto done;
    }

    vEventGroupDelete(events);
    printf("LAB16_OVERLAP status=complete msg1_session=1 msg2_session=2 seconds=%" PRIu32 "\n", stream_seconds);
    fflush(stdout);
    ok = true;

done:
    vSemaphoreDelete(sd_mu);
    return ok;
}
#endif

void app_main(void)
{
#ifdef CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v4 harness=1\n", CONFIG_LAB16_BUILD_ID);
#else
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v3 harness=1\n", CONFIG_LAB16_BUILD_ID);
#endif
    fflush(stdout);

    if (CONFIG_LAB16_BOOT_SETTLE_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LAB16_BOOT_SETTLE_MS));
    }
    printf("LAB16_BOOT status=stable settle_ms=%d\n", CONFIG_LAB16_BOOT_SETTLE_MS);
    fflush(stdout);

    lab16_sd_session_t session;
    esp_err_t session_err = sd_session_open(&session);
    lab16_sd_probe_t probe = {0};
    if (session_err == ESP_OK) {
        probe = probe_lba0(&session);
    } else {
        probe.err = session_err;
    }
    print_sd_line(&probe);

    bool harness_ok = false;
    if (probe.err == ESP_OK && probe.read_lba0_ok) {
#ifdef CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD
        harness_ok = run_overlap_harness(&session, &probe);
#else
        bool stream_ok = false;
        bool audio_ok = false;
        bool upload_ok = false;
        const uint64_t target_pcm_bytes = pcm_bytes_for_seconds((uint32_t)CONFIG_LAB16_STREAM_SECONDS);
        const uint64_t start_lba = (uint64_t)CONFIG_LAB16_PCM_START_LBA;
        stream_ok = (stream_mic_to_sd(&session, &probe, start_lba, (uint32_t)CONFIG_LAB16_STREAM_SECONDS, 0, NULL,
                                      NULL) == ESP_OK);
        if (stream_ok) {
            audio_ok = (validate_written_pcm(&session, start_lba, target_pcm_bytes) == ESP_OK);
        }
        if (stream_ok && audio_ok && lab16_station_join()) {
            int uploaded = 0;
            int failed = 0;
            upload_ok = (lab16_upload_pcm_from_sd(&session.card, start_lba, (uint32_t)CONFIG_LAB16_STREAM_SECONDS,
                                                  1, NULL, NULL, &uploaded, &failed) == ESP_OK);
        } else if (stream_ok && audio_ok) {
            printf("LAB16_UPLOAD status=failed reason=wifi_join\n");
        }
        harness_ok = stream_ok && audio_ok && upload_ok;
#endif
    }
    sd_session_close(&session);

    if (probe.err == ESP_OK && probe.read_lba0_ok && harness_ok) {
        printf("LAB16_HARNESS_DONE status=complete\n");
    } else {
        printf("LAB16_HARNESS_DONE status=failed\n");
    }
    fflush(stdout);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
