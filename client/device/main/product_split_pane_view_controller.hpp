#pragma once

#include "product_view_controller.hpp"

namespace product_ui
{

// Owns only the split chrome. Subclasses own their feature widgets in these
// slots; there are no child view controllers or appearance forwarding here.
class SplitPaneViewController : public ViewController
{
  public:
    const char *name() const override
    {
        return "SplitPane";
    }

    lv_obj_t *upperSlot() const
    {
        return upper_;
    }

    lv_obj_t *lowerSlot() const
    {
        return lower_;
    }

    struct ContentBounds {
        int32_t x;
        int32_t y;
        int32_t width;
        int32_t height;
    };

    // Coordinates relative to each slot, from LAYOUT-SPLIT-PANE.
    static ContentBounds upperContentBounds()
    {
        return {12, 12, 296, 56};
    }

    static ContentBounds lowerContentBounds()
    {
        return {12, 12, 296, 136};
    }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;
    // Allows deterministic slot-allocation failure tests using real LVGL.
    virtual lv_obj_t *createSlot(lv_obj_t *parent);

  private:
    lv_obj_t *upper_ = nullptr;
    lv_obj_t *lower_ = nullptr;
};

} // namespace product_ui
