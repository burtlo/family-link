/*
 * h17 — Live button panel + analog mic mute + chirps.
 *
 * BOX-3 keys: top mute (latch, GPIO1 / BSP_MUTE_STATUS), boot/config
 * (GPIO0), red circle (GT911). Reset is not programmable. Mic stays
 * closed; mute status is the hardware gate, not a software toggle.
 * Red circle chirps on press and on release (higher, then lower).
 * -- PASS h17 after a chirp and both muted / not-muted GPIO states.
 *
 * Tested 2026-08-23 on the desk kit: chirps audible; mute GPIO matches the LED.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "bsp/esp-bsp.h"
#include "pass.h"

static const char *TAG = "h17";

#define SAMPLE_RATE 16000
#define SPK_VOLUME  70
#define CHIRP_MS    160
#define DRAIN_MS    60
#define WRITE_CHUNK 1024

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = SAMPLE_RATE,
    .channel = 1,
    .bits_per_sample = 16,
};

typedef struct {
    lv_obj_t *row;
    lv_obj_t *value;
} btn_row_t;

static esp_codec_dev_handle_t s_spk;
static btn_row_t s_mute;
static btn_row_t s_boot;
static btn_row_t s_circle;
static lv_obj_t *s_mic_banner;
static lv_obj_t *s_mic_label;

static volatile bool s_mute_down;
static volatile bool s_boot_down;
static volatile bool s_circle_down;
static volatile bool s_mic_muted;
static volatile int s_pending_hz[4];
static volatile uint8_t s_pending_head;
static volatile uint8_t s_pending_tail;

static bool s_saw_muted;
static bool s_saw_unmuted;
static bool s_chirped;
static bool s_passed;
static int16_t s_pcm[4000];

static bool mic_hardware_muted(void)
{
    /* Active-low. 0 = mute engaged = analog mics dead. Same GPIO as mute key. */
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static void fade_edges(int16_t *pcm, int n)
{
    int fade = n / 8;
    if (fade < 8) {
        fade = 8;
    }
    if (fade * 2 > n) {
        fade = n / 4;
    }
    for (int i = 0; i < fade; i++) {
        pcm[i] = (int16_t)((int)pcm[i] * i / fade);
        pcm[n - 1 - i] = (int16_t)((int)pcm[n - 1 - i] * i / fade);
    }
}

static bool write_pcm(const int16_t *pcm, int samples)
{
    const uint8_t *p = (const uint8_t *)pcm;
    int bytes = samples * 2;
    while (bytes > 0) {
        int n = bytes > WRITE_CHUNK ? WRITE_CHUNK : bytes;
        if (esp_codec_dev_write(s_spk, (void *)p, n) != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "speaker write failed");
            return false;
        }
        p += n;
        bytes -= n;
    }
    return true;
}

static void play_chirp(int hz)
{
    int n = SAMPLE_RATE * CHIRP_MS / 1000;
    int drain = SAMPLE_RATE * DRAIN_MS / 1000;
    int cap = (int)(sizeof(s_pcm) / sizeof(s_pcm[0]));
    if (n + drain > cap) {
        n = cap - drain;
    }
    int half = SAMPLE_RATE / (hz * 2);
    if (half < 1) {
        half = 1;
    }
    int sign = 1;
    int left = half;
    for (int i = 0; i < n; i++) {
        s_pcm[i] = (int16_t)(sign * 8000);
        if (--left <= 0) {
            sign = -sign;
            left = half;
        }
    }
    fade_edges(s_pcm, n);
    memset(s_pcm + n, 0, (size_t)drain * sizeof(int16_t));

    if (esp_codec_dev_open(s_spk, &s_fs) != ESP_OK) {
        ESP_LOGE(TAG, "speaker open failed");
        return;
    }
    (void)esp_codec_dev_set_out_vol(s_spk, SPK_VOLUME);
    (void)esp_codec_dev_set_out_mute(s_spk, false);
    /* PA GPIO46 needs a beat after enable; first I2S write after open can be a no-op. */
    vTaskDelay(pdMS_TO_TICKS(30));

    ESP_LOGI(TAG, "chirp %d Hz %d ms", hz, CHIRP_MS);
    if (!write_pcm(s_pcm, n + drain)) {
        (void)esp_codec_dev_close(s_spk);
        return;
    }
    (void)esp_codec_dev_close(s_spk);
    s_chirped = true;
}

static void request_chirp(int hz)
{
    uint8_t next = (uint8_t)((s_pending_head + 1u) % 4u);
    if (next == s_pending_tail) {
        return;
    }
    s_pending_hz[s_pending_head] = hz;
    s_pending_head = next;
}

static int take_chirp(void)
{
    if (s_pending_tail == s_pending_head) {
        return 0;
    }
    int hz = s_pending_hz[s_pending_tail];
    s_pending_tail = (uint8_t)((s_pending_tail + 1u) % 4u);
    return hz;
}

static void style_row(btn_row_t *row, bool down)
{
    uint32_t bg = down ? 0x2E6B4A : 0x2A3038;
    uint32_t fg = down ? 0xE8F8EE : 0xA8B0B8;
    lv_obj_set_style_bg_color(row->row, lv_color_hex(bg), 0);
    lv_label_set_text(row->value, down ? "down" : "up");
    lv_obj_set_style_text_color(row->value, lv_color_hex(fg), 0);
}

