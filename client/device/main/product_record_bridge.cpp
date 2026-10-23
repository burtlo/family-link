#include "product_record_fw.h"

#include "bsp/esp-box-3.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fl_product_record_lab10.h"
#include "fl_product_record_media.hpp"
#include "fl_product_record_session.hpp"
#include "product_shell_fw.h"
#include "product_stage_router.h"

#include <new>
#include <stdio.h>
#include <string.h>

namespace {

constexpr const char *kTag = "product_record";
fl_product_record::RecordSession *s_record = nullptr;
fl_product_record::FileMediaBackend s_media;
lv_obj_t *s_root = nullptr;
lv_obj_t *s_reveal = nullptr;
lv_obj_t *s_canvas = nullptr;
lv_obj_t *s_rail = nullptr;
lv_obj_t *s_ring = nullptr;
bool s_attached = false;
bool s_media_ready = false;
int s_begin_failed = 0;
int s_mount_warned = 0;
constexpr size_t kCanvasWidth = 240;
constexpr size_t kCanvasHeight = 240;
constexpr size_t kCanvasBytes = kCanvasWidth * kCanvasHeight * 2u; /* LV_COLOR_FORMAT_RGB565 */
static uint8_t *s_canvas_buf = nullptr;

void layout_record_surface(bool visible)
{
    if (s_root == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_root);
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ensure_session()
{
    if (s_record != nullptr) {
        return true;
    }
    s_record = new (std::nothrow) fl_product_record::RecordSession();
    if (s_record == nullptr) {
        ESP_LOGE(kTag, "record_session_alloc_failed");
        return false;
    }
    s_record->boot((int64_t)(esp_timer_get_time() / 1000LL));
    return true;
}

bool ensure_media()
{
    char path[64];
    esp_err_t mounted;
    if (!ensure_session()) {
        return false;
    }
    if (s_media_ready) {
        return true;
    }
    mounted = bsp_sdcard_mount();
    if (mounted != ESP_OK && mounted != ESP_ERR_INVALID_STATE) {
        if (s_mount_warned == 0) {
            ESP_LOGW(kTag, "sd mount failed");
            s_mount_warned = 1;
        }
        return false;
    }
    snprintf(path, sizeof(path), "%s/fl-messages.bin", BSP_SD_MOUNT_POINT);
    if (!s_media.open_path(path, false) && !s_media.open_path(path, true)) {
        ESP_LOGW(kTag, "media open failed");
        return false;
    }
    s_record->attach_media(&s_media);
    s_media_ready = true;
    return true;
}

void show_full_reveal()
{
    if (s_reveal == nullptr || s_canvas == nullptr || s_rail == nullptr) {
        return;
    }
    lv_obj_remove_flag(s_reveal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_reveal, 0, 0);
    lv_obj_set_size(s_reveal, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_set_pos(s_rail, LAB10_RAIL_X, 0);
    if (s_ring != nullptr) {
        lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    }
}

void place_ring(int32_t radius)
{
    if (s_ring == nullptr) {
        return;
    }
    if (radius < 1) {
        radius = 1;
    }
    lv_obj_set_size(s_ring, radius * 2, radius * 2);
    lv_obj_set_pos(s_ring, LAB10_RING_ORIGIN_X - radius, LAB10_RING_ORIGIN_Y - radius);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ring);
}

void paint_motion()
{
    lab10_ui_ring_reveal_clip_t clip;
    int32_t radius;
    if (s_record == nullptr) {
        return;
    }
    if (!s_record->wiping() && !s_record->reversing()) {
        if (s_record->capture_active()) {
            show_full_reveal();
        }
        return;
    }
    radius = s_record->ring_outer_radius();
    clip = lab10_ui_ring_reveal_clip(LAB10_RING_ORIGIN_Y, radius);
    if (s_reveal != nullptr) {
        if (clip.hidden) {
            lv_obj_add_flag(s_reveal, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_reveal, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_reveal, clip.x, clip.y);
            lv_obj_set_size(s_reveal, clip.width, clip.height);
            if (s_canvas != nullptr) {
                lv_obj_set_pos(s_canvas, 0, clip.content_offset_y);
            }
            if (s_rail != nullptr) {
                lv_obj_set_pos(s_rail, LAB10_RAIL_X, clip.content_offset_y);
            }
        }
    }
    place_ring(radius);
}

uint8_t touch_phase(lv_event_code_t code)
{
    switch (code) {
    case LV_EVENT_PRESSED:
        return 0;
    case LV_EVENT_PRESSING:
        return 1;
    case LV_EVENT_RELEASED:
        return 2;
    default:
        return 3;
    }
}

void on_record_pointer(lv_event_t *event)
{
    lv_point_t point;
    lv_indev_t *indev;
    if (s_record == nullptr || !s_record->capture_active()) {
        return;
    }
    indev = lv_event_get_indev(event);
    if (indev == nullptr) {
        indev = lv_indev_active();
    }
    if (indev == nullptr) {
        return;
    }
    lv_indev_get_point(indev, &point);
    (void)s_record->route_touch((int16_t)point.x, (int16_t)point.y, touch_phase(lv_event_get_code(event)), 0);
}

void bind_pointer(lv_obj_t *obj)
{
    if (obj == nullptr) {
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, on_record_pointer, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, on_record_pointer, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(obj, on_record_pointer, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(obj, on_record_pointer, LV_EVENT_PRESS_LOST, nullptr);
}

void note_session_result()
{
    int notice;
    if (s_begin_failed != 0) {
        s_begin_failed = 0;
        (void)product_stage_on_capture_failed();
    }
    if (s_record == nullptr) {
        return;
    }
    notice = s_record->consume_notice();
    if (notice == fl_product_record::kSessionNoticeWipeCompleted) {
        (void)product_stage_on_wipe_complete();
    } else if (notice == fl_product_record::kSessionNoticeTakeFinished) {
        (void)product_stage_on_take_finished();
    } else if (notice == fl_product_record::kSessionNoticeCaptureFailed) {
        (void)product_stage_on_capture_failed();
    }
}

void paint_swatch(lv_obj_t *parent, int center_x, int center_y, uint32_t rgb)
{
    lv_obj_t *swatch = lv_obj_create(parent);
    if (swatch == nullptr) {
        return;
    }
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 44, 44);
    lv_obj_set_pos(swatch, center_x - 22, center_y - 22);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0xB8BEC6), 0);
    bind_pointer(swatch);
}

} /* namespace */

