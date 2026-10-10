#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    // Repeat a bounded diagnostic so a monitor opened after reset can verify this build.
    for (;;) {
        ESP_LOGI("bootstrap", "V2_BOOTSTRAP_READY build=%s", FAMILY_BUILD_ID);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
