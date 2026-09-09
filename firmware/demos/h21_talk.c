/*
 * h21 — Live voice between two boxes while unmuted (Mazi ↔ Arlo).
 *
 * Start muted. Mute up (LED off) streams 20 ms PCM through the server.
 * Mute down stops sending. Incoming audio plays in a jitter queue so the
 * WebSocket task never blocks on the speaker. Do not send from the WS
 * callback (that deadlocks and reboots).
 *
 * Mute state rides the same WebSocket as PCM (`status` / `peer_status`)
 * so the friend card shows muted/talking, not a stuck "waiting".
 *
 * Speaker uses the same ROOMVOL notches as h18: mute, then 78 … 100 by 2.
 *
 * Host (leave running):
 *   python -m demos.server.h21_talk.server --host 0.0.0.0 --port 8080
 *
 *   make flash DEMO=h21 WHO=mazi
 *   make flash DEMO=h21 WHO=arlo
 * -- PASS h21 after ~10 frames each way.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iot_button.h"
#include "lvgl.h"

#include "board.h"
#include "pass.h"
#include "who.h"
#include "wifi_sta.h"

static const char *TAG = "h21";
#define CHUNK 640
#define STATUS_PERIOD_MS 800
#define MIC_GAIN_DB 28.0f
#define PLAY_Q 6
#define LINK_WAIT_MS 2500

/*
 * ROOMVOL_* — same stepped speaker control as h18_playback_screen.c.
 * Mute, then 78, 80, … 100. Starts at 82 (two-room duplex; 90 howls).
 */
#ifndef ROOMVOL_SHOW_LEVEL
#define ROOMVOL_SHOW_LEVEL 1
#endif
#ifndef ROOMVOL_FIRST_ON
#define ROOMVOL_FIRST_ON 78
#endif
#ifndef ROOMVOL_STEP
#define ROOMVOL_STEP 2
#endif
#ifndef ROOMVOL_MAX
#define ROOMVOL_MAX 100
#endif
#define ROOMVOL_ON_COUNT (((ROOMVOL_MAX - ROOMVOL_FIRST_ON) / ROOMVOL_STEP) + 1)
#define ROOMVOL_SLIDER_MAX ROOMVOL_ON_COUNT
#define ROOMVOL_DEFAULT_NOTCH (1 + ((82 - ROOMVOL_FIRST_ON) / ROOMVOL_STEP))

static const esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = 16000,
    .channel = 1,
    .bits_per_sample = 16,
};

static esp_websocket_client_handle_t s_ws;
static esp_codec_dev_handle_t s_mic;
static esp_codec_dev_handle_t s_spk;
static volatile bool s_open;
static volatile bool s_spk_open;
static volatile int s_peer_online = -1;
static volatile int s_peer_open = -1;
static volatile int s_tx;
static volatile int s_rx;
static volatile int s_notch;
static volatile int s_volume;
static volatile bool s_need_status;
static volatile bool s_need_hello;
static volatile bool s_ui_dirty;
static volatile bool s_link_down;
static TickType_t s_link_lost_at;
static TickType_t s_peer_lost_at;
static bool s_passed;

static QueueHandle_t s_playq;

static lv_obj_t *s_you_name;
static lv_obj_t *s_you_state;
static lv_obj_t *s_peer_name_lbl;
static lv_obj_t *s_peer_state;
static lv_obj_t *s_counts;
static lv_obj_t *s_vol_lab;

static char s_self_name[24];
static char s_peer_name[24];
static char s_txt[512];
static int s_txt_n;
static uint8_t s_bin[CHUNK];
static int s_bin_n;
static uint8_t s_drop[CHUNK];

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

static int json_tristate(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v || cJSON_IsNull(v)) {
        return -1;
    }
    if (cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint)) {
        return 1;
    }
    if (cJSON_IsFalse(v) || cJSON_IsNumber(v)) {
        return 0;
    }
    return -1;
}

static const char *peer_label(void)
{
    if (s_peer_online != 1) {
        return "waiting";
    }
    if (s_peer_open < 0) {
        return "here";
    }
    return s_peer_open ? "talking" : "muted";
}

