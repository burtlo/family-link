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
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task.h"
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
#ifndef CONFIG_LAB16_CAPTURE_EVIDENCE
#define CONFIG_LAB16_CAPTURE_EVIDENCE 0
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

#if CONFIG_LAB16_CAPTURE_EVIDENCE
enum {
    LAB16_MEM_SAMPLE_INTERVAL_MS = 50,
    LAB16_MEM_SAMPLER_STACK_BYTES = 4096,
};

// ESP-IDF 5.4.2's bundled ESP32-S3 Wi-Fi driver calls
// config_get_wifi_task_stack_size(): 3 KiB base, 6 KiB when WPA3-SAE is
// enabled, plus TASK_EXTRA_STACK_SIZE unless newlib nano format is enabled.
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
#define LAB16_WIFI_TASK_STACK_BASE_BYTES 6144U
#else
#define LAB16_WIFI_TASK_STACK_BASE_BYTES 3072U
#endif
#if CONFIG_NEWLIB_NANO_FORMAT
#define LAB16_WIFI_TASK_STACK_BYTES LAB16_WIFI_TASK_STACK_BASE_BYTES
#else
#define LAB16_WIFI_TASK_STACK_BYTES (LAB16_WIFI_TASK_STACK_BASE_BYTES + TASK_EXTRA_STACK_SIZE)
#endif
#if defined(CONFIG_LWIP_TCPIP_CORE_LOCKING) && CONFIG_LWIP_TCPIP_CORE_LOCKING
#define LAB16_EVENT_LOOP_CORE_LOCKING_EXTRA_BYTES 2048U
#else
#define LAB16_EVENT_LOOP_CORE_LOCKING_EXTRA_BYTES 0U
#endif

typedef struct {
    uint32_t hwm_sdk_units;
    bool seen;
} lab16_task_stack_window_t;

typedef struct {
    uint32_t sample_count;
    int64_t start_t_us;
    int64_t end_t_us;
    size_t internal_total;
    size_t internal_min_free;
    int64_t internal_min_free_t_us;
    size_t internal_min_largest;
    int64_t internal_min_largest_t_us;
    size_t internal_cumulative_min_free;
    int64_t internal_cumulative_min_free_t_us;
    size_t internal_current_free;
    size_t internal_current_largest;
    size_t dma_total;
    size_t dma_min_free;
    int64_t dma_min_free_t_us;
    size_t dma_min_largest;
    int64_t dma_min_largest_t_us;
    size_t dma_cumulative_min_free;
    int64_t dma_cumulative_min_free_t_us;
    size_t dma_current_free;
    size_t dma_current_largest;
    size_t psram_total;
    size_t psram_min_free;
    int64_t psram_min_free_t_us;
    size_t psram_min_largest;
    int64_t psram_min_largest_t_us;
    size_t psram_cumulative_min_free;
    int64_t psram_cumulative_min_free_t_us;
    size_t psram_current_free;
    size_t psram_current_largest;
    uint64_t pcm_bytes_max;
    uint32_t sd_driver_writes_max;
    uint32_t outstanding_buffer_count_max;
    size_t outstanding_buffer_bytes_max;
    uint32_t request_inflight_max;
    uint32_t accepted_requests_max;
    uint32_t failed_requests_max;
    uint32_t retries_max;
    uint32_t main_stack_hwm_sdk_units;
    uint32_t sampler_stack_hwm_sdk_units;
    lab16_task_stack_window_t event_loop_stack;
    lab16_task_stack_window_t wifi_stack;
    lab16_task_stack_window_t tcpip_stack;
} lab16_mem_window_t;

static const char *volatile s_mem_beat = "boot";
static volatile uint64_t s_pcm_bytes_written;
static volatile uint32_t s_sd_driver_writes;
static volatile bool s_mem_sampler_running;
static volatile bool s_mem_sampler_enabled;
static volatile bool s_mem_sampler_stop;
static TaskHandle_t s_mem_sampler_task;
static TaskHandle_t s_main_task;
static SemaphoreHandle_t s_mem_sample_mu;
static uint32_t s_mem_sample_count;
static uint32_t s_mem_window_seq;
static size_t s_baseline_internal_cumulative_min_free;
static size_t s_baseline_dma_cumulative_min_free;
static size_t s_baseline_psram_cumulative_min_free;
static bool s_mem_baseline_ready;
static lab16_mem_window_t s_mem_window;
static portMUX_TYPE s_mem_mux = portMUX_INITIALIZER_UNLOCKED;
static void mem_begin_beat(const char *beat);

static uint32_t mem_stack_hwm(TaskHandle_t task)
{
    return task == NULL ? 0U : (uint32_t)uxTaskGetStackHighWaterMark2(task);
}

static void mem_stack_window_sample(lab16_task_stack_window_t *window, TaskHandle_t task)
{
    if (task == NULL) return;
    const uint32_t hwm = mem_stack_hwm(task);
    if (!window->seen || hwm < window->hwm_sdk_units) window->hwm_sdk_units = hwm;
    window->seen = true;
}

