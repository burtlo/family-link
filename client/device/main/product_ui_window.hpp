#pragma once

#include "lvgl.h"

#include "desk_layout.hpp"

namespace product_ui
{

enum class Scene {
    None,
    Desk,
    Record,
};

/*
 * Root LVGL host for the device desk.
 *
 * Owns: active screen sizing, themed shell backdrop, transparent content root,
 *       desk vs record scene, desk module attach orchestration.
 * Split-pane (stage_shell::SplitPane) owns upper/lower slots and stage bookmark;
 * UiWindow passes stage_shell::DeskLayout at mount.
 */
class UiWindow
{
  public:
    bool create(lv_obj_t *screen);
    void destroy();

    [[nodiscard]] bool created() const
    {
        return created_;
    }
    [[nodiscard]] Scene scene() const
    {
        return scene_;
    }
    lv_obj_t *contentRoot() const
    {
        return content_root_;
    }

    bool mountDesk();
    bool mountRecord();
    bool deskRoots(lv_obj_t **upper_out, lv_obj_t **lower_out);

  private:
    bool ensureContentRoot(lv_obj_t *screen);
    bool ensureDeskSlots();
    bool attachDeskModulesOnce();
    bool deskSplitLayout(stage_shell::DeskLayout *layout_out) const;

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *content_root_ = nullptr;
    Scene scene_ = Scene::None;
    bool created_ = false;
    bool desk_modules_attached_ = false;
};

UiWindow &uiWindow();

} /* namespace product_ui */
