/*
 * p10 — Packed parent portrait on the LCD (RGB565 from the persona pipeline).
 * Uses firmware/assets/persona/ if present, else persona.example stand-in.
 */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "esp_log.h"

#if __has_include("assets/persona/persona_idle.h")
#include "assets/persona/persona_idle.h"
#define PERSONA_SRC "overlay"
#else
#include "assets/persona.example/persona_idle.h"
#define PERSONA_SRC "example"
#endif

static const char *TAG = "p10";

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p10", "display");
        return;
    }
    if (!board_lvgl_lock(0)) {
        demo_fail("p10", "lvgl lock");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &persona_idle);
    lv_obj_center(img);

    lv_obj_t *cap = lv_label_create(scr);
    lv_label_set_text(cap, PERSONA_SRC);
    lv_obj_set_style_text_color(cap, lv_color_hex(0xD0E0E8), 0);
    lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -6);

    board_lvgl_unlock();
    ESP_LOGI(TAG, "portrait from %s  %ux%u", PERSONA_SRC,
             (unsigned)persona_idle.header.w, (unsigned)persona_idle.header.h);
    demo_pass("p10");
}
