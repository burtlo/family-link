#ifndef PRODUCT_DISPLAY_H
#define PRODUCT_DISPLAY_H

#include "bsp/esp-box-3.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *product_display_start(void);

#ifdef __cplusplus
}
#endif

#endif
