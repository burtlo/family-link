#ifndef PRODUCT_SEND_FW_H
#define PRODUCT_SEND_FW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PRODUCT_SEND_INTENT_RED 1
#define PRODUCT_SEND_INTENT_BOOT 2
#define PRODUCT_SEND_INTENT_MUTE 3
#define PRODUCT_SEND_INTENT_WIPE_COMPLETE 4
#define PRODUCT_SEND_INTENT_CAPTURE_FAILED 5
#define PRODUCT_SEND_INTENT_TAKE_FINISHED 6

typedef struct product_send_stage_result {
    int place;
    int shell_stage;
    int changed;
    int ignored;
    int opened_volume;
    int show_record_canvas;
    int show_split_pane;
    int left_wait;
} product_send_stage_result_t;

bool product_send_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void product_send_detach(void);
void product_send_sync_stage(void);
void product_send_hide_for_login(void);
void product_send_note_inbox(void);
void product_send_set_wifi(int up);
void product_send_set_refresh_failed(int failed);
void product_send_on_signed_in(const char *user_id);
product_send_stage_result_t product_send_apply_intent(int intent, int signed_in);

#ifdef __cplusplus
}
#endif

#endif
