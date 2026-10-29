#pragma once

#include "lvgl.h"

#include <cstdint>

namespace fl_product_inbox
{
class CarouselSession;
}
namespace fl_product_login
{
class SessionWriter;
}

namespace product_volume
{

bool attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
/* Split-pane teardown: drop pointers to widgets the shell already deleted. */
void forget_widgets();
bool build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void service(int64_t now_ms);
[[nodiscard]] bool active();
int on_boot_from_inbox();
int on_boot_leave();
int on_mute();
int on_red_record();
void on_wifi(bool up);
/* Called only by product_stage::present(). */
void set_visible(bool visible);

void bind_models(fl_product_inbox::CarouselSession *inbox, fl_product_login::SessionWriter *login);

} /* namespace product_volume */
