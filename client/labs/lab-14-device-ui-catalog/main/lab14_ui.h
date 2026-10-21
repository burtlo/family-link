#pragma once

#include "lab14_catalog.h"
#include "lvgl.h"

#include <stdbool.h>

bool lab14_ui_init(lv_obj_t *root, lab14_catalog_t *catalog, bool scripted);
void lab14_ui_show_index(void);
void lab14_ui_handle_boot(void);
bool lab14_ui_show_scene(const char *id);
bool lab14_ui_show_offset(int delta);
bool lab14_ui_handle_line(const char *line);
void lab14_run_harness(void);
