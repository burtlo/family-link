#pragma once

#include "lvgl.h"

#include <stdint.h>

namespace stage_shell
{

/* Desk split-pane geometry and slot fills; supplied by product_ui::UiWindow at mount. */
struct DeskLayout {
    lv_coord_t canvas_w = 0;
    lv_coord_t canvas_h = 0;
    lv_coord_t upper_h = 0;
    uint32_t upper_rgb24 = 0;
    uint32_t lower_rgb24 = 0;

    [[nodiscard]] bool valid() const
    {
        return canvas_w > 0 && canvas_h > 0 && upper_h > 0 && upper_h < canvas_h;
    }
};

} /* namespace stage_shell */
