#include "bsp/esp-box-3.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "product_login_fw.h"
#include "product_shell_fw.h"

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    uint8_t mac[6];
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_LOGI("bootstrap", "version=%s idf=%s reset_reason=%d cores=%d revision=%d",
             app->version, app->idf_ver, (int)esp_reset_reason(), chip.cores, chip.revision);
    ESP_LOGI("bootstrap", "hardware=%02x:%02x:%02x:%02x:%02x:%02x free_heap=%lu",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             (unsigned long)esp_get_free_heap_size());
    ESP_LOGI("bootstrap", "V2_BOOTSTRAP_READY build=%s", FAMILY_BUILD_ID);

    lv_display_t *display = bsp_display_start();
    if (display == NULL) {
        ESP_LOGE("bootstrap", "display_init_failed");
        return;
    }
    bsp_display_brightness_set(80);
    if (!bsp_display_lock(5000)) {
        ESP_LOGE("bootstrap", "lvgl_lock_failed");
        return;
    }
    if (!product_shell_init(lv_screen_active())) {
        ESP_LOGE("bootstrap", "product_shell_init_failed");
        bsp_display_unlock();
        return;
    }
    lv_obj_t *upper = NULL;
    lv_obj_t *lower = NULL;
    if (product_shell_slot_roots(&upper, &lower) && !product_login_attach(upper, lower)) {
        ESP_LOGE("bootstrap", "product_login_attach_failed");
        bsp_display_unlock();
        return;
    }
    product_login_set_wifi(true);
    bsp_display_unlock();
}
