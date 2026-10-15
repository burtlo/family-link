#pragma once

#include "lab02/carousel_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Emits one bounded, typed sample. Call while the LVGL owner lock is held. */
void lab02_resource_sample_emit(const lab02_carousel_t *carousel);

#ifdef __cplusplus
}
#endif
