#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "esp_log.h"

static const char *TAG = "p01";

/* Geometric stand-in only: circles + rounded rects. Not a licensed character. */

static lv_obj_t *shape(lv_obj_t *parent, int w, int h, int radius, uint32_t hex)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void paint_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Body block so the head is a character, not a floating icon. */
    lv_obj_t *body = shape(scr, 220, 96, 36, 0x2A6A6E);
    lv_obj_set_pos(body, 50, 148);

    /* ~200 px head — readable from a chair on 320x240. */
    lv_obj_t *head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(head, 60, 6);

    lv_obj_t *eye_l = shape(head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_l, 32, 52);
    lv_obj_t *pupil_l = shape(eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_l, 14, 14);
    lv_obj_t *glint_l = shape(eye_l, 10, 10, LV_RADIUS_CIRCLE, 0xFFFFFF);
    lv_obj_set_pos(glint_l, 32, 8);

    lv_obj_t *eye_r = shape(head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_r, 112, 52);
    lv_obj_t *pupil_r = shape(eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_r, 14, 14);
    lv_obj_t *glint_r = shape(eye_r, 10, 10, LV_RADIUS_CIRCLE, 0xFFFFFF);
    lv_obj_set_pos(glint_r, 32, 8);

    lv_obj_t *mouth = shape(head, 92, 22, 11, 0xC0453C);
    lv_obj_set_pos(mouth, 54, 140);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p01", "display");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("p01", "lvgl lock");
        return;
    }
    paint_face();
    board_lvgl_unlock();

    ESP_LOGI(TAG, "static geometric face on 320x240");
    demo_pass("p01");
}
