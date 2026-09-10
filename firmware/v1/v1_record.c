#include "v1_record.h"

#include "v1_api.h"
#include "v1_carousel.h"
#include "v1_connect.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "http_bearer.h"
#include "who.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "v1_record";

#define CHUNK 640

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = V1_SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static v1_record_cfg_t s_cfg;
static char *s_session;
static char s_send_to[16];
static bool s_send_all;
static volatile bool s_stop_record;
static volatile bool s_cancel_pick;
static int64_t s_pick_open_us;
static uint8_t *s_pcm;
static lv_obj_t *s_overlay;

static void record_task_fn(void *arg);
static void paint_pick(lv_obj_t *scr);
static void paint_record_overlay(lv_obj_t *scr);

static int64_t now_us(void) { return esp_timer_get_time(); }

static int16_t pcm_peak(const uint8_t *p, size_t n)
{
    int16_t peak = 0;
    const int16_t *s = (const int16_t *)p;
    for (size_t i = 0; i < n / 2; i++) {
        int16_t a = s[i];
        if (a < 0) {
            a = -a;
        }
        if (a > peak) {
            peak = a;
        }
    }
    return peak;
}

static bool mute_latched(void)
{
    return gpio_get_level(BSP_MUTE_STATUS) == 0;
}

void v1_record_init(const v1_record_cfg_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
        s_session = cfg->session_user;
    }
}

void v1_record_start_task(void)
{
    xTaskCreate(record_task_fn, "rec", 8192, NULL, 5, NULL);
}

void v1_record_paint_pick(lv_obj_t *scr) { paint_pick(scr); }
void v1_record_paint_overlay(lv_obj_t *scr) { paint_record_overlay(scr); }

void v1_record_on_circle_stop(void) { s_stop_record = true; }

void v1_record_on_shoulder_cancel(void)
{
    s_cancel_pick = true;
    s_stop_record = true;
    if (s_cfg.play_chirp_pair) {
        s_cfg.play_chirp_pair(523, 392);
    }
    if (v1_state_get() == ST_PICK) {
        v1_state_post_goto(ST_CAROUSEL);
        v1_ui_request_repaint();
    }
}

void v1_record_set_send_all(bool all) { s_send_all = all; }

void v1_record_set_send_to(const char *user_id)
{
    if (user_id) {
        strncpy(s_send_to, user_id, sizeof(s_send_to) - 1);
    }
}

void v1_record_open_pick(int64_t t_us)
{
    s_cancel_pick = false;
    s_pick_open_us = t_us;
    v1_state_post_goto(ST_PICK);
    v1_ui_request_repaint();
}

bool v1_record_tick_pick_timeout(int64_t now)
{
    if (v1_state_get() != ST_PICK || s_pick_open_us <= 0) {
        return false;
    }
    if ((now - s_pick_open_us) <= (int64_t)V1_UI_PICK_TIMEOUT_MS * 1000) {
        return false;
    }
    s_pick_open_us = 0;
    if (s_cfg.play_chirp_pair) {
        s_cfg.play_chirp_pair(523, 392);
    }
    v1_state_post_goto(ST_CAROUSEL);
    v1_ui_request_repaint();
    return true;
}

void v1_record_give_work(void)
{
    if (s_cfg.work_sem) {
        xSemaphoreGive(s_cfg.work_sem);
    }
}

