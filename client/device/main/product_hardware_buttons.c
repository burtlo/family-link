#include "product_hardware_buttons.h"

#include "product_inbox_fw.h"
#include "product_login_fw.h"
#include "product_stage_router.h"
#include "product_volume_fw.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "iot_button.h"

#include <stdatomic.h>
#include <stdbool.h>

static const char *kTag = "product_hw_btn";
static button_handle_t s_buttons[BSP_BUTTON_NUM];
static atomic_bool s_boot_pressed;
static atomic_bool s_mute_pressed;
static atomic_bool s_red_pressed;

static void on_boot_press(void *button, void *context)
{
    (void)button;
    (void)context;
    atomic_store_explicit(&s_boot_pressed, true, memory_order_release);
}

static void on_mute_press(void *button, void *context)
{
    (void)button;
    (void)context;
    atomic_store_explicit(&s_mute_pressed, true, memory_order_release);
}

static void on_red_press(void *button, void *context)
{
    (void)button;
    (void)context;
    atomic_store_explicit(&s_red_pressed, true, memory_order_release);
}

bool product_hardware_buttons_init(void)
{
    int button_count = 0;
    atomic_store_explicit(&s_boot_pressed, false, memory_order_relaxed);
    atomic_store_explicit(&s_mute_pressed, false, memory_order_relaxed);
    atomic_store_explicit(&s_red_pressed, false, memory_order_relaxed);

    if (bsp_iot_button_create(s_buttons, &button_count, BSP_BUTTON_NUM) != ESP_OK) {
        ESP_LOGW(kTag, "bsp_iot_button_create failed; physical keys inactive");
        return false;
    }
    if (s_buttons[BSP_BUTTON_CONFIG] != NULL) {
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL,
                                   on_boot_press, NULL) != ESP_OK) {
            ESP_LOGW(kTag, "BOOT callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "BOOT button handle missing");
    }
    if (s_buttons[BSP_BUTTON_MUTE] != NULL) {
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL,
                                   on_mute_press, NULL) != ESP_OK) {
            ESP_LOGW(kTag, "mute callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "mute button handle missing");
    }
    if (s_buttons[BSP_BUTTON_MAIN] != NULL) {
        if (iot_button_register_cb(s_buttons[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL,
                                   on_red_press, NULL) != ESP_OK) {
            ESP_LOGW(kTag, "red record callback registration failed");
        }
    } else {
        ESP_LOGW(kTag, "red record button handle missing");
    }
    ESP_LOGI(kTag, "physical buttons armed (count=%d)", button_count);
    return true;
}

void product_hardware_buttons_poll(void)
{
    if (atomic_exchange_explicit(&s_boot_pressed, false, memory_order_acq_rel)) {
        (void)product_inbox_on_boot_pressed();
    }
    if (atomic_exchange_explicit(&s_mute_pressed, false, memory_order_acq_rel)) {
        if (product_volume_on_mute() == 0) {
            product_login_on_mute_pressed();
        }
    }
    if (atomic_exchange_explicit(&s_red_pressed, false, memory_order_acq_rel)) {
        if (product_volume_on_red_record() != 0) {
            (void)product_stage_on_red_record();
        }
    }
}
