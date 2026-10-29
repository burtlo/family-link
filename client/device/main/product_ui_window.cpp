#include "product_ui_window.hpp"

#include "esp_log.h"
#include "product_canvas.hpp"
#include "product_login.hpp"
#include "product_record.hpp"
#include "product_split_pane.hpp"
#include "product_theme.hpp"

namespace product_ui
{

namespace
{

constexpr const char *kTag = "product_ui_window";

/* docs/product/device-ui/catalog/tokens.yaml — split_pane.split_y */
constexpr lv_coord_t kSplitPaneUpperHeight = 80;

void apply_full_bleed_lvgl_chrome(lv_obj_t *obj)
{
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
}

} /* namespace */

UiWindow &uiWindow()
{
    static UiWindow window;
    return window;
}

bool UiWindow::canvasSize(lv_coord_t *width_out, lv_coord_t *height_out) const
{
    if (width_out == nullptr || height_out == nullptr) {
        return false;
    }
    *width_out = static_cast<lv_coord_t>(product_canvas::kWidth);
    *height_out = static_cast<lv_coord_t>(product_canvas::kHeight);
    return true;
}

bool UiWindow::splitPaneUpperHeight(lv_coord_t *height_out) const
{
    if (height_out == nullptr) {
        return false;
    }
    *height_out = kSplitPaneUpperHeight;
    return true;
}

bool UiWindow::splitPaneSlotColors(uint32_t *upper_rgb24_out, uint32_t *lower_rgb24_out) const
{
    if (upper_rgb24_out == nullptr || lower_rgb24_out == nullptr) {
        return false;
    }
    *upper_rgb24_out = product_theme::Dark::kSurfaceUpperRgb24;
    *lower_rgb24_out = product_theme::Dark::kSurfaceLowerRgb24;
    return true;
}

bool UiWindow::ensureContentRoot(lv_obj_t *screen)
{
    if (screen == nullptr) {
        return false;
    }
    lv_coord_t canvas_w = 0;
    lv_coord_t canvas_h = 0;
    if (!canvasSize(&canvas_w, &canvas_h)) {
        return false;
    }
    screen_ = screen;
    content_root_ = lv_obj_create(screen);
    if (content_root_ == nullptr) {
        return false;
    }

    lv_obj_remove_style_all(content_root_);

    /* Product: canvas size and active theme shell backdrop (THEME-DARK surface). */
    lv_obj_set_size(screen, canvas_w, canvas_h);
    lv_obj_set_style_bg_color(screen, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_set_size(content_root_, canvas_w, canvas_h);
    lv_obj_set_pos(content_root_, 0, 0);
    lv_obj_set_style_bg_opa(content_root_, LV_OPA_TRANSP, 0);

    /* LVGL: full-bleed chrome (size must stay after remove_style_all above). */
    apply_full_bleed_lvgl_chrome(screen);
    apply_full_bleed_lvgl_chrome(content_root_);
    return true;
}

bool UiWindow::create(lv_obj_t *screen)
{
    if (created_) {
        return true;
    }
    if (screen == nullptr) {
        screen = lv_screen_active();
    }
    if (!ensureContentRoot(screen)) {
        return false;
    }
    if (!stage_shell::splitPane().mount(content_root_)) {
        lv_obj_delete(content_root_);
        content_root_ = nullptr;
        screen_ = nullptr;
        return false;
    }
    created_ = true;
    scene_ = Scene::None;
    desk_modules_attached_ = false;
    ESP_LOGI(kTag, "V2_PRODUCT_UI_WINDOW_READY build=%s", FIRMWARE_BUILD_ID);
    return true;
}

void UiWindow::destroy()
{
    if (!created_) {
        return;
    }
    if (scene_ == Scene::Record) {
        product_record::release_surface();
    }
    if (desk_modules_attached_) {
        product_login::split_pane_release();
        desk_modules_attached_ = false;
    }
    stage_shell::splitPane().unmount();
    if (content_root_ != nullptr) {
        lv_obj_delete(content_root_);
    }
    content_root_ = nullptr;
    screen_ = nullptr;
    scene_ = Scene::None;
    created_ = false;
}

bool UiWindow::deskRoots(lv_obj_t **upper_out, lv_obj_t **lower_out)
{
    return stage_shell::splitPane().slotRoots(upper_out, lower_out);
}

bool UiWindow::ensureDeskSlots()
{
    auto &pane = stage_shell::splitPane();
    if (pane.slotsLive()) {
        return true;
    }
    if (desk_modules_attached_) {
        product_login::split_pane_rebuild();
        return pane.slotsLive();
    }
    return pane.createSlots();
}

bool UiWindow::attachDeskModulesOnce()
{
    lv_obj_t *upper = nullptr;
    lv_obj_t *lower = nullptr;

    if (desk_modules_attached_) {
        return true;
    }
    /* product_login::attach calls product_stage_present → mountDesk; set early. */
    desk_modules_attached_ = true;
    if (!deskRoots(&upper, &lower) || !product_login::attach(upper, lower)) {
        desk_modules_attached_ = false;
        return false;
    }
    return true;
}

bool UiWindow::mountDesk()
{
    if (!created_) {
        return false;
    }
    if (scene_ == Scene::Desk && stage_shell::splitPane().slotsLive()) {
        return true;
    }
    if (!ensureDeskSlots()) {
        return false;
    }
    if (!attachDeskModulesOnce()) {
        return false;
    }
    scene_ = Scene::Desk;
    return true;
}

bool UiWindow::mountRecord()
{
    if (!created_) {
        return false;
    }
    if (scene_ == Scene::Record) {
        return true;
    }
    if (desk_modules_attached_ && stage_shell::splitPane().slotsLive()) {
        product_login::split_pane_release();
    }
    if (!product_record::ensure(content_root_)) {
        return false;
    }
    scene_ = Scene::Record;
    return true;
}

} /* namespace product_ui */

extern "C" bool product_ui_window_canvas_size(lv_coord_t *width_out, lv_coord_t *height_out)
{
    return product_ui::uiWindow().canvasSize(width_out, height_out);
}

extern "C" bool product_ui_window_split_pane_upper_height(lv_coord_t *height_out)
{
    return product_ui::uiWindow().splitPaneUpperHeight(height_out);
}

extern "C" bool product_ui_window_split_pane_slot_colors(uint32_t *upper_rgb24_out,
                                                         uint32_t *lower_rgb24_out)
{
    return product_ui::uiWindow().splitPaneSlotColors(upper_rgb24_out, lower_rgb24_out);
}
