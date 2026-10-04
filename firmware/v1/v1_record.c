#include "v1_record.h"

#include "v1_api.h"
#include "v1_carousel.h"
#include "v1_connect.h"
#include "v1_sketch.h"
#include "v1_state.h"
#include "v1_timing.h"
#include "v1_ui_common.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "http_bearer.h"
#include "who.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "v1_record";

#define CHUNK 640
#define V1_SEND_STUCK_MS 25000

static esp_codec_dev_sample_info_t s_fs = {
    .sample_rate = V1_SAMPLE_RATE, .channel = 1, .bits_per_sample = 16,
};

static v1_record_cfg_t s_cfg;
static char *s_session;
static char s_send_to[16];
static char s_send_tos[V1_USER_MAX][16];
static int s_send_to_n;
static bool s_send_all;
static volatile bool s_stop_record;
static volatile bool s_cancel_pick;
static volatile bool s_record_armed;
static volatile bool s_recording;
static uint8_t *s_pcm;
static int64_t s_record_t0_us;

/** Picker carousel (same geometry as sign-in roster). */
#define PICK_ITEM_ALL (-1)
static lv_obj_t *s_pick_scroll;
static lv_obj_t *s_pick_cards[V1_USER_MAX + 1];
static int s_pick_user_at[V1_USER_MAX + 1];
static bool s_pick_selected[V1_USER_MAX + 1];
static int s_pick_n;
static int s_pick_focus;
static bool s_pick_scroll_lock;
static int32_t s_pick_snap_target;
static lv_obj_t *s_pick_rec_btn;
static lv_obj_t *s_pick_rec_disk;

/** Recording canvas + sketch capture. */
#define RECORD_CANVAS_W   V1_LCD_W
#define RECORD_CANVAS_H   V1_LCD_H
#define RECORD_FB_BYTES   (RECORD_CANVAS_W * RECORD_CANVAS_H * 2)
#define RECORD_BRUSH      2
#define RECORD_MIN_MOVE   2
#define RECORD_BG_HEX     0x000000
#define RECORD_INK_HEX    0xE8F0E8
#define RECORD_TIMER_HEX  0x788088
#define RECORD_PULSE_MS   600
#define RECORD_DISK_DIM   0x8A2E2E
#define RECORD_DISK_BRT   0xE85A5A
#define RECORD_TIMER_DISK_GAP 8
#define RECORD_TIMER_PAD_R    6

static uint16_t *s_record_fb;
static lv_image_dsc_t s_record_dsc;
static lv_obj_t *s_record_img;
static lv_obj_t *s_record_timer_lab;
static lv_obj_t *s_record_rec_btn;
static lv_obj_t *s_record_rec_disk;
static lv_timer_t *s_record_pulse_timer;
static lv_timer_t *s_record_elapsed_timer;
static bool s_record_disk_on;
static int16_t s_record_last_x;
static int16_t s_record_last_y;
static v1_sketch_ink_t s_record_ink;
static v1_sketch_pt_t *s_sketch_pts;
static int s_sketch_n;
static uint8_t *s_sketch_blob;

typedef enum {
    SEND_PHASE_FINISHING,
    SEND_PHASE_SENDING,
    SEND_PHASE_SENT,
    SEND_PHASE_FAILED,
} send_phase_t;

static volatile send_phase_t s_send_phase;
static int64_t s_send_enter_us;

static TaskHandle_t s_record_task;

static void record_task_fn(void *arg);
static void paint_pick(lv_obj_t *scr);
static void paint_record(lv_obj_t *scr);
static void paint_send(lv_obj_t *scr);
static void send_set_phase(send_phase_t ph);
static void send_enter_finishing(void);
static void record_abort_mic(void);
static void pick_begin_recording(void);
static bool pick_has_selection(void);
static void pick_refresh_rec_btn(void);
static void pick_refresh_selection_borders(void);
static bool pick_scroll_aligned_to_focus(void);
static void pick_scroll_x_exec(void *obj, int32_t v);
static bool sketch_buffers_ready(void);

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

