#include "product_app.hpp"

#include "esp_log.h"
#include "lvgl.h"
#include "product_app_idle.hpp"
#include "product_buttons.hpp"
#include "product_display.hpp"
#include "product_display_lock.hpp"
#include "product_mem.hpp"
#include "product_network.hpp"
#include "product_ui_window.hpp"

#define APP_LOG_TAG "app_main"
#define APP_LVGL_ATTACH_LOCK_MS 5000

namespace product_app
{

namespace
{
bool attach_product_ui();
} /* namespace */

void run()
{
    const bool ui_ready = attach_product_ui();

    if (ui_ready) {
        if (product_buttons::mute_latched() == 0) {
            product_display::backlight_on();
        }
        ESP_LOGI(APP_LOG_TAG, "product_ui_ready build=%s", FIRMWARE_BUILD_ID);
        product_network::start();
    }
    idle_forever();
}

namespace
{

bool attach_product_ui()
{
    product_display::DisplayLock lock(APP_LVGL_ATTACH_LOCK_MS);
    if (!lock) {
        ESP_LOGE(APP_LOG_TAG, "lvgl_lock_failed");
        return false;
    }

    product_ui::UiWindow &win = product_ui::uiWindow();
    if (!win.create(lv_screen_active())) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_window_create_failed");
        return false;
    }
    if (!win.mountDesk()) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_window_show_desk_failed");
        return false;
    }
    lv_refr_now(lv_display_get_default());
    product_mem::probe("ui_ready", 0);
    return true;
}

} /* namespace */

} /* namespace product_app */