static uint32_t peer_color(void)
{
    if (s_peer_online != 1) {
        return 0x888888;
    }
    if (s_peer_open == 1) {
        return 0x7DCC7A;
    }
    if (s_peer_open == 0) {
        return 0xCC8844;
    }
    return 0xA8B0B8;
}

static int roomvol_codec(int notch)
{
    if (notch <= 0) {
        return 0;
    }
    int v = ROOMVOL_FIRST_ON + (notch - 1) * ROOMVOL_STEP;
    if (v > ROOMVOL_MAX) {
        v = ROOMVOL_MAX;
    }
    return v;
}

#if ROOMVOL_SHOW_LEVEL
static void fmt_level(char *out, size_t cap, int codec)
{
    if (codec <= 0) {
        snprintf(out, cap, "mute");
        return;
    }
    snprintf(out, cap, "%d", codec);
}
#endif

static void apply_volume(int vol)
{
    if (vol < 0) {
        vol = 0;
    }
    if (vol > 100) {
        vol = 100;
    }
    s_volume = vol;
    if (s_spk) {
        (void)esp_codec_dev_set_out_vol(s_spk, vol);
        (void)esp_codec_dev_set_out_mute(s_spk, vol == 0);
    }
}

static void apply_notch(int notch)
{
    if (notch < 0) {
        notch = 0;
    }
    if (notch > ROOMVOL_SLIDER_MAX) {
        notch = ROOMVOL_SLIDER_MAX;
    }
    s_notch = notch;
    apply_volume(roomvol_codec(notch));
}

static void maybe_pass(void)
{
    if (s_passed || s_tx < 10 || s_rx < 10) {
        return;
    }
    s_passed = true;
    demo_pass("h21");
}

static void on_volume(lv_event_t *e);

static void make_card(lv_obj_t *scr, int y, lv_obj_t **name_out, lv_obj_t **state_out,
                      const char *name, const char *state)
{
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 296, 52);
    lv_obj_set_pos(card, 12, y);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x2A3038), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    lv_obj_t *nm = lv_label_create(card);
    lv_obj_set_width(nm, 272);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_WRAP);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_opa(nm, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 12, 6);

    lv_obj_t *st = lv_label_create(card);
    lv_label_set_text(st, state);
    lv_obj_set_style_text_opa(st, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(st, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 12, -6);

    *name_out = nm;
    *state_out = st;
}

static void paint_idle(void)
{
    char line[12];
    if (!board_lvgl_lock(200)) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "talk");
    lv_obj_set_style_text_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8A9298), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 6);

    s_counts = lv_label_create(scr);
    lv_label_set_text(s_counts, "tx 0   rx 0");
    lv_obj_set_style_text_opa(s_counts, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_counts, lv_color_hex(0x666666), 0);
    lv_obj_align(s_counts, LV_ALIGN_TOP_RIGHT, -12, 6);

    make_card(scr, 26, &s_you_name, &s_you_state, "you", "muted");
    make_card(scr, 84, &s_peer_name_lbl, &s_peer_state, DEMO_PEER_NAME, "waiting");

    lv_obj_t *vol_tag = lv_label_create(scr);
    lv_label_set_text(vol_tag, "vol");
    lv_obj_set_style_text_opa(vol_tag, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(vol_tag, lv_color_hex(0xA8B0B8), 0);
    lv_obj_set_pos(vol_tag, 16, 148);

    s_vol_lab = NULL;
#if ROOMVOL_SHOW_LEVEL
    s_vol_lab = lv_label_create(scr);
    fmt_level(line, sizeof(line), (int)s_volume);
    lv_label_set_text(s_vol_lab, line);
    lv_obj_set_style_text_opa(s_vol_lab, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_vol_lab, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(s_vol_lab, LV_ALIGN_TOP_RIGHT, -16, 148);
#else
    (void)line;
#endif

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, 232, 18);
    lv_obj_set_pos(slider, 48, 176);
    lv_slider_set_range(slider, 0, ROOMVOL_SLIDER_MAX);
    lv_slider_set_value(slider, s_notch, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x2A3038), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x5AA0E8), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xE8F0E8), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, NULL);

    board_lvgl_unlock();
}