static void paint_unlocked(void)
{
    style_row(&s_mute, s_mute_down);
    style_row(&s_boot, s_boot_down);
    style_row(&s_circle, s_circle_down);

    if (s_mic_muted) {
        lv_obj_set_style_bg_color(s_mic_banner, lv_color_hex(0x8B2E2E), 0);
        lv_label_set_text(s_mic_label, "microphone  muted");
        lv_obj_set_style_text_color(s_mic_label, lv_color_hex(0xFFE8E8), 0);
    } else {
        lv_obj_set_style_bg_color(s_mic_banner, lv_color_hex(0x2E6B4A), 0);
        lv_label_set_text(s_mic_label, "microphone  not muted");
        lv_obj_set_style_text_color(s_mic_label, lv_color_hex(0xE8F8EE), 0);
    }
}

static void paint(void)
{
    if (!board_lvgl_lock(50)) {
        return;
    }
    paint_unlocked();
    board_lvgl_unlock();
}

static void maybe_pass(void)
{
    if (s_passed || !s_chirped || !s_saw_muted || !s_saw_unmuted) {
        return;
    }
    s_passed = true;
    demo_pass("h17");
}

static void note_mute_gpio(bool muted)
{
    s_mic_muted = muted;
    s_mute_down = muted;
    if (muted) {
        s_saw_muted = true;
    } else {
        s_saw_unmuted = true;
    }
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    note_mute_gpio(true);
    ESP_LOGI(TAG, "MUTE down  mic muted (GPIO%d)", (int)BSP_MUTE_STATUS);
    paint();
    request_chirp(880);
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    note_mute_gpio(false);
    ESP_LOGI(TAG, "MUTE up  mic not muted (GPIO%d)", (int)BSP_MUTE_STATUS);
    paint();
    request_chirp(660);
}

static void on_boot_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_boot_down = true;
    ESP_LOGI(TAG, "BOOT down  GPIO%d", (int)BSP_BUTTON_CONFIG_IO);
    paint();
    request_chirp(523);
}

static void on_boot_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_boot_down = false;
    ESP_LOGI(TAG, "BOOT up");
    paint();
}

static void on_circle_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_circle_down = true;
    ESP_LOGI(TAG, "CIRCLE down");
    paint();
    request_chirp(1320);
}

static void on_circle_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    s_circle_down = false;
    ESP_LOGI(TAG, "CIRCLE up");
    paint();
    request_chirp(990);
}

static void on_lcd(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "LCD tap");
        request_chirp(988);
    }
}

static btn_row_t make_row(lv_obj_t *parent, int y, const char *name)
{
    btn_row_t out;
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, 296, 40);
    lv_obj_set_pos(row, 12, y);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x2A3038), 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "up");
    lv_obj_set_style_text_color(value, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -12, 0);

    out.row = row;
    out.value = value;
    return out;
}

static void ui_start(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "buttons");
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A9298), 0);
    lv_obj_set_pos(title, 16, 8);

    s_mute = make_row(scr, 28, "mute");
    s_boot = make_row(scr, 74, "boot");
    s_circle = make_row(scr, 120, "red circle");

    s_mic_banner = lv_obj_create(scr);
    lv_obj_remove_style_all(s_mic_banner);
    lv_obj_remove_flag(s_mic_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_mic_banner, 296, 56);
    lv_obj_set_pos(s_mic_banner, 12, 172);
    lv_obj_set_style_radius(s_mic_banner, 8, 0);
    lv_obj_set_style_bg_opa(s_mic_banner, LV_OPA_COVER, 0);

    s_mic_label = lv_label_create(s_mic_banner);
    lv_label_set_text(s_mic_label, "microphone");
    lv_obj_set_style_text_color(s_mic_label, lv_color_hex(0xE8F8EE), 0);
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(s_mic_label, &lv_font_montserrat_28, 0);
#endif
    lv_obj_center(s_mic_label);

    /* LCD tap chirps; does not steal the GT911 red-circle button. */
    lv_obj_t *tap = lv_obj_create(scr);
    lv_obj_remove_style_all(tap);
    lv_obj_set_size(tap, 320, 240);
    lv_obj_set_pos(tap, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tap, on_lcd, LV_EVENT_PRESSED, NULL);
    lv_obj_move_foreground(s_mic_banner);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h17", "display");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (s_spk == NULL) {
        demo_fail("h17", "ES8311");
        return;
    }

    /* Display first so BSP_BUTTON_MAIN (red circle) can attach. */
    if (!board_lvgl_lock(1000)) {
        demo_fail("h17", "lvgl lock");
        return;
    }
    ui_start();
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }
    if (btns[BSP_BUTTON_MUTE] == NULL) {
        demo_fail("h17", "mute button");
        return;
    }
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
    iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    if (btns[BSP_BUTTON_CONFIG]) {
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL, on_boot_down, NULL);
        iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_UP, NULL, on_boot_up, NULL);
    }
    if (btns[BSP_BUTTON_MAIN]) {
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, on_circle_down, NULL);
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL, on_circle_up, NULL);
    }

    note_mute_gpio(mic_hardware_muted());
    paint();

    ESP_LOGI(TAG, "press mute / boot / red circle / LCD. mute latches analog mic.");
    ESP_LOGI(TAG, "mic %s at boot (GPIO%d)", s_mic_muted ? "muted" : "not muted",
             (int)BSP_MUTE_STATUS);
    play_chirp(784);

    while (1) {
        bool muted = mic_hardware_muted();
        if (muted != s_mic_muted) {
            note_mute_gpio(muted);
            ESP_LOGI(TAG, "MUTE_STATUS %s", muted ? "muted" : "not muted");
            paint();
        }
        int hz = take_chirp();
        if (hz) {
            play_chirp(hz);
        }
        maybe_pass();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
