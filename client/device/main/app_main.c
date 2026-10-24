#include "bsp/esp-box-3.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "product_display.h"
#include "sdkconfig.h"
#include "product_hardware_buttons.h"
#include "product_login_fw.h"
#include "product_network.h"
#include "product_shell_fw.h"

#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
static void log_mem_diag(const char *phase, int with_lvgl)
{
    size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (with_lvgl) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        ESP_LOGI("bootstrap",
                 "mem_diag phase=%s dma_largest_internal=%u lv_used_pct=%u lv_used_kb=%u",
                 phase, (unsigned)dma_largest, (unsigned)mon.used_pct,
                 (unsigned)(mon.cur_used / 1024U));
        return;
    }
    ESP_LOGI("bootstrap", "mem_diag phase=%s dma_largest_internal=%u", phase, (unsigned)dma_largest);
}
#endif

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    uint8_t mac[6];

#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
    log_mem_diag("before_display", 0);
#endif
    /* Bring up LCD/backlight before NVS or Wi-Fi (GPIO 47 / LEDC on ESP-BOX-3). */
    lv_display_t *display = product_display_start();
    if (display == NULL) {
        ESP_LOGE("bootstrap", "display_init_failed");
        return;
    }
#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
    log_mem_diag("after_display", 1);
#endif
    /* Latched boot stays at post-init duty 0, including attach failure. */
    if (product_hardware_buttons_mute_latched()) {
        (void)bsp_display_backlight_off();
    } else {
        (void)bsp_display_brightness_set(80);
        (void)bsp_display_backlight_on();
    }
    ESP_LOGI("bootstrap", "V2_BOOTSTRAP_READY build=%s", FAMILY_BUILD_ID);

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI("bootstrap", "version=%s idf=%s reset_reason=%d cores=%d revision=%d",
             app->version, app->idf_ver, (int)esp_reset_reason(), chip.cores, chip.revision);
    ESP_LOGI("bootstrap", "hardware=%02x:%02x:%02x:%02x:%02x:%02x free_heap=%lu",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             (unsigned long)esp_get_free_heap_size());
    if (!bsp_display_lock(5000)) {
        ESP_LOGE("bootstrap", "lvgl_lock_failed");
        return;
    }
    if (!product_shell_init(lv_screen_active())) {
        ESP_LOGE("bootstrap", "product_shell_init_failed");
        bsp_display_unlock();
        goto idle;
    }
    lv_obj_t *upper = NULL;
    lv_obj_t *lower = NULL;
    if (!product_shell_slot_roots(&upper, &lower) || !product_login_attach(upper, lower)) {
        ESP_LOGE("bootstrap", "product_login_attach_failed");
        bsp_display_unlock();
        goto idle;
    }
    lv_refr_now(lv_display_get_default());
    bsp_display_unlock();
    if (!product_hardware_buttons_mute_latched()) {
        (void)bsp_display_backlight_on();
    }
    ESP_LOGI("bootstrap", "product_ui_ready");
    product_network_start();
idle:
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
