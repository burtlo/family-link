#include "product_app_run.h"
#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "product_hardware_buttons.h"
#include "product_mem_probe.h"
#include "product_network.h"
#include "product_ui_window.h"

#define APP_LOG_TAG "app_main"
#define APP_LVGL_ATTACH_LOCK_MS 5000

namespace
{

class DisplayLock
{
  public:
    explicit DisplayLock(int timeout_ms)
        : locked_(bsp_display_lock(timeout_ms) != 0) {}
    ~DisplayLock()
    {
        if (locked_) {
            bsp_display_unlock();
        }
    }
    explicit operator bool() const
    {
        return locked_;
    }

    DisplayLock(const DisplayLock &) = delete;
    DisplayLock &operator=(const DisplayLock &) = delete;

  private:
    bool locked_;
};

bool attach_product_ui()
{
    if (!product_ui_window_create(lv_screen_active())) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_window_create_failed");
        return false;
    }
    if (!product_ui_window_show_desk()) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_window_show_desk_failed");
        return false;
    }
    lv_refr_now(lv_display_get_default());
    product_mem_probe("ui_ready", 0);
    return true;
}

void product_app_idle(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} /* namespace */

extern "C" void product_app_run(void)
{
    bool ui_ready = false;

    {
        DisplayLock lock(APP_LVGL_ATTACH_LOCK_MS);
        if (!lock) {
            ESP_LOGE(APP_LOG_TAG, "lvgl_lock_failed");
        } else {
            ui_ready = attach_product_ui();
        }
    }

    if (ui_ready) {
        if (!product_hardware_buttons_mute_latched()) {
            (void)bsp_display_backlight_on();
        }
        ESP_LOGI(APP_LOG_TAG, "product_ui_ready build=%s", FIRMWARE_BUILD_ID);
        product_network_start();
    }
    product_app_idle();
}
