/*
 * h04 — Mute button is hold-to-talk (GPIO edges), not a software toggle.
 *
 * Top mute key is BSP_BUTTON_MUTE on GPIO 1. Stock firmware used it to
 * arm a wake word; this demo only logs PRESS_DOWN / PRESS_UP. Debounce
 * is the iot_button component default. No mic, no wake word.
 */

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "lvgl.h"
#include "pass.h"

static const char *TAG = "h04";

static lv_obj_t *s_state_label;
static volatile bool s_held;
static volatile bool s_passed;
static uint32_t s_down_ms;

static void ui_set(const char *text)
{
    if (s_state_label == NULL) {
        return;
    }
    if (!board_lvgl_lock(50)) {
        return;
    }
    lv_label_set_text(s_state_label, text);
    board_lvgl_unlock();
}

static void mute_down_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;

    /* Edge, not toggle: DOWN means held. A click cannot turn "talk" on. */
    s_held = true;
    s_down_ms = esp_log_timestamp();
    ESP_LOGI(TAG, "DOWN t=%u ms GPIO%d mute (PTT press, not toggle)",
             (unsigned)s_down_ms, (int)BSP_BUTTON_MUTE_IO);
    ui_set("held");
}

static void mute_up_cb(void *button_handle, void *usr_data)
{
    (void)button_handle;
    (void)usr_data;

    uint32_t up_ms = esp_log_timestamp();
    uint32_t held_ms = 0;
    bool clean_pair = s_held;

    if (clean_pair && up_ms >= s_down_ms) {
        held_ms = up_ms - s_down_ms;
    }

    /* Edge, not toggle: UP means idle. A second click does not turn it off. */
    s_held = false;
    ESP_LOGI(TAG, "UP t=%u ms held=%u ms GPIO%d mute (PTT release, not toggle)",
             (unsigned)up_ms, (unsigned)held_ms, (int)BSP_BUTTON_MUTE_IO);
    ui_set("idle");

    if (clean_pair && !s_passed) {
        s_passed = true;
        demo_pass("h04");
    }
}

static void ui_start(void)
{
    if (board_display_start() != ESP_OK) {
        ESP_LOGW(TAG, "display optional; continuing without LCD");
        return;
    }
    if (!board_lvgl_lock(1000)) {
        return;
    }
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101810), 0);
    s_state_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xE8F0E8), 0);
    lv_label_set_text(s_state_label, "idle");
    lv_obj_center(s_state_label);
    board_lvgl_unlock();
}

void app_main(void)
{
    /* Display first so BSP_BUTTON_MAIN (touch) can attach if the BSP wants it. */
    ui_start();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (btns[BSP_BUTTON_MUTE] == NULL) {
        ESP_LOGE(TAG, "mute button missing (GPIO %d)", (int)BSP_BUTTON_MUTE_IO);
        demo_fail("h04", "mute button init failed");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s (mute handle ok)", esp_err_to_name(err));
    }

    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, mute_down_cb, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, mute_up_cb, NULL);

    ESP_LOGI(TAG, "hold mute (GPIO %d) to talk; release to idle. No wake word.",
             (int)BSP_BUTTON_MUTE_IO);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
