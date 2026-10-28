#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lab02_board_display_start(void);
void lab02_board_backlight_set(int percent);
bool lab02_board_lvgl_lock(uint32_t timeout_ms);
void lab02_board_lvgl_unlock(void);
void lab02_board_status_set(const char *line);

#ifdef __cplusplus
}
#endif
