#ifndef PRODUCT_LOGIN_FW_H
#define PRODUCT_LOGIN_FW_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_login_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void product_login_set_wifi(bool up);

#ifdef __cplusplus
}
#endif

#endif
