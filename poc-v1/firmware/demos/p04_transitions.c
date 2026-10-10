/*
 * p04 — Same moods as p03, but 200–400 ms tweens instead of hard cuts.
 * UART prints each tween duration. -- PASS p04 after all five states.
 */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "lvgl.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "p04";

typedef enum {
    MOOD_IDLE = 0,
    MOOD_LISTEN,
    MOOD_MAIL,
    MOOD_LOCKED,
    MOOD_ERROR,
    MOOD_COUNT,
} mood_t;

typedef struct {
    lv_obj_t *head;
    lv_obj_t *mouth;
    lv_obj_t *caption;
} face_t;

static face_t s_face;
static mood_t s_mood;
static unsigned s_seen_mask;
static bool s_passed;
static bool s_busy;
static int64_t s_tween_t0;

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

static const char *mood_name(mood_t m)
{
    switch (m) {
    case MOOD_LISTEN:
        return "listen";
    case MOOD_MAIL:
        return "mail";
    case MOOD_LOCKED:
        return "locked";
    case MOOD_ERROR:
        return "error";
    default:
        return "idle";
    }
}

static void mood_geom(mood_t mood, int *head_x, int *head_y, int *mouth_h)
{
    *head_x = 60;
    *head_y = 6;
    *mouth_h = 22;
    if (mood == MOOD_LISTEN) {
        *head_x = 42;
        *head_y = 18;
        *mouth_h = 52;
    } else if (mood == MOOD_MAIL) {
        *head_y = 0;
        *mouth_h = 28;
    } else if (mood == MOOD_LOCKED) {
        *head_y = 14;
        *mouth_h = 14;
    } else if (mood == MOOD_ERROR) {
        *head_x = 68;
        *mouth_h = 10;
    }
}

static void anim_x(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

static void anim_y(void *obj, int32_t v)
{
    lv_obj_set_y((lv_obj_t *)obj, v);
}

static void anim_h(void *obj, int32_t v)
{
    lv_obj_set_height((lv_obj_t *)obj, v);
    lv_obj_set_style_radius((lv_obj_t *)obj, v / 2, 0);
}

static void tween_done(lv_anim_t *a)
{
    (void)a;
    int ms = (int)((esp_timer_get_time() - s_tween_t0) / 1000);
    ESP_LOGI(TAG, "tween -> %s  %d ms", mood_name(s_mood), ms);
    s_busy = false;
    s_seen_mask |= (1u << s_mood);
    if (!s_passed && s_seen_mask == ((1u << MOOD_COUNT) - 1u)) {
        s_passed = true;
        demo_pass("p04");
    }
}

static void start_tween(mood_t to)
{
    int x0, y0, h0, x1, y1, h1;
    mood_geom(s_mood, &x0, &y0, &h0);
    mood_geom(to, &x1, &y1, &h1);
    s_mood = to;
    s_busy = true;
    s_tween_t0 = esp_timer_get_time();
    lv_label_set_text(s_face.caption, mood_name(to));

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, s_face.head);
    lv_anim_set_values(&ax, x0, x1);
    lv_anim_set_duration(&ax, 300);
    lv_anim_set_exec_cb(&ax, anim_x);
    lv_anim_start(&ax);

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, s_face.head);
    lv_anim_set_values(&ay, y0, y1);
    lv_anim_set_duration(&ay, 300);
    lv_anim_set_exec_cb(&ay, anim_y);
    lv_anim_start(&ay);

    lv_anim_t ah;
    lv_anim_init(&ah);
    lv_anim_set_var(&ah, s_face.mouth);
    lv_anim_set_values(&ah, h0, h1);
    lv_anim_set_duration(&ah, 300);
    lv_anim_set_exec_cb(&ah, anim_h);
    lv_anim_set_completed_cb(&ah, tween_done);
    lv_anim_start(&ah);
}

static void on_mute(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (s_busy) {
        return;
    }
    if (!board_lvgl_lock(80)) {
        return;
    }
    start_tween((mood_t)((s_mood + 1) % MOOD_COUNT));
    board_lvgl_unlock();
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p04", "display");
        return;
    }
    if (!board_lvgl_lock(0)) {
        demo_fail("p04", "lvgl lock");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);

    lv_obj_t *body = shape(scr, 220, 96, 36, 0x2A6A6E);
    lv_obj_set_pos(body, 50, 148);
    s_face.head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    lv_obj_set_pos(s_face.head, 60, 6);
    lv_obj_t *eye_l = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_l, 32, 52);
    lv_obj_t *pupil_l = shape(eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_l, 14, 14);
    lv_obj_t *eye_r = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    lv_obj_set_pos(eye_r, 112, 52);
    lv_obj_t *pupil_r = shape(eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    lv_obj_set_pos(pupil_r, 14, 14);
    s_face.mouth = shape(s_face.head, 92, 22, 11, 0xC0453C);
    lv_obj_set_pos(s_face.mouth, 54, 140);
    s_face.caption = lv_label_create(scr);
    lv_obj_set_style_text_color(s_face.caption, lv_color_hex(0xD0E0E8), 0);
    lv_obj_align(s_face.caption, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_label_set_text(s_face.caption, "idle");
    s_seen_mask = 1u << MOOD_IDLE;
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL,
                                           on_mute, NULL));
    ESP_LOGI(TAG, "mute tap tweens to the next mood (~300 ms)");
}
