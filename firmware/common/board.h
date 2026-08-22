#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * I2C + LVGL display + backlight on. Call from demos that need the LCD.
 * Uses espressif/esp-box-3 BSP (do not hand-wire ILI9341).
 */
esp_err_t board_display_start(void);

/** 0–100. BOX-3 backlight is PWM via BSP. */
void board_backlight_set(int percent);

/** Take LVGL lock, or return false. */
bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

/** Large status line for demos that are not a full UI (avoids a black LCD). */
void board_status_set(const char *line);

#ifdef __cplusplus
}
#endif
