/*
 * p09 — Mouth flap while a fixture tone plays. Pass if the clip stays clean.
 * If writes fail while the mouth moves, that is a freeze-the-face product decision.
 */

#include <stdint.h>
#include <string.h>

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "p09";

#define SAMPLE_RATE 16000
#define PLAY_MS     2500
#define CHUNK       640

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static lv_obj_t *s_mouth;
static int s_underruns;
static int s_flaps;

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

static void flap_mouth(void)
{
    s_flaps++;
    int h = (s_flaps % 2) ? 40 : 18;
    if (!board_lvgl_lock(20)) {
        return;
    }
    lv_obj_set_height(s_mouth, h);
    lv_obj_set_style_radius(s_mouth, h / 2, 0);
    lv_refr_now(NULL);
    board_lvgl_unlock();
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p09", "display");
        return;
    }
    esp_codec_dev_handle_t spk = bsp_audio_codec_speaker_init();
    if (spk == NULL) {
        demo_fail("p09", "ES8311");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("p09", "lvgl lock");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_t *head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(head, 60, 6);
    lv_obj_t *eye_l = shape(head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_l, 32, 52);
    lv_obj_t *eye_r = shape(head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_r, 112, 52);
    s_mouth = shape(head, 92, 22, 11, 0xC0453C);
    lv_obj_set_pos(s_mouth, 54, 140);
    board_lvgl_unlock();

    int samples = SAMPLE_RATE * PLAY_MS / 1000;
    int16_t *pcm = heap_caps_malloc((size_t)samples * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc((size_t)samples * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        demo_fail("p09", "pcm alloc");
        return;
    }
    int half = SAMPLE_RATE / (440 * 2);
    int sign = 1, left = half;
    for (int i = 0; i < samples; i++) {
        pcm[i] = (int16_t)(sign * 3800);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }

    (void)esp_codec_dev_set_out_vol(spk, 60);
    if (esp_codec_dev_open(spk, &s_fs) != ESP_OK) {
        demo_fail("p09", "speaker open");
        return;
    }

    int64_t next_flap = esp_timer_get_time();
    uint8_t *p = (uint8_t *)pcm;
    int left_bytes = samples * 2;
    while (left_bytes > 0) {
        if (esp_timer_get_time() >= next_flap) {
            flap_mouth();
            next_flap += 150 * 1000;
        }
        int n = left_bytes > CHUNK ? CHUNK : left_bytes;
        if (esp_codec_dev_write(spk, p, n) != ESP_CODEC_DEV_OK) {
            s_underruns++;
            ESP_LOGW(TAG, "write fail / underrun count=%d", s_underruns);
        }
        p += n;
        left_bytes -= n;
    }
    (void)esp_codec_dev_close(spk);
    heap_caps_free(pcm);

    ESP_LOGI(TAG, "flaps=%d underruns=%d", s_flaps, s_underruns);
    if (s_underruns == 0) {
        demo_pass("p09");
    } else {
        demo_fail("p09", "audio glitched while mouth moved — freeze the face");
    }
}
