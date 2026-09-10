/*
 * x02_main — v1 product orchestration: app_main, ui_task, paint dispatcher.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

#include "v1_auth.h"
#include "v1_api.h"
#include "v1_carousel.h"
#include "v1_connect.h"
#include "v1_record.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_types.h"
#include "v1_ui_common.h"

#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
#include "esp_sntp.h"
#endif

static const char *TAG = "x02";

static char s_session_user[16];
static int64_t s_connect_start_us;
static int64_t s_idle_us;
static int s_volume;
static int s_vol_notch;

static msg_t s_msgs[V1_MSG_MAX];
static int s_msg_n;
static int s_focus;

static uint8_t *s_buf;
static esp_codec_dev_handle_t s_spk;
static esp_codec_dev_handle_t s_mic;
static SemaphoreHandle_t s_work;
static SemaphoreHandle_t s_login_work;

static v1_ui_settings_cfg_t s_settings_cfg;
static v1_ui_sleep_cfg_t s_sleep_cfg;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static void bump_idle(void)
{
    s_idle_us = now_us();
    v1_ui_sleep_bump_idle();
}

static void note_activity(void)
{
    v1_ui_sleep_note_activity();
    bump_idle();
    v1_ui_sleep_wake_from_asleep(true);
}

static void on_auth_ok(const char *user_id, bool pin_reset)
{
    strncpy(s_session_user, user_id, sizeof(s_session_user) - 1);
    s_session_user[sizeof(s_session_user) - 1] = 0;
    if (pin_reset) {
        v1_ui_set_toast(NULL, "PIN reset - ask Lynn");
    }
    v1_carousel_on_auth_ok();
}

static void on_ws_inbox(const char *user_id)
{
    v1_carousel_on_ws_inbox(user_id);
}

static bool session_signed_in(void)
{
    state_t st = v1_state_get();
    return s_session_user[0] != 0 &&
           (st == ST_CAROUSEL || st == ST_SETTINGS || st == ST_PIN);
}

static bool signed_out_pre_auth(void)
{
    return v1_connect_signed_out_pre_auth(s_session_user, v1_state_get());
}

#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
static void sntp_wait(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    for (int i = 0; i < 50; i++) {
        time_t now = 0;
        time(&now);
        if (now > 1700000000) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
#endif

static void on_circle_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    if (v1_ui_sleep_is_asleep()) {
        note_activity();
        return;
    }
    state_t st = v1_state_get();
    if (st == ST_CAROUSEL) {
        v1_carousel_on_circle_press(now_us());
        note_activity();
    } else if (st == ST_RECORD) {
        v1_record_on_circle_stop();
        note_activity();
    }
}

static void on_boot_press(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    state_t st = v1_state_get();
    if (st == ST_PIN) {
        v1_auth_on_boot_press();
    } else if (st == ST_CAROUSEL || st == ST_SETTINGS) {
        v1_carousel_on_shoulder_press();
    } else if (st == ST_PICK || st == ST_RECORD) {
        v1_record_on_shoulder_cancel();
    }
    note_activity();
}

static void paint(void)
{
    lv_obj_t *scr = lv_screen_active();
    if (v1_ui_sleep_is_asleep()) {
        v1_ui_paint_sleep(scr, &s_sleep_cfg);
        v1_ui_clear_repaint();
        v1_ui_refresh_sleep_anim(&s_sleep_cfg);
        return;
    }
    v1_connect_clear_conn_dots();
    switch (v1_state_get()) {
    case ST_CONNECTING:
        v1_connect_paint_connecting(scr);
        break;
    case ST_WIFI_ERR:
        v1_connect_paint_wifi_error(scr);
        break;
    case ST_ROSTER:
        v1_connect_paint_roster(scr);
        break;
    case ST_PIN:
        v1_auth_paint_pin(scr);
        break;
    case ST_PICK:
        v1_record_paint_pick(scr);
        break;
    case ST_CAROUSEL:
        v1_carousel_paint(scr);
        break;
    case ST_SETTINGS:
        v1_ui_paint_settings(scr, &s_settings_cfg);
        break;
    case ST_RECORD:
        if (scr != NULL) {
            v1_carousel_paint(scr);
            v1_record_paint_overlay(scr);
        }
        break;
    }
    v1_ui_clear_repaint();
}

static void ui_task(void *arg)
{
    (void)arg;
    board_lvgl_lock(0);
    paint();
    board_lvgl_unlock();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (!v1_carousel_is_playing()) {
            int a = 0;
            int b = 0;
            if (v1_ui_take_chirp(&a, &b)) {
                v1_carousel_play_chirp(a);
                if (b) {
                    v1_carousel_play_chirp(b);
                }
            }
        }
        s_sleep_cfg.msg_n = s_msg_n;
        v1_ui_sleep_tick(now_us(), v1_state_get(), &s_sleep_cfg);
        v1_state_drain();
        if (signed_out_pre_auth()) {
            v1_connect_tick(now_us(), true);
        }
        if (v1_state_get() == ST_WIFI_ERR &&
            (now_us() - v1_connect_wifi_retry_us()) > (int64_t)V1_CONNECT_WIFI_RETRY_MS * 1000) {
            v1_connect_set_wifi_retry_us(now_us());
            if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) == ESP_OK) {
                v1_state_apply(ST_CONNECTING);
                s_connect_start_us = now_us();
                v1_connect_set_conn_retry_us(now_us());
                v1_api_ws_start();
                (void)v1_connect_load_hangout();
                if (v1_connect_online()) {
                    v1_state_apply(ST_ROSTER);
                }
                v1_ui_request_repaint();
            }
        }
        if (v1_state_get() == ST_CONNECTING) {
            board_lvgl_lock(0);
            v1_connect_refresh_conn_dots();
            board_lvgl_unlock();
        }
        if (v1_state_get() == ST_CAROUSEL || v1_state_get() == ST_SETTINGS) {
            board_lvgl_lock(0);
            v1_carousel_refresh_offline_ribbon();
            board_lvgl_unlock();
        }
        if (v1_ui_repaint_pending() || v1_carousel_is_playing() || v1_ui_transport_dirty()) {
            board_lvgl_lock(0);
            if (v1_ui_repaint_pending()) {
                paint();
            } else if (v1_state_get() == ST_CAROUSEL) {
                v1_carousel_refresh_transport();
            }
            v1_ui_clear_transport_dirty();
            board_lvgl_unlock();
        }
        if (v1_state_get() == ST_PIN) {
            board_lvgl_lock(0);
            v1_auth_tick(v1_state_get());
            board_lvgl_unlock();
        }
        (void)v1_record_tick_pick_timeout(now_us());
        (void)v1_carousel_tick_inbox();
        if ((v1_state_get() == ST_CAROUSEL || v1_state_get() == ST_SETTINGS) &&
            (now_us() - s_idle_us) > (int64_t)V1_UI_IDLE_RELOCK_MS * 1000) {
            v1_carousel_stop_playback();
            v1_auth_clear_entry();
            v1_state_apply(ST_PIN);
            board_lvgl_lock(0);
            paint();
            board_lvgl_unlock();
            bump_idle();
            continue;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "x02 v1 shell (%s / %s, peer %s) %s://%s:%d",
             WHO_DEVICE_NAME, WHO_DEVICE_ID, WHO_PEER_NAME,
#if DEMO_SERVER_TLS
             "https",
#else
             "http",
#endif
             DEMO_SERVER_HOST, DEMO_SERVER_PORT);

    if (board_display_start() != ESP_OK) {
        ESP_LOGE(TAG, "display start failed");
        return;
    }
    board_backlight_set(V1_BRIGHT_NORM);
    v1_ui_sleep_init(now_us());
    bump_idle();
    s_connect_start_us = now_us();

    v1_state_init();
    v1_api_init();
    v1_ui_init();
    v1_connect_init();

    v1_carousel_cfg_t car_cfg = {
        .session_user = s_session_user,
        .spk = NULL,
        .playback_buf = NULL,
        .playback_buf_cap = V1_PLAYBACK_BUF_CAP,
        .volume = &s_volume,
        .vol_notch = &s_vol_notch,
    };
    v1_carousel_init(&car_cfg);
    v1_carousel_bind_inbox(s_msgs, &s_msg_n, &s_focus, V1_MSG_MAX);
    v1_api_ws_on_inbox(on_ws_inbox);
    v1_ui_set_activity_cb(note_activity);
    v1_auth_set_login_ok_cb(on_auth_ok);

    {
        int64_t t = now_us();
        v1_connect_set_conn_retry_us(t);
        v1_connect_set_wifi_retry_us(t);
    }
    {
        char last[16];
        v1_connect_nvs_load_last(last, sizeof(last));
    }
    v1_carousel_nvs_load_card_grad();
    (void)v1_connect_nvs_load_hangout();
    v1_state_apply(ST_CONNECTING);

    s_buf = heap_caps_malloc(V1_PLAYBACK_BUF_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf) {
        s_buf = heap_caps_malloc(V1_PLAYBACK_BUF_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_buf) {
        board_status_set("OOM playback buf");
        ESP_LOGE(TAG, "playback buffer alloc failed");
        return;
    }
    car_cfg.playback_buf = s_buf;
    v1_carousel_init(&car_cfg);

    s_work = xSemaphoreCreateBinary();
    s_login_work = xSemaphoreCreateBinary();
    v1_auth_init(s_login_work);
    v1_auth_start_task();

    v1_record_cfg_t rec_cfg = {
        .session_user = s_session_user,
        .mic = NULL,
        .spk = NULL,
        .work_sem = s_work,
        .play_chirp_pair = v1_carousel_play_chirp_pair,
    };
    v1_record_init(&rec_cfg);

    s_settings_cfg = (v1_ui_settings_cfg_t){
        .session_user = s_session_user,
        .vol_notch = &s_vol_notch,
        .apply_volume = v1_carousel_apply_volume,
        .roomvol_codec = v1_carousel_roomvol_codec,
        .card_grad = NULL,
        .nvs_save_card_grad = v1_carousel_save_card_grad,
        .stop_playback = v1_carousel_stop_playback,
        .msgs = s_msgs,
        .msg_n = V1_MSG_MAX,
    };
    s_sleep_cfg = (v1_ui_sleep_cfg_t){
        .session_user = s_session_user,
        .msgs = s_msgs,
        .msg_n = 0,
        .session_signed_in = session_signed_in,
        .stop_playback = v1_carousel_stop_playback,
    };

    xTaskCreate(ui_task, "ui", 12288, NULL, 5, NULL);

    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        ESP_LOGE(TAG, "wifi join failed");
        v1_state_apply(ST_WIFI_ERR);
        v1_ui_request_repaint();
    } else {
#if DEMO_SERVER_TLS && !DEMO_TLS_SKIP_VERIFY
        sntp_wait();
#endif
        v1_api_ws_start();
        (void)v1_connect_load_hangout();
        if (v1_connect_online()) {
            ESP_LOGI(TAG, "hangout %d users", v1_connect_user_count());
            v1_state_apply(ST_ROSTER);
        } else {
            ESP_LOGW(TAG, "GET /v1/hangout failed");
            v1_state_apply(ST_CONNECTING);
        }
        v1_ui_request_repaint();
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    car_cfg.spk = s_spk;
    v1_carousel_init(&car_cfg);
    rec_cfg.mic = s_mic;
    rec_cfg.spk = s_spk;
    v1_record_init(&rec_cfg);

    if (!s_spk || !s_mic) {
        board_status_set("codec failed");
        ESP_LOGE(TAG, "codec init failed spk=%p mic=%p", (void *)s_spk, (void *)s_mic);
    } else {
        (void)esp_codec_dev_set_out_vol(s_spk, 0);
    }

    v1_carousel_start_tasks();
    v1_record_start_task();

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    if (bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM) == ESP_OK) {
        if (btns[BSP_BUTTON_MAIN]) {
            iot_button_register_cb(btns[BSP_BUTTON_MAIN], BUTTON_PRESS_UP, NULL,
                                  on_circle_up, NULL);
        }
        if (btns[BSP_BUTTON_CONFIG]) {
            iot_button_register_cb(btns[BSP_BUTTON_CONFIG], BUTTON_PRESS_DOWN, NULL,
                                  on_boot_press, NULL);
        }
    }

    s_vol_notch = 4;
    v1_carousel_apply_volume(v1_carousel_roomvol_codec(s_vol_notch));
}
