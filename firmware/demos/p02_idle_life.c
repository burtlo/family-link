/*
 * p02 — Same geometric face as p01, with a blink and a slow breathe.
 * Prints average FPS and max ms/frame over 10 s, then -- PASS p02.
 */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "p02";

typedef struct {
    lv_obj_t *body;
    lv_obj_t *head;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *lid_l;
    lv_obj_t *lid_r;
} face_t;

static face_t s_face;
static int s_frames;
static int64_t s_max_frame_us;
static int s_blink_phase;
static int s_breathe;
static int s_ticks;

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

static void paint_tick(void)
{
    int64_t t0 = esp_timer_get_time();

    /* Slow 2 px breathe on the body. Blink lids every ~3 s (30 ticks of 100 ms). */
    s_breathe = (s_breathe + 1) % 40;
    int body_y = 148 + ((s_breathe < 20) ? (s_breathe / 10) : ((40 - s_breathe) / 10));
    lv_obj_set_y(s_face.body, body_y);

    s_blink_phase++;
    int lid = 0;
    if (s_blink_phase >= 30 && s_blink_phase <= 32) {
        lid = 56;
    }
    if (s_blink_phase > 32) {
        s_blink_phase = 0;
    }
    lv_obj_set_height(s_face.lid_l, lid);
    lv_obj_set_height(s_face.lid_r, lid);

    lv_refr_now(NULL);
    int64_t dt = esp_timer_get_time() - t0;
    if (dt > s_max_frame_us) {
        s_max_frame_us = dt;
    }
    s_frames++;
}

static void build_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    s_face.body = shape(scr, 220, 96, 36, 0x2A6A6E);
    lv_obj_set_pos(s_face.body, 50, 148);

    s_face.head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(s_face.head, 60, 6);

    s_face.eye_l = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(s_face.eye_l, 32, 52);
    lv_obj_t *pupil_l = shape(s_face.eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_l, 14, 14);

    s_face.eye_r = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(s_face.eye_r, 112, 52);
    lv_obj_t *pupil_r = shape(s_face.eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_r, 14, 14);

    s_face.lid_l = shape(s_face.eye_l, 56, 0, 0, 0xF2C07A);
    lv_obj_set_pos(s_face.lid_l, 0, 0);
    s_face.lid_r = shape(s_face.eye_r, 56, 0, 0, 0xF2C07A);
    lv_obj_set_pos(s_face.lid_r, 0, 0);

    lv_obj_t *mouth = shape(s_face.head, 92, 22, 11, 0xC0453C);
    lv_obj_set_pos(mouth, 54, 140);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p02", "display");
        return;
    }
    if (!board_lvgl_lock(0)) {
        demo_fail("p02", "lvgl lock");
        return;
    }
    build_face();
    board_lvgl_unlock();

    /* 10 s window at 10 Hz idle — the number we want to record, not 30 fps. */
    while (s_ticks < 100) {
        if (board_lvgl_lock(50)) {
            paint_tick();
            board_lvgl_unlock();
        }
        s_ticks++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    float fps = s_frames / 10.0f;
    int max_ms = (int)(s_max_frame_us / 1000);
    ESP_LOGI(TAG, "idle fps=%.1f frames=%d max_ms/frame=%d (10 s)", fps, s_frames, max_ms);
    demo_pass("p02");
}
