/*
 * p08 — Fake inbox notice: perk, chirp, badge 1 then 2. No message body.
 * -- PASS p08 after badge 2. Optional PIN (DEMO_PIN) clears the badge.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEMO_PIN
#define DEMO_PIN "1234"
#endif

static const char *TAG = "p08";
#define SAMPLE_RATE 16000

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static lv_obj_t *s_head;
static lv_obj_t *s_badge;
static lv_obj_t *s_caption;
static lv_obj_t *s_dots;
static esp_codec_dev_handle_t s_spk;
static int s_unread;
static bool s_passed;
static char s_entry[8];
static size_t s_len;

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

static void chirp_notice(void)
{
    int hop = SAMPLE_RATE * 80 / 1000;
    int16_t pcm[3200];
    int half = SAMPLE_RATE / (988 * 2);
    int sign = 1, left = half;
    for (int i = 0; i < hop * 2; i++) {
        int hz_half = (i < hop) ? half : (SAMPLE_RATE / (1318 * 2));
        if (i == hop) {
            sign = 1;
            left = hz_half;
            half = hz_half;
        }
        pcm[i] = (int16_t)(sign * 4200);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 52);
    if (esp_codec_dev_open(s_spk, &s_fs) == ESP_OK) {
        (void)esp_codec_dev_write(s_spk, pcm, hop * 4);
        (void)esp_codec_dev_close(s_spk);
    }
}

static void show_mail(int n)
{
    s_unread = n;
    char badge[8];
    snprintf(badge, sizeof(badge), "%d", n);
    lv_obj_set_y(s_head, 0);
    lv_label_set_text(s_badge, badge);
    lv_label_set_text(s_caption, "mail");
    ESP_LOGI(TAG, "notice badge=%d (no body)", n);
    if (!s_passed && n >= 2) {
        s_passed = true;
        demo_pass("p08");
    }
}

static void on_key(lv_event_t *e)
{
    const char *key = lv_event_get_user_data(e);
    if (key == NULL || s_unread == 0) {
        return;
    }
    if (strcmp(key, "C") == 0) {
        s_len = 0;
        s_entry[0] = 0;
        lv_label_set_text(s_dots, " ");
        return;
    }
    if (s_len >= 7) {
        return;
    }
    s_entry[s_len++] = key[0];
    s_entry[s_len] = 0;
    char dots[8];
    memset(dots, '*', s_len);
    dots[s_len] = 0;
    lv_label_set_text(s_dots, dots);
    if (strcmp(s_entry, DEMO_PIN) == 0) {
        s_unread = 0;
        lv_obj_set_y(s_head, 6);
        lv_label_set_text(s_badge, "");
        lv_label_set_text(s_caption, "unlocked");
        lv_label_set_text(s_dots, " ");
        ESP_LOGI(TAG, "PIN ok; badge cleared; still no message body");
    }
}

static void add_key(lv_obj_t *parent, const char *label, int x, int y)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 56, 32);
    lv_obj_set_pos(btn, x, y);
    lv_obj_t *t = lv_label_create(btn);
    lv_label_set_text(t, label);
    lv_obj_center(t);
    lv_obj_add_event_cb(btn, on_key, LV_EVENT_CLICKED, (void *)label);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p08", "display");
        return;
    }
    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("p08", "ES8311");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("p08", "lvgl lock");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);

    lv_obj_t *body = shape(scr, 220, 80, 32, 0x2A6A6E);
    lv_obj_set_pos(body, 50, 100);
    s_head = shape(scr, 140, 140, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(s_head, 90, 6);
    lv_obj_t *eye_l = shape(s_head, 36, 36, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_l, 24, 40);
    lv_obj_t *eye_r = shape(s_head, 36, 36, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_r, 80, 40);
    lv_obj_t *mouth = shape(s_head, 56, 16, 8, 0xC0453C);
    lv_obj_set_pos(mouth, 42, 96);

    s_badge = lv_label_create(scr);
    lv_obj_set_style_text_color(s_badge, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_badge, LV_ALIGN_TOP_RIGHT, -16, 8);
    lv_label_set_text(s_badge, "");

    s_caption = lv_label_create(scr);
    lv_obj_set_style_text_color(s_caption, lv_color_hex(0xD0E0E8), 0);
    lv_obj_align(s_caption, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_label_set_text(s_caption, "idle");

    s_dots = lv_label_create(scr);
    lv_obj_set_style_text_color(s_dots, lv_color_hex(0x8899AA), 0);
    lv_obj_align(s_dots, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_label_set_text(s_dots, " ");

    add_key(scr, "1", 20, 188);
    add_key(scr, "2", 88, 188);
    add_key(scr, "3", 156, 188);
    add_key(scr, "C", 224, 188);
    board_lvgl_unlock();

    ESP_LOGI(TAG, "fake mail in a few seconds; no voicemail, no text");
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (board_lvgl_lock(80)) {
        show_mail(1);
        board_lvgl_unlock();
    }
    chirp_notice();
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (board_lvgl_lock(80)) {
        show_mail(2);
        board_lvgl_unlock();
    }
    chirp_notice();
}