static const char *mem_stack_status(bool seen)
{
    return seen ? "captured" : "not_yet_started";
}

static void mem_reset_window(void)
{
    memset(&s_mem_window, 0, sizeof(s_mem_window));
}

// Caller holds s_mem_sample_mu.  Samples are aggregated in RAM; serial output is
// emitted only when a named beat closes.
static void mem_accumulate_sample(const char *beat)
{
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA;
    const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal_total = heap_caps_get_total_size(internal_caps);
    const size_t internal_free = heap_caps_get_free_size(internal_caps);
    const size_t internal_largest = heap_caps_get_largest_free_block(internal_caps);
    const size_t internal_cumulative_min_free = heap_caps_get_minimum_free_size(internal_caps);
    const size_t dma_total = heap_caps_get_total_size(dma_caps);
    const size_t dma_free = heap_caps_get_free_size(dma_caps);
    const size_t dma_largest = heap_caps_get_largest_free_block(dma_caps);
    const size_t dma_cumulative_min_free = heap_caps_get_minimum_free_size(dma_caps);
    const size_t psram_total = heap_caps_get_total_size(psram_caps);
    const size_t psram_free = heap_caps_get_free_size(psram_caps);
    const size_t psram_largest = heap_caps_get_largest_free_block(psram_caps);
    const size_t psram_cumulative_min_free = heap_caps_get_minimum_free_size(psram_caps);
    const int64_t t_us = esp_timer_get_time();
    const uint32_t main_hwm = mem_stack_hwm(s_main_task);
    const uint32_t sampler_hwm = mem_stack_hwm(s_mem_sampler_task);
    TaskHandle_t event_loop_task = xTaskGetHandle("sys_evt");
    TaskHandle_t wifi_task = xTaskGetHandle("wifi");
    TaskHandle_t tcpip_task = xTaskGetHandle("tiT");

    lab16_upload_capture_stats_t upload_stats = {0};
    lab16_upload_capture_snapshot(&upload_stats);

    portENTER_CRITICAL(&s_mem_mux);
    lab16_mem_window_t *window = &s_mem_window;
    if (window->sample_count == 0) {
        window->start_t_us = t_us;
        window->internal_total = internal_total;
        window->internal_min_free = internal_free;
        window->internal_min_free_t_us = t_us;
        window->internal_min_largest = internal_largest;
        window->internal_min_largest_t_us = t_us;
        window->internal_cumulative_min_free = internal_cumulative_min_free;
        window->internal_cumulative_min_free_t_us = t_us;
        window->dma_total = dma_total;
        window->dma_min_free = dma_free;
        window->dma_min_free_t_us = t_us;
        window->dma_min_largest = dma_largest;
        window->dma_min_largest_t_us = t_us;
        window->dma_cumulative_min_free = dma_cumulative_min_free;
        window->dma_cumulative_min_free_t_us = t_us;
        window->psram_total = psram_total;
        window->psram_min_free = psram_free;
        window->psram_min_free_t_us = t_us;
        window->psram_min_largest = psram_largest;
        window->psram_min_largest_t_us = t_us;
        window->psram_cumulative_min_free = psram_cumulative_min_free;
        window->psram_cumulative_min_free_t_us = t_us;
    } else {
        if (internal_free < window->internal_min_free) {
            window->internal_min_free = internal_free;
            window->internal_min_free_t_us = t_us;
        }
        if (internal_largest < window->internal_min_largest) {
            window->internal_min_largest = internal_largest;
            window->internal_min_largest_t_us = t_us;
        }
        if (internal_cumulative_min_free < window->internal_cumulative_min_free) {
            window->internal_cumulative_min_free = internal_cumulative_min_free;
            window->internal_cumulative_min_free_t_us = t_us;
        }
        if (dma_free < window->dma_min_free) {
            window->dma_min_free = dma_free;
            window->dma_min_free_t_us = t_us;
        }
        if (dma_largest < window->dma_min_largest) {
            window->dma_min_largest = dma_largest;
            window->dma_min_largest_t_us = t_us;
        }
        if (dma_cumulative_min_free < window->dma_cumulative_min_free) {
            window->dma_cumulative_min_free = dma_cumulative_min_free;
            window->dma_cumulative_min_free_t_us = t_us;
        }
        if (psram_free < window->psram_min_free) {
            window->psram_min_free = psram_free;
            window->psram_min_free_t_us = t_us;
        }
        if (psram_largest < window->psram_min_largest) {
            window->psram_min_largest = psram_largest;
            window->psram_min_largest_t_us = t_us;
        }
        if (psram_cumulative_min_free < window->psram_cumulative_min_free) {
            window->psram_cumulative_min_free = psram_cumulative_min_free;
            window->psram_cumulative_min_free_t_us = t_us;
        }
    }
    window->end_t_us = t_us;
    window->internal_current_free = internal_free;
    window->internal_current_largest = internal_largest;
    window->dma_current_free = dma_free;
    window->dma_current_largest = dma_largest;
    window->psram_current_free = psram_free;
    window->psram_current_largest = psram_largest;
    window->main_stack_hwm_sdk_units = main_hwm;
    window->sampler_stack_hwm_sdk_units = sampler_hwm;
    mem_stack_window_sample(&window->event_loop_stack, event_loop_task);
    mem_stack_window_sample(&window->wifi_stack, wifi_task);
    mem_stack_window_sample(&window->tcpip_stack, tcpip_task);
    const uint64_t pcm_bytes = s_pcm_bytes_written;
    const uint32_t driver_writes = s_sd_driver_writes;
    if (pcm_bytes > window->pcm_bytes_max) window->pcm_bytes_max = pcm_bytes;
    if (driver_writes > window->sd_driver_writes_max) window->sd_driver_writes_max = driver_writes;
    if (upload_stats.app_buffer_count > window->outstanding_buffer_count_max) {
        window->outstanding_buffer_count_max = upload_stats.app_buffer_count;
    }
    if (upload_stats.app_buffer_bytes > window->outstanding_buffer_bytes_max) {
        window->outstanding_buffer_bytes_max = upload_stats.app_buffer_bytes;
    }
    if (upload_stats.request_inflight > window->request_inflight_max) {
        window->request_inflight_max = upload_stats.request_inflight;
    }
    if (upload_stats.accepted_requests > window->accepted_requests_max) {
        window->accepted_requests_max = upload_stats.accepted_requests;
    }
    if (upload_stats.failed_requests > window->failed_requests_max) {
        window->failed_requests_max = upload_stats.failed_requests;
    }
    if (upload_stats.retries > window->retries_max) window->retries_max = upload_stats.retries;
    window->sample_count++;
    s_mem_sample_count++;
    portEXIT_CRITICAL(&s_mem_mux);

    if (!s_mem_baseline_ready && strcmp(beat, "baseline_before_sd") == 0) {
        s_baseline_internal_cumulative_min_free = internal_cumulative_min_free;
        s_baseline_dma_cumulative_min_free = dma_cumulative_min_free;
        s_baseline_psram_cumulative_min_free = psram_cumulative_min_free;
        s_mem_baseline_ready = true;
    }
}

