#pragma once

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace product_record
{

bool mount_storage();
bool attach(lv_obj_t *panel_root);
int ensure(lv_obj_t *panel_root);
void release_surface();
/* Called only by product_stage::present(). */
void set_visible(bool visible);
int can_start_second();
int capture_can_start();
int begin_wipe(const char *client_message_id, int64_t now_ms);
void begin_reverse(int64_t now_ms);
void begin_finish_collapse(int64_t now_ms);
int read_finished(const char *client_message_id, int kind, uint32_t offset, uint8_t *dest, uint32_t len,
                  uint32_t *out_read);
int finished_info(const char *client_message_id, uint32_t *audio_bytes, uint32_t *sketch_bytes);
int mark_uploading(const char *client_message_id);
void clear_upload(const char *client_message_id);
int push_pcm(const uint8_t *pcm, size_t len);
void set_pcm_observer(int (*push)(const uint8_t *pcm, size_t len, void *context), void *context);
void notify_mic_failed();
void discard_in_progress();
void cancel_recording();
int finish_take();
void service(int64_t now_ms);

} /* namespace product_record */
