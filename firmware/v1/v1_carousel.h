#ifndef V1_CAROUSEL_H
#define V1_CAROUSEL_H

#include "esp_codec_dev.h"
#include "lvgl.h"
#include "v1_types.h"

#include "freertos/FreeRTOS.h"

typedef struct {
    char *session_user;
    esp_codec_dev_handle_t spk;
    uint8_t *playback_buf;
    size_t playback_buf_cap;
    int *volume;
    int *vol_notch;
} v1_carousel_cfg_t;

void v1_carousel_init(const v1_carousel_cfg_t *cfg);
void v1_carousel_bind_inbox(msg_t *msgs, int *msg_n, int *focus, int msg_max);

void v1_carousel_on_auth_ok(void);
void v1_carousel_on_ws_inbox(const char *user_id);
bool v1_carousel_tick_inbox(void);

void v1_carousel_stop_playback(void);
bool v1_carousel_is_playing(void);
void v1_carousel_start_tasks(void);

void v1_carousel_paint(lv_obj_t *scr);
void v1_carousel_refresh_transport(void);
void v1_carousel_refresh_offline_ribbon(void);

void v1_carousel_on_circle_press(int64_t now_us);
void v1_carousel_on_shoulder_press(void);

void v1_carousel_apply_volume(int vol);
int v1_carousel_roomvol_codec(int notch);
void v1_carousel_play_chirp(int hz);
void v1_carousel_play_chirp_pair(int a, int b);

void v1_carousel_nvs_load_card_grad(void);
void v1_carousel_save_card_grad(void);
uint8_t v1_carousel_card_grad(void);
void v1_carousel_set_card_grad(uint8_t id);

msg_t *v1_carousel_focus_msg(void);
msg_t *v1_carousel_msgs(void);
int v1_carousel_msg_n(void);
int v1_carousel_focus_idx(void);
void v1_carousel_set_focus_idx(int idx);

#endif /* V1_CAROUSEL_H */
