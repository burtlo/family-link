#include "pass.h"
#include "board.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "iot_button.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "p05";

typedef enum {
    FACE_IDLE = 0,
    FACE_LISTEN,
    FACE_POKE,
} face_t;

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
} face_objs_t;

static face_objs_t s_face;
static face_t s_shown = FACE_IDLE;
static bool s_mute_held;
static bool s_saw_listen;
static bool s_passed;
static esp_timer_handle_t s_poke_timer;

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

static void apply_face(face_t mood)
{
    /* Idle: big centered head. Listen: lean in + open mouth. Poke: wide eyes. */
    int head_x = 60;
    int head_y = 6;
    int head_d = 200;
    int eye = 56;
    int eye_y = 52;
    int pupil = 28;
    int mouth_w = 92;
    int mouth_h = 22;
    int mouth_x = 54;
    int mouth_y = 140;
    int brow_opa = 0;

    if (mood == FACE_LISTEN) {
        head_x = 42;
        head_y = 18;
        head_d = 216;
        eye = 60;
        eye_y = 48;
        pupil = 30;
        mouth_w = 72;
        mouth_h = 52;
        mouth_x = 72;
        mouth_y = 128;
        brow_opa = LV_OPA_COVER;
    } else if (mood == FACE_POKE) {
        eye = 72;
        eye_y = 40;
        pupil = 22;
        mouth_w = 36;
        mouth_h = 36;
        mouth_x = 82;
        mouth_y = 132;
    }

    lv_obj_set_pos(s_face.body, 50, 148);
    lv_obj_set_pos(s_face.head, head_x, head_y);
    lv_obj_set_size(s_face.head, head_d, head_d);

    lv_obj_set_size(s_face.eye_l, eye, eye);
    lv_obj_set_size(s_face.eye_r, eye, eye);
    lv_obj_set_pos(s_face.eye_l, 32, eye_y);
    lv_obj_set_pos(s_face.eye_r, 32 + eye + 24, eye_y);

    lv_obj_set_size(s_face.pupil_l, pupil, pupil);
    lv_obj_set_size(s_face.pupil_r, pupil, pupil);
    int pup_off = (eye - pupil) / 2;
    lv_obj_set_pos(s_face.pupil_l, pup_off, pup_off);
    lv_obj_set_pos(s_face.pupil_r, pup_off, pup_off);

    lv_obj_set_size(s_face.mouth, mouth_w, mouth_h);
    lv_obj_set_style_radius(s_face.mouth, mouth_h / 2, 0);
    lv_obj_set_pos(s_face.mouth, mouth_x, mouth_y);

    lv_obj_set_style_bg_opa(s_face.brow_l, brow_opa, 0);
    lv_obj_set_style_bg_opa(s_face.brow_r, brow_opa, 0);
    lv_obj_set_pos(s_face.brow_l, 36, 28);
    lv_obj_set_pos(s_face.brow_r, 120, 28);

    s_shown = mood;
}

static bool paint_face(face_t mood, int64_t t_gpio_us)
{
    if (!board_lvgl_lock(80)) {
        ESP_LOGW(TAG, "lvgl lock miss");
        return false;
    }
    apply_face(mood);
    lv_refr_now(NULL);
    int64_t dt_ms = (esp_timer_get_time() - t_gpio_us) / 1000;
    board_lvgl_unlock();

    const char *name = (mood == FACE_LISTEN) ? "listen" : (mood == FACE_POKE) ? "poke" : "idle";
    ESP_LOGI(TAG, "face %s  gpio->lvgl %lld ms", name, (long long)dt_ms);
    return true;
}

static void maybe_pass(void)
{
    if (s_passed || !s_saw_listen) {
        return;
    }
    if (s_shown != FACE_IDLE) {
        return;
    }
    s_passed = true;
    demo_pass("p05");
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    int64_t t0 = esp_timer_get_time();
    s_mute_held = true;
    if (s_poke_timer) {
        esp_timer_stop(s_poke_timer);
    }
    ESP_LOGI(TAG, "MUTE DOWN");
    if (s_shown == FACE_LISTEN) {
        return;
    }
    if (paint_face(FACE_LISTEN, t0)) {
        s_saw_listen = true;
    }
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    int64_t t0 = esp_timer_get_time();
    s_mute_held = false;
    ESP_LOGI(TAG, "MUTE UP");
    if (s_shown == FACE_IDLE) {
        maybe_pass();
        return;
    }
    if (paint_face(FACE_IDLE, t0)) {
        maybe_pass();
    }
}

static void poke_settle(void *arg)
{
    (void)arg;
    if (s_mute_held) {
        return;
    }
    int64_t t0 = esp_timer_get_time();
    paint_face(FACE_IDLE, t0);
}

static void do_poke(int64_t t0)
{
    if (s_mute_held) {
        return;
    }
    if (s_poke_timer) {
        esp_timer_stop(s_poke_timer);
    }
    if (paint_face(FACE_POKE, t0) && s_poke_timer) {
        esp_timer_start_once(s_poke_timer, 350 * 1000);
    }
}

static void on_main_poke(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    ESP_LOGI(TAG, "MAIN poke");
    do_poke(esp_timer_get_time());
}

static void on_tap_poke(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_PRESSED) {
        return;
    }
    ESP_LOGI(TAG, "LCD poke");
    do_poke(esp_timer_get_time());
}

static void build_idle_face(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x143044), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    s_face.body = shape(scr, 220, 96, 36, 0x2A6A6E);
    s_face.head = shape(scr, 200, 200, LV_RADIUS_CIRCLE, 0xF2C07A);

    s_face.brow_l = shape(s_face.head, 44, 10, 5, 0x5A3A20);
    s_face.brow_r = shape(s_face.head, 44, 10, 5, 0x5A3A20);

    s_face.eye_l = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    s_face.pupil_l = shape(s_face.eye_l, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);

    s_face.eye_r = shape(s_face.head, 56, 56, LV_RADIUS_CIRCLE, 0xFFF6E8);
    s_face.pupil_r = shape(s_face.eye_r, 28, 28, LV_RADIUS_CIRCLE, 0x1A1A1A);

    s_face.mouth = shape(s_face.head, 92, 22, 11, 0xC0453C);

    apply_face(FACE_IDLE);

    /* Full-screen poke target. Does not gate -- PASS p05. */
    lv_obj_t *poke = lv_obj_create(scr);
    lv_obj_remove_style_all(poke);
    lv_obj_set_size(poke, 320, 240);
    lv_obj_set_pos(poke, 0, 0);
    lv_obj_set_style_bg_opa(poke, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(poke, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(poke, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(poke, on_tap_poke, LV_EVENT_PRESSED, NULL);
}

void app_main(void)
{
    if (board_display_start() != ESP_OK) {
        demo_fail("p05", "display");
        return;
    }

    const esp_timer_create_args_t poke_args = {
        .callback = poke_settle,
        .name = "poke",
    };
    ESP_ERROR_CHECK(esp_timer_create(&poke_args, &s_poke_timer));

    if (!board_lvgl_lock(0)) {
        demo_fail("p05", "lvgl lock");
        return;
    }
    build_idle_face();
    board_lvgl_unlock();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL,
                                           on_mute_down, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL,
                                           on_mute_up, NULL));
    if (btns[BSP_BUTTON_MAIN]) {
        iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_DOWN, NULL, on_main_poke, NULL);
    }

    ESP_LOGI(TAG, "mute hold=listen, release=idle; poke is optional. mic closed");
}
