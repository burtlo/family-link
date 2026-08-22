/*
 * p11 — Play packed greeting WAV (later: your voice). Mute tap replays.
 * Mic stays off. Portrait stays on screen. UI chirps remain a different slot.
 */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "iot_button.h"
#include "lvgl.h"

#if __has_include("assets/persona/persona_idle.h")
#include "assets/persona/persona_idle.h"
#else
#include "assets/persona.example/persona_idle.h"
#endif

#if __has_include("assets/persona/persona_greeting.h")
#include "assets/persona/persona_greeting.h"
#else
#include "assets/persona.example/persona_greeting.h"
#endif

static const char *TAG = "p11";

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = PERSONA_GREETING_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_codec_dev_handle_t s_spk;
static bool s_passed;

static bool play_greeting(void)
{
    (void)esp_codec_dev_set_out_vol(s_spk, 62);
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        demo_fail("p11", "speaker open");
        return false;
    }
    const uint8_t *p = persona_greeting_pcm;
    int left = PERSONA_GREETING_BYTES;
    while (left > 0) {
        int n = left > 1024 ? 1024 : left;
        if (esp_codec_dev_write(s_spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(s_spk);
            demo_fail("p11", "speaker write");
            return false;
        }
        p += n;
        left -= n;
    }
    (void)esp_codec_dev_close(s_spk);
    ESP_LOGI(TAG, "played greeting %d bytes @ %d Hz", PERSONA_GREETING_BYTES, PERSONA_GREETING_RATE);
    return true;
}

static void on_mute(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (play_greeting() && !s_passed) {
        s_passed = true;
        demo_pass("p11");
    }
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p11", "display");
        return;
    }
    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("p11", "ES8311");
        return;
    }

    if (board_lvgl_lock(0)) {
        lv_obj_t *scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, &persona_idle);
        lv_obj_center(img);
        lv_obj_t *cap = lv_label_create(scr);
        lv_label_set_text(cap, "mute = greeting");
        lv_obj_set_style_text_color(cap, lv_color_hex(0xD0E0E8), 0);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -6);
        board_lvgl_unlock();
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute, NULL);

    ESP_LOGI(TAG, "mute tap plays packed greeting; mic closed");
    if (play_greeting() && !s_passed) {
        s_passed = true;
        demo_pass("p11");
    }
}
