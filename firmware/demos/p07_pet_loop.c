/*
 * p07 — Pet loop: idle blink + mute listen face + chirps. No network.
 * -- PASS p07 after a mute down/up and 10 s of idle FPS printed.
 */

#include <stdint.h>
#include <string.h>

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

static const char *TAG = "p07";

#define SAMPLE_RATE 16000

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    lv_obj_t *body;
    lv_obj_t *head;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *lid_l;
    lv_obj_t *lid_r;
    lv_obj_t *mouth;
} face_t;

static face_t s_face;
static esp_codec_dev_handle_t s_spk;
static bool s_held;
static bool s_saw_listen;
static bool s_saw_idle;
static bool s_passed;
static int s_frames;
static int64_t s_max_us;
static int s_ticks;
static int s_blink;

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

static void chirp(int hz, int ms)
{
    int n = SAMPLE_RATE * ms / 1000;
    int16_t pcm[3200];
    if (n > 3200) {
        n = 3200;
    }
    int half = SAMPLE_RATE / (hz * 2);
    if (half < 1) {
        half = 1;
    }
    int sign = 1, left = half;
    for (int i = 0; i < n; i++) {
        pcm[i] = (int16_t)(sign * 4000);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 50);
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        return;
    }
    (void)esp_codec_dev_write(s_spk, pcm, n * 2);
    (void)esp_codec_dev_close(s_spk);
}

static void apply_listen(bool listen)
{
    int head_x = listen ? 42 : 60;
    int mouth_h = listen ? 52 : 22;
    lv_obj_set_x(s_face.head, head_x);
    lv_obj_set_height(s_face.mouth, mouth_h);
    lv_obj_set_style_radius(s_face.mouth, mouth_h / 2, 0);
}

static void maybe_pass(void)
{
    if (s_passed || !s_saw_listen || !s_saw_idle || s_ticks < 100) {
        return;
    }
    s_passed = true;
    float fps = s_frames / 10.0f;
    ESP_LOGI(TAG, "pet fps=%.1f frames=%d max_ms=%d", fps, s_frames, (int)(s_max_us / 1000));
    demo_pass("p07");
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_held = true;
    if (board_lvgl_lock(80)) {
        apply_listen(true);
        lv_refr_now(NULL);
        board_lvgl_unlock();
    }
    chirp(880, 70);
    s_saw_listen = true;
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_held = false;
    if (board_lvgl_lock(80)) {
        apply_listen(false);
        lv_refr_now(NULL);
        board_lvgl_unlock();
    }
    chirp(660, 70);
    s_saw_idle = true;
    maybe_pass();
}

static void on_poke(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    chirp(1320, 40);
}

static void idle_tick(void)
{
    int64_t t0 = esp_timer_get_time();
    s_blink++;
    int lid = (s_blink >= 28 && s_blink <= 30) ? 56 : 0;
    if (s_blink > 30) {
        s_blink = 0;
    }
    if (!s_held) {
        lv_obj_set_height(s_face.lid_l, lid);
        lv_obj_set_height(s_face.lid_r, lid);
        int y = 148 + ((s_ticks % 40) < 20 ? 1 : 0);
        lv_obj_set_y(s_face.body, y);
    }
    lv_refr_now(NULL);
    int64_t dt = esp_timer_get_time() - t0;
    if (dt > s_max_us) {
        s_max_us = dt;
    }
    s_frames++;
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p07", "display");
        return;
    }
    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("p07", "ES8311");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("p07", "lvgl lock");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    s_face.body = shape(scr, 220, 96, 36, 0x2A6A6E);
    lv_obj_set_pos(s_face.body, 50, 148);
    s_face.head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(s_face.head, 60, 6);
    s_face.eye_l = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(s_face.eye_l, 32, 52);
    lv_obj_t *pl = shape(s_face.eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pl, 14, 14);
    s_face.eye_r = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(s_face.eye_r, 112, 52);
    lv_obj_t *pr = shape(s_face.eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pr, 14, 14);
    s_face.lid_l = shape(s_face.eye_l, 56, 0, 0, 0xF2C07A);
    s_face.lid_r = shape(s_face.eye_r, 56, 0, 0, 0xF2C07A);
    s_face.mouth = shape(s_face.head, 92, 22, 11, 0xC0453C);
    lv_obj_set_pos(s_face.mouth, 54, 140);
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    if (btns[BSP_BUTTON_MAIN]) {
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, on_poke, NULL);
    }

    ESP_LOGI(TAG, "sit with it: blink, hold mute, poke. mic closed.");
    while (1) {
        if (board_lvgl_lock(50)) {
            idle_tick();
            board_lvgl_unlock();
        }
        s_ticks++;
        if (s_ticks == 100) {
            float fps = s_frames / 10.0f;
            ESP_LOGI(TAG, "idle fps=%.1f frames=%d max_ms=%d", fps, s_frames,
                     (int)(s_max_us / 1000));
            maybe_pass();
            if (!s_passed) {
                ESP_LOGI(TAG, "hold mute then release to finish -- PASS p07");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