static void wav_header(uint8_t *p, uint32_t pcm_bytes)
{
    uint32_t rate = V1_SAMPLE_RATE;
    uint16_t ch = 1, bps = 16, audio = 1, block = 2;
    uint32_t riff = 36 + pcm_bytes;
    uint32_t fmt = 16;
    uint32_t byte_rate = rate * 2;
    memcpy(p, "RIFF", 4);
    memcpy(p + 4, &riff, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    memcpy(p + 16, &fmt, 4);
    memcpy(p + 20, &audio, 2);
    memcpy(p + 22, &ch, 2);
    memcpy(p + 24, &rate, 4);
    memcpy(p + 28, &byte_rate, 4);
    memcpy(p + 32, &block, 2);
    memcpy(p + 34, &bps, 2);
    memcpy(p + 36, "data", 4);
    memcpy(p + 40, &pcm_bytes, 4);
}

static int post_wav(const uint8_t *wav, int wav_len, bool broadcast, const char *to_user)
{
    char url[128];
    v1_api_format_url(url, sizeof(url), "/v1/messages");
    static const char *bnd = "----FamilyLinkX02";
    char pre[512];
    int pre_len;
    if (broadcast) {
        pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"broadcast\"\r\n\r\n"
                           "true\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; "
                           "filename=\"clip.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
                           bnd, bnd, bnd);
    } else {
        pre_len = snprintf(pre, sizeof(pre),
                           "--%s\r\nContent-Disposition: form-data; name=\"kind\"\r\n\r\n"
                           "audio\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"to_user_id\"\r\n\r\n"
                           "%s\r\n"
                           "--%s\r\nContent-Disposition: form-data; name=\"blob\"; "
                           "filename=\"clip.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
                           bnd, bnd, to_user, bnd);
    }
    char post[64];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000 };
    v1_api_apply_tls(&cfg);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "X-User-Id", s_session);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(c, "Content-Type", ctype);
    if (esp_http_client_open(c, pre_len + wav_len + post_len) != ESP_OK) {
        esp_http_client_cleanup(c);
        return -1;
    }
    esp_http_client_write(c, pre, pre_len);
    esp_http_client_write(c, (const char *)wav, wav_len);
    esp_http_client_write(c, post, post_len);
    (void)esp_http_client_fetch_headers(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return st;
}

