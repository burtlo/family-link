#include "product_settings_view_controller.hpp"
#include "product_theme.hpp"

namespace product_ui
{
bool SettingsViewController::createView(lv_obj_t *parent)
{
    if (!SplitPaneViewController::createView(parent)) return false;
    heading_ = lv_label_create(upperSlot());
    slider_ = lv_slider_create(lowerSlot());
    if (!heading_ || !slider_) return false;
    lv_label_set_text(heading_, "Volume");
    lv_obj_set_style_text_font(heading_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(heading_, lv_color_hex(product_theme::Dark::kTextPrimaryRgb24), 0);
    lv_obj_set_pos(heading_, 12, 26);
    lv_obj_set_clickable(heading_, false);
    lv_slider_set_range(slider_, 0, fl_product_volume::kVolumeStopCount - 1);
    lv_obj_set_size(slider_, 272, 24);
    lv_obj_center(slider_);
    lv_obj_set_style_pad_all(slider_, 8, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider_, lv_color_hex(product_theme::Dark::kAccentRgb24), LV_PART_INDICATOR);
    lv_obj_add_event_cb(slider_, onValue, LV_EVENT_VALUE_CHANGED, this);
    render();
    return true;
}
void SettingsViewController::render()
{
    if (!slider_) return;
    const auto snapshot = service_.snapshot();
    if (lv_slider_get_value(slider_) != snapshot.stop) lv_slider_set_value(slider_, snapshot.stop, LV_ANIM_OFF);
    const bool disabled = !snapshot.ready || snapshot.saving;
    if (lv_obj_has_state(slider_, LV_STATE_DISABLED) != disabled) {
        if (disabled) lv_obj_add_state(slider_, LV_STATE_DISABLED);
        else lv_obj_remove_state(slider_, LV_STATE_DISABLED);
    }
}
void SettingsViewController::onValue(lv_event_t *event)
{
    auto *controller = static_cast<SettingsViewController *>(lv_event_get_user_data(event));
    if (!controller || !controller->loaded() || !controller->appeared() || !controller->slider_) return;
    (void)controller->service_.setStop(lv_slider_get_value(controller->slider_));
    controller->render();
}
void SettingsViewController::onBoot()
{
    (void)service_.prepareLeave(SettingsLeaveReason::Boot);
}
void SettingsViewController::viewWasRemoved()
{
    slider_ = heading_ = nullptr;
    SplitPaneViewController::viewWasRemoved();
}
} // namespace product_ui