static int64_t mem_watermark_delta(size_t baseline, size_t observed)
{
    return (int64_t)baseline - (int64_t)observed;
}

static void mem_emit_window_locked(const char *beat)
{
    const lab16_mem_window_t *w = &s_mem_window;
    if (w->sample_count == 0) return;
    const uint32_t seq = ++s_mem_window_seq;
    printf("LAB16_MEM schema=lab16-memory/v3 event=window seq=%" PRIu32
           " sample_interval_ms=%d sample_count=%" PRIu32 " t_start_us=%" PRId64 " t_end_us=%" PRId64
           " beat=%s internal_total=%zu internal_interval_min_free=%zu internal_interval_min_free_t_us=%" PRId64
           " internal_interval_min_largest=%zu internal_interval_min_largest_t_us=%" PRId64
           " internal_cumulative_min_free_since_heap_init=%zu internal_cumulative_min_free_t_us=%" PRId64
           " internal_cumulative_min_free_delta_from_baseline=%" PRId64
           " internal_current_free=%zu internal_current_largest=%zu"
           " dma_total=%zu dma_interval_min_free=%zu dma_interval_min_free_t_us=%" PRId64
           " dma_interval_min_largest=%zu dma_interval_min_largest_t_us=%" PRId64
           " dma_cumulative_min_free_since_heap_init=%zu dma_cumulative_min_free_t_us=%" PRId64
           " dma_cumulative_min_free_delta_from_baseline=%" PRId64
           " dma_current_free=%zu dma_current_largest=%zu"
           " psram_total=%zu psram_interval_min_free=%zu psram_interval_min_free_t_us=%" PRId64
           " psram_interval_min_largest=%zu psram_interval_min_largest_t_us=%" PRId64
           " psram_cumulative_min_free_since_heap_init=%zu psram_cumulative_min_free_t_us=%" PRId64
           " psram_cumulative_min_free_delta_from_baseline=%" PRId64
           " psram_current_free=%zu psram_current_largest=%zu"
           " pcm_bytes_max=%" PRIu64 " sd_driver_writes_max=%" PRIu32
           " outstanding_buffer_count_max=%" PRIu32 " outstanding_buffer_bytes_max=%zu"
           " request_inflight_max=%" PRIu32 " accepted_requests_max=%" PRIu32
           " failed_requests_max=%" PRIu32 " retries_max=%" PRIu32
           " main_task_name=main main_task_configured_stack_bytes=%u main_task_allocated_stack_bytes=%u"
           " main_task_status=captured main_task_stack_hwm_sdk_units=%" PRIu32
           " main_task_stack_hwm_unit=StackType_t_items main_task_stack_type_bytes=%zu"
           " sampler_task_name=lab16_mem sampler_task_configured_stack_bytes=%u"
           " sampler_task_allocated_stack_bytes=%u sampler_task_status=captured"
           " sampler_task_stack_hwm_sdk_units=%" PRIu32
           " sampler_task_stack_hwm_unit=StackType_t_items sampler_task_stack_type_bytes=%zu"
           " event_loop_task_name=sys_evt event_loop_task_configured_stack_bytes=%u"
           " event_loop_task_allocated_stack_bytes=%u event_loop_task_status=%s"
           " event_loop_task_stack_hwm_sdk_units=%" PRIu32
           " event_loop_task_stack_hwm_unit=StackType_t_items event_loop_task_stack_type_bytes=%zu"
           " wifi_task_name=wifi wifi_task_configured_stack_bytes=%u wifi_task_allocated_stack_bytes=%u"
           " wifi_task_status=%s wifi_task_stack_hwm_sdk_units=%" PRIu32
           " wifi_task_stack_hwm_unit=StackType_t_items wifi_task_stack_type_bytes=%zu"
           " tcpip_task_name=tiT tcpip_task_configured_stack_bytes=%u tcpip_task_allocated_stack_bytes=%u"
           " tcpip_task_status=%s tcpip_task_stack_hwm_sdk_units=%" PRIu32
           " tcpip_task_stack_hwm_unit=StackType_t_items tcpip_task_stack_type_bytes=%zu\n",
           seq, LAB16_MEM_SAMPLE_INTERVAL_MS, w->sample_count, w->start_t_us, w->end_t_us, beat,
           w->internal_total, w->internal_min_free, w->internal_min_free_t_us,
           w->internal_min_largest, w->internal_min_largest_t_us,
           w->internal_cumulative_min_free, w->internal_cumulative_min_free_t_us,
           mem_watermark_delta(s_baseline_internal_cumulative_min_free, w->internal_cumulative_min_free),
           w->internal_current_free, w->internal_current_largest,
           w->dma_total, w->dma_min_free, w->dma_min_free_t_us, w->dma_min_largest, w->dma_min_largest_t_us,
           w->dma_cumulative_min_free, w->dma_cumulative_min_free_t_us,
           mem_watermark_delta(s_baseline_dma_cumulative_min_free, w->dma_cumulative_min_free),
           w->dma_current_free, w->dma_current_largest,
           w->psram_total, w->psram_min_free, w->psram_min_free_t_us,
           w->psram_min_largest, w->psram_min_largest_t_us,
           w->psram_cumulative_min_free, w->psram_cumulative_min_free_t_us,
           mem_watermark_delta(s_baseline_psram_cumulative_min_free, w->psram_cumulative_min_free),
           w->psram_current_free, w->psram_current_largest,
           w->pcm_bytes_max, w->sd_driver_writes_max, w->outstanding_buffer_count_max,
           w->outstanding_buffer_bytes_max, w->request_inflight_max, w->accepted_requests_max,
           w->failed_requests_max, w->retries_max,
           (unsigned)CONFIG_ESP_MAIN_TASK_STACK_SIZE,
           (unsigned)(CONFIG_ESP_MAIN_TASK_STACK_SIZE + TASK_EXTRA_STACK_SIZE),
           w->main_stack_hwm_sdk_units, sizeof(StackType_t),
           LAB16_MEM_SAMPLER_STACK_BYTES, LAB16_MEM_SAMPLER_STACK_BYTES,
           w->sampler_stack_hwm_sdk_units, sizeof(StackType_t),
           (unsigned)CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE,
           (unsigned)(CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE + TASK_EXTRA_STACK_SIZE
                      + LAB16_EVENT_LOOP_CORE_LOCKING_EXTRA_BYTES),
           mem_stack_status(w->event_loop_stack.seen), w->event_loop_stack.hwm_sdk_units, sizeof(StackType_t),
           (unsigned)LAB16_WIFI_TASK_STACK_BYTES, (unsigned)LAB16_WIFI_TASK_STACK_BYTES,
           mem_stack_status(w->wifi_stack.seen), w->wifi_stack.hwm_sdk_units, sizeof(StackType_t),
           (unsigned)CONFIG_LWIP_TCPIP_TASK_STACK_SIZE,
           (unsigned)(CONFIG_LWIP_TCPIP_TASK_STACK_SIZE + TASK_EXTRA_STACK_SIZE),
           mem_stack_status(w->tcpip_stack.seen), w->tcpip_stack.hwm_sdk_units, sizeof(StackType_t));
}

