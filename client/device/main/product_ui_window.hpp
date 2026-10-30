#pragma once

#include "lvgl.h"

namespace product_ui
{
class UiWindow
{
  public:
    bool create(lv_obj_t *screen);
    void destroy();
    bool created() const { return content_root_ != nullptr; }
    lv_obj_t *contentRoot() const { return content_root_; }
  private:
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *content_root_ = nullptr;
};

UiWindow &uiWindow();
} // namespace product_ui
