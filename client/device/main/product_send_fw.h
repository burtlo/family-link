#ifndef PRODUCT_SEND_FW_H
#define PRODUCT_SEND_FW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool product_send_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void product_send_detach(void);

#ifdef __cplusplus
}
#endif

#endif