static size_t record_take(size_t cap)
{
    size_t n = 0;
    int64_t last_talk = now_us();
    const size_t trim_bytes = (V1_RECORD_TRIM_MS * V1_SAMPLE_RATE * 2) / 1000;
    const size_t max_pcm = cap - 44;

    if (mute_latched()) {
        return 0;
    }
    if (esp_codec_dev_open(s_cfg.mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_cfg.mic, false);
    (void)esp_codec_dev_set_in_gain(s_cfg.mic, 42.0f);
    vTaskDelay(pdMS_TO_TICKS(V1_RECORD_TRIM_MS));

    while (n + CHUNK <= max_pcm) {
        if (s_stop_record || s_cancel_pick) {
            break;
        }
        if (esp_codec_dev_read(s_cfg.mic, s_pcm + 44 + n, CHUNK) != ESP_CODEC_DEV_OK) {
            break;
        }
        int16_t pk = pcm_peak(s_pcm + 44 + n, CHUNK);
        if (pk >= V1_TALK_PEAK) {
            last_talk = now_us();
        } else if (pk < 64 && (now_us() - last_talk) > 500000) {
            break;
        }
        n += CHUNK;
        if ((now_us() - last_talk) > (int64_t)V1_RECORD_SILENCE_SEC * 1000000) {
            break;
        }
        if (n >= (size_t)V1_RECORD_MAX_SEC * V1_SAMPLE_RATE * 2) {
            break;
        }
    }
    (void)esp_codec_dev_close(s_cfg.mic);
    if (n <= trim_bytes) {
        return 0;
    }
    memmove(s_pcm + 44, s_pcm + 44 + trim_bytes, n - trim_bytes);
    return n - trim_bytes;
}


static bool pcm_buffer_ready(void)
{
    if (s_pcm) {
        return true;
    }
    size_t pcm_cap = 44 + (size_t)V1_RECORD_MAX_SEC * V1_SAMPLE_RATE * 2;
    s_pcm = heap_caps_malloc(pcm_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm) {
        s_pcm = heap_caps_malloc(pcm_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_pcm) {
        ESP_LOGE(TAG, "record buffer alloc failed (%u bytes)", (unsigned)pcm_cap);
    }
    return s_pcm != NULL;
}


static void record_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_cfg.work_sem, portMAX_DELAY);
        if (v1_state_get() != ST_RECORD) {
            continue;
        }
        if (!v1_connect_online()) {
            v1_ui_set_toast(NULL, "can't send right now");
            v1_state_post_goto(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (!s_cfg.mic || !pcm_buffer_ready()) {
            v1_ui_set_toast(NULL, "no mic");
            v1_state_post_goto(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (mute_latched()) {
            v1_ui_set_toast(NULL, "unmute first");
            v1_state_post_goto(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (s_cfg.play_chirp_pair) s_cfg.play_chirp_pair(523, 784);
        s_stop_record = false;
        size_t pcm = record_take(44 + (size_t)V1_RECORD_MAX_SEC * V1_SAMPLE_RATE * 2);
        if (s_cfg.play_chirp_pair) s_cfg.play_chirp_pair(784, 392);
        if (pcm > 0 && !s_cancel_pick) {
            wav_header(s_pcm, (uint32_t)pcm);
            int wav_len = 44 + (int)pcm;
            int st = post_wav(s_pcm, wav_len, s_send_all, s_send_to);
            ESP_LOGI(TAG, "upload status %d", st);
            if (st == 200) {
                int keep = -1;
                msg_t *fm = v1_carousel_focus_msg(); /* inbox focus preserved */
                if (fm) {
                    keep = fm->seq;
                }
                if (v1_api_reload_inbox(s_session) && keep > 0) {
                    for (int i = 0; i < v1_carousel_msg_n(); i++) {
                        if (v1_carousel_msgs()[i].seq == keep) {
                            v1_carousel_set_focus_idx(i);
                            break;
                        }
                    }
                }
                v1_ui_set_toast(NULL, "sent");
            } else {
                v1_connect_mark_offline();
                v1_ui_set_toast(NULL, "couldn't send");
            }
        } else if (s_cancel_pick) {
            v1_ui_set_toast(NULL, "cancelled");
        }
        s_cancel_pick = false;
        v1_state_post_goto(ST_CAROUSEL);
        v1_ui_request_repaint();
    }
}

static void on_pick_btn(lv_event_t *e)
{
    if (!v1_connect_online()) {
        v1_ui_set_toast(NULL, "can't send right now");
        v1_state_post_goto(ST_CAROUSEL);
        v1_ui_request_repaint();
        return;
    }
    const char *id = (const char *)lv_event_get_user_data(e);
    s_send_all = (id == NULL);
    if (!s_send_all && id) {
        strncpy(s_send_to, id, sizeof(s_send_to) - 1);
    }
    v1_state_post_goto(ST_RECORD);
    v1_ui_request_repaint();
    v1_record_give_work();
    v1_ui_bump_activity();
}


static void paint_pick(lv_obj_t *scr)
{
    (void)v1_connect_load_hangout();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "send to");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8F0E8), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    int pick_rows = 1;
    for (int i = 0; i < v1_connect_user_count(); i++) {
        if (strcmp(v1_connect_users_mut()[i].id, s_session) != 0) {
            pick_rows++;
        }
    }
    int y0, row_h, row_step, face_sz;
    v1_ui_roster_row_layout(pick_rows, &y0, &row_h, &row_step, &face_sz);
    int face_y = (row_h - face_sz - 4) / 2;
    int label_x = 12 + face_sz + 8;
    int y = pick_rows > 3 ? 22 : 28;
    for (int i = 0; i < v1_connect_user_count(); i++) {
        if (strcmp(v1_connect_users_mut()[i].id, s_session) == 0) {
            continue;
        }
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_pos(b, 16, y);
        lv_obj_set_size(b, 288, row_h);
        lv_obj_set_style_pad_all(b, 0, 0);
        v1_ui_style_list_row(b);
        v1_ui_paint_face_sized(b, i, 8, face_y, face_sz);
        lv_obj_t *t = lv_label_create(b);
        lv_label_set_text(t, v1_connect_users_mut()[i].name);
        v1_ui_style_list_row_label(t);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, label_x, 0);
        lv_obj_add_event_cb(b, on_pick_btn, LV_EVENT_CLICKED, v1_connect_users_mut()[i].id);
        y += row_step;
    }
    lv_obj_t *all = lv_button_create(scr);
    lv_obj_set_pos(all, 16, y);
    lv_obj_set_size(all, 288, row_h);
    lv_obj_set_style_pad_all(all, 0, 0);
    v1_ui_style_list_row(all);
    v1_ui_paint_asterisk_icon(all, 8, face_y);
    lv_obj_t *at = lv_label_create(all);
    lv_label_set_text(at, "Everyone");
    v1_ui_style_list_row_label(at);
    lv_obj_align(at, LV_ALIGN_LEFT_MID, label_x, 0);
    lv_obj_add_event_cb(all, on_pick_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "shoulder or 10s = cancel");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xA8B0B8), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    v1_ui_hook_scr(scr);
}


static void paint_record_overlay(lv_obj_t *scr)
{
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 300, 80);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x2A3038), 0);
    lv_obj_t *t = lv_label_create(s_overlay);
    lv_label_set_text(t, "listening...\ntap circle · shoulder cancel");
    lv_obj_set_style_text_color(t, lv_color_hex(0xE8F0E8), 0);
    lv_obj_center(t);
}


