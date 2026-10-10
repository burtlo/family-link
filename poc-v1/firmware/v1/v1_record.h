#ifndef V1_RECORD_H
#define V1_RECORD_H

#include "esp_codec_dev.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    char *session_user;
    esp_codec_dev_handle_t mic;
    esp_codec_dev_handle_t spk;
    SemaphoreHandle_t work_sem;
    void (*play_chirp_pair)(int a, int b);
} v1_record_cfg_t;

void v1_record_init(const v1_record_cfg_t *cfg);
void v1_record_start_task(void);

void v1_record_paint_pick(lv_obj_t *scr);
void v1_record_paint_record(lv_obj_t *scr);
void v1_record_paint_send(lv_obj_t *scr);

void v1_record_invalidate_send(void);

void v1_record_on_circle_stop(void);
/** ST_PICK: start capture when at least one recipient is selected. */
void v1_record_on_circle_start(void);
void v1_record_on_shoulder_cancel(void);
void v1_record_invalidate_pick(void);
void v1_record_invalidate_record(void);
bool v1_record_tick_pick_timeout(int64_t now_us);

void v1_record_set_send_all(bool all);
void v1_record_set_send_to(const char *user_id);

void v1_record_open_pick(int64_t now_us);
void v1_record_give_work(void);
void v1_record_tick_send(int64_t now_us);

#endif /* V1_RECORD_H */