static void mem_sample_tick(void)
{
    if (s_mem_sample_mu == NULL) return;
    xSemaphoreTake(s_mem_sample_mu, portMAX_DELAY);
    if (!s_mem_sampler_stop) mem_accumulate_sample(s_mem_beat);
    xSemaphoreGive(s_mem_sample_mu);
}

static void mem_sampler(void *arg)
{
    (void)arg;
    s_mem_sampler_running = true;
    TickType_t last_wake = xTaskGetTickCount();
    while (!s_mem_sampler_stop) {
        if (!s_mem_sampler_enabled) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LAB16_MEM_SAMPLE_INTERVAL_MS));
        if (!s_mem_sampler_stop && s_mem_sampler_enabled) mem_sample_tick();
    }
    s_mem_sampler_running = false;
    s_mem_sampler_task = NULL;
    vTaskDelete(NULL);
}

static bool mem_sampler_start(void)
{
    mem_reset_window();
    s_mem_sample_count = 0;
    s_mem_window_seq = 0;
    s_mem_baseline_ready = false;
    s_mem_sampler_running = false;
    s_mem_sampler_enabled = false;
    s_mem_beat = "baseline_before_sd";
    s_mem_sampler_stop = false;
    s_main_task = xTaskGetCurrentTaskHandle();

    // Allocate the sampler task and its coordination mutex before the baseline
    // so their persistent heap/stack cost is present in every measurement.
    s_mem_sample_mu = xSemaphoreCreateMutex();
    if (s_mem_sample_mu == NULL) {
        printf("LAB16_MEM schema=lab16-memory/v3 event=monitor_start_failed reason=sample_mutex\n");
        return false;
    }
    if (xTaskCreate(mem_sampler, "lab16_mem", LAB16_MEM_SAMPLER_STACK_BYTES, NULL, 3,
                    &s_mem_sampler_task) != pdPASS) {
        printf("LAB16_MEM schema=lab16-memory/v3 event=monitor_start_failed t_us=%" PRId64 "\n",
               esp_timer_get_time());
        return false;
    }
    while (!s_mem_sampler_running) vTaskDelay(1);
    printf("LAB16_EVT schema=lab16-event/v1 event=beat t_us=%" PRId64 " beat=baseline_before_sd\n",
           esp_timer_get_time());
    xSemaphoreTake(s_mem_sample_mu, portMAX_DELAY);
    mem_accumulate_sample("baseline_before_sd");
    s_mem_sampler_enabled = true;
    xTaskNotifyGive(s_mem_sampler_task);
    xSemaphoreGive(s_mem_sample_mu);
    mem_begin_beat("sd_mount_probe");
    return true;
}

