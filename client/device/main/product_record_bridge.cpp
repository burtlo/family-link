#include "product_record_fw.h"

#include "bsp/esp-box-3.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "fl_product_record_lab10.h"
#include "fl_product_record_media.hpp"
#include "fl_product_record_session.hpp"
#include "product_mem_probe.h"
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
bool s_storage_mounted = false;
int s_begin_failed = 0;
int s_mount_warned = 0;
constexpr size_t kCanvasWidth = 240;
constexpr size_t kCanvasHeight = 240;
constexpr size_t kCanvasBytes = kCanvasWidth * kCanvasHeight * 2u; /* LV_COLOR_FORMAT_RGB565 */
static uint8_t *s_canvas_buf = nullptr;
constexpr int32_t kLogicalScale = (int32_t)kCanvasWidth / LAB10_LOGICAL_WIDTH;
lv_obj_t *s_swatches[LAB10_SWATCH_COUNT] = {};
int s_outlined_swatch = -1;
bool s_renderer_hooked = false;
uint32_t s_touch_events = 0;

void log_ink_tick(int64_t now_ms)
{
#if CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC
    static int64_t s_last_ms = 0;
    size_t source = 0;
    size_t pending = 0;
    size_t drawn = 0;
    if (s_record == nullptr || !s_record->render_backlog(&source, &pending, &drawn)) {
        return;
    }
    if (s_touch_events != 0 || pending != 0 || drawn != 0) {
        ESP_LOGI(kTag, "V2_INK dt=%lld touches=%u points=%u pending=%u drawn=%u", (long long)(now_ms - s_last_ms),
                 (unsigned)s_touch_events, (unsigned)source, (unsigned)pending, (unsigned)drawn);
    }
    s_last_ms = now_ms;
    s_touch_events = 0;
#else
    (void)now_ms;
#endif
}

/* Ink goes straight into the canvas buffer: no LVGL draw tasks, no pool allocation. */
void on_publish_pixel(const lab10_render_pixel_event_t *event, void *context)
{
    uint16_t *pixels = reinterpret_cast<uint16_t *>(s_canvas_buf);
    const uint16_t color = lv_color_to_u16(lv_color_hex(lab10_color_rgb(event->color_index)));
    const size_t x0 = (size_t)event->logical_x * kLogicalScale;
    const size_t y0 = (size_t)event->logical_y * kLogicalScale;
    (void)context;
    if (pixels == nullptr || event->logical_x >= LAB10_LOGICAL_WIDTH ||
        event->logical_y >= LAB10_LOGICAL_HEIGHT) {
        return;
    }
    for (size_t dy = 0; dy < (size_t)kLogicalScale; ++dy) {
        for (size_t dx = 0; dx < (size_t)kLogicalScale; ++dx) {
            pixels[(y0 + dy) * kCanvasWidth + x0 + dx] = color;
        }
    }
}

void on_invalidation(const lab10_render_invalidation_t *invalidation, void *context)
{
    lv_area_t area;
    (void)context;
    if (s_canvas == nullptr) {
        return;
    }
    lv_obj_get_coords(s_canvas, &area);
    area.x2 = area.x1 + (invalidation->max_x + 1) * kLogicalScale - 1;
    area.y2 = area.y1 + (invalidation->max_y + 1) * kLogicalScale - 1;
    area.x1 += invalidation->min_x * kLogicalScale;
    area.y1 += invalidation->min_y * kLogicalScale;
    lv_obj_invalidate_area(s_canvas, &area);
}

void hook_renderer()
{
    lab10_render_hooks_t hooks;
    lab10_render_t *renderer;
    if (s_renderer_hooked || s_record == nullptr) {
        return;
    }
    renderer = static_cast<lab10_render_t *>(s_record->renderer_handle());
    if (renderer == nullptr) {
        return;
    }
    memset(&hooks, 0, sizeof(hooks));
    hooks.on_publish_pixel = on_publish_pixel;
    hooks.on_invalidation = on_invalidation;
    s_renderer_hooked = lab10_render_set_hooks(renderer, &hooks);
}

void outline_selected_swatch()
{
    const int selected = s_record != nullptr ? (int)s_record->selected_color() : -1;
    if (selected == s_outlined_swatch) {
        return;
    }
    if (s_outlined_swatch >= 0 && s_outlined_swatch < LAB10_SWATCH_COUNT &&
        s_swatches[s_outlined_swatch] != nullptr) {
        lv_obj_set_style_outline_width(s_swatches[s_outlined_swatch], 0, 0);
    }
    if (selected >= 0 && selected < LAB10_SWATCH_COUNT && s_swatches[selected] != nullptr) {
        lv_obj_set_style_outline_width(s_swatches[selected], 2, 0);
    }
    s_outlined_swatch = selected;
}

void clear_canvas()
{
    if (s_canvas != nullptr) {
        lv_canvas_fill_bg(s_canvas, lv_color_hex(0x000000), LV_OPA_COVER);
    }
}

