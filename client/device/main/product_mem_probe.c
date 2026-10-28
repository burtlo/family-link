#include "product_mem_probe.h"

#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *kTag = "product_mem";

static void log_sample(const char *phase, int32_t detail, int with_lvgl)
{
    const unsigned dma_total =
        (unsigned)heap_caps_get_total_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const unsigned dma_free =
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const unsigned dma_big =
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    const unsigned int_total =
        (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned int_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned int_min = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const unsigned psram_total = (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const unsigned psram_free = (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const unsigned stack_hw = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    const long long t_ms = (long long)(esp_timer_get_time() / 1000LL);
    if (with_lvgl) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        ESP_LOGI(kTag,
                 "V2_MEM phase=%s detail=%ld t=%lld lv_total=%u lv_free=%u lv_big=%u lv_max_used=%u "
                 "lv_used_pct=%u lv_frag_pct=%u dma_total=%u dma_free=%u dma_big=%u int_total=%u int_free=%u "
                 "int_min=%u psram_total=%u psram_free=%u task=%s stack_hw=%u",
                 phase, (long)detail, t_ms, (unsigned)mon.total_size, (unsigned)mon.free_size,
                 (unsigned)mon.free_biggest_size, (unsigned)mon.max_used, (unsigned)mon.used_pct,
                 (unsigned)mon.frag_pct, dma_total, dma_free, dma_big, int_total, int_free, int_min, psram_total,
                 psram_free, pcTaskGetName(NULL), stack_hw);
        return;
    }
    ESP_LOGI(kTag,
             "V2_MEM phase=%s detail=%ld t=%lld dma_total=%u dma_free=%u dma_big=%u int_total=%u int_free=%u "
             "int_min=%u psram_total=%u psram_free=%u task=%s stack_hw=%u",
             phase, (long)detail, t_ms, dma_total, dma_free, dma_big, int_total, int_free, int_min, psram_total,
             psram_free, pcTaskGetName(NULL), stack_hw);
}

void product_mem_probe(const char *phase, int32_t detail)
{
    log_sample(phase, detail, 1);
}

void product_mem_probe_heap(const char *phase, int32_t detail)
{
    log_sample(phase, detail, 0);
}

#endif