static void mem_begin_beat(const char *beat)
{
    if (s_mem_sample_mu == NULL) return;
    xSemaphoreTake(s_mem_sample_mu, portMAX_DELAY);
    mem_emit_window_locked(s_mem_beat);
    mem_reset_window();
    s_mem_beat = beat;
    printf("LAB16_EVT schema=lab16-event/v1 event=beat t_us=%" PRId64 " beat=%s\n",
           esp_timer_get_time(), beat);
    mem_accumulate_sample(beat);
    xSemaphoreGive(s_mem_sample_mu);
    fflush(stdout);
}

static void mem_set_pcm_progress(uint64_t bytes, uint32_t writes)
{
    portENTER_CRITICAL(&s_mem_mux);
    s_pcm_bytes_written = bytes;
    s_sd_driver_writes = writes;
    portEXIT_CRITICAL(&s_mem_mux);
}

static void mem_sampler_stop_and_wait(void)
{
    if (s_mem_sampler_task == NULL) return;
    s_mem_sampler_stop = true;
    xTaskNotifyGive(s_mem_sampler_task);
    while (s_mem_sampler_task != NULL) vTaskDelay(1);
    xSemaphoreTake(s_mem_sample_mu, portMAX_DELAY);
    mem_emit_window_locked(s_mem_beat);
    printf("LAB16_MEM schema=lab16-memory/v3 event=monitor_stopped t_us=%" PRId64
           " samples=%" PRIu32 " windows=%" PRIu32 "\n",
           esp_timer_get_time(), s_mem_sample_count, s_mem_window_seq);
    xSemaphoreGive(s_mem_sample_mu);
    fflush(stdout);
}
#endif

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

static void memory_beats_for_stream(int msg_index, const char **open_start, const char **open_complete,
                                    const char **recording)
{
#if defined(CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD) && CONFIG_LAB16_CAPTURE_EVIDENCE
    if (msg_index == 1) {
        *open_start = "overlap_msg1_mic_i2s_open_start";
        *open_complete = "overlap_msg1_mic_i2s_open_complete";
        *recording = "overlap_msg1_recording";
        return;
    }
    if (msg_index == 2) {
        *open_start = "overlap_msg2_mic_i2s_open_start";
        *open_complete = "overlap_msg2_mic_i2s_open_complete";
        *recording = "overlap_msg2_recording";
        return;
    }
#endif
    *open_start = "mic_i2s_open_start";
    *open_complete = "mic_i2s_open_complete";
    *recording = "sd_mic_recording";
}

static esp_err_t stream_mic_to_sd(lab16_sd_session_t *session, const lab16_sd_probe_t *probe, uint64_t start_lba,
                                  uint32_t stream_seconds, int msg_index, SemaphoreHandle_t sd_mu,
                                  uint64_t *next_lba_out, bool advance_memory_beats)
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

    const char *beat_open_start = "mic_i2s_open_start";
    const char *beat_open_complete = "mic_i2s_open_complete";
    const char *beat_recording = "sd_mic_recording";
    memory_beats_for_stream(msg_index, &beat_open_start, &beat_open_complete, &beat_recording);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    if (advance_memory_beats) {
        mem_begin_beat(beat_open_start);
    }
