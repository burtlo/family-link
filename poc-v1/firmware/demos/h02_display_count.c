/* Proves the BOX-3 320x240 LCD can show a locked-idle "N new" screen and PWM-dim. */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void paint_idle(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    lv_obj_t *name = lv_label_create(scr);
    lv_label_set_text(name, "desk");
    lv_obj_set_style_text_color(name, lv_color_hex(0xaaaaaa), 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *count = lv_label_create(scr);
    lv_label_set_text(count, "2 new");
    lv_obj_set_style_text_color(count, lv_color_white(), 0);
#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
    lv_obj_set_style_text_font(count, &lv_font_montserrat_48, 0);
#elif defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(count, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(count, LV_ALIGN_CENTER, 0, 8);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h02", "display");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("h02", "lvgl lock");
        return;
    }
    paint_idle();
    board_lvgl_unlock();

    demo_pass("h02");

    vTaskDelay(pdMS_TO_TICKS(4000));
    board_backlight_set(20);
}
