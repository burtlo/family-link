#include "product_buttons.hpp"

#include "product_inbox.hpp"
#include "product_login.hpp"
#include "product_stage_router.hpp"
#include "product_volume.hpp"

#include "bsp/esp-box-3.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "iot_button.h"

#include <atomic>

namespace product_buttons
{

namespace
{

const char *kTag = "product_hw_btn";
button_handle_t s_buttons[BSP_BUTTON_NUM];
std::atomic<bool> s_boot_pressed;
std::atomic<bool> s_mute_pressed;
std::atomic<bool> s_red_pressed;
bool s_mute_gpio_ready;

} /* namespace */

extern "C" {

static void on_boot_press(void *button, void *context)
{
    (void)button;
    (void)context;
    s_boot_pressed.store(true, std::memory_order_release);
}

static void on_mute_press(void *button, void *context)
{
    (void)button;
    (void)context;
    s_mute_pressed.store(true, std::memory_order_release);
}

static void on_red_press(void *button, void *context)
{
    (void)button;
    (void)context;
    s_red_pressed.store(true, std::memory_order_release);
}

} /* extern "C" */

bool init()
{
    int button_count = 0;
    s_boot_pressed.store(false, std::memory_order_relaxed);
    s_mute_pressed.store(false, std::memory_order_relaxed);
    s_red_pressed.store(false, std::memory_order_relaxed);

    if (bsp_iot_button_create(s_buttons, &button_count, BSP_BUTTON_NUM) != ESP_OK) {
        ESP_LOGW(kTag, "bsp_iot_button_create failed; physical keys inactive");
        return false;
    }
    if (s_buttons[BSP_BUTTON_CONFIG] != NULL) {
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot_press, NULL) !=
            ESP_OK) {
            ESP_LOGW(kTag, "BOOT callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "BOOT button handle missing");
    }
    if (s_buttons[BSP_BUTTON_MUTE] != NULL) {
        gpio_config_t mute_cfg = {
            .pin_bit_mask = 1ULL << BSP_BUTTON_MUTE_IO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&mute_cfg) == ESP_OK) {
            s_mute_gpio_ready = true;
        }
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_press, NULL) !=
            ESP_OK) {
            ESP_LOGW(kTag, "mute callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "mute button handle missing");
    }
    if (s_buttons[BSP_BUTTON_MAIN] != NULL) {
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, on_red_press, NULL) !=
            ESP_OK) {
            ESP_LOGW(kTag, "red record callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "red record button handle missing");
    }
    ESP_LOGI(kTag, "physical buttons armed (count=%d)", button_count);
    return true;
}

int mute_gpio_ready()
{
    return s_mute_gpio_ready ? 1 : 0;
}

int mute_gpio_level()
{
    if (!s_mute_gpio_ready) {
        return -1;
    }
    return gpio_get_level(BSP_BUTTON_MUTE_IO);
}

int mute_button_level()
{
    if (s_buttons[BSP_BUTTON_MUTE] == NULL) {
        return -1;
    }
    return iot_button_get_key_level(s_buttons[BSP_BUTTON_MUTE]);
}

int mute_latched()
{
    /* GPIO tracks the physical latch on BOX-3; iot_button is momentary-press oriented. */
    if (s_mute_gpio_ready) {
        return gpio_get_level(BSP_BUTTON_MUTE_IO) == 0 ? 1 : 0;
    }
    if (s_buttons[BSP_BUTTON_MUTE] != NULL) {
        return iot_button_get_key_level(s_buttons[BSP_BUTTON_MUTE]) != 0 ? 1 : 0;
    }
    return 0;
}

void poll()
{
    if (s_boot_pressed.exchange(false, std::memory_order_acq_rel)) {
        if (!product_login::is_signed_in()) {
            product_login::on_boot_pressed();
        } else if (product_volume::active()) {
            (void)product_inbox::on_boot_pressed();
        } else {
            (void)product_stage::on_boot();
        }
    }
    /* Mute is a latch. The press edge is not the level. */
    (void)s_mute_pressed.exchange(false, std::memory_order_acq_rel);
    product_login::on_mute_level(mute_latched());
    if (s_red_pressed.exchange(false, std::memory_order_acq_rel)) {
        /* Volume owns red while its screen is up. Signed-out red stays on login. */
        if (product_login::is_signed_in() && product_volume::on_red_record() != 0) {
            (void)product_stage::on_red_record();
        }
    }
}

} /* namespace product_buttons */
