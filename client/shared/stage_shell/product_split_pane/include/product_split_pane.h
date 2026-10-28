#ifndef PRODUCT_SPLIT_PANE_H
#define PRODUCT_SPLIT_PANE_H

#include "fl_shell_model.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool product_split_pane_create(lv_obj_t *parent);
void product_split_pane_destroy(void);
bool product_split_pane_slot_roots(lv_obj_t **upper_out, lv_obj_t **lower_out);
/* Deletes both split-pane slots and every child widget. */
void product_split_pane_destroy_slots(void);
/* Recreates empty slots after destroy_slots; a no-op while they are live. */
bool product_split_pane_create_slots(void);
bool product_split_pane_slots_live(void);
bool product_split_pane_created(void);
fl_shell_stage_t product_stage_get(void);
void product_stage_set(fl_shell_stage_t stage);

#ifdef __cplusplus
}
#endif

#endif
