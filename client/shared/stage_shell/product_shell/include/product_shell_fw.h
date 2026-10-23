#ifndef PRODUCT_SHELL_FW_H
#define PRODUCT_SHELL_FW_H

#include "lvgl.h"
#include <stdbool.h>

bool product_shell_init(lv_obj_t *screen);
bool product_shell_slot_roots(lv_obj_t **upper_out, lv_obj_t **lower_out);

#endif
