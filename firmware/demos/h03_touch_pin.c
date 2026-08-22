/* Proves GT911 touch via LVGL can enter a PIN and unlock with no audio or message body. */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef DEMO_PIN
#define DEMO_PIN "1234"
#endif

#include <stdbool.h>
#include <string.h>

#define ENTRY_MAX 8

static char s_entry[ENTRY_MAX + 1];
static size_t s_len;
static bool s_unlocked;
static bool s_passed;
static lv_obj_t *s_status;
static lv_obj_t *s_dots;

static void set_status(const char *text, uint32_t color)
{
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(color), 0);
}

static void refresh_dots(void)
{
    char dots[ENTRY_MAX + 1];
    memset(dots, '*', s_len);
    dots[s_len] = '\0';
    lv_label_set_text(s_dots, s_len ? dots : " ");
}

static void on_key(lv_event_t *e)
{
    if (s_unlocked) {
        return;
    }

    const char *key = lv_event_get_user_data(e);
    if (key == NULL) {
        return;
    }

    if (strcmp(key, "Clear") == 0) {
        s_len = 0;
        s_entry[0] = '\0';
        set_status("locked", 0xcccccc);
        refresh_dots();
        return;
    }

    if (s_len >= ENTRY_MAX) {
        return;
    }
    s_entry[s_len++] = key[0];
    s_entry[s_len] = '\0';
    refresh_dots();

    size_t pin_len = strlen(DEMO_PIN);
    if (s_len < pin_len) {
        set_status("locked", 0xcccccc);
        return;
    }

    if (strcmp(s_entry, DEMO_PIN) == 0) {
        s_unlocked = true;
        set_status("unlocked", 0x55dd88);
        if (!s_passed) {
            s_passed = true;
            demo_pass("h03");
        }
        return;
    }

    s_len = 0;
    s_entry[0] = '\0';
    set_status("wrong", 0xff5555);
    refresh_dots();
}

static void make_key(lv_obj_t *parent, const char *label, int x, int y, int w, int h)
{
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *btn = lv_button_create(parent);
#else
    lv_obj_t *btn = lv_btn_create(parent);
#endif
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, label);
    lv_obj_center(txt);
    lv_obj_add_event_cb(btn, on_key, LV_EVENT_CLICKED, (void *)label);
}

static void paint_pad(void)
{
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a1a), 0);

    s_status = lv_label_create(scr);
    set_status("locked", 0xcccccc);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 6);

    s_dots = lv_label_create(scr);
    lv_label_set_text(s_dots, " ");
    lv_obj_set_style_text_color(s_dots, lv_color_white(), 0);
    lv_obj_align(s_dots, LV_ALIGN_TOP_MID, 0, 28);

    /* 320x240: 3x4 pad under the status line. Clear spans two cells. */
    const int ox = 8;
    const int oy = 52;
    const int gap = 6;
    const int bw = 98;
    const int bh = 42;
    static const char *const digits[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9",
    };
    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        make_key(scr, digits[i], ox + col * (bw + gap), oy + row * (bh + gap), bw, bh);
    }
    make_key(scr, "Clear", ox, oy + 3 * (bh + gap), bw * 2 + gap, bh);
    make_key(scr, "0", ox + 2 * (bw + gap), oy + 3 * (bh + gap), bw, bh);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("h03", "display");
        return;
    }

    if (!board_lvgl_lock(0)) {
        demo_fail("h03", "lvgl lock");
        return;
    }
    paint_pad();
    board_lvgl_unlock();
}
