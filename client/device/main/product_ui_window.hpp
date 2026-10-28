#pragma once

#include "lvgl.h"

namespace product_ui {

enum class Scene {
    None,
    Desk,
    Record,
};

/* Root LVGL host: content root on the active screen, desk split-pane vs record scene. */
class UiWindow {
public:
    bool create(lv_obj_t *screen);
    void destroy();

    [[nodiscard]] bool created() const { return created_; }
    [[nodiscard]] Scene scene() const { return scene_; }
    lv_obj_t *contentRoot() const { return content_root_; }

    bool mountDesk();
    bool mountRecord();
    bool deskRoots(lv_obj_t **upper_out, lv_obj_t **lower_out);

private:
    bool ensureContentRoot(lv_obj_t *screen);
    bool ensureDeskSlots();
    bool attachDeskModulesOnce();

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *content_root_ = nullptr;
    Scene scene_ = Scene::None;
    bool created_ = false;
    bool desk_modules_attached_ = false;
};

UiWindow &uiWindow();

} /* namespace product_ui */
