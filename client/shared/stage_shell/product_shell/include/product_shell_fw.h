#ifndef PRODUCT_SHELL_FW_H
#define PRODUCT_SHELL_FW_H

#include "fl_shell_model.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_shell_init(lv_obj_t *screen);
bool product_shell_slot_roots(lv_obj_t **upper_out, lv_obj_t **lower_out);
fl_shell_stage_t product_shell_stage(void);
void product_shell_set_stage(fl_shell_stage_t stage);

#ifdef __cplusplus
}
#endif

#endif
