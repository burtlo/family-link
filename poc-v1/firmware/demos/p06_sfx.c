/*
 * p06 — Short non-speech chirps on the ES8311. Mic stays off.
 * Long-press mute to mute SFX (quiet-hours hook). -- PASS p06 after all six.
 */

#include <stdint.h>
#include <string.h>

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"

static const char *TAG = "p06";

#define SAMPLE_RATE 16000
#define SPK_VOLUME  55

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_codec_dev_handle_t s_spk;
static bool s_sfx_muted;
static unsigned s_played_mask;
static bool s_passed;

enum {
    SFX_LISTEN = 0,
    SFX_RELEASE,
    SFX_POKE,
    SFX_NOTICE,
    SFX_OK,
    SFX_NOPE,
    SFX_COUNT,
};

static void fade_edges(int16_t *pcm, int n)
{
    int fade = n / 16;
    if (fade < 16) {
        fade = 16;
    }
    if (fade * 2 > n) {
        fade = n / 4;
    }
    for (int i = 0; i < fade; i++) {
        pcm[i] = (int16_t)((int)pcm[i] * i / fade);
        pcm[n - 1 - i] = (int16_t)((int)pcm[n - 1 - i] * i / fade);
    }
}

static void fill_square(int16_t *pcm, int n, int hz, int amp)
{
    int half = SAMPLE_RATE / (hz * 2);
    if (half < 1) {
        half = 1;
    }
    int sign = 1;
    int left = half;
    for (int i = 0; i < n; i++) {
        pcm[i] = (int16_t)(sign * amp);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    fade_edges(pcm, n);
}

static bool play_pcm(int16_t *pcm, int samples)
{
    if (s_sfx_muted) {
        ESP_LOGI(TAG, "sfx muted, skip");
        return true;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        demo_fail("p06", "speaker open");
        return false;
    }
    int bytes = samples * 2;
    uint8_t *p = (uint8_t *)pcm;
    while (bytes > 0) {
        int n = bytes > 1024 ? 1024 : bytes;
        if (esp_codec_dev_write(s_spk, p, n) != ESP_CODEC_DEV_OK) {
            (void)esp_codec_dev_close(s_spk);
            demo_fail("p06", "speaker write");
            return false;
        }
        p += n;
        bytes -= n;
    }
    (void)esp_codec_dev_close(s_spk);
    return true;
}

static const char *sfx_name(int id)
{
    switch (id) {
    case SFX_LISTEN:
        return "listen";
    case SFX_RELEASE:
        return "release";
    case SFX_POKE:
        return "poke";
    case SFX_NOTICE:
        return "notice";
    case SFX_OK:
        return "ok";
    default:
        return "nope";
    }
}

static bool play_sfx(int id)
{
    int16_t pcm[4000];
    int n = 0;
    memset(pcm, 0, sizeof(pcm));

    if (id == SFX_LISTEN) {
        n = SAMPLE_RATE * 80 / 1000;
        fill_square(pcm, n, 880, 4000);
    } else if (id == SFX_RELEASE) {
        n = SAMPLE_RATE * 80 / 1000;
        fill_square(pcm, n, 660, 4000);
    } else if (id == SFX_POKE) {
        int hop = SAMPLE_RATE * 40 / 1000;
        fill_square(pcm, hop, 1320, 5000);
        fill_square(pcm + hop + 80, hop, 1320, 5000);
        n = hop + 80 + hop;
    } else if (id == SFX_NOTICE) {
        int hop = SAMPLE_RATE * 90 / 1000;
        fill_square(pcm, hop, 988, 4500);
        fill_square(pcm + hop, hop, 1318, 4500);
        n = hop * 2;
    } else if (id == SFX_OK) {
        int hop = SAMPLE_RATE * 70 / 1000;
        fill_square(pcm, hop, 523, 4000);
        fill_square(pcm + hop, hop, 784, 4000);
        n = hop * 2;
    } else {
        n = SAMPLE_RATE * 180 / 1000;
        fill_square(pcm, n, 200, 3500);
    }

    ESP_LOGI(TAG, "chirp %s (%d samples, not speech)", sfx_name(id), n);
    if (!play_pcm(pcm, n)) {
        return false;
    }
    s_played_mask |= (1u << id);
    if (!s_passed && s_played_mask == ((1u << SFX_COUNT) - 1u)) {
        s_passed = true;
        demo_pass("p06");
    }
    return true;
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    play_sfx(SFX_LISTEN);
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    play_sfx(SFX_RELEASE);
}

static void on_mute_long(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_sfx_muted = !s_sfx_muted;
    ESP_LOGI(TAG, "quiet-hours sfx %s", s_sfx_muted ? "OFF" : "ON");
}

static void on_main(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    play_sfx(SFX_POKE);
}

static void on_tap(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    play_sfx(SFX_POKE);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p06", "display");
        return;
    }
    board_status_set("chirps: mute / poke\nlong mute = quiet");

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("p06", "ES8311");
        return;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_LONG_PRESS_START, NULL, on_mute_long, NULL);
    if (btns[BSP_BUTTON_MAIN]) {
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, on_main, NULL);
    }

    if (board_lvgl_lock(50)) {
        lv_obj_t *poke = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(poke);
        lv_obj_set_size(poke, 320, 240);
        lv_obj_add_flag(poke, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(poke, on_tap, LV_EVENT_PRESSED, NULL);
        board_lvgl_unlock();
    }

    /* Auto-play notice/ok/nope so PASS does not depend on those extra buttons. */
    vTaskDelay(pdMS_TO_TICKS(400));
    play_sfx(SFX_NOTICE);
    vTaskDelay(pdMS_TO_TICKS(250));
    play_sfx(SFX_OK);
    vTaskDelay(pdMS_TO_TICKS(250));
    play_sfx(SFX_NOPE);
    ESP_LOGI(TAG, "hold mute (listen+release) and poke the screen to finish");
}
