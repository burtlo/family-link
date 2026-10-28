#include "product_ui_window.hpp"

#include "product_ui_window.h"

#include "esp_log.h"
#include "product_login_fw.h"
#include "product_record_fw.h"
#include "product_split_pane.h"

namespace product_ui {

namespace {

constexpr const char *kTag = "product_ui_window";

product_ui_scene_t to_c_scene(Scene scene)
{
    switch (scene) {
    case Scene::Desk:
        return PRODUCT_UI_SCENE_DESK;
    case Scene::Record:
        return PRODUCT_UI_SCENE_RECORD;
    case Scene::None:
    default:
        return PRODUCT_UI_SCENE_NONE;
    }
}

} /* namespace */

UiWindow &uiWindow()
{
    static UiWindow window;
    return window;
}

bool UiWindow::ensureContentRoot(lv_obj_t *screen)
{
    if (screen == nullptr) {
        return false;
    }
    screen_ = screen;
    content_root_ = lv_obj_create(screen);
    if (content_root_ == nullptr) {
        return false;
    }
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_remove_style_all(content_root_);
    lv_obj_set_size(content_root_, 320, 240);
    lv_obj_set_pos(content_root_, 0, 0);
    lv_obj_set_style_bg_opa(content_root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_root_, 0, 0);
    lv_obj_set_style_pad_all(content_root_, 0, 0);
    lv_obj_set_scrollable(content_root_, false);
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
    if (!product_split_pane_create(content_root_)) {
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
        product_record_release_surface();
    }
    if (desk_modules_attached_) {
        product_split_pane_release();
        desk_modules_attached_ = false;
    }
    product_split_pane_destroy();
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
    return product_split_pane_slot_roots(upper_out, lower_out);
}

bool UiWindow::ensureDeskSlots()
{
    if (product_split_pane_slots_live()) {
        return true;
    }
    if (desk_modules_attached_) {
        product_split_pane_rebuild();
        return product_split_pane_slots_live();
    }
    return product_split_pane_create_slots();
}

bool UiWindow::attachDeskModulesOnce()
{
    lv_obj_t *upper = nullptr;
    lv_obj_t *lower = nullptr;

    if (desk_modules_attached_) {
        return true;
    }
    /* product_login_attach calls product_stage_present → mountDesk; set early. */
    desk_modules_attached_ = true;
    if (!deskRoots(&upper, &lower) || !product_login_attach(upper, lower)) {
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
    if (scene_ == Scene::Desk && product_split_pane_slots_live()) {
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
    if (desk_modules_attached_ && product_split_pane_slots_live()) {
        product_split_pane_release();
    }
    if (!product_record_ensure(content_root_)) {
        return false;
    }
    scene_ = Scene::Record;
    return true;
}

} /* namespace product_ui */

extern "C" bool product_ui_window_create(lv_obj_t *screen)
{
    return product_ui::uiWindow().create(screen);
}

extern "C" void product_ui_window_destroy(void)
{
    product_ui::uiWindow().destroy();
}

extern "C" bool product_ui_window_created(void)
{
    return product_ui::uiWindow().created();
}

extern "C" lv_obj_t *product_ui_window_content_root(void)
{
    return product_ui::uiWindow().contentRoot();
}

extern "C" product_ui_scene_t product_ui_window_scene(void)
{
    return product_ui::to_c_scene(product_ui::uiWindow().scene());
}

extern "C" bool product_ui_window_desk_roots(lv_obj_t **upper_out, lv_obj_t **lower_out)
{
    return product_ui::uiWindow().deskRoots(upper_out, lower_out);
}

extern "C" bool product_ui_window_show_desk(void)
{
    return product_ui::uiWindow().mountDesk();
}

extern "C" bool product_ui_window_show_record(void)
{
    return product_ui::uiWindow().mountRecord();
}
