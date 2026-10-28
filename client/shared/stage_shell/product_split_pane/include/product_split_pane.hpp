#pragma once

#include "fl_shell_model.h"
#include "lvgl.h"

#include <stdint.h>

namespace stage_shell {

/*
 * Desk split-pane LVGL chrome: upper/lower slots and stage bookmark.
 *
 * Does not own canvas size or the content root — product_ui::UiWindow sets the
 * full-bleed content root and exposes layout via product_ui_window_* queries.
 * SplitPane only mounts slot objects inside that parent.
 */
class SplitPane {
public:
    bool mount(lv_obj_t *content_root);
    void unmount();

    [[nodiscard]] bool mounted() const { return mounted_; }
    [[nodiscard]] bool slotsLive() const;
    bool slotRoots(lv_obj_t **upper_out, lv_obj_t **lower_out) const;

    void destroySlots();
    bool createSlots();

    [[nodiscard]] fl_shell_stage_t stage() const;
    void setStage(fl_shell_stage_t stage);

private:
    bool queryLayoutFromWindow();
    bool buildSlots();
    void styleSlot(lv_obj_t *slot, uint32_t bg_rgb24);

    lv_obj_t *parent_ = nullptr;
    lv_obj_t *upper_ = nullptr;
    lv_obj_t *lower_ = nullptr;
    lv_coord_t canvas_w_ = 0;
    lv_coord_t canvas_h_ = 0;
    lv_coord_t upper_h_ = 0;
    uint32_t upper_rgb24_ = 0;
    uint32_t lower_rgb24_ = 0;
    fl_shell_stage_t stage_ = FL_SHELL_STAGE_ROSTER;
    bool mounted_ = false;
};

SplitPane &splitPane();

} /* namespace stage_shell */