void layout_record_surface(bool visible)
{
    if (s_root == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ensure_session()
{
    if (s_record != nullptr) {
        return true;
    }
    product_mem_probe("session_new_before", (int32_t)sizeof(fl_product_record::RecordSession));
    s_record = new (std::nothrow) fl_product_record::RecordSession();
    if (s_record == nullptr) {
        ESP_LOGE(kTag, "record_session_alloc_failed");
        return false;
    }
    product_mem_probe("session_new_after", 0);
    s_record->boot((int64_t)(esp_timer_get_time() / 1000LL));
    product_mem_probe("session_boot_after", s_record->renderer_handle() != nullptr ? 1 : 0);
    hook_renderer();
    return true;
}

bool ensure_media()
{
    char path[64];
    if (!ensure_session()) {
        return false;
    }
    if (s_media_ready) {
        return true;
    }
    if (!s_storage_mounted) {
        if (s_mount_warned == 0) {
            ESP_LOGW(kTag, "no sd card mounted at boot");
            s_mount_warned = 1;
        }
        return false;
    }
    /* FATFS is built without long file names: the name must fit 8.3. */
    snprintf(path, sizeof(path), "%s/flmsgs.bin", BSP_SD_MOUNT_POINT);
    product_mem_probe("media_open_before", 0);
    if (!s_media.open_path(path, false) && !s_media.open_path(path, true)) {
        ESP_LOGW(kTag, "media open failed");
        return false;
    }
    s_record->attach_media(&s_media);
    s_media_ready = true;
    product_mem_probe("media_open_after", 0);
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
#if CONFIG_FAMILY_DEVICE_DIAG_SKIP_RING
    return;
#endif
    if (radius < 1) {
        radius = 1;
    }
    lv_obj_set_size(s_ring, radius * 2, radius * 2);
    lv_obj_set_pos(s_ring, LAB10_RING_ORIGIN_X - radius, LAB10_RING_ORIGIN_Y - radius);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
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
    product_mem_probe(s_record->wiping() ? "wipe_frame" : "reverse_frame", radius);
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
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        product_mem_probe("touch_down", 0);
    }
    ++s_touch_events;
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
        product_mem_probe("wipe_complete", 0);
        (void)product_stage_on_wipe_complete();
    } else if (notice == fl_product_record::kSessionNoticeTakeFinished) {
        product_mem_probe("take_finished_notice", 0);
        (void)product_stage_on_take_finished();
    } else if (notice == fl_product_record::kSessionNoticeCaptureFailed) {
        product_mem_probe("capture_failed_notice", 0);
        (void)product_stage_on_capture_failed();
    }
}

lv_obj_t *paint_swatch(lv_obj_t *parent, int center_x, int center_y, uint32_t rgb)
{
    lv_obj_t *swatch = lv_obj_create(parent);
    if (swatch == nullptr) {
        return nullptr;
    }
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 44, 44);
    lv_obj_set_pos(swatch, center_x - 22, center_y - 22);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, lv_color_hex(0xB8BEC6), 0);
    lv_obj_set_style_outline_color(swatch, lv_color_hex(0xE8C040), 0);
    lv_obj_set_style_outline_pad(swatch, 1, 0);
    lv_obj_set_style_outline_width(swatch, 0, 0);
    bind_pointer(swatch);
    return swatch;
}

} /* namespace */

/* Powering the BOX-3 card slot while the LCD is running leaves the panel white
 * until power cycle, so the card is mounted once, before the display starts. */
extern "C" void product_record_mount_storage(void)
{
    const esp_err_t mounted = bsp_sdcard_mount();
    s_storage_mounted = mounted == ESP_OK || mounted == ESP_ERR_INVALID_STATE;
    ESP_LOGI(kTag, "sd mount err=%s mounted=%d", esp_err_to_name(mounted), s_storage_mounted ? 1 : 0);
}

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
            s_swatches[index] =
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
    int ok;
    if (s_attached) {
        return 1;
    }
    product_mem_probe("record_attach_before", 0);
    ok = product_record_attach(panel_root) ? 1 : 0;
    product_mem_probe("record_attach_after", ok);
    return ok;
}

extern "C" void product_record_release_surface(void)
{
    unsigned index;
    if (s_root != nullptr) {
        lv_obj_delete(s_root);
    }
    s_root = nullptr;
    s_reveal = nullptr;
    s_canvas = nullptr;
    s_rail = nullptr;
    s_ring = nullptr;
    for (index = 0; index < LAB10_SWATCH_COUNT; ++index) {
        s_swatches[index] = nullptr;
    }
    s_outlined_swatch = -1;
    s_attached = false;
    product_mem_probe("record_surface_released", 0);
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
        product_mem_probe("wipe_begin", 0);
        return 0;
    }
    product_mem_probe("wipe_begin", 1);
    clear_canvas();
    s_outlined_swatch = -1;
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
    product_mem_probe("collapse_begin", 0);
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
    int64_t started_us;
    int ok;
    if (s_record == nullptr) {
        return 0;
    }
    product_mem_probe("finish_take_begin", 0);
    started_us = esp_timer_get_time();
    ok = s_record->stop_recording() ? 1 : 0;
    product_mem_probe(ok ? "finish_take_ok_ms" : "finish_take_fail_ms",
                      (int32_t)((esp_timer_get_time() - started_us) / 1000LL));
    return ok;
}

extern "C" void product_record_service(int64_t now_ms)
{
    /* Connectivity loss does not stop this clock. */
    if (s_record != nullptr) {
        s_record->advance_to(now_ms);
    }
    note_session_result();
    if (product_shell_stage() == FL_SHELL_STAGE_RECORD) {
        paint_motion();
        if (s_record != nullptr && s_record->capture_active()) {
            s_record->render_update();
            log_ink_tick(now_ms);
            outline_selected_swatch();
        }
    }
}
