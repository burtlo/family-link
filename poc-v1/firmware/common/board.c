#include "board.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "board";
static lv_display_t *s_disp;
static lv_obj_t *s_status;

esp_err_t board_display_start(void)
{
    s_disp = bsp_display_start();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start failed");
        return ESP_FAIL;
    }
    bsp_display_brightness_set(80);
    ESP_LOGI(TAG, "display up 320x240");
    return ESP_OK;
}

void board_backlight_set(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    bsp_display_brightness_set(percent);
}

bool board_lvgl_lock(uint32_t timeout_ms)
{
    return bsp_display_lock(timeout_ms);
}

void board_lvgl_unlock(void)
{
    bsp_display_unlock();
}

void board_status_set(const char *line)
{
    if (s_disp == NULL) {
        return;
    }
    if (!bsp_display_lock(200)) {
        return;
    }
    if (s_status == NULL) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
        s_status = lv_label_create(scr);
        lv_obj_set_style_text_color(s_status, lv_color_hex(0xE8F0E8), 0);
        lv_obj_set_width(s_status, 300);
        lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
        lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 0);
    }
    lv_label_set_text(s_status, line ? line : "");
    bsp_display_unlock();
}
