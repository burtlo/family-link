#ifndef PRODUCT_RECORD_FW_H
#define PRODUCT_RECORD_FW_H

#include "lvgl.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_record_attach(lv_obj_t *panel_root);
int product_record_ensure(lv_obj_t *panel_root);
void product_record_set_visible(int visible);
int product_record_can_start_second(void);
int product_record_capture_can_start(void);
int product_record_begin_wipe(const char *client_message_id, int64_t now_ms);
void product_record_begin_reverse(int64_t now_ms);
void product_record_begin_finish_collapse(int64_t now_ms);
int product_record_read_finished(const char *client_message_id, int kind, uint32_t offset, uint8_t *dest,
                                 uint32_t len, uint32_t *out_read);
int product_record_finished_info(const char *client_message_id, uint32_t *audio_bytes, uint32_t *sketch_bytes);
int product_record_mark_uploading(const char *client_message_id);
void product_record_clear_upload(const char *client_message_id);
int product_record_push_pcm(const uint8_t *pcm, size_t len);
void product_record_notify_mic_failed(void);
void product_record_discard_in_progress(void);
void product_record_cancel_recording(void);
int product_record_finish_take(void);
void product_record_service(int64_t now_ms, int record_stage_active);

#ifdef __cplusplus
}
#endif

#endif