static void on_volume(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    apply_notch((int)lv_slider_get_value(slider));
#if ROOMVOL_SHOW_LEVEL
    if (s_vol_lab) {
        char line[12];
        fmt_level(line, sizeof(line), (int)s_volume);
        lv_label_set_text(s_vol_lab, line);
    }
#endif
}

static void refresh_ui(void)
{
    char you[48];
    char peer[48];
    char counts[32];
    snprintf(you, sizeof(you), "%s (you)", s_self_name);
    snprintf(peer, sizeof(peer), "%s", s_peer_name);
    snprintf(counts, sizeof(counts), "tx %d   rx %d", s_tx, s_rx);

    if (!board_lvgl_lock(200)) {
        return;
    }
    if (s_you_name) {
        lv_label_set_text(s_you_name, you);
    }
    if (s_you_state) {
        lv_label_set_text(s_you_state, s_open ? "talking" : "muted");
        lv_obj_set_style_text_color(s_you_state,
                                    lv_color_hex(s_open ? 0x7DCC7A : 0xCC8844), 0);
    }
    if (s_peer_name_lbl) {
        lv_label_set_text(s_peer_name_lbl, peer);
    }
    if (s_peer_state) {
        lv_label_set_text(s_peer_state, peer_label());
        lv_obj_set_style_text_color(s_peer_state, lv_color_hex(peer_color()), 0);
    }
    if (s_counts) {
        lv_label_set_text(s_counts, counts);
    }
#if ROOMVOL_SHOW_LEVEL
    if (s_vol_lab) {
        char line[12];
        fmt_level(line, sizeof(line), (int)s_volume);
        lv_label_set_text(s_vol_lab, line);
    }
#endif
    board_lvgl_unlock();
    s_ui_dirty = false;
}

static void apply_mute(bool muted)
{
    bool open = !muted;
    if (s_open == open) {
        return;
    }
    s_open = open;
    s_need_status = true;
    s_ui_dirty = true;
    ESP_LOGI(TAG, "%s", s_open ? "open -> sending" : "away -> silent");
}

static void on_mute_down(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    apply_mute(true);
}

static void on_mute_up(void *btn, void *usr)
{
    (void)btn;
    (void)usr;
    apply_mute(false);
}

static void send_json(const char *json)
{
    if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
        return;
    }
    (void)esp_websocket_client_send_text(s_ws, json, (int)strlen(json), pdMS_TO_TICKS(20));
}

static void send_hello(void)
{
    char hello[256];
    snprintf(hello, sizeof(hello),
             "{\"type\":\"hello\",\"device_id\":\"%s\",\"token\":\"%s\",\"available\":%s}",
             DEMO_DEVICE_ID, DEMO_DEVICE_TOKEN, s_open ? "true" : "false");
    send_json(hello);
    s_need_hello = false;
}

static void send_status(void)
{
    char json[48];
    snprintf(json, sizeof(json), "{\"type\":\"status\",\"available\":%s}",
             s_open ? "true" : "false");
    send_json(json);
    s_need_status = false;
}

static void copy_str(char *dst, size_t cap, cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(v) && v->valuestring && v->valuestring[0]) {
        strncpy(dst, v->valuestring, cap - 1);
        dst[cap - 1] = 0;
    }
}

static void apply_peer(cJSON *j, bool from_hello)
{
    int online = json_tristate(j, from_hello ? "peer_online" : "online");
    if (online < 0) {
        online = json_tristate(j, "peer_online");
    }
    int available = json_tristate(j, from_hello ? "peer_available" : "available");
    if (available < 0) {
        available = json_tristate(j, "peer_available");
    }
    if (online >= 0) {
        if (online == 1) {
            s_peer_online = 1;
            s_peer_lost_at = 0;
        } else if (s_peer_lost_at == 0) {
            s_peer_lost_at = xTaskGetTickCount();
        }
    }
    if (available >= 0 && s_peer_online != 0) {
        s_peer_open = available;
        if (s_peer_online < 0) {
            s_peer_online = 1;
        }
    }
    copy_str(s_peer_name, sizeof(s_peer_name), j, "peer_name");
    s_ui_dirty = true;
}

