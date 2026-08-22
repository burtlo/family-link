/*
 * p03 — Hard cuts between named face states. Mute key cycles them.
 * -- PASS p03 after idle, listen, mail, locked, and error have each been shown.
 */

#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "iot_button.h"
#include "lvgl.h"

#include "esp_log.h"

static const char *TAG = "p03";

typedef enum {
    MOOD_IDLE = 0,
    MOOD_LISTEN,
    MOOD_MAIL,
    MOOD_LOCKED,
    MOOD_ERROR,
    MOOD_COUNT,
} mood_t;

typedef struct {
    lv_obj_t *body;
    lv_obj_t *head;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *pupil_l;
    lv_obj_t *pupil_r;
    lv_obj_t *mouth;
    lv_obj_t *brow_l;
    lv_obj_t *brow_r;
    lv_obj_t *badge;
    lv_obj_t *caption;
} face_t;

static face_t s_face;
static mood_t s_mood;
static unsigned s_seen_mask;
static bool s_passed;

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

static void apply_mood(mood_t mood)
{
    int head_x = 60, head_y = 6, head_d = 200;
    int eye = 56, eye_y = 52, pupil = 28;
    int mouth_w = 92, mouth_h = 22, mouth_x = 54, mouth_y = 140;
    int brow_h = 10, brow_opa = 0;
    uint32_t mouth_hex = 0xC0453C;
    const char *badge = "";
    const char *caption = "idle";

    if (mood == MOOD_LISTEN) {
        head_x = 42;
        head_y = 18;
        head_d = 216;
        eye = 60;
        mouth_w = 72;
        mouth_h = 52;
        mouth_x = 72;
        mouth_y = 128;
        brow_opa = LV_OPA_COVER;
        caption = "listen";
    } else if (mood == MOOD_MAIL) {
        head_y = 0;
        eye_y = 44;
        mouth_w = 100;
        mouth_h = 28;
        badge = "2";
        caption = "mail  2";
    } else if (mood == MOOD_LOCKED) {
        eye = 56;
        eye_y = 64;
        pupil = 20;
        mouth_w = 48;
        mouth_h = 14;
        mouth_x = 76;
        mouth_y = 150;
        badge = "2";
        caption = "locked  2";
    } else if (mood == MOOD_ERROR) {
        brow_h = 8;
        brow_opa = LV_OPA_COVER;
        mouth_w = 64;
        mouth_h = 10;
        mouth_y = 152;
        mouth_hex = 0x6A2030;
        caption = "error";
    }

    lv_obj_set_pos(s_face.head, head_x, head_y);
    lv_obj_set_size(s_face.head, head_d, head_d);
    lv_obj_set_size(s_face.eye_l, eye, eye);
    lv_obj_set_size(s_face.eye_r, eye, eye);
    lv_obj_set_pos(s_face.eye_l, 32, eye_y);
    lv_obj_set_pos(s_face.eye_r, 32 + eye + 24, eye_y);
    lv_obj_set_size(s_face.pupil_l, pupil, pupil);
    lv_obj_set_size(s_face.pupil_r, pupil, pupil);
    int off = (eye - pupil) / 2;
    lv_obj_set_pos(s_face.pupil_l, off, off);
    lv_obj_set_pos(s_face.pupil_r, off, off);
    lv_obj_set_size(s_face.mouth, mouth_w, mouth_h);
    lv_obj_set_style_radius(s_face.mouth, mouth_h / 2, 0);
    lv_obj_set_style_bg_color(s_face.mouth, lv_color_hex(mouth_hex), 0);
    lv_obj_set_pos(s_face.mouth, mouth_x, mouth_y);
    lv_obj_set_height(s_face.brow_l, brow_h);
    lv_obj_set_height(s_face.brow_r, brow_h);
    lv_obj_set_style_bg_opa(s_face.brow_l, brow_opa, 0);
    lv_obj_set_style_bg_opa(s_face.brow_r, brow_opa, 0);
    lv_obj_set_pos(s_face.brow_l, 36, (mood == MOOD_ERROR) ? 40 : 28);
    lv_obj_set_pos(s_face.brow_r, 120, (mood == MOOD_ERROR) ? 40 : 28);
    lv_label_set_text(s_face.badge, badge);
    lv_label_set_text(s_face.caption, caption);

    s_mood = mood;
    s_seen_mask |= (1u << mood);
    ESP_LOGI(TAG, "mood %s", mood_name(mood));

    if (!s_passed && s_seen_mask == ((1u << MOOD_COUNT) - 1u)) {
        s_passed = true;
        demo_pass("p03");
    }
}

static void on_mute(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (!board_lvgl_lock(80)) {
        return;
    }
    apply_mood((mood_t)((s_mood + 1) % MOOD_COUNT));
    lv_refr_now(NULL);
    board_lvgl_unlock();
}

static void build_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_face.body = shape(scr, 220, 96, 36, 0x2A6A6E);
    lv_obj_set_pos(s_face.body, 50, 148);
    s_face.head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);
    s_face.brow_l = shape(s_face.head, 44, 10, 5, 0x5A3A20);
    s_face.brow_r = shape(s_face.head, 44, 10, 5, 0x5A3A20);
    s_face.eye_l = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    s_face.pupil_l = shape(s_face.eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    s_face.eye_r = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    s_face.pupil_r = shape(s_face.eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);
    s_face.mouth = shape(s_face.head, 92, 22, 11, 0xC0453C);

    s_face.badge = lv_label_create(scr);
    lv_obj_set_style_text_color(s_face.badge, lv_color_hex(0xFFFFFF), 0);
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(s_face.badge, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(s_face.badge, LV_ALIGN_TOP_RIGHT, -12, 8);

    s_face.caption = lv_label_create(scr);
    lv_obj_set_style_text_color(s_face.caption, lv_color_hex(0xD0E0E8), 0);
    lv_obj_align(s_face.caption, LV_ALIGN_BOTTOM_MID, 0, -6);

    apply_mood(MOOD_IDLE);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p03", "display");
        return;
    }
    if (!board_lvgl_lock(0)) {
        demo_fail("p03", "lvgl lock");
        return;
    }
    build_face();
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL,
                                           on_mute, NULL));
    ESP_LOGI(TAG, "mute tap cycles idle/listen/mail/locked/error. badge is a number.");
}