#endif
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
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    if (advance_memory_beats) {
        mem_begin_beat(beat_open_complete);
    }
#endif

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
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    if (advance_memory_beats) {
        mem_begin_beat(beat_recording);
    }
#endif
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
#if CONFIG_LAB16_CAPTURE_EVIDENCE
        mem_set_pcm_progress(writer.pcm_bytes, writer.driver_writes);
#endif

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

#ifdef CONFIG_LAB16_LIVE_STREAM_FAILOVER
enum {
    LIVE_CHUNK_BYTES = SAMPLE_RATE * 2,
    LIVE_UPLOAD_ATTEMPTS = 2,
    LIVE_DRAIN_ATTEMPTS = 6,
    LIVE_HTTP_TIMEOUT_MS = 5000,
    LIVE_DRAIN_TIMEOUT_MS = 20000,
};

static bool run_live_failover_harness(lab16_sd_session_t *session, const lab16_sd_probe_t *probe)
{
    const uint32_t stream_seconds = (uint32_t)CONFIG_LAB16_STREAM_SECONDS;
    const uint64_t start_lba = (uint64_t)CONFIG_LAB16_PCM_START_LBA;
    const uint64_t target_pcm_bytes = pcm_bytes_for_seconds(stream_seconds);
    const uint64_t pcm_sectors = (target_pcm_bytes + SECTOR_BYTES - 1) / SECTOR_BYTES;
    const uint64_t end_lba = start_lba + 1 + pcm_sectors;

    if (end_lba > probe->sectors) {
        printf("LAB16_LIVE status=failed reason=capacity\n");
        return false;
    }
    if (hardware_mute_engaged()) {
        printf("LAB16_LIVE status=failed reason=hardware_mute_engaged\n");
        return false;
    }
    if (!lab16_station_join()) {
        printf("LAB16_LIVE status=failed reason=wifi_join\n");
        return false;
    }
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    printf("LAB16_EVT schema=lab16-event/v1 event=wifi_join t_us=%" PRId64
           " beat=live_stream_upload_active status=connected\n", esp_timer_get_time());
    lab16_upload_set_evidence_beat("live_stream_upload_active");
    mem_begin_beat("live_mic_i2s_open_start");
#endif

    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    if (mic == NULL) {
        printf("LAB16_LIVE status=failed reason=mic_init\n");
        return false;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = SAMPLE_RATE,
        .channel = PCM_CHANNELS,
        .bits_per_sample = PCM_BITS,
    };
    if (esp_codec_dev_open(mic, &fs) != ESP_OK) {
        printf("LAB16_LIVE status=failed reason=mic_open\n");
        return false;
    }
    (void)esp_codec_dev_set_in_mute(mic, false);
    (void)esp_codec_dev_set_in_gain(mic, 42.0f);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    mem_begin_beat("live_mic_i2s_open_complete");
#endif

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
    if (writer_write_header_sector(session, &writer, &hdr, NULL) != ESP_OK) {
        (void)esp_codec_dev_close(mic);
        printf("LAB16_LIVE status=failed reason=header_write\n");
        return false;
    }

    printf("LAB16_LIVE status=streaming session=1 seconds=%" PRIu32 " start_lba=%" PRIu32 "\n", stream_seconds,
           (uint32_t)start_lba);
    fflush(stdout);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    mem_begin_beat("live_stream_upload_active");
#endif

    bool uploaded_live[CONFIG_LAB16_STREAM_SECONDS];
    memset(uploaded_live, 0, sizeof(uploaded_live));
    int failover_events = 0;
    int live_ok = 0;
    uint8_t *sec_pcm = heap_caps_malloc(LIVE_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (sec_pcm == NULL) {
        (void)esp_codec_dev_close(mic);
        printf("LAB16_LIVE status=failed reason=no_mem\n");
        return false;
    }

    const int64_t start_us = esp_timer_get_time();
    int last_progress_s = 0;
    esp_err_t err = ESP_OK;
#if CONFIG_LAB16_CAPTURE_EVIDENCE
    bool outage_beat_open = false;
#endif

    for (uint32_t seq = 0; seq < stream_seconds && err == ESP_OK; seq++) {
        size_t filled = 0;
        while (filled < LIVE_CHUNK_BYTES) {
            size_t want = MIC_CHUNK_BYTES;
            if (want > LIVE_CHUNK_BYTES - filled) {
                want = LIVE_CHUNK_BYTES - filled;
            }
            if (esp_codec_dev_read(mic, mic_chunk, want) != ESP_CODEC_DEV_OK) {
                err = ESP_FAIL;
                break;
            }
            err = writer_append(session, &writer, mic_chunk, want, NULL);
            if (err != ESP_OK) {
                break;
            }
            memcpy(sec_pcm + filled, mic_chunk, want);
            filled += want;
        }
        if (err != ESP_OK) {
            break;
        }

        int http = lab16_upload_pcm_chunk(1, (int)seq, sec_pcm, LIVE_CHUNK_BYTES, LIVE_UPLOAD_ATTEMPTS,
                                          LIVE_HTTP_TIMEOUT_MS);
        if (http == 200) {
            uploaded_live[seq] = true;
            live_ok++;
        } else {
            failover_events++;
            printf("LAB16_FAILOVER seq=%" PRIu32 " reason=http_%d\n", seq, http);
            fflush(stdout);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            if (!outage_beat_open) {
                mem_begin_beat("live_failover_outage");
                lab16_upload_set_evidence_beat("live_failover_outage");
                outage_beat_open = true;
            }
#endif
        }

        int64_t now_us = esp_timer_get_time();
        int elapsed_s = (int)((now_us - start_us) / 1000000LL);
        if (elapsed_s >= last_progress_s + PROGRESS_INTERVAL_S) {
            last_progress_s = elapsed_s - (elapsed_s % PROGRESS_INTERVAL_S);
            printf("LAB16_LIVE progress elapsed_s=%d seq=%" PRIu32 " live_ok=%d failover=%d\n", elapsed_s, seq,
                   live_ok, failover_events);
            fflush(stdout);
        }
    }

    (void)esp_codec_dev_close(mic);
    free(sec_pcm);

    if (err == ESP_OK) {
        err = writer_flush_full(session, &writer, NULL);
    }
    const int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000LL;
    if (err != ESP_OK || writer.pcm_bytes < target_pcm_bytes) {
        printf("LAB16_LIVE status=failed error=%d pcm_bytes=%" PRIu64 "\n", (int)err, writer.pcm_bytes);
        return false;
    }

    printf("LAB16_STREAM status=complete elapsed_ms=%" PRId64 " pcm_bytes=%" PRIu64
           " sectors_written=%" PRIu32 " driver_writes=%" PRIu32 " start_lba=%" PRIu32
           " sample_rate=%d\n",
           elapsed_ms, writer.pcm_bytes, (uint32_t)(1 + pcm_sectors), writer.driver_writes, (uint32_t)start_lba,
           SAMPLE_RATE);
    fflush(stdout);

    if (validate_written_pcm(session, start_lba, target_pcm_bytes) != ESP_OK) {
        return false;
    }

#if CONFIG_LAB16_CAPTURE_EVIDENCE
    lab16_upload_set_evidence_beat("live_sd_drain_active");
    mem_begin_beat("live_sd_drain_active");
#endif

    int drain_ok = 0;
    int drain_fail = 0;
    for (uint32_t seq = 0; seq < stream_seconds; seq++) {
        if (uploaded_live[seq]) {
            continue;
        }
        uint8_t *pcm = heap_caps_malloc(LIVE_CHUNK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (pcm == NULL) {
            drain_fail++;
            continue;
        }
        if (lab16_read_pcm_seq(&session->card, start_lba, seq, pcm, NULL) != ESP_OK) {
            free(pcm);
            drain_fail++;
            continue;
        }
        int http = lab16_upload_pcm_chunk(1, (int)seq, pcm, LIVE_CHUNK_BYTES, LIVE_DRAIN_ATTEMPTS,
                                          LIVE_DRAIN_TIMEOUT_MS);
        free(pcm);
        if (http == 200) {
            uploaded_live[seq] = true;
            drain_ok++;
        } else {
            drain_fail++;
        }
    }

    printf("LAB16_LIVE status=drain complete recovered=%d failed=%d failover_events=%d\n", drain_ok, drain_fail,
           failover_events);
    fflush(stdout);

    int total_uploaded = 0;
    for (uint32_t seq = 0; seq < stream_seconds; seq++) {
        if (uploaded_live[seq]) {
            total_uploaded++;
        }
    }

    if (drain_fail != 0 || total_uploaded != (int)stream_seconds) {
        printf("LAB16_UPLOAD session=1 status=failed uploaded=%d failed=%d expected=%" PRIu32 "\n", total_uploaded,
               drain_fail, stream_seconds);
        return false;
    }

    printf("LAB16_UPLOAD session=1 status=complete uploaded=%d failed=0\n", total_uploaded);
    printf("LAB16_LIVE status=complete live_uploaded=%d drained=%d failover_events=%d\n", live_ok, drain_ok,
           failover_events);
    fflush(stdout);
    return true;
}
#endif

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
    if (stream_mic_to_sd(session, probe, lba_msg1, stream_seconds, 1, NULL, &lba_msg2, true) != ESP_OK) {
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

#if CONFIG_LAB16_CAPTURE_EVIDENCE
    lab16_upload_set_evidence_beat("overlap_concurrent_peak");
    mem_begin_beat("overlap_concurrent_peak");
#endif
    printf("LAB16_OVERLAP status=recording_msg2 upload_session=1_active start_lba=%" PRIu64 "\n", lba_msg2);
    fflush(stdout);
    if (stream_mic_to_sd(session, probe, lba_msg2, stream_seconds, 2, sd_mu, NULL, false) != ESP_OK) {
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

#if CONFIG_LAB16_CAPTURE_EVIDENCE
    lab16_upload_set_evidence_beat("overlap_msg2_upload");
    mem_begin_beat("overlap_msg2_upload");
#endif
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
#if CONFIG_LAB16_CAPTURE_EVIDENCE && defined(CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD)
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v4 upload_fixture=lab16-upload/v2 harness=1\n",
           CONFIG_LAB16_BUILD_ID);
#elif CONFIG_LAB16_CAPTURE_EVIDENCE && defined(CONFIG_LAB16_LIVE_STREAM_FAILOVER)
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v5 upload_fixture=lab16-diary/v1 harness=1\n",
           CONFIG_LAB16_BUILD_ID);
#elif CONFIG_LAB16_CAPTURE_EVIDENCE
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v3 upload_fixture=lab16-upload/v2 harness=1\n",
           CONFIG_LAB16_BUILD_ID);
#elif defined(CONFIG_LAB16_OVERLAP_RECORD_DURING_UPLOAD)
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v4 harness=1\n", CONFIG_LAB16_BUILD_ID);
#elif defined(CONFIG_LAB16_LIVE_STREAM_FAILOVER)
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v5 harness=1\n", CONFIG_LAB16_BUILD_ID);
#else
    printf("LAB16_READY build=%s mode=scripted fixture=lab16-sd/v3 harness=1\n", CONFIG_LAB16_BUILD_ID);
#endif
    fflush(stdout);

    if (CONFIG_LAB16_BOOT_SETTLE_MS > 0) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LAB16_BOOT_SETTLE_MS));
    }
    printf("LAB16_BOOT status=stable settle_ms=%d\n", CONFIG_LAB16_BOOT_SETTLE_MS);
    fflush(stdout);

