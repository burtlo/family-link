/*
 * p12 — Tamagotchi creature: bounces when quiet, listens when it hears you.
 *
 * Drawn into a 320×240 RGB565 buffer (same path as h13 photos) and shown as
 * one image. Moving LVGL widgets on SPI tears: corner flashes, half-sprites.
 *
 * Mute latch up (red LED off) opens the analog mics. Speech above the noise
 * floor turns the creature gold with a small round mouth. Mute latch down
 * is hardware-dead audio — the face will not react.
 *
 * -- PASS p12 after bounce + listen while unmuted (talk near the box, then wait).
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "pass.h"

static const char *TAG = "p12";

#define W              320
#define H              240
#define FB_BYTES       (W * H * 2)
#define SAMPLE_RATE    16000
#define CHUNK          640
#define HEAR_HANG_MS   700
#define MIC_GAIN_DB    42.0f

#define COL_BG       0x1A3044
#define COL_SHADOW   0x0E2030
#define COL_BODY     0x5AD4A8
#define COL_BELLY    0x8EEFCC
#define COL_FOOT     0x3AAE82
#define COL_BODY_L   0xF4C15D
#define COL_BELLY_L  0xFFE8B0
#define COL_FOOT_L   0xE09A3A
#define COL_EYE      0xFFF8EE
#define COL_PUPIL    0x1A2830
#define COL_MOUTH    0x2A6A52
#define COL_MOUTH_L  0x6A2030
#define COL_BROW     0x5A3A20
#define COL_MUTE     0xC0453C
#define COL_LIVE     0x3AAE82
#define COL_HEAR     0xF4C15D

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static uint16_t *s_fb;
static lv_image_dsc_t s_dsc;
static lv_obj_t *s_img;
static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static volatile bool s_mic_muted;
static volatile bool s_hearing;
static volatile bool s_mic_open;
static volatile int s_peak;
static volatile int s_noise;
static volatile int s_thresh;

static float s_bounce_t;
static int s_blink;
static int s_ticks;
static int s_frames;
static int64_t s_max_us;
static bool s_listening;
static bool s_saw_bounce;
static bool s_saw_listen;
static bool s_saw_unmuted;
static bool s_passed;

static uint16_t rgb565(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static void fill_bg(uint16_t c)
{
    uint32_t pair = ((uint32_t)c << 16) | c;
    uint32_t *p = (uint32_t *)s_fb;
    for (int i = 0; i < (W * H) / 2; i++) {
        p[i] = pair;
    }
}

static void fill_rect(int x0, int y0, int x1, int y1, uint16_t c)
{
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > W - 1) {
        x1 = W - 1;
    }
    if (y1 > H - 1) {
        y1 = H - 1;
    }
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = s_fb + y * W;
        for (int x = x0; x <= x1; x++) {
            row[x] = c;
        }
    }
}

static void fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 1 || ry < 1) {
        return;
    }
    int y0 = cy - ry;
    int y1 = cy + ry;
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > H - 1) {
        y1 = H - 1;
    }
    float rx2 = (float)(rx * rx);
    float ry2 = (float)(ry * ry);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        float inner = 1.0f - (dy * dy) / ry2;
        if (inner < 0.0f) {
            continue;
        }
        int span = (int)(sqrtf(inner * rx2) + 0.5f);
        int x0 = cx - span;
        int x1 = cx + span;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 > W - 1) {
            x1 = W - 1;
        }
        uint16_t *row = s_fb + y * W;
        for (int x = x0; x <= x1; x++) {
            row[x] = c;
        }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t c)
{
    fill_ellipse(cx, cy, r, r, c);
}

static bool mic_hardware_muted(void)
{
    /* Active-low. 0 = mute latch down = analog mics dead. */
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static int16_t pcm_peak(const uint8_t *p, size_t nbytes)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)p;
    size_t n = nbytes / 2;
    for (size_t i = 0; i < n; i++) {
        int16_t a = s[i];
        if (a < 0) {
            a = (int16_t)(-a);
        }
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static void paint_creature(void)
{
    bool listen = !s_mic_muted && s_hearing;
    if (listen && !s_listening) {
        ESP_LOGI(TAG, "listening peak=%d noise=%d thresh=%d", s_peak, s_noise, s_thresh);
        s_saw_listen = true;
    }
    s_listening = listen;

    s_bounce_t += listen ? 0.04f : 0.14f;
    float hop = fabsf(sinf(s_bounce_t));
    int squash = listen ? 2 : (int)(8.0f * (1.0f - hop));
    int cx = 160 + (int)((listen ? 12.0f : 70.0f) * sinf(s_bounce_t * 0.55f));
    int cy = listen ? 110 : (88 + (int)(46.0f * hop));

    s_blink++;
    bool lids = (s_blink >= 32 && s_blink <= 34);
    if (s_blink > 34) {
        s_blink = 0;
    }

    fill_bg(rgb565(COL_BG));

    uint32_t body = listen ? COL_BODY_L : COL_BODY;
    uint32_t belly = listen ? COL_BELLY_L : COL_BELLY;
    uint32_t foot = listen ? COL_FOOT_L : COL_FOOT;

    fill_ellipse(cx, cy + 62, 42 + squash / 2, 8, rgb565(COL_SHADOW));
    fill_ellipse(cx, cy, 52 + squash / 2, 52 - squash, rgb565(body));
    fill_ellipse(cx, cy + 14, 28, 22, rgb565(belly));
    fill_ellipse(cx - 26, cy + 48, 14, 9, rgb565(foot));
    fill_ellipse(cx + 26, cy + 48, 14, 9, rgb565(foot));

    if (listen) {
        fill_ellipse(cx - 22, cy - 24, 14, 5, rgb565(COL_BROW));
        fill_ellipse(cx + 22, cy - 24, 14, 5, rgb565(COL_BROW));
        if (!lids) {
            fill_ellipse(cx - 18, cy - 8, 11, 7, rgb565(COL_EYE));
            fill_ellipse(cx + 18, cy - 8, 11, 7, rgb565(COL_EYE));
            fill_circle(cx - 16, cy - 12, 4, rgb565(COL_PUPIL));
            fill_circle(cx + 20, cy - 12, 4, rgb565(COL_PUPIL));
        }
        /* Small round "o" — not the idle smile ellipse. */
        fill_circle(cx, cy + 18, 8, rgb565(COL_MOUTH_L));
        fill_circle(cx, cy + 18, 4, rgb565(belly));
    } else {
        if (!lids) {
            fill_ellipse(cx - 18, cy - 10, 14, 16, rgb565(COL_EYE));
            fill_ellipse(cx + 18, cy - 10, 14, 16, rgb565(COL_EYE));
            fill_circle(cx - 18, cy - 6, 7, rgb565(COL_PUPIL));
            fill_circle(cx + 18, cy - 6, 7, rgb565(COL_PUPIL));
        }
        fill_ellipse(cx, cy + 20, 16, 7, rgb565(COL_MOUTH));
    }

    /* Mute / live strip + mic VU so a dead latch is obvious from the chair. */
    uint32_t strip = s_mic_muted ? COL_MUTE : (listen ? COL_HEAR : COL_LIVE);
    fill_rect(0, 0, W - 1, 5, rgb565(strip));
    int bar = s_peak / 40;
    if (bar < 0) {
        bar = 0;
    }
    if (bar > W - 16) {
        bar = W - 16;
    }
    fill_rect(8, H - 10, 8 + bar, H - 5, rgb565(listen ? COL_HEAR : COL_LIVE));

    s_saw_bounce = true;
}

static void maybe_pass(void)
{
    if (s_passed || !s_saw_bounce || !s_saw_listen || !s_saw_unmuted || s_ticks < 80) {
        return;
    }
    s_passed = true;
    float fps = s_frames / ((float)s_ticks * 0.08f);
    ESP_LOGI(TAG, "tamagotchi fps=%.1f max_ms=%d", fps, (int)(s_max_us / 1000));
    demo_pass("p12");
}

static void audio_task(void *arg)
{
    (void)arg;
    static uint8_t chunk[CHUNK];
    int64_t last_hear_us = 0;
    int64_t last_log_us = 0;
    int noise = 40;

    while (1) {
        bool muted = mic_hardware_muted();
        s_mic_muted = muted;
        if (!muted) {
            s_saw_unmuted = true;
        }

        /* Analog mute is a hardware gate. Still try to keep the codec open
         * whenever the latch is up so we actually see speech peaks. */
        if (muted) {
            if (s_mic_open) {
                (void)esp_codec_dev_close(s_mic);
                s_mic_open = false;
            }
            s_hearing = false;
            s_peak = 0;
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (!s_mic_open) {
            if (esp_codec_dev_open(s_mic, &s_fs) == ESP_OK) {
                (void)esp_codec_dev_set_in_mute(s_mic, false);
                (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);
                s_mic_open = true;
                ESP_LOGI(TAG, "mic open gain=%.0f", (double)MIC_GAIN_DB);
            } else {
                ESP_LOGW(TAG, "mic open failed");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        int peak = 0;
        if (esp_codec_dev_read(s_mic, chunk, CHUNK) == ESP_CODEC_DEV_OK) {
            peak = pcm_peak(chunk, CHUNK);
        }
        s_peak = peak;

        int thresh = noise * 3;
        if (thresh < 120) {
            thresh = 120;
        }
        if (thresh > 2500) {
            thresh = 2500;
        }
        s_noise = noise;
        s_thresh = thresh;

        int64_t now = esp_timer_get_time();
        if (peak >= thresh) {
            last_hear_us = now;
            s_hearing = true;
        } else if (!s_hearing) {
            noise = (noise * 15 + peak) / 16;
            if (noise < 8) {
                noise = 8;
            }
        }

        if (last_hear_us == 0 || (now - last_hear_us) > (int64_t)HEAR_HANG_MS * 1000) {
            s_hearing = false;
        }

        if (now - last_log_us > 1000000) {
            ESP_LOGI(TAG, "mic %s peak=%d noise=%d thresh=%d hear=%d",
                     muted ? "MUTED" : "open", peak, noise, thresh, (int)s_hearing);
            last_log_us = now;
        }
    }
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p12", "display");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("p12", "I2S bring-up");
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, 0);
    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == NULL) {
        demo_fail("p12", "ES7210");
        return;
    }

    /* Same GPIO bring-up as h17/h22 — otherwise MUTE_STATUS may read stuck. */
    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }

    s_fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_fb == NULL) {
        demo_fail("p12", "fb alloc");
        return;
    }

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = W;
    s_dsc.header.h = H;
    s_dsc.header.stride = W * 2;
    s_dsc.data_size = FB_BYTES;
    s_dsc.data = (const uint8_t *)s_fb;

    if (!board_lvgl_lock(0)) {
        demo_fail("p12", "lvgl lock");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    s_img = lv_image_create(scr);
    lv_image_set_src(s_img, &s_dsc);
    lv_obj_set_pos(s_img, 0, 0);
    board_lvgl_unlock();

    s_mic_muted = mic_hardware_muted();
    ESP_LOGI(TAG, "mute GPIO%d is %s (red LED on = muted, analog mics dead)",
             (int)BSP_MUTE_STATUS, s_mic_muted ? "MUTED" : "open");
    ESP_LOGI(TAG, "flip the top latch until the strip is green, then talk");

    xTaskCreate(audio_task, "p12_audio", 4096, NULL, 5, NULL);

    while (1) {
        int64_t t0 = esp_timer_get_time();
        paint_creature();
        if (board_lvgl_lock(80)) {
            lv_obj_invalidate(s_img);
            lv_refr_now(NULL);
            board_lvgl_unlock();
        }
        int64_t dt = esp_timer_get_time() - t0;
        if (dt > s_max_us) {
            s_max_us = dt;
        }
        s_frames++;
        s_ticks++;
        maybe_pass();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}