static void on_text(const char *s, int n)
{
    char tmp[512];
    if (n >= (int)sizeof(tmp)) {
        n = (int)sizeof(tmp) - 1;
    }
    memcpy(tmp, s, n);
    tmp[n] = 0;
    ESP_LOGI(TAG, "ws %s", tmp);
    cJSON *j = cJSON_Parse(tmp);
    if (!j) {
        return;
    }
    cJSON *type = cJSON_GetObjectItem(j, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    if (strcmp(t, "hello_ok") == 0) {
        copy_str(s_self_name, sizeof(s_self_name), j, "name");
        apply_peer(j, true);
        s_need_status = true;
    } else if (strcmp(t, "peer_status") == 0 || strcmp(t, "peer_online") == 0) {
        apply_peer(j, false);
    }
    cJSON_Delete(j);
}

static void on_ws(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_websocket_event_data_t *ev = data;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        s_link_down = false;
        s_need_hello = true;
        return;
    }
    if (id == WEBSOCKET_EVENT_DISCONNECTED) {
        if (!s_link_down) {
            s_link_lost_at = xTaskGetTickCount();
        }
        s_link_down = true;
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || ev == NULL) {
        return;
    }
    if ((ev->op_code == 0x02 || (ev->op_code == 0x00 && s_bin_n > 0)) && ev->data_ptr &&
        ev->data_len > 0) {
        if (ev->payload_offset == 0) {
            s_bin_n = 0;
        }
        int room = CHUNK - s_bin_n;
        int n = ev->data_len < room ? ev->data_len : room;
        if (n > 0) {
            memcpy(s_bin + s_bin_n, ev->data_ptr, n);
            s_bin_n += n;
        }
        int got = ev->payload_offset + ev->data_len;
        bool done = (ev->payload_len <= 0) || (got >= (int)ev->payload_len);
        if (done) {
            if (s_bin_n == CHUNK && s_playq) {
                s_rx++;
                if (s_peer_online != 1) {
                    s_peer_online = 1;
                    s_ui_dirty = true;
                }
                if (xQueueSend(s_playq, s_bin, 0) != pdTRUE) {
                    (void)xQueueReceive(s_playq, s_drop, 0);
                    (void)xQueueSend(s_playq, s_bin, 0);
                }
                maybe_pass();
                if ((s_rx % 25) == 0) {
                    s_ui_dirty = true;
                }
            }
            s_bin_n = 0;
        }
        return;
    }
    bool text = (ev->op_code == 0x01) || (ev->op_code == 0x00 && s_txt_n > 0);
    if (!text || ev->data_ptr == NULL || ev->data_len <= 0) {
        return;
    }
    if (ev->payload_offset == 0) {
        s_txt_n = 0;
    }
    int room = (int)sizeof(s_txt) - 1 - s_txt_n;
    int n = ev->data_len < room ? ev->data_len : room;
    if (n > 0) {
        memcpy(s_txt + s_txt_n, ev->data_ptr, n);
        s_txt_n += n;
    }
    int got = ev->payload_offset + ev->data_len;
    bool done = (ev->payload_len <= 0) || (got >= (int)ev->payload_len);
    if (done && s_txt_n > 0) {
        on_text(s_txt, s_txt_n);
        s_txt_n = 0;
    }
}