extern "C" bool product_record_attach(lv_obj_t *panel_root)
{
    unsigned index;
    if (panel_root == nullptr || s_attached) {
        return false;
    }
    if (!ensure_session()) {
        return false;
    }
    if (s_canvas_buf == nullptr) {
        s_canvas_buf = static_cast<uint8_t *>(
            heap_caps_malloc(kCanvasBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (s_canvas_buf == nullptr) {
            s_canvas_buf = static_cast<uint8_t *>(
                heap_caps_malloc(kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (s_canvas_buf == nullptr) {
            ESP_LOGE(kTag, "canvas_alloc_failed bytes=%u", (unsigned)kCanvasBytes);
            return false;
        }
    }
    s_root = lv_obj_create(panel_root);
    if (s_root == nullptr) {
        return false;
    }
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_reveal = lv_obj_create(s_root);
    if (s_reveal == nullptr) {
        return false;
    }
    lv_obj_remove_style_all(s_reveal);
    lv_obj_set_pos(s_reveal, 0, 0);
    lv_obj_set_size(s_reveal, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_remove_flag(s_reveal, LV_OBJ_FLAG_SCROLLABLE);
    s_canvas = lv_canvas_create(s_reveal);
    s_rail = lv_obj_create(s_reveal);
    if (s_canvas == nullptr || s_rail == nullptr) {
        return false;
    }
    lv_canvas_set_buffer(s_canvas, s_canvas_buf, (int32_t)kCanvasWidth, (int32_t)kCanvasHeight,
                         LV_COLOR_FORMAT_RGB565);
    lv_canvas_fill_bg(s_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    lv_obj_set_pos(s_canvas, 0, 0);
    lv_obj_remove_style_all(s_rail);
    lv_obj_set_size(s_rail, 80, 240);
    lv_obj_set_pos(s_rail, 240, 0);
    lv_obj_set_style_bg_color(s_rail, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(s_rail, LV_OPA_COVER, 0);
    for (index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        const lab10_swatch_geometry_t *swatch = lab10_swatch_geometry(index);
        if (swatch != nullptr) {
            paint_swatch(s_rail, swatch->center_x - LAB10_RAIL_X, swatch->center_y, swatch->color_rgb);
        }
    }
    bind_pointer(s_canvas);
    bind_pointer(s_rail);
    s_ring = lv_arc_create(s_root);
    if (s_ring == nullptr) {
        return false;
    }
    lv_obj_remove_style_all(s_ring);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, LAB10_RING_BAND_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, lv_color_hex(0xE53935), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_ring, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_angles(s_ring, 0, 360);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    if (s_record->wiping() || s_record->reversing()) {
        paint_motion();
    }
    s_attached = true;
    ESP_LOGI(kTag, "V2_PRODUCT_RECORD_READY build=%s", FAMILY_BUILD_ID);
    return true;
}

extern "C" int product_record_ensure(lv_obj_t *panel_root)
{
    if (s_attached) {
        return 1;
    }
    return product_record_attach(panel_root) ? 1 : 0;
}

extern "C" void product_record_set_visible(int visible)
{
    layout_record_surface(visible != 0);
}

extern "C" int product_record_can_start_second(void)
{
    if (s_record == nullptr) {
        return 1;
    }
    return s_record->storage().can_start_second_recording() ? 1 : 0;
}

extern "C" int product_record_capture_can_start(void) { return ensure_media() ? 1 : 0; }

extern "C" int product_record_begin_wipe(const char *client_message_id, int64_t now_ms)
{
    if (!ensure_media() || client_message_id == nullptr) {
        s_begin_failed = 1;
        return 0;
    }
    s_record->advance_to(now_ms);
    if (!s_record->try_start(client_message_id, fl_product_record::kMicReady)) {
        s_begin_failed = 1;
        return 0;
    }
    paint_motion();
    return 1;
}

extern "C" void product_record_begin_reverse(int64_t now_ms)
{
    if (!ensure_session()) {
        return;
    }
    s_record->begin_reverse(now_ms);
    paint_motion();
}

extern "C" void product_record_begin_finish_collapse(int64_t now_ms)
{
    if (!ensure_session()) {
        return;
    }
    s_record->begin_finish_collapse(now_ms);
    paint_motion();
}

extern "C" int product_record_read_finished(const char *client_message_id, int kind, uint32_t offset, uint8_t *dest,
                                            uint32_t len, uint32_t *out_read)
{
    size_t n = 0;
    fl_product_record::MediaKind media;
    if (s_record == nullptr || client_message_id == nullptr || out_read == nullptr) {
        return 0;
    }
    *out_read = 0;
    if (kind == 2) {
        media = fl_product_record::kMediaSketch;
    } else if (kind == 1) {
        media = fl_product_record::kMediaAudio;
    } else {
        return 0;
    }
    if (!s_record->storage().read_finished(client_message_id, media, offset, dest, (size_t)len, &n)) {
        return 0;
    }
    *out_read = (uint32_t)n;
    return 1;
}

extern "C" int product_record_finished_info(const char *client_message_id, uint32_t *audio_bytes,
                                            uint32_t *sketch_bytes)
{
    int i;
    if (s_record == nullptr || client_message_id == nullptr) {
        return 0;
    }
    for (i = 0; i < fl_product_record::kAttachedFinishedSlots; ++i) {
        const fl_product_record::StoredObject *slot = s_record->storage().finished_slot(i);
        if (slot == nullptr || strcmp(slot->client_message_id, client_message_id) != 0) {
            continue;
        }
        if (audio_bytes != nullptr) {
            *audio_bytes = slot->audio_bytes;
        }
        if (sketch_bytes != nullptr) {
            *sketch_bytes = slot->sketch_bytes;
        }
        return 1;
    }
    return 0;
}

extern "C" int product_record_mark_uploading(const char *client_message_id)
{
    if (s_record == nullptr || client_message_id == nullptr) {
        return 0;
    }
    return s_record->storage().mark_finished_uploading(client_message_id) ? 1 : 0;
}

extern "C" void product_record_clear_upload(const char *client_message_id)
{
    if (s_record == nullptr || client_message_id == nullptr) {
        return;
    }
    s_record->storage().clear_finished_upload(client_message_id);
}

extern "C" int product_record_push_pcm(const uint8_t *pcm, size_t len)
{
    if (s_record == nullptr) {
        return 0;
    }
    return s_record->push_pcm(pcm, len) ? 1 : 0;
}

extern "C" void product_record_notify_mic_failed(void)
{
    if (s_record == nullptr) {
        return;
    }
    s_record->notify_mic_failed();
}

extern "C" void product_record_discard_in_progress(void)
{
    if (s_record == nullptr) {
        return;
    }
    s_record->storage().discard_in_progress();
}

extern "C" void product_record_cancel_recording(void)
{
    if (s_record == nullptr) {
        return;
    }
    s_record->cancel_recording();
}

extern "C" int product_record_finish_take(void)
{
    if (s_record == nullptr) {
        return 0;
    }
    return s_record->stop_recording() ? 1 : 0;
}

extern "C" void product_record_service(int64_t now_ms, int record_stage_active)
{
    int show;
    /* Connectivity loss does not stop this clock. The canvas stays in front
     * of inbox status copy for the whole wipe and take. */
    (void)record_stage_active;
    if (s_record != nullptr) {
        s_record->advance_to(now_ms);
    }
    note_session_result();
    show = product_shell_stage() == FL_SHELL_STAGE_RECORD;
    layout_record_surface(show != 0);
    if (show != 0) {
        paint_motion();
    }
}
