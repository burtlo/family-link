#ifndef LAB11_FW_H
#define LAB11_FW_H

#include "bsp/esp-box-3.h"
#include "lvgl.h"

#include <stdbool.h>

bool lab11_fw_init(lv_obj_t *screen, const char *mode);
void lab11_fw_start(void);

#endif