static uint16_t rgb565(uint32_t hex)
{
    unsigned r = (hex >> 16) & 0xFFu;
    unsigned g = (hex >> 8) & 0xFFu;
    unsigned b = hex & 0xFFu;
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static bool sketch_buffers_ready(void)
{
    if (!s_record_fb) {
        s_record_fb = heap_caps_malloc(RECORD_FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_record_fb) {
            s_record_fb = heap_caps_malloc(RECORD_FB_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_sketch_pts) {
        s_sketch_pts = heap_caps_malloc(sizeof(v1_sketch_pt_t) * V1_SKETCH_MAX_POINTS,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sketch_pts) {
            s_sketch_pts = heap_caps_malloc(sizeof(v1_sketch_pt_t) * V1_SKETCH_MAX_POINTS,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_sketch_blob) {
        size_t cap = sizeof(v1_sketch_hdr_t) + sizeof(v1_sketch_pt_t) * V1_SKETCH_MAX_POINTS;
        s_sketch_blob = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_sketch_blob) {
            s_sketch_blob = heap_caps_malloc(cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_record_fb || !s_sketch_pts || !s_sketch_blob) {
        ESP_LOGE(TAG, "record sketch alloc failed fb=%p pts=%p blob=%p",
                 (void *)s_record_fb, (void *)s_sketch_pts, (void *)s_sketch_blob);
        return false;
    }
    return true;
}

static bool record_screen_active(void)
{
    return v1_state_get() == ST_RECORD && s_record_t0_us > 0;
}

static void record_refresh_elapsed_label(void)
{
    if (!s_record_timer_lab || !record_screen_active()) {
        return;
    }
    int64_t dt = (now_us() - s_record_t0_us) / 1000000;
    int sec = dt > 0 ? (int)dt : 0;
    char txt[16];
    snprintf(txt, sizeof(txt), " %d:%02d", sec / 60, sec % 60);
    lv_label_set_text(s_record_timer_lab, txt);
}

static void record_set_disk_color(uint32_t hex)
{
    if (s_record_rec_disk) {
        lv_obj_set_style_bg_color(s_record_rec_disk, lv_color_hex(hex), 0);
    }
}

static void record_ui_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_record_rec_disk || !record_screen_active()) {
        return;
    }
    s_record_disk_on = !s_record_disk_on;
    record_set_disk_color(s_record_disk_on ? RECORD_DISK_BRT : RECORD_DISK_DIM);
}

static void record_elapsed_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (v1_state_get() != ST_RECORD) {
        return;
    }
    record_refresh_elapsed_label();
}

void v1_record_init(const v1_record_cfg_t *cfg)
{
    if (cfg) {
        s_cfg = *cfg;
        s_session = cfg->session_user;
    }
}

static bool record_task_try_create(uint32_t stack_words)
{
    if (s_record_task) {
        eTaskState st = eTaskGetState(s_record_task);
        if (st != eDeleted && st != eInvalid) {
            return true;
        }
        s_record_task = NULL;
    }
    BaseType_t ok = xTaskCreate(record_task_fn, "rec", stack_words, NULL, 7, &s_record_task);
    if (ok != pdPASS || !s_record_task) {
        ESP_LOGE(TAG, "record task create failed stack=%u internal=%u largest=%u",
                 (unsigned)stack_words,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                           MALLOC_CAP_8BIT));
        s_record_task = NULL;
        return false;
    }
    ESP_LOGI(TAG, "record task ready stack=%u internal=%u", (unsigned)stack_words,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return true;
}

void v1_record_start_task(void)
{
    if (s_record_task) {
        eTaskState st = eTaskGetState(s_record_task);
        if (st != eDeleted && st != eInvalid) {
            return;
        }
        s_record_task = NULL;
    }
    if (!record_task_try_create(8192) && !record_task_try_create(6144)) {
        ESP_LOGE(TAG, "record task create failed");
    }
}

void v1_record_paint_pick(lv_obj_t *scr) { paint_pick(scr); }
void v1_record_paint_record(lv_obj_t *scr) { paint_record(scr); }
void v1_record_paint_send(lv_obj_t *scr) { paint_send(scr); }

void v1_record_invalidate_send(void)
{
    s_send_phase = SEND_PHASE_FINISHING;
}

void v1_record_invalidate_pick(void)
{
    if (s_pick_scroll) {
        lv_anim_delete(s_pick_scroll, pick_scroll_x_exec);
    }
    s_pick_scroll = NULL;
    memset(s_pick_cards, 0, sizeof(s_pick_cards));
    memset(s_pick_user_at, 0, sizeof(s_pick_user_at));
    s_pick_n = 0;
    s_pick_focus = 0;
    s_pick_scroll_lock = false;
    s_pick_rec_btn = NULL;
    s_pick_rec_disk = NULL;
}

void v1_record_invalidate_record(void)
{
    s_record_t0_us = 0;
    s_recording = false;
    s_record_img = NULL;
    s_record_timer_lab = NULL;
    s_record_rec_btn = NULL;
    s_record_rec_disk = NULL;
    if (s_record_pulse_timer) {
        lv_timer_delete(s_record_pulse_timer);
        s_record_pulse_timer = NULL;
    }
    if (s_record_elapsed_timer) {
        lv_timer_delete(s_record_elapsed_timer);
        s_record_elapsed_timer = NULL;
    }
}

static void pick_arm_only_focus(void)
{
    memset(s_pick_selected, 0, sizeof(s_pick_selected));
    if (s_pick_focus >= 0 && s_pick_focus < s_pick_n) {
        s_pick_selected[s_pick_focus] = true;
    }
}

static bool pick_arm_focus_if_needed(void)
{
    if (pick_has_selection()) {
        return true;
    }
    if (s_pick_focus < 0 || s_pick_focus >= s_pick_n || !pick_scroll_aligned_to_focus()) {
        return false;
    }
    pick_arm_only_focus();
    pick_refresh_selection_borders();
    pick_refresh_rec_btn();
    return pick_has_selection();
}

void v1_record_on_circle_start(void)
{
    ESP_LOGI(TAG, "circle start st=%d focus=%d selected=%d aligned=%d armed=%d",
             (int)v1_state_get(), s_pick_focus, (int)pick_has_selection(),
             (int)pick_scroll_aligned_to_focus(), (int)s_record_armed);
    if (v1_state_get() != ST_PICK) {
        return;
    }
    if (!pick_arm_focus_if_needed()) {
        return;
    }
    pick_begin_recording();
}

static void record_stop_ui_timers(void)
{
    if (s_record_pulse_timer) {
        lv_timer_delete(s_record_pulse_timer);
        s_record_pulse_timer = NULL;
    }
    if (s_record_elapsed_timer) {
        lv_timer_delete(s_record_elapsed_timer);
        s_record_elapsed_timer = NULL;
    }
}

static void send_set_phase(send_phase_t ph)
{
    s_send_phase = ph;
}

static void send_enter_finishing(void)
{
    send_set_phase(SEND_PHASE_FINISHING);
    s_send_enter_us = now_us();
    record_stop_ui_timers();
    v1_state_apply(ST_SEND);
    v1_ui_request_repaint();
}

void v1_record_on_circle_stop(void)
{
    ESP_LOGI(TAG, "circle stop st=%d recording=%d armed=%d",
             (int)v1_state_get(), (int)s_recording, (int)s_record_armed);
    s_stop_record = true;
    if (!s_recording && v1_state_get() == ST_RECORD) {
        if (!s_record_armed) {
            s_cancel_pick = true;
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
        } else {
            send_enter_finishing();
            v1_record_give_work();
        }
        return;
    }
    if (v1_state_get() == ST_RECORD) {
        send_enter_finishing();
    }
}

void v1_record_on_shoulder_cancel(void)
{
    s_cancel_pick = true;
    s_stop_record = true;
    v1_ui_request_chirp_pair(523, 392);
    state_t st = v1_state_get();
    if (st == ST_PICK || st == ST_RECORD) {
        v1_state_apply(ST_CAROUSEL);
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
    (void)t_us;
    s_cancel_pick = false;
    v1_state_post_goto(ST_PICK);
    v1_ui_request_repaint();
}

bool v1_record_tick_pick_timeout(int64_t now)
{
    (void)now;
    return false;
}

void v1_record_give_work(void)
{
    if (!s_record_task) {
        v1_record_start_task();
    }
    if (s_cfg.work_sem) {
        (void)xSemaphoreGive(s_cfg.work_sem);
    }
    if (s_record_task) {
        xTaskNotifyGive(s_record_task);
    }
}

void v1_record_tick_send(int64_t now)
{
    if (v1_state_get() != ST_SEND) {
        return;
    }
    if (s_send_phase != SEND_PHASE_FINISHING && s_send_phase != SEND_PHASE_SENDING) {
        return;
    }
    if (s_send_enter_us <= 0 ||
        (now - s_send_enter_us) < (int64_t)V1_SEND_STUCK_MS * 1000) {
        return;
    }
    ESP_LOGW(TAG, "send receipt stuck phase=%d — leaving", (int)s_send_phase);
    s_stop_record = true;
    s_record_armed = false;
    s_send_enter_us = 0;
    v1_state_apply(ST_CAROUSEL);
    v1_ui_request_repaint();
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

static void record_abort_mic(void)
{
    if (s_recording && s_cfg.mic) {
        (void)esp_codec_dev_close(s_cfg.mic);
    }
}

static int post_wav(const uint8_t *wav, int wav_len, bool broadcast, const char *to_user,
                    const uint8_t *sketch, int sketch_len)
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
    char sketch_pre[192];
    int sketch_pre_len = 0;
    if (sketch && sketch_len > 0) {
        sketch_pre_len = snprintf(sketch_pre, sizeof(sketch_pre),
                                  "\r\n--%s\r\nContent-Disposition: form-data; name=\"sketch\"; "
                                  "filename=\"sketch.flsk\"\r\n"
                                  "Content-Type: application/octet-stream\r\n\r\n",
                                  bnd);
    }
    char post[64];
    int post_len = snprintf(post, sizeof(post), "\r\n--%s--\r\n", bnd);
    int total_len = pre_len + wav_len + sketch_pre_len + sketch_len + post_len;
    if (total_len <= 0) {
        return -1;
    }
    uint8_t *body = heap_caps_malloc((size_t)total_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        body = heap_caps_malloc((size_t)total_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!body) {
        ESP_LOGE(TAG, "post body alloc failed (%d bytes)", total_len);
        return -1;
    }
    int pos = 0;
    memcpy(body + pos, pre, (size_t)pre_len);
    pos += pre_len;
    memcpy(body + pos, wav, (size_t)wav_len);
    pos += wav_len;
    if (sketch_pre_len > 0 && sketch_len > 0) {
        memcpy(body + pos, sketch_pre, (size_t)sketch_pre_len);
        pos += sketch_pre_len;
        memcpy(body + pos, sketch, (size_t)sketch_len);
        pos += sketch_len;
    }
    memcpy(body + pos, post, (size_t)post_len);
    esp_http_client_config_t cfg = { .url = url, .method = HTTP_METHOD_POST, .timeout_ms = 20000 };
    v1_api_apply_tls(&cfg);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        heap_caps_free(body);
        return -1;
    }
    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEMO_DEVICE_TOKEN);
    esp_http_client_set_header(c, "Authorization", auth);
    esp_http_client_set_header(c, "X-User-Id", s_session);
    char ctype[80];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bnd);
    esp_http_client_set_header(c, "Content-Type", ctype);
    esp_http_client_set_post_field(c, (const char *)body, total_len);
    esp_err_t err = esp_http_client_perform(c);
    int st = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    heap_caps_free(body);
    ESP_LOGI(TAG, "post_wav status=%d err=%d (%s)", st, (int)err, esp_err_to_name(err));
    if (st >= 200 && st <= 299) {
        return st;
    }
    return err == ESP_OK ? st : -1;
}

static size_t record_take(size_t cap)
{
    size_t n = 0;
    int64_t last_talk = now_us();
    const size_t trim_bytes = (V1_RECORD_TRIM_MS * V1_SAMPLE_RATE * 2) / 1000;
    const size_t max_pcm = cap - 44;

    if (mute_latched() || s_stop_record || s_cancel_pick) {
        return 0;
    }
    if (esp_codec_dev_open(s_cfg.mic, &s_fs) != ESP_OK) {
        return 0;
    }
    (void)esp_codec_dev_set_in_mute(s_cfg.mic, false);
    (void)esp_codec_dev_set_in_gain(s_cfg.mic, 42.0f);
    for (int trim_wait = 0; trim_wait < V1_RECORD_TRIM_MS && !s_stop_record && !s_cancel_pick;
         trim_wait += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_stop_record || s_cancel_pick) {
        if (s_cfg.mic) {
            (void)esp_codec_dev_close(s_cfg.mic);
        }
        return 0;
    }

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
    if (s_cfg.mic) {
        (void)esp_codec_dev_close(s_cfg.mic);
    }
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
        if (s_cfg.work_sem) {
            (void)xSemaphoreTake(s_cfg.work_sem, portMAX_DELAY);
        } else if (s_record_task) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        (void)ulTaskNotifyTake(pdTRUE, 0);
        if (!s_record_armed) {
            ESP_LOGW(TAG, "record wake without arm (st=%d)", (int)v1_state_get());
            continue;
        }
        state_t wake_st = v1_state_get();
        if (wake_st != ST_RECORD && wake_st != ST_SEND) {
            s_record_armed = false;
            ESP_LOGW(TAG, "record wake st=%d (expected RECORD/SEND) — recovering",
                     (int)wake_st);
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        s_record_armed = false;
        if (!v1_connect_online()) {
            v1_carousel_queue_toast("can't send right now");
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (!s_cfg.mic || !pcm_buffer_ready()) {
            v1_carousel_queue_toast("no mic");
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (mute_latched()) {
            v1_carousel_queue_toast("unmute first");
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        if (s_cancel_pick) {
            s_cancel_pick = false;
            s_stop_record = false;
            ESP_LOGI(TAG, "record cancelled before capture");
            v1_state_apply(ST_CAROUSEL);
            v1_ui_request_repaint();
            continue;
        }
        ESP_LOGI(TAG, "record start (stop=%d st=%d)", (int)s_stop_record, (int)v1_state_get());
        v1_ui_request_chirp_pair(523, 784);
        s_recording = true;
        size_t pcm = record_take(44 + (size_t)V1_RECORD_MAX_SEC * V1_SAMPLE_RATE * 2);
        s_recording = false;
        v1_ui_request_chirp_pair(784, 392);
        s_stop_record = false;
        if (v1_state_get() == ST_RECORD) {
            send_enter_finishing();
        }
        if (pcm > 0 && !s_cancel_pick) {
            send_set_phase(SEND_PHASE_SENDING);
            v1_ui_request_repaint();
            wav_header(s_pcm, (uint32_t)pcm);
            int wav_len = 44 + (int)pcm;
            int sketch_len = 0;
            if (s_sketch_n > 0 && s_sketch_blob) {
                sketch_len = (int)v1_sketch_pack(
                    s_sketch_blob,
                    sizeof(v1_sketch_hdr_t) + sizeof(v1_sketch_pt_t) * V1_SKETCH_MAX_POINTS,
                    s_sketch_pts, (size_t)s_sketch_n);
                if (sketch_len <= 0) {
                    ESP_LOGW(TAG, "sketch pack failed points=%d", s_sketch_n);
                }
            }
            int st = 0;
            if (s_send_all) {
                st = post_wav(s_pcm, wav_len, true, NULL, s_sketch_blob, sketch_len);
                ESP_LOGI(TAG, "upload broadcast status %d", st);
            } else if (s_send_to_n > 0) {
                for (int i = 0; i < s_send_to_n; i++) {
                    st = post_wav(s_pcm, wav_len, false, s_send_tos[i], s_sketch_blob,
                                  sketch_len);
                    ESP_LOGI(TAG, "upload to %s status %d", s_send_tos[i], st);
                    if (st != 200) {
                        break;
                    }
                    v1_connect_note_send_recipient(s_send_tos[i]);
                }
            } else {
                ESP_LOGW(TAG, "upload skipped: no recipients armed");
                st = -1;
            }
            if (st == 200) {
                int keep = -1;
                msg_t *fm = v1_carousel_focus_msg();
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
                send_set_phase(SEND_PHASE_SENT);
            } else {
                send_set_phase(SEND_PHASE_FAILED);
            }
            v1_ui_request_repaint();
            vTaskDelay(pdMS_TO_TICKS(V1_SEND_RECEIPT_MS));
        } else if (s_cancel_pick) {
            v1_carousel_queue_toast("cancelled");
        } else if (pcm == 0) {
            send_set_phase(SEND_PHASE_FAILED);
            v1_ui_request_repaint();
            vTaskDelay(pdMS_TO_TICKS(V1_SEND_RECEIPT_MS));
        }
        s_cancel_pick = false;
        ESP_LOGI(TAG, "record stack free=%u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        v1_state_apply(ST_CAROUSEL);
        v1_ui_request_repaint();
    }
}

static bool pick_has_selection(void)
{
    for (int i = 0; i < s_pick_n; i++) {
        if (s_pick_selected[i]) {
            return true;
        }
    }
    return false;
}

static void pick_refresh_rec_btn(void)
{
    if (!s_pick_rec_btn || !s_pick_rec_disk) {
        return;
    }
    bool on = pick_has_selection();
    uint32_t col = on ? (uint32_t)V1_UI_ERROR : 0x6A7078;
    lv_obj_set_style_bg_color(s_pick_rec_disk, lv_color_hex(col), 0);
    if (on) {
        lv_obj_add_flag(s_pick_rec_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(s_pick_rec_btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_remove_flag(s_pick_rec_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_opa(s_pick_rec_btn, LV_OPA_70, 0);
    }
}

static void pick_refresh_selection_borders(void)
{
    for (int i = 0; i < s_pick_n; i++) {
        lv_obj_t *card = s_pick_cards[i];
        if (!card) {
            continue;
        }
        if (s_pick_selected[i]) {
            lv_obj_set_style_border_width(card, 2, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(V1_UI_ACCENT), 0);
        } else {
            lv_obj_set_style_border_width(card, 0, 0);
        }
    }
}

static void pick_arm_send_targets(void)
{
    s_send_all = false;
    s_send_to_n = 0;
    s_send_to[0] = 0;
    for (int i = 0; i < s_pick_n; i++) {
        if (!s_pick_selected[i]) {
            continue;
        }
        if (s_pick_user_at[i] == PICK_ITEM_ALL) {
            s_send_all = true;
            s_send_to_n = 0;
            return;
        }
        int uidx = s_pick_user_at[i];
        if (uidx < 0 || uidx >= v1_connect_user_count()) {
            continue;
        }
        const char *id = v1_connect_users_mut()[uidx].id;
        if (s_send_to_n < V1_USER_MAX) {
            strncpy(s_send_tos[s_send_to_n], id, sizeof(s_send_tos[0]) - 1);
            s_send_tos[s_send_to_n][sizeof(s_send_tos[0]) - 1] = 0;
            s_send_to_n++;
            strncpy(s_send_to, id, sizeof(s_send_to) - 1);
        }
    }
}

static void pick_begin_recording(void)
{
    state_t st = v1_state_get();
    ESP_LOGI(TAG, "pick begin st=%d selected=%d armed=%d online=%d",
             (int)st, (int)pick_has_selection(), (int)s_record_armed,
             (int)v1_connect_online());
    if (st == ST_RECORD || st == ST_SEND || s_record_armed) {
        return;
    }
    if (!v1_connect_online()) {
        v1_carousel_queue_toast("can't send right now");
        v1_state_apply(ST_CAROUSEL);
        v1_ui_request_repaint();
        return;
    }
    if (!pick_has_selection()) {
        return;
    }
    if (mute_latched()) {
        v1_carousel_queue_toast("unmute first");
        return;
    }
    pick_arm_send_targets();
    ESP_LOGI(TAG, "pick_begin recip=%d all=%d", s_send_to_n, (int)s_send_all);
    s_cancel_pick = false;
    s_stop_record = false;
    s_record_armed = true;
    v1_state_apply(ST_RECORD);
    v1_ui_request_repaint();
    v1_record_give_work();
    v1_ui_bump_activity();
}

static void on_pick_rec_btn(lv_event_t *e)
{
    (void)e;
    if (!pick_arm_focus_if_needed()) {
        return;
    }
    pick_begin_recording();
}

static void apply_pick_card_grad(lv_obj_t *card)
{
    static const struct {
        uint8_t id;
        uint32_t top;
        uint32_t bot;
    } grads[V1_CARD_GRAD_N] = {
        {1, 0x5AA0E8, 0x101418},
        {2, 0xE85A5A, 0xE8C040},
        {5, 0xF0F2F5, 0x8898A8},
    };
    uint8_t id = v1_carousel_card_grad();
    const uint32_t *top = &grads[0].top;
    const uint32_t *bot = &grads[0].bot;
    for (int i = 0; i < V1_CARD_GRAD_N; i++) {
        if (grads[i].id == id) {
            top = &grads[i].top;
            bot = &grads[i].bot;
            break;
        }
    }
    lv_obj_set_style_bg_color(card, lv_color_hex(*top), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(*bot), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
}

static int32_t pick_snap_target_x(lv_obj_t *card, lv_obj_t *scroller)
{
    int32_t card_x = lv_obj_get_x(card);
    int32_t card_w = lv_obj_get_width(card);
    int32_t view_w = lv_obj_get_width(scroller);
    int32_t target = card_x + card_w / 2 - view_w / 2;
    int32_t content_right = 0;
    uint32_t n = lv_obj_get_child_cnt(scroller);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(scroller, i);
        int32_t right = lv_obj_get_x(ch) + lv_obj_get_width(ch);
        if (right > content_right) {
            content_right = right;
        }
    }
    int32_t max_x = content_right - view_w;
    if (max_x < 0) {
        max_x = 0;
    }
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    return target;
}

static int pick_center_index(void)
{
    if (!s_pick_scroll || s_pick_n <= 0) {
        return s_pick_focus;
    }
    int32_t mid = lv_obj_get_scroll_x(s_pick_scroll) + lv_obj_get_width(s_pick_scroll) / 2;
    int best = 0;
    int32_t best_dist = INT32_MAX;
    for (int i = 0; i < s_pick_n; i++) {
        lv_obj_t *ch = s_pick_cards[i];
        if (!ch) {
            continue;
        }
        int32_t center = lv_obj_get_x(ch) + lv_obj_get_width(ch) / 2;
        int32_t dist = center > mid ? center - mid : mid - center;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

static bool pick_scroll_aligned_to_focus(void)
{
    if (s_pick_focus < 0 || s_pick_focus >= s_pick_n) {
        return false;
    }
    lv_obj_t *card = s_pick_cards[s_pick_focus];
    if (!card || !s_pick_scroll) {
        return false;
    }
    int32_t target = pick_snap_target_x(card, s_pick_scroll);
    int32_t cur = lv_obj_get_scroll_x(s_pick_scroll);
    int32_t d = cur > target ? cur - target : target - cur;
    return d <= 1;
}

static void pick_refresh_visuals(void)
{
    if (!s_pick_scroll || s_pick_n <= 0) {
        return;
    }
    int visual = s_pick_scroll_lock || pick_scroll_aligned_to_focus() ? s_pick_focus
                                                                    : pick_center_index();
    for (int i = 0; i < s_pick_n; i++) {
        lv_obj_t *card = s_pick_cards[i];
        if (!card) {
            continue;
        }
        lv_obj_set_style_opa(card, i == visual ? LV_OPA_COVER : LV_OPA_50, 0);
    }
}

static void pick_scroll_x_exec(void *obj, int32_t v)
{
    lv_obj_scroll_to_x((lv_obj_t *)obj, v, LV_ANIM_OFF);
    if (v1_state_get() == ST_PICK) {
        pick_refresh_visuals();
    }
}

static void pick_snap_anim_done(lv_anim_t *a)
{
    lv_obj_t *scroller = (lv_obj_t *)lv_anim_get_user_data(a);
    if (scroller != NULL) {
        lv_obj_scroll_to_x(scroller, s_pick_snap_target, LV_ANIM_OFF);
    }
    s_pick_scroll_lock = false;
    pick_refresh_visuals();
}

static void pick_snap_scroll_to(lv_obj_t *scroller, int32_t target)
{
    int32_t start = lv_obj_get_scroll_x(scroller);
    lv_anim_delete(scroller, pick_scroll_x_exec);
    s_pick_snap_target = target;
    s_pick_scroll_lock = true;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, scroller);
    lv_anim_set_user_data(&a, scroller);
    int32_t delta = target > start ? target - start : start - target;
    int duration = V1_CAROUSEL_SNAP_MS_MIN + (int)(delta * 2 / 5);
    if (duration > V1_CAROUSEL_SNAP_MS_MAX) {
        duration = V1_CAROUSEL_SNAP_MS_MAX;
    }
    lv_anim_set_values(&a, start, target);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_exec_cb(&a, pick_scroll_x_exec);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_completed_cb(&a, pick_snap_anim_done);
    lv_anim_start(&a);
}

static void pick_snap_to_focus(void)
{
    if (s_pick_focus < 0 || s_pick_focus >= s_pick_n) {
        return;
    }
    lv_obj_t *card = s_pick_cards[s_pick_focus];
    if (!card || !s_pick_scroll) {
        return;
    }
    int32_t target = pick_snap_target_x(card, s_pick_scroll);
    if (lv_obj_get_scroll_x(s_pick_scroll) == target) {
        s_pick_scroll_lock = false;
        pick_refresh_visuals();
        return;
    }
    pick_snap_scroll_to(s_pick_scroll, target);
}

static void on_pick_scroll(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_SCROLL) {
        pick_refresh_visuals();
        return;
    }
    if (code == LV_EVENT_SCROLL_END && !s_pick_scroll_lock) {
        ESP_LOGI(TAG, "pick scroll end st=%d focus=%d x=%ld",
                 (int)v1_state_get(), s_pick_focus,
                 s_pick_scroll ? (long)lv_obj_get_scroll_x(s_pick_scroll) : -1L);
        if (pick_scroll_aligned_to_focus()) {
            pick_refresh_visuals();
            return;
        }
        s_pick_focus = pick_center_index();
        pick_snap_to_focus();
    }
}

static void on_pick_card_click(lv_event_t *e)
{
    intptr_t display_idx = (intptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "pick click st=%d idx=%d focus=%d aligned=%d",
             (int)v1_state_get(), (int)display_idx, s_pick_focus,
             (int)pick_scroll_aligned_to_focus());
    if (display_idx < 0 || (int)display_idx >= s_pick_n) {
        return;
    }
    lv_obj_t *card = s_pick_cards[(int)display_idx];
    if (!card || !s_pick_scroll) {
        return;
    }
    if ((int)display_idx != s_pick_focus || !pick_scroll_aligned_to_focus()) {
        s_pick_focus = (int)display_idx;
        pick_snap_scroll_to(s_pick_scroll, pick_snap_target_x(card, s_pick_scroll));
        pick_refresh_visuals();
        v1_ui_request_chirp(784);
        v1_ui_bump_activity();
        return;
    }
    pick_arm_only_focus();
    pick_begin_recording();
}

static void on_pick_card_long_press(lv_event_t *e)
{
    intptr_t display_idx = (intptr_t)lv_event_get_user_data(e);
    if (display_idx < 0 || (int)display_idx >= s_pick_n) {
        return;
    }
    if ((int)display_idx != s_pick_focus || !pick_scroll_aligned_to_focus()) {
        return;
    }
    s_pick_selected[s_pick_focus] = !s_pick_selected[s_pick_focus];
    pick_refresh_selection_borders();
    pick_refresh_rec_btn();
    v1_ui_request_chirp(523);
    v1_ui_bump_activity();
}

static void pick_add_user_card(int display_idx, int user_idx, int x)
{
    lv_obj_t *card = lv_obj_create(s_pick_scroll);
    s_pick_cards[display_idx] = card;
    s_pick_user_at[display_idx] = user_idx;
    s_pick_selected[display_idx] = false;
    lv_obj_set_size(card, V1_SCROLL_CARD_W, V1_SCROLL_CARD_H);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_radius(card, V1_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    apply_pick_card_grad(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_pick_card_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)display_idx);
    lv_obj_add_event_cb(card, on_pick_card_long_press, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)display_idx);
    const int face_sz = V1_CARD_PORTRAIT * 3;
    v1_ui_paint_user_portrait_aligned(card, user_idx, face_sz, LV_ALIGN_CENTER, 0, -14);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, v1_connect_users_mut()[user_idx].name);
    lv_obj_set_style_text_color(name, lv_color_hex(V1_UI_TEXT), 0);
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
#endif
    lv_obj_set_width(name, V1_SCROLL_CARD_W - 16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
}

static void pick_add_all_card(int display_idx, int x)
{
    lv_obj_t *card = lv_obj_create(s_pick_scroll);
    s_pick_cards[display_idx] = card;
    s_pick_user_at[display_idx] = PICK_ITEM_ALL;
    s_pick_selected[display_idx] = false;
    lv_obj_set_size(card, V1_SCROLL_CARD_W, V1_SCROLL_CARD_H);
    lv_obj_set_pos(card, x, 0);
    lv_obj_set_style_radius(card, V1_CARD_RADIUS, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    apply_pick_card_grad(card);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(card, on_pick_card_click, LV_EVENT_CLICKED,
                        (void *)(intptr_t)display_idx);
    lv_obj_add_event_cb(card, on_pick_card_long_press, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)display_idx);
    const int face_sz = V1_CARD_PORTRAIT * 3;
    const int icon_y = (V1_SCROLL_CARD_H - face_sz) / 2 - 14;
    v1_ui_paint_asterisk_icon(card, (V1_SCROLL_CARD_W - face_sz) / 2, icon_y);
    lv_obj_t *name = lv_label_create(card);
    lv_label_set_text(name, "Everyone");
    lv_obj_set_style_text_color(name, lv_color_hex(V1_UI_TEXT), 0);
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
#endif
    lv_obj_set_width(name, V1_SCROLL_CARD_W - 16);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
}

static const char *send_phase_headline(send_phase_t ph)
{
    switch (ph) {
    case SEND_PHASE_FINISHING:
        return "Finishing";
    case SEND_PHASE_SENDING:
        return "Sending...";
    case SEND_PHASE_SENT:
        return "Sent";
    case SEND_PHASE_FAILED:
        return "Couldn't send";
    default:
        return "";
    }
}

static void send_recipient_summary(char *buf, size_t cap)
{
    buf[0] = 0;
    if (s_send_all) {
        snprintf(buf, cap, "Everyone");
        return;
    }
    if (s_send_to_n <= 0) {
        return;
    }
    const char *n0 = v1_connect_user_name(s_send_tos[0]);
    if (!n0 || !n0[0]) {
        n0 = s_send_tos[0];
    }
    if (s_send_to_n == 1) {
        snprintf(buf, cap, "%s", n0);
        return;
    }
    const char *n1 = v1_connect_user_name(s_send_tos[1]);
    if (!n1 || !n1[0]) {
        n1 = s_send_tos[1];
    }
    if (s_send_to_n == 2) {
        snprintf(buf, cap, "%s, %s", n0, n1);
        return;
    }
    if (s_send_to_n == 3) {
        const char *n2 = v1_connect_user_name(s_send_tos[2]);
        if (!n2 || !n2[0]) {
            n2 = s_send_tos[2];
        }
        snprintf(buf, cap, "%s, %s, %s", n0, n1, n2);
        return;
    }
    snprintf(buf, cap, "%s, %s +%d", n0, n1, s_send_to_n - 2);
}

static void paint_send(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    lv_obj_t *ribbon_top, *ribbon_bot, *count_lab, *offline_lab, *toast;
    v1_ui_paint_ribbons(scr, v1_connect_online(), s_session, &ribbon_top, &ribbon_bot,
                        &count_lab, &offline_lab, &toast);
    v1_ui_bind_toast(toast);

    send_phase_t ph = s_send_phase;
    const char *headline = send_phase_headline(ph);
    bool show_recipients = ph == SEND_PHASE_SENT || ph == SEND_PHASE_FAILED;
    int face_sz = V1_ROSTER_FACE_SZ;
    int cy = V1_RIBBON_H + V1_CONTENT_H / 2;

    if (ph == SEND_PHASE_SENT) {
        lv_obj_t *chk = lv_label_create(scr);
        lv_label_set_text(chk, LV_SYMBOL_OK);
        lv_obj_set_style_text_color(chk, lv_color_hex(V1_UI_ACCENT), 0);
        lv_obj_align(chk, LV_ALIGN_TOP_MID, 0, cy - 52);
    }

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, headline);
    uint32_t title_col =
        (ph == SEND_PHASE_FAILED) ? (uint32_t)V1_UI_ERROR : (uint32_t)V1_UI_TEXT;
    lv_obj_set_style_text_color(title, lv_color_hex(title_col), 0);
#if defined(LV_FONT_MONTSERRAT_28) && LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, cy - (ph == SEND_PHASE_SENT ? 20 : 12));

    if (show_recipients) {
        char names[96];
        send_recipient_summary(names, sizeof(names));
        int row_y = cy + 8;
        if (s_send_all) {
            v1_ui_paint_asterisk_icon(scr, (V1_LCD_W - 28) / 2, row_y);
        } else {
            int show_n = s_send_to_n;
            if (show_n > 3) {
                show_n = 3;
            }
            int total_w = show_n * face_sz + (show_n > 1 ? (show_n - 1) * 8 : 0);
            int x0 = (V1_LCD_W - total_w) / 2;
            for (int i = 0; i < show_n; i++) {
                int idx = v1_connect_user_index(s_send_tos[i]);
                lv_obj_t *box = lv_obj_create(scr);
                lv_obj_set_size(box, face_sz, face_sz);
                lv_obj_set_pos(box, x0 + i * (face_sz + 8), row_y);
                lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(box, 0, 0);
                lv_obj_set_style_pad_all(box, 0, 0);
                if (idx >= 0) {
                    v1_ui_paint_user_portrait(box, idx, face_sz);
                }
            }
        }
        if (names[0]) {
            lv_obj_t *sub = lv_label_create(scr);
            lv_label_set_text(sub, names);
            lv_obj_set_style_text_color(sub, lv_color_hex(V1_UI_TEXT_MUT), 0);
            lv_obj_set_width(sub, V1_LCD_W - 24);
            lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, row_y + face_sz + 16);
        }
    }

    v1_ui_hook_scr(scr);
}

static void paint_pick(lv_obj_t *scr)
{
    v1_record_invalidate_pick();
    lv_obj_clean(scr);
    (void)v1_connect_load_hangout_ms(V1_CONNECT_PROBE_MS);
    lv_obj_set_style_bg_color(scr, lv_color_hex(V1_UI_BG), 0);

    lv_obj_t *ribbon_top, *ribbon_bot, *count_lab, *offline_lab, *toast;
    v1_ui_paint_ribbons(scr, v1_connect_online(), s_session, &ribbon_top, &ribbon_bot,
                        &count_lab, &offline_lab, &toast);
    (void)ribbon_top;
    (void)ribbon_bot;
    (void)offline_lab;
    if (count_lab) {
        lv_label_set_text(count_lab, "");
    }
    v1_ui_bind_toast(toast);

    const int header_y_ofs = (V1_CAROUSEL_HEADER_Y + V1_CAROUSEL_HEADER_H / 2)
                             - (V1_CONTENT_H + 2 * V1_RIBBON_H) / 2;

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Send a message to ...");
    lv_obj_set_style_text_color(title, lv_color_hex(V1_UI_TEXT), 0);
#if defined(LV_FONT_MONTSERRAT_24) && LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#elif defined(LV_FONT_MONTSERRAT_22) && LV_FONT_MONTSERRAT_22
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
#endif
    lv_obj_set_width(title, V1_LCD_W - V1_CAROUSEL_PLAY - V1_CAROUSEL_PLAY_PAD * 3);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, V1_CAROUSEL_PLAY_PAD, header_y_ofs);

    v1_ui_create_rec_disk(scr, on_pick_rec_btn, NULL, &s_pick_rec_btn, &s_pick_rec_disk);

    s_pick_scroll = lv_obj_create(scr);
    lv_obj_set_pos(s_pick_scroll, 0, V1_SCROLL_CARD_Y);
    lv_obj_set_size(s_pick_scroll, V1_LCD_W, V1_SCROLL_CARD_H);
    lv_obj_set_style_bg_opa(s_pick_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pick_scroll, 0, 0);
    lv_obj_set_style_pad_all(s_pick_scroll, 0, 0);
    lv_obj_add_flag(s_pick_scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_pick_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_pick_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_pick_scroll, on_pick_scroll, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(s_pick_scroll, on_pick_scroll, LV_EVENT_SCROLL_END, NULL);

    int order[V1_USER_MAX];
    int nu = v1_connect_recipient_order(order, V1_USER_MAX, s_session);
    s_pick_n = 0;
    s_pick_focus = 0;
    int x = V1_SCROLL_CARD_GAP;
    for (int i = 0; i < nu; i++) {
        pick_add_user_card(s_pick_n, order[i], x);
        s_pick_n++;
        x += V1_SCROLL_CARD_W + V1_SCROLL_CARD_GAP;
    }
    pick_add_all_card(s_pick_n, x);
    s_pick_n++;
    x += V1_SCROLL_CARD_W + V1_SCROLL_CARD_GAP;
    lv_obj_t *end = lv_obj_create(s_pick_scroll);
    lv_obj_remove_style_all(end);
    lv_obj_set_size(end, 1, 1);
    lv_obj_set_pos(end, x, 0);

    if (s_pick_n > 0) {
        lv_obj_t *focus_card = s_pick_cards[s_pick_focus];
        if (focus_card) {
            int32_t target = pick_snap_target_x(focus_card, s_pick_scroll);
            lv_obj_scroll_to_x(s_pick_scroll, target, LV_ANIM_OFF);
        }
        pick_refresh_visuals();
    }
    pick_refresh_rec_btn();

    ESP_LOGI(TAG, "paint pick st=%d cards=%d heap=%u", (int)v1_state_get(), s_pick_n,
             (unsigned)esp_get_free_heap_size());
    v1_ui_hook_scr(scr);
}


static int16_t clamp_x_local(int x)
{
    if (x < 0) {
        return 0;
    }
    if (x > RECORD_CANVAS_W - 1) {
        return RECORD_CANVAS_W - 1;
    }
    return (int16_t)x;
}

static int16_t clamp_y_local(int y)
{
    if (y < 0) {
        return 0;
    }
    if (y > RECORD_CANVAS_H - 1) {
        return RECORD_CANVAS_H - 1;
    }
    return (int16_t)y;
}

static void invalidate_canvas(void)
{
    if (s_record_img) {
        lv_obj_invalidate(s_record_img);
    }
}

static void record_capture_ink(uint8_t phase, int16_t x, int16_t y)
{
    if (!record_screen_active() || !s_sketch_pts || s_sketch_n >= V1_SKETCH_MAX_POINTS) {
        return;
    }
    if (!v1_sketch_capture_point(s_sketch_pts, V1_SKETCH_MAX_POINTS, &s_sketch_n, phase,
                                 x, y, RECORD_CANVAS_W - 1, RECORD_CANVAS_H - 1,
                                 now_us(), s_record_t0_us)) {
        return;
    }
    v1_sketch_ink_point(&s_record_ink, phase, x, y, rgb565(RECORD_INK_HEX));
    invalidate_canvas();
}

static void on_record_disk_stop(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (v1_state_get() != ST_RECORD) {
        return;
    }
    v1_record_on_circle_stop();
    v1_ui_bump_activity();
}

static void on_record_pointer(lv_event_t *e)
{
    if (!record_screen_active()) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    uint8_t phase;
    if (code == LV_EVENT_PRESSED) {
        phase = V1_SKETCH_PHASE_DOWN;
    } else if (code == LV_EVENT_PRESSING) {
        phase = V1_SKETCH_PHASE_MOVE;
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        phase = V1_SKETCH_PHASE_UP;
    } else {
        return;
    }
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) {
        indev = lv_indev_active();
    }
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(s_record_img, &a);
    int16_t x = clamp_x_local(p.x - a.x1);
    int16_t y = clamp_y_local(p.y - a.y1);

    if (phase == V1_SKETCH_PHASE_MOVE && s_record_last_x >= 0) {
        int dx = x - s_record_last_x;
        int dy = y - s_record_last_y;
        if (dx * dx + dy * dy < RECORD_MIN_MOVE * RECORD_MIN_MOVE) {
            return;
        }
    }
    record_capture_ink(phase, x, y);
    s_record_last_x = x;
    s_record_last_y = y;
    if (phase == V1_SKETCH_PHASE_UP) {
        s_record_last_x = -1;
        s_record_last_y = -1;
    }
}

static void paint_record(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_hex(RECORD_BG_HEX), 0);

    if (!sketch_buffers_ready()) {
        v1_carousel_queue_toast("no sketch memory");
        return;
    }
    s_sketch_n = 0;
    s_record_last_x = -1;
    s_record_last_y = -1;

    v1_sketch_ink_init(&s_record_ink, s_record_fb, RECORD_CANVAS_W, RECORD_CANVAS_H, RECORD_BRUSH);
    v1_sketch_ink_clear(&s_record_ink, rgb565(RECORD_BG_HEX));

    memset(&s_record_dsc, 0, sizeof(s_record_dsc));
    s_record_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_record_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_record_dsc.header.w = RECORD_CANVAS_W;
    s_record_dsc.header.h = RECORD_CANVAS_H;
    s_record_dsc.header.stride = RECORD_CANVAS_W * 2;
    s_record_dsc.data_size = RECORD_FB_BYTES;
    s_record_dsc.data = (const uint8_t *)s_record_fb;

    s_record_img = lv_image_create(scr);
    lv_image_set_src(s_record_img, &s_record_dsc);
    lv_obj_set_pos(s_record_img, 0, 0);
    lv_obj_set_size(s_record_img, RECORD_CANVAS_W, RECORD_CANVAS_H);
    lv_obj_set_style_border_width(s_record_img, 0, 0);
    lv_obj_add_flag(s_record_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_record_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_record_img, on_record_pointer, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_record_img, on_record_pointer, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_record_img, on_record_pointer, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_record_img, on_record_pointer, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_move_foreground(s_record_img);

    s_record_t0_us = now_us();

    v1_ui_create_rec_disk(scr, on_record_disk_stop, NULL, &s_record_rec_btn, &s_record_rec_disk);

    s_record_timer_lab = lv_label_create(scr);
    lv_label_set_text(s_record_timer_lab, " 0:00");
    lv_obj_set_style_text_color(s_record_timer_lab, lv_color_hex(RECORD_TIMER_HEX), 0);
    lv_obj_set_style_bg_opa(s_record_timer_lab, LV_OPA_40, 0);
    lv_obj_set_style_bg_color(s_record_timer_lab, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_right(s_record_timer_lab, RECORD_TIMER_PAD_R, 0);
    lv_obj_set_style_pad_left(s_record_timer_lab, 4, 0);
    lv_obj_align_to(s_record_timer_lab, s_record_rec_btn, LV_ALIGN_OUT_LEFT_MID,
                    -RECORD_TIMER_DISK_GAP, 0);
    record_refresh_elapsed_label();

    lv_obj_move_foreground(s_record_rec_btn);
    lv_obj_move_foreground(s_record_timer_lab);
    s_record_disk_on = true;
    record_set_disk_color(RECORD_DISK_BRT);

    if (s_record_pulse_timer) {
        lv_timer_delete(s_record_pulse_timer);
    }
    if (s_record_elapsed_timer) {
        lv_timer_delete(s_record_elapsed_timer);
    }
    s_record_pulse_timer = lv_timer_create(record_ui_timer_cb, RECORD_PULSE_MS, NULL);
    s_record_elapsed_timer = lv_timer_create(record_elapsed_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "paint record heap=%u", (unsigned)esp_get_free_heap_size());
    v1_ui_hook_scr(scr);
}


