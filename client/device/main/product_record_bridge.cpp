#include "product_record_fw.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fl_product_record_lab10.h"
#include "fl_product_record_session.hpp"
#include "product_shell_fw.h"

#include <new>

namespace {

constexpr const char *kTag = "product_record";
fl_product_record::RecordSession *s_record = nullptr;
lv_obj_t *s_root = nullptr;
lv_obj_t *s_canvas = nullptr;
lv_obj_t *s_rail = nullptr;
bool s_attached = false;
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
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
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
}

} /* namespace */

extern "C" bool product_record_attach(lv_obj_t *panel_root)
{
    unsigned index;
    if (panel_root == nullptr || s_attached) {
        return false;
    }
    if (s_record == nullptr) {
        s_record = new (std::nothrow) fl_product_record::RecordSession();
        if (s_record == nullptr) {
            ESP_LOGE(kTag, "record_session_alloc_failed");
            return false;
        }
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
    s_record->boot(0);
    s_root = lv_obj_create(panel_root);
    if (s_root == nullptr) {
        return false;
    }
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 320, 240);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_canvas = lv_canvas_create(s_root);
    s_rail = lv_obj_create(s_root);
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
    s_attached = true;
    ESP_LOGI(kTag, "V2_PRODUCT_RECORD_READY build=%s", FAMILY_BUILD_ID);
    return true;
}

extern "C" void product_record_service(int64_t now_ms, int record_stage_active)
{
    static int s_armed = 0;
    layout_record_surface(record_stage_active != 0);
    if (s_record == nullptr) {
        return;
    }
    s_record->advance_to(now_ms);
    if (record_stage_active) {
        if (!s_armed && !s_record->capture_active()) {
            (void)s_record->try_start("device-capture", fl_product_record::kMicReady);
            s_armed = 1;
        }
    } else {
        s_armed = 0;
    }
}
