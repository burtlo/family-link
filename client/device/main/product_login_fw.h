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
void product_login_on_mute_pressed(void);

#ifdef __cplusplus
}
#endif

#endif
