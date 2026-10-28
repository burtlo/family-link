#include "product_display.h"

#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#define PRODUCT_LCD_DRAW_LINES 16

lv_display_t *product_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = (uint32_t)BSP_LCD_H_RES * PRODUCT_LCD_DRAW_LINES,
        .double_buffer = false,
        .flags = {
            /* Keep the draw buffer in internal DMA memory. A PSRAM buffer can
             * leave the flush-done interrupt unfinished, and the UI task then
             * spins until touch stops working. */
            .buff_dma = true,
            .buff_spiram = false,
        },
    };
    /* Port default is 7168. Login applies a hangout roster on this task. */
    cfg.lvgl_port_cfg.task_stack = 12288;
    lv_display_t *display = bsp_display_start_with_config(&cfg);
    if (display != NULL) {
        ESP_LOGI("product_display", "display_start ok draw_lines=%d lvgl_stack=%u",
                 PRODUCT_LCD_DRAW_LINES, (unsigned)cfg.lvgl_port_cfg.task_stack);
    }
    return display;
}
