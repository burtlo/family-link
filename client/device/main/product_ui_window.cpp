#include "product_ui_window.hpp"

#include "product_canvas.hpp"
#include "product_theme.hpp"

namespace product_ui
{

UiWindow &uiWindow()
{
    static UiWindow window;
    return window;
}

bool UiWindow::create(lv_obj_t *screen)
{
    if (content_root_) return true;
    if (!screen) screen = lv_screen_active();
    if (!screen) return false;
    content_root_ = lv_obj_create(screen);
    if (!content_root_) return false;
    screen_ = screen;
    lv_obj_set_size(screen_, product_canvas::kWidth, product_canvas::kHeight);
    lv_obj_set_style_bg_color(screen_, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen_, 0, 0);
    lv_obj_set_style_border_width(screen_, 0, 0);
    lv_obj_set_scrollable(screen_, false);
    lv_obj_remove_style_all(content_root_);
    lv_obj_set_pos(content_root_, 0, 0);
    lv_obj_set_size(content_root_, product_canvas::kWidth, product_canvas::kHeight);
    lv_obj_set_style_bg_opa(content_root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(content_root_, 0, 0);
    lv_obj_set_style_border_width(content_root_, 0, 0);
    lv_obj_set_scrollable(content_root_, false);
    lv_obj_set_clickable(content_root_, false);
    return true;
}

void UiWindow::destroy()
{
    if (content_root_) lv_obj_delete(content_root_);
    content_root_ = nullptr;
    screen_ = nullptr;
}

} // namespace product_ui