static void play_task(void *arg)
{
    (void)arg;
    uint8_t frame[CHUNK];
    while (1) {
        if (xQueueReceive(s_playq, frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_spk_open) {
            (void)esp_codec_dev_write(s_spk, frame, CHUNK);
        }
    }
}

static void talk_task(void *arg)
{
    (void)arg;
    uint8_t chunk[CHUNK];
    while (1) {
        if (!s_open) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (esp_codec_dev_open(s_mic, &s_fs) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        (void)esp_codec_dev_set_in_mute(s_mic, false);
        (void)esp_codec_dev_set_in_gain(s_mic, MIC_GAIN_DB);
        int n = 0;
        while (s_open) {
            if (esp_codec_dev_read(s_mic, chunk, CHUNK) != ESP_CODEC_DEV_OK) {
                continue;
            }
            if (!s_ws || !esp_websocket_client_is_connected(s_ws)) {
                continue;
            }
            if (esp_websocket_client_send_bin(s_ws, (const char *)chunk, CHUNK, pdMS_TO_TICKS(10)) <
                0) {
                continue;
            }
            s_tx++;
            maybe_pass();
            if ((++n % 50) == 0) {
                s_ui_dirty = true;
            }
        }
        (void)esp_codec_dev_close(s_mic);
        s_ui_dirty = true;
    }
}

void app_main(void)
{
    if (board_display_start() == ESP_OK) {
        board_status_set("h21 wifi...");
    }
    if (wifi_sta_join(DEMO_WIFI_SSID, DEMO_WIFI_PASS, 25000) != ESP_OK) {
        demo_fail("h21", "wifi");
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (s_mic == NULL || s_spk == NULL) {
        demo_fail("h21", "codec");
        return;
    }
    apply_notch(ROOMVOL_DEFAULT_NOTCH);
    if (esp_codec_dev_open(s_spk, &s_fs) == ESP_OK) {
        apply_volume((int)s_volume);
        s_spk_open = true;
    }

    button_handle_t btns[BSP_BUTTON_NUM] = {0};
    esp_err_t err = bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "bsp_iot_button_create: %s", esp_err_to_name(err));
    }
    if (btns[BSP_BUTTON_MUTE]) {
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_DOWN, NULL, on_mute_down, NULL);
        iot_button_register_cb(btns[BSP_BUTTON_MUTE], BUTTON_PRESS_UP, NULL, on_mute_up, NULL);
    }

    paint_idle();
    board_backlight_set(70);
    apply_mute(mute_latched());
    who_str(s_self_name, sizeof(s_self_name), DEMO_DEVICE_NAME);
    who_str(s_peer_name, sizeof(s_peer_name), DEMO_PEER_NAME);
    s_need_status = true;
    s_ui_dirty = true;
    refresh_ui();

    s_playq = xQueueCreate(PLAY_Q, CHUNK);
    if (s_playq == NULL) {
        demo_fail("h21", "queue");
        return;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%d/v1/ws", DEMO_SERVER_HOST, DEMO_SERVER_PORT);
    esp_websocket_client_config_t cfg = {
        .uri = uri,
        .buffer_size = 4096,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 2000,
        .network_timeout_ms = 10000,
        .disable_pingpong_discon = true,
        .ping_interval_sec = 30,
        .keep_alive_enable = true,
    };
    s_ws = esp_websocket_client_init(&cfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, on_ws, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        demo_fail("h21", "ws start");
        return;
    }

    xTaskCreate(play_task, "play", 6144, NULL, 6, NULL);
    xTaskCreate(talk_task, "talk", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "who id=%s name=%s friend=%s  uri=%s  vol=%d", DEMO_DEVICE_ID, DEMO_DEVICE_NAME,
             DEMO_PEER_NAME, uri, (int)s_volume);

    TickType_t last_status = xTaskGetTickCount();
    while (1) {
        apply_mute(mute_latched());
        TickType_t now = xTaskGetTickCount();
        bool due = (now - last_status) >= pdMS_TO_TICKS(STATUS_PERIOD_MS);
        if (s_link_down && s_peer_online != 0 &&
            (now - s_link_lost_at) >= pdMS_TO_TICKS(LINK_WAIT_MS)) {
            s_peer_online = 0;
            s_peer_open = -1;
            s_ui_dirty = true;
        }
        if (s_peer_lost_at != 0 && s_peer_online != 0 &&
            (now - s_peer_lost_at) >= pdMS_TO_TICKS(LINK_WAIT_MS)) {
            s_peer_online = 0;
            s_peer_open = -1;
            s_peer_lost_at = 0;
            s_ui_dirty = true;
        }
        if (s_ws && esp_websocket_client_is_connected(s_ws)) {
            if (s_need_hello) {
                send_hello();
            } else if (s_need_status || due) {
                send_status();
                last_status = now;
            }
        }
        if (s_ui_dirty) {
            refresh_ui();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
