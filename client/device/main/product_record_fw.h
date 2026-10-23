#ifndef PRODUCT_RECORD_FW_H
#define PRODUCT_RECORD_FW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool product_record_attach(lv_obj_t *panel_root);
void product_record_service(int64_t now_ms, int record_stage_active);

#ifdef __cplusplus
}
#endif

#endif
