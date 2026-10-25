#ifndef PRODUCT_LOGIN_FW_H
#define PRODUCT_LOGIN_FW_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_login_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
/* Safe from Wi-Fi / network tasks; LVGL updates run on the login timer. */
void product_login_schedule_wifi_link(bool up, int disconnect_reason);
void product_login_schedule_wifi_driver_failed(void);
void product_login_on_boot_pressed(void);
void product_login_on_mute_level(int latched);
/* Called only by product_stage_present(). */
void product_login_present(int visible);
int product_login_is_signed_in(void);
/* Deletes the shell slots and every module widget in them; models stay. */
void product_split_pane_release(void);
/* Recreates the slots and module widgets in boot order; a no-op while live. */
void product_split_pane_rebuild(void);

#ifdef __cplusplus
}
#include "fl_product_login_session.hpp"
fl_product_login::SessionWriter *product_login_session(void);
#endif

#endif
