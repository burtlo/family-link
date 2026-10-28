#include "product_app_run.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "product_hardware_buttons.h"
#include "product_login_fw.h"
#include "product_mem_probe.h"
#include "product_network.h"
#include "product_split_pane.h"

#define APP_LOG_TAG              "app_main"
#define APP_LVGL_ATTACH_LOCK_MS  5000

static bool product_app_lvgl_lock_for_attach(void);
static void product_app_idle(void);

void product_app_run(void)
{
    if (!product_app_lvgl_lock_for_attach()) {
        return;
    }

    if (!product_split_pane_init(lv_screen_active())) {
        ESP_LOGE(APP_LOG_TAG, "product_split_pane_init_failed");
        bsp_display_unlock();
        goto idle;
    }
    lv_obj_t *upper = NULL;
    lv_obj_t *lower = NULL;
    if (!product_split_pane_slot_roots(&upper, &lower) || !product_login_attach(upper, lower)) {
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

/* LVGL is single-threaded: the BSP runs lv_timer_handler on its own task. app_main must
 * take bsp_display_lock before any lv_* call (split pane init, login attach) and unlock after. */
static bool product_app_lvgl_lock_for_attach(void)
{
    if (!bsp_display_lock(APP_LVGL_ATTACH_LOCK_MS)) {
        ESP_LOGE(APP_LOG_TAG, "lvgl_lock_failed");
        return false;
    }
    return true;
}

/* app_main must not return: LVGL, login timer, and product_net already run elsewhere.
 * This task has no further work, including after a failed pane/login attach (goto idle)
 * when subsystems stay up for serial diagnosis. A blunt sleep loop until boot owns a
 * clearer main-task lifecycle. */
static void product_app_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
