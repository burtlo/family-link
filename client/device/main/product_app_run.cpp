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
#include "product_runtime.hpp"
#include "esp_timer.h"
#include <memory>

#define APP_LOG_TAG "app_main"
#define APP_LVGL_ATTACH_LOCK_MS 5000

namespace product_app
{

namespace
{
bool attach_product_ui();
// Root's controllers hold navigation pointing at the runtime, so the runtime
// must be destroyed after Root.
std::unique_ptr<product_ui::ProductRuntime> s_runtime;
std::unique_ptr<product_ui::RootViewController> s_root;
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
    (void)product_buttons::init();
    auto &service = product_ui::loginService();
    const int64_t now = esp_timer_get_time() / 1000LL;
    service.boot(now, false);
    s_root.reset(new product_ui::RootViewController(win.contentRoot()));
    s_runtime.reset(new product_ui::ProductRuntime(*s_root, service));
    if (!s_root->replace(std::unique_ptr<product_ui::ViewController>(
            new product_ui::LoginViewController(service, s_runtime->navigation())), false)) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_login_load_failed");
        return false;
    }
    if (!s_runtime->start()) {
        ESP_LOGE(APP_LOG_TAG, "product_ui_runtime_start_failed");
        return false;
    }
    s_runtime->tick(now);
    lv_refr_now(lv_display_get_default());
    product_mem::probe("ui_ready", 0);
    return true;
}

} /* namespace */

} /* namespace product_app */
