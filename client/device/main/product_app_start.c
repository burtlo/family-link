#include "product_app_start.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "product_hardware_buttons.h"
#include "product_login_fw.h"
#include "product_mem_probe.h"
#include "product_network.h"
#include "product_shell_fw.h"

#define APP_LOG_TAG "app_main"

static void product_app_idle(void);

void product_app_run(void)
{
    if (!bsp_display_lock(5000)) {
        ESP_LOGE(APP_LOG_TAG, "lvgl_lock_failed");
        return;
    }
    if (!product_shell_init(lv_screen_active())) {
        ESP_LOGE(APP_LOG_TAG, "product_shell_init_failed");
        bsp_display_unlock();
        goto idle;
    }
    lv_obj_t *upper = NULL;
    lv_obj_t *lower = NULL;
    if (!product_shell_slot_roots(&upper, &lower) || !product_login_attach(upper, lower)) {
        ESP_LOGE(APP_LOG_TAG, "product_login_attach_failed");
        bsp_display_unlock();
        goto idle;
    }
    lv_refr_now(lv_display_get_default());
    product_mem_probe("ui_ready", 0);
    bsp_display_unlock();
    if (!product_hardware_buttons_mute_latched()) {
        (void)bsp_display_backlight_on();
    }
    ESP_LOGI(APP_LOG_TAG, "product_ui_ready build=%s", FIRMWARE_BUILD_ID);
    product_network_start();
idle:
    product_app_idle();
}

static void product_app_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
