#include "product_sketch_view_controller.hpp"

#include "fl_product_record_lab10.h"
#include "product_theme.hpp"

namespace product_ui
{
namespace
{
lv_obj_t *makeSwatch(lv_obj_t *parent, int centerX, int centerY, uint32_t rgb)
{
    auto *object = lv_obj_create(parent);
    if (!object)
        return nullptr;
    lv_obj_remove_style_all(object);
    lv_obj_set_size(object, LAB10_SWATCH_DIAMETER, LAB10_SWATCH_DIAMETER);
    lv_obj_set_pos(object, centerX - LAB10_SWATCH_RADIUS, centerY - LAB10_SWATCH_RADIUS);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(rgb), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 2, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(product_theme::Dark::kSwatchRingRgb24), 0);
    lv_obj_set_style_outline_color(object, lv_color_hex(product_theme::Dark::kAccentRgb24), 0);
    lv_obj_set_style_outline_pad(object, 1, 0);
    lv_obj_set_style_outline_width(object, 0, 0);
    lv_obj_set_clickable(object, false);
    return object;
}
} // namespace

bool SketchViewController::createView(lv_obj_t *parent)
{
    SurfacePlace place = SurfacePlace::Host;
#ifdef ESP_PLATFORM
    place = SurfacePlace::Psram;
#endif
    if (!surfaces_.acquireRecord("sketch", place, &lease_))
        return false;
    auto *pixels = surfaces_.pixels(lease_);
    root_ = lv_obj_create(parent);
    reveal_ = root_ ? lv_obj_create(root_) : nullptr;
    canvas_ = reveal_ ? lv_canvas_create(reveal_) : nullptr;
    rail_ = reveal_ ? lv_obj_create(reveal_) : nullptr;
    if (!root_ || !reveal_ || !canvas_ || !rail_ || !pixels) {
        viewWillUnload();
        return false;
    }
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_clickable(root_, true);
    lv_obj_add_event_cb(root_, onPointer, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(root_, onPointer, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(root_, onPointer, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(root_, onPointer, LV_EVENT_PRESS_LOST, this);
    lv_obj_remove_style_all(reveal_);
    lv_obj_set_size(reveal_, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_scrollable(reveal_, false);
    lv_canvas_set_buffer(canvas_, pixels, LAB10_CANVAS_WIDTH, LAB10_CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(canvas_, LAB10_CANVAS_WIDTH, LAB10_CANVAS_HEIGHT);
    lv_canvas_fill_bg(canvas_, lv_color_hex(product_theme::Dark::kSketchCanvasRgb24), LV_OPA_COVER);
    lv_obj_remove_style_all(rail_);
    lv_obj_set_size(rail_, LAB10_RAIL_WIDTH, LAB10_LCD_HEIGHT);
    lv_obj_set_pos(rail_, LAB10_RAIL_X, 0);
    lv_obj_set_style_bg_color(rail_, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_set_style_bg_opa(rail_, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(rail_, false);
    for (int i = 0; i < LAB10_SWATCH_COUNT; ++i) {
        const auto *geometry = lab10_swatch_geometry(static_cast<uint8_t>(i));
        if (!geometry)
            continue;
        swatches_[i] = makeSwatch(rail_, geometry->center_x - LAB10_RAIL_X, geometry->center_y, geometry->color_rgb);
        if (!swatches_[i]) {
            viewWillUnload();
            return false;
        }
    }
    ring_ = lv_arc_create(root_);
    if (!ring_) {
        viewWillUnload();
        return false;
    }
    lv_obj_remove_style_all(ring_);
    lv_obj_set_clickable(ring_, false);
    lv_obj_set_scrollable(ring_, false);
    lv_obj_set_style_arc_width(ring_, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring_, LAB10_RING_BAND_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring_, lv_color_hex(product_theme::Dark::kRecordRingRgb24), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(ring_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(ring_, 0, 360);
    lv_arc_set_angles(ring_, 0, 360);
    delivery_.capture().attachSurface(pixels, canvas_);
    outlineSwatch();
    paintMotion();
    return true;
}

void SketchViewController::render()
{
    if (!canvas_)
        return;
    delivery_.capture().render();
    outlineSwatch();
    paintMotion();
}

void SketchViewController::onBoot()
{
    if (navigation_.request)
        navigation_.request(navigation_.context, SketchIntent::Boot);
}

void SketchViewController::onRed()
{
    if (navigation_.request)
        navigation_.request(navigation_.context, SketchIntent::Red);
}

lv_obj_t *SketchViewController::swatch(int index) const
{
    return index >= 0 && index < LAB10_SWATCH_COUNT ? swatches_[index] : nullptr;
}

void SketchViewController::onPointer(lv_event_t *event)
{
    auto *view = static_cast<SketchViewController *>(lv_event_get_user_data(event));
    if (!view || !view->delivery_.capture().inputOpen())
        return;
    auto *input = lv_event_get_indev(event);
    if (!input)
        input = lv_indev_active();
    if (!input)
        return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    uint8_t phase = 3;
    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED:
            phase = 0;
            break;
        case LV_EVENT_PRESSING:
            phase = 1;
            break;
        case LV_EVENT_RELEASED:
            phase = 2;
            break;
        default:
            break;
    }
    (void)view->delivery_.capture().routeTouch(static_cast<int16_t>(point.x), static_cast<int16_t>(point.y), phase);
}

void SketchViewController::paintMotion()
{
    auto &capture = delivery_.capture();
    if (!capture.wiping() && !capture.reversing()) {
        if (reveal_) {
            lv_obj_set_hidden(reveal_, false);
            lv_obj_set_pos(reveal_, 0, 0);
            lv_obj_set_size(reveal_, LAB10_LCD_WIDTH, LAB10_LCD_HEIGHT);
        }
        if (canvas_)
            lv_obj_set_pos(canvas_, 0, 0);
        if (rail_)
            lv_obj_set_pos(rail_, LAB10_RAIL_X, 0);
        if (ring_)
            lv_obj_set_hidden(ring_, true);
        return;
    }
    const int32_t radius = capture.ringRadius();
    const auto clip = lab10_ui_ring_reveal_clip(LAB10_RING_ORIGIN_Y, radius);
    if (reveal_) {
        lv_obj_set_hidden(reveal_, clip.hidden);
        if (!clip.hidden) {
            lv_obj_set_pos(reveal_, clip.x, clip.y);
            lv_obj_set_size(reveal_, clip.width, clip.height);
        }
    }
    if (canvas_)
        lv_obj_set_pos(canvas_, 0, clip.content_offset_y);
    if (rail_)
        lv_obj_set_pos(rail_, LAB10_RAIL_X, clip.content_offset_y);
    if (!ring_)
        return;
    const int32_t shown = radius < 1 ? 1 : radius;
    lv_obj_set_hidden(ring_, false);
    lv_obj_set_size(ring_, shown * 2, shown * 2);
    lv_obj_set_pos(ring_, LAB10_RING_ORIGIN_X - shown, LAB10_RING_ORIGIN_Y - shown);
}

void SketchViewController::outlineSwatch()
{
    const int selected = delivery_.capture().selectedColor();
    if (selected == outlined_)
        return;
    if (outlined_ >= 0 && outlined_ < LAB10_SWATCH_COUNT && swatches_[outlined_])
        lv_obj_set_style_outline_width(swatches_[outlined_], 0, 0);
    if (selected >= 0 && selected < LAB10_SWATCH_COUNT && swatches_[selected])
        lv_obj_set_style_outline_width(swatches_[selected], 2, 0);
    outlined_ = selected;
}

void SketchViewController::viewWillUnload()
{
    delivery_.capture().detachSurface();
    if (lease_.generation != 0)
        (void)surfaces_.release(&lease_);
}

void SketchViewController::viewWasRemoved()
{
    reveal_ = canvas_ = rail_ = ring_ = nullptr;
    for (auto &swatchObject : swatches_)
        swatchObject = nullptr;
    outlined_ = -1;
}

} // namespace product_ui
