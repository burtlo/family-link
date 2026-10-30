#include "product_runtime.hpp"

#include <cstring>
#ifdef ESP_PLATFORM
#include "product_buttons.hpp"
#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif

namespace product_ui
{

namespace
{
#ifdef ESP_PLATFORM
void blank_login_display()
{
    (void)bsp_display_backlight_off();
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY blank dark=1");
}

void wake_login_display()
{
    (void)bsp_display_brightness_set(80);
    lv_indev_t *input = bsp_display_get_input_dev();
    if (input) lv_indev_reset(input, nullptr);
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY wake dark=0");
}
#endif
}

ProductRuntime::ProductRuntime(RootViewController &root, LoginService &service, RuntimePlatform platform)
    : root_(root), service_(service), platform_(platform)
{
#ifdef ESP_PLATFORM
    if (!platform_.muteLatched) platform_.muteLatched = product_buttons::mute_latched;
    if (!platform_.blankDisplay) platform_.blankDisplay = blank_login_display;
    if (!platform_.wakeDisplay) platform_.wakeDisplay = wake_login_display;
    if (!platform_.takeBoot) platform_.takeBoot = product_buttons::take_boot_press;
    if (!platform_.takeRed) platform_.takeRed = product_buttons::take_red_press;
#endif
}

ProductRuntime::~ProductRuntime()
{
    if (timer_) lv_timer_delete(timer_);
}

bool ProductRuntime::start()
{
    if (timer_) return true;
    timer_ = lv_timer_create(on_product_timer, 50, this);
    return timer_ != nullptr;
}

void ProductRuntime::on_product_timer(lv_timer_t *timer)
{
    auto *runtime = static_cast<ProductRuntime *>(lv_timer_get_user_data(timer));
#ifdef ESP_PLATFORM
    runtime->tick(esp_timer_get_time() / 1000LL);
#else
    runtime->tick(lv_tick_get());
#endif
}

LoginNavigation ProductRuntime::navigation()
{
    return {this, requestNavigation};
}

void ProductRuntime::requestNavigation(void *context, LoginDestination destination, const char *user)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (!user || !*user || destination != LoginDestination::PinEntry || runtime->root_.busy() || runtime->muted_) return;
    std::strncpy(runtime->selected_user_, user, sizeof(runtime->selected_user_) - 1);
    runtime->navigation_pending_ = true;
}

void ProductRuntime::pollInput(int64_t now)
{
    const bool latched = platform_.muteLatched && platform_.muteLatched() != 0;
    if (!input_known_ || latched != muted_) {
        input_known_ = true;
#ifdef ESP_PLATFORM
        ESP_LOGI("product_mute", "V2_MUTE_EDGE latch=%d", latched ? 1 : 0);
#endif
        if (latched) {
            root_.finishTransition();
            if (platform_.blankDisplay) platform_.blankDisplay();
            service_.signOut(now);
            navigation_pending_ = false;
            muted_ = true;
        } else if (muted_) {
            service_.refreshHangout(now);
            if (!root_.current() || std::strcmp(root_.current()->name(), "Login") != 0) {
                root_.replace(std::unique_ptr<ViewController>(new LoginViewController(service_, navigation())), false);
            }
            root_.render();
            muted_ = false;
            if (platform_.wakeDisplay) platform_.wakeDisplay();
        }
    }
    // Presses are always consumed so one made where no handler exists cannot
    // fire later on a controller that does handle it.
    const bool boot = platform_.takeBoot && platform_.takeBoot();
    const bool red = platform_.takeRed && platform_.takeRed();
    auto *controller = root_.current();
    if (!muted_ && !root_.busy() && controller) {
        if (boot && controller->handlesBoot()) root_.dispatchBoot();
        if (red && controller->handlesRed()) root_.dispatchRed();
    }
}

void ProductRuntime::serviceFeatures()
{
    // No signed-in feature service participates in this first controller slice.
}

void ProductRuntime::present()
{
    if (!muted_) {
        if (navigation_pending_ && !root_.busy()) {
            navigation_pending_ = false;
            root_.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), true);
        }
        root_.render();
    }
}

void ProductRuntime::tick(int64_t now)
{
    pollInput(now);
    service_.flushConnectivity(now);
    service_.advance(now);
    serviceFeatures();
    present();
}

} // namespace product_ui
