#include "product_split_pane.hpp"

#include "esp_log.h"

namespace stage_shell
{

namespace
{

constexpr const char *kTag = "product_split_pane";

void apply_full_bleed(lv_obj_t *obj)
{
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
}

} /* namespace */

SplitPane &splitPane()
{
    static SplitPane pane;
    return pane;
}

bool SplitPane::applyLayout(const DeskLayout &layout)
{
    if (!layout.valid()) {
        return false;
    }
    layout_ = layout;
    canvas_w_ = layout.canvas_w;
    canvas_h_ = layout.canvas_h;
    upper_h_ = layout.upper_h;
    upper_rgb24_ = layout.upper_rgb24;
    lower_rgb24_ = layout.lower_rgb24;
    return true;
}

void SplitPane::styleSlot(lv_obj_t *slot, uint32_t bg_rgb24)
{
    apply_full_bleed(slot);
    lv_obj_set_style_bg_color(slot, lv_color_hex(bg_rgb24), 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
}

bool SplitPane::buildSlots()
{
    const lv_coord_t lower_h = canvas_h_ - upper_h_;

    upper_ = lv_obj_create(parent_);
    lower_ = lv_obj_create(parent_);
    if (upper_ == nullptr || lower_ == nullptr) {
        return false;
    }

    lv_obj_remove_style_all(upper_);
    lv_obj_remove_style_all(lower_);
    lv_obj_set_size(upper_, canvas_w_, upper_h_);
    lv_obj_set_pos(upper_, 0, 0);
    styleSlot(upper_, upper_rgb24_);

    lv_obj_set_size(lower_, canvas_w_, lower_h);
    lv_obj_set_pos(lower_, 0, upper_h_);
    styleSlot(lower_, lower_rgb24_);
    return true;
}

bool SplitPane::mount(lv_obj_t *content_root, const DeskLayout &layout)
{
    if (content_root == nullptr) {
        return false;
    }
    if (mounted_) {
        return slotsLive();
    }
    if (!applyLayout(layout)) {
        return false;
    }
    parent_ = content_root;
    stage_ = FL_SHELL_STAGE_ROSTER;
    if (!buildSlots()) {
        parent_ = nullptr;
        return false;
    }
    mounted_ = true;
    ESP_LOGI(kTag, "V2_PRODUCT_SPLIT_PANE_READY build=%s", FIRMWARE_BUILD_ID);
    return true;
}

void SplitPane::unmount()
{
    destroySlots();
    parent_ = nullptr;
    layout_ = {};
    canvas_w_ = 0;
    canvas_h_ = 0;
    upper_h_ = 0;
    upper_rgb24_ = 0;
    lower_rgb24_ = 0;
    stage_ = FL_SHELL_STAGE_ROSTER;
    mounted_ = false;
}

bool SplitPane::slotsLive() const
{
    return mounted_ && upper_ != nullptr && lower_ != nullptr;
}

bool SplitPane::slotRoots(lv_obj_t **upper_out, lv_obj_t **lower_out) const
{
    if (!mounted_ || upper_out == nullptr || lower_out == nullptr) {
        return false;
    }
    *upper_out = upper_;
    *lower_out = lower_;
    return upper_ != nullptr && lower_ != nullptr;
}

fl_shell_stage_t SplitPane::stage() const
{
    return mounted_ ? stage_ : FL_SHELL_STAGE_ROSTER;
}

void SplitPane::setStage(fl_shell_stage_t stage)
{
    if (!mounted_ || static_cast<unsigned>(stage) >= FL_SHELL_STAGE_COUNT) {
        return;
    }
    stage_ = stage;
}

void SplitPane::destroySlots()
{
    if (!mounted_) {
        return;
    }
    if (upper_ != nullptr) {
        lv_obj_delete(upper_);
    }
    if (lower_ != nullptr) {
        lv_obj_delete(lower_);
    }
    upper_ = nullptr;
    lower_ = nullptr;
}

bool SplitPane::createSlots()
{
    if (!mounted_ || !layout_.valid()) {
        return false;
    }
    if (upper_ != nullptr && lower_ != nullptr) {
        return true;
    }
    destroySlots();
    return buildSlots();
}

} /* namespace stage_shell */
