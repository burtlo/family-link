#pragma once
#include "lvgl.h"
#include "lab12_profile_sync.h"
bool lab12_ui_init(lv_obj_t *screen, bool shared, bool scripted);
bool lab12_ui_profile_ready(const lab12_profile_t *profile);
void lab12_ui_profile_unavailable(void);
void lab12_run_harness(void);