#if CONFIG_LAB16_CAPTURE_EVIDENCE
    const bool mem_capture_ready = mem_sampler_start();
#endif

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
#elif defined(CONFIG_LAB16_LIVE_STREAM_FAILOVER)
        harness_ok = run_live_failover_harness(&session, &probe);
#else
        bool stream_ok = false;
        bool audio_ok = false;
        bool upload_ok = false;
        const uint64_t target_pcm_bytes = pcm_bytes_for_seconds((uint32_t)CONFIG_LAB16_STREAM_SECONDS);
        const uint64_t start_lba = (uint64_t)CONFIG_LAB16_PCM_START_LBA;
        stream_ok = (stream_mic_to_sd(&session, &probe, start_lba, (uint32_t)CONFIG_LAB16_STREAM_SECONDS, 0, NULL,
                                      NULL, true) == ESP_OK);
        if (stream_ok) {
            audio_ok = (validate_written_pcm(&session, start_lba, target_pcm_bytes) == ESP_OK);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            printf("LAB16_EVT schema=lab16-event/v1 event=sd_readback t_us=%" PRId64
                   " beat=sd_mic_recording pcm_bytes=%" PRIu64 " validation=%s\n",
                   esp_timer_get_time(), target_pcm_bytes, audio_ok ? "pass" : "fail");
#endif
        }
        if (stream_ok && audio_ok) {
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            mem_begin_beat("wifi_upload_active");
#endif
        }
        if (stream_ok && audio_ok && lab16_station_join()) {
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            printf("LAB16_EVT schema=lab16-event/v1 event=wifi_join t_us=%" PRId64
                   " beat=wifi_upload_active status=connected\n", esp_timer_get_time());
#endif
            int uploaded = 0;
            int failed = 0;
            upload_ok = (lab16_upload_pcm_from_sd(&session.card, start_lba, (uint32_t)CONFIG_LAB16_STREAM_SECONDS,
                                                  1, NULL, NULL, &uploaded, &failed) == ESP_OK);
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            printf("LAB16_EVT schema=lab16-event/v1 event=upload_terminal t_us=%" PRId64
                   " beat=wifi_upload_active uploaded=%d failed=%d expected=%" PRIu32 "\n",
                   esp_timer_get_time(), uploaded, failed, (uint32_t)CONFIG_LAB16_STREAM_SECONDS);
#endif
        } else if (stream_ok && audio_ok) {
            printf("LAB16_UPLOAD status=failed reason=wifi_join\n");
#if CONFIG_LAB16_CAPTURE_EVIDENCE
            printf("LAB16_EVT schema=lab16-event/v1 event=wifi_join t_us=%" PRId64
                   " beat=wifi_upload_active status=failed\n", esp_timer_get_time());
#endif
        }
        harness_ok = stream_ok && audio_ok && upload_ok;
#endif
    }
    sd_session_close(&session);

#if CONFIG_LAB16_CAPTURE_EVIDENCE
    mem_begin_beat("upload_complete_workers_quiescent");
    mem_sampler_stop_and_wait();
    if (!mem_capture_ready) {
        printf("LAB16_MEM schema=lab16-memory/v3 event=capture_incomplete reason=monitor_start_failed\n");
    }
#endif

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
