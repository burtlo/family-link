#pragma once

#include "lvgl.h"

namespace fl_product_login
{
class SessionWriter;
}

namespace product_login
{

bool attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void schedule_wifi_link(bool up, int disconnect_reason);
void schedule_wifi_driver_failed();
void on_boot_pressed();
void on_mute_level(int latched);
/* Called only by product_stage::present(). */
void present(bool visible);
bool is_signed_in();
void split_pane_release();
void split_pane_rebuild();

fl_product_login::SessionWriter *session();

} /* namespace product_login */
