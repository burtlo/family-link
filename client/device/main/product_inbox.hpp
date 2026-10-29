#pragma once

#include "lvgl.h"

#include <cstdint>

namespace fl_product_inbox
{
class CarouselSession;
}

namespace product_inbox
{

bool attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void forget_widgets();
bool build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void service(int64_t now_ms);
void on_signed_in();
void on_mute();
void discard_results();
void note_refresh(int64_t now_ms);
void note_refresh_failure(int64_t now_ms);
void set_wifi(bool up);
/* Called only by product_stage::present(). */
void set_ui_visible(bool visible);
int on_boot_pressed();

fl_product_inbox::CarouselSession *session();

} /* namespace product_inbox */
