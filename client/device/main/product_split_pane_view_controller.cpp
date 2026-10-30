#include "product_split_pane_view_controller.hpp"

#include "product_theme.hpp"

namespace product_ui
{

namespace
{

void styleSlot(lv_obj_t *slot, int32_t y, int32_t height, uint32_t color)
{
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, 320, height);
    lv_obj_set_pos(slot, 0, y);
    lv_obj_set_style_bg_color(slot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);
    lv_obj_set_scrollable(slot, false);
    lv_obj_set_clickable(slot, false);
    lv_obj_set_overflow_visible(slot, false);
}

} // namespace

lv_obj_t *SplitPaneViewController::createSlot(lv_obj_t *parent)
{
    return lv_obj_create(parent);
}

bool SplitPaneViewController::createView(lv_obj_t *parent)
{
    if (!ViewController::createView(parent)) {
        return false;
    }
    lv_obj_set_clickable(root_, false);
    lv_obj_set_overflow_visible(root_, false);
    upper_ = createSlot(root_);
    if (!upper_) {
        return false;
    }
    styleSlot(upper_, 0, 80, product_theme::Dark::kSurfaceUpperRgb24);
    lower_ = createSlot(root_);
    if (!lower_) {
        return false;
    }
    styleSlot(lower_, 80, 160, product_theme::Dark::kSurfaceLowerRgb24);
    return true;
}

void SplitPaneViewController::viewWasRemoved()
{
    upper_ = nullptr;
    lower_ = nullptr;
}

} // namespace product_ui
